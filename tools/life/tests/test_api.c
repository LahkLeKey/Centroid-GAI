/** @file test_api.c @brief Public ownership, learning, audit and exact restart checks. */
#include "centroid_life.h"
#include "test_utils.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, "Public Life API check failed")
#define SNAPSHOT "test-life-api.snapshot"
#define BAD_SNAPSHOT "test-life-api.bad.snapshot"

static cgai_life *new_owner(void) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life *owner = NULL;
    config.enable_merges = 0U;
    CHECK(cgai_life_create(&config, &owner) == CGAI_LIFE_OK);
    CHECK(owner != NULL);
    return owner;
}

static void invalid_configs(void) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life *owner = NULL;
    config.seed = UINT64_MAX;
    CHECK(cgai_life_create(&config, &owner) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(owner == NULL);
    config = cgai_life_config_default();
    config.training_epochs = CGAI_LIFE_MAX_TRAINING_EPOCHS + 1U;
    CHECK(cgai_life_create(&config, &owner) == CGAI_LIFE_INVALID_ARGUMENT);
    config = cgai_life_config_default();
    config.enable_merges = 2U;
    CHECK(cgai_life_create(&config, &owner) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(owner == NULL);
}

static void invalid_scenarios(void) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life *owner = NULL;
    config.scenario = 4U;
    CHECK(cgai_life_create(&config, &owner) == CGAI_LIFE_INVALID_ARGUMENT);
    config = cgai_life_config_default();
    CHECK(owner == NULL);
    CHECK(cgai_life_create(NULL, &owner) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_create(&config, NULL) == CGAI_LIFE_INVALID_ARGUMENT);
}

static void invalid_calls(cgai_life *owner) {
    const uint64_t original = cgai_life_hash(owner);
    cgai_life_stats untouched;
    cgai_life_stats expected;
    uint32_t completed = 123U;
    memset(&untouched, 0x5a, sizeof(untouched));
    expected = untouched;
    CHECK(cgai_life_train_step(owner, 0U, &completed) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_train_step(owner, CGAI_LIFE_MAX_STEP_GENERATIONS + 1U, &completed) ==
          CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(completed == 123U);
    CHECK(cgai_life_get_stats(NULL, &untouched) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(memcmp(&untouched, &expected, sizeof(untouched)) == 0);
    CHECK(cgai_life_save(owner, "") == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_load(owner, NULL) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_hash(owner) == original);
}

static void learns_in_isolation(cgai_life *trained, const cgai_life *untouched) {
    cgai_life_stats before;
    cgai_life_stats after;
    const uint64_t other_hash = cgai_life_hash(untouched);
    uint32_t completed = 0U;
    CHECK(cgai_life_get_stats(trained, &before) == CGAI_LIFE_OK);
    CHECK(before.population > 0U);
    CHECK(cgai_life_train_step(trained, 3U, &completed) == CGAI_LIFE_OK);
    CHECK(completed == 3U);
    CHECK(cgai_life_get_stats(trained, &after) == CGAI_LIFE_OK);
    CHECK(after.generation == 3U);
    CHECK(after.collision_records > before.collision_records);
    CHECK(after.training_updates > before.training_updates);
    CHECK(after.policy_hash != before.policy_hash);
    CHECK(after.shared_hash == before.shared_hash);
    CHECK(cgai_life_hash(untouched) == other_hash);
}

static void exact_restart(cgai_life *original, cgai_life *restored) {
    uint32_t completed = 0U;
    CHECK(cgai_life_save(original, SNAPSHOT) == CGAI_LIFE_OK);
    CHECK(cgai_life_load(restored, SNAPSHOT) == CGAI_LIFE_OK);
    CHECK(cgai_life_hash(original) == cgai_life_hash(restored));
    for (uint32_t i = 0U; i < 5U; ++i) {
        CHECK(cgai_life_train_step(original, 1U, &completed) == CGAI_LIFE_OK);
        CHECK(completed == 1U);
        CHECK(cgai_life_train_step(restored, 1U, NULL) == CGAI_LIFE_OK);
        CHECK(cgai_life_hash(original) == cgai_life_hash(restored));
    }
}

static void overwritten_checkpoint(cgai_life *owner, cgai_life *restored) {
    const uint64_t before = cgai_life_hash(owner);
    CHECK(cgai_life_save(owner, SNAPSHOT) == CGAI_LIFE_OK);
    FILE *sidecar = fopen(SNAPSHOT ".policy", "rb");
    CHECK(sidecar == NULL);
    CHECK(cgai_life_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(cgai_life_hash(owner) != before);
    CHECK(cgai_life_save(owner, SNAPSHOT) == CGAI_LIFE_OK);
    CHECK(cgai_life_load(restored, SNAPSHOT) == CGAI_LIFE_OK);
    CHECK(cgai_life_hash(restored) == cgai_life_hash(owner));
}

static void write_invalid(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fputs(text, file) >= 0);
    CHECK(fclose(file) == 0);
}

static void write_truncated_policy(void) {
    FILE *input = fopen(SNAPSHOT, "rb");
    FILE *output = fopen(BAD_SNAPSHOT, "wb");
    char line[512];
    int found = 0;
    CHECK(input != NULL && output != NULL);
    while (fgets(line, sizeof(line), input) != NULL) {
        if (strncmp(line, "CGAI_LIFE_POLICY", strlen("CGAI_LIFE_POLICY")) == 0) {
            CHECK(fputs("CGAI_LIFE_POLICY truncated\n", output) >= 0);
            found = 1;
            break;
        }
        CHECK(fputs(line, output) >= 0);
    }
    CHECK(found && !ferror(input));
    CHECK(fclose(input) == 0 && fclose(output) == 0);
}

static void rejected_restore(cgai_life *owner) {
    const uint64_t original = cgai_life_hash(owner);
    write_invalid(BAD_SNAPSHOT, "LIFE_SNAPSHOT 999\n");
    CHECK(cgai_life_load(owner, BAD_SNAPSHOT) == CGAI_LIFE_IO_ERROR);
    CHECK(cgai_life_hash(owner) == original);
    write_truncated_policy();
    CHECK(cgai_life_load(owner, BAD_SNAPSHOT) == CGAI_LIFE_IO_ERROR);
    CHECK(cgai_life_hash(owner) == original);
    CHECK(cgai_life_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
}

static void frozen_evaluation(cgai_life *owner) {
    cgai_life_stats before;
    cgai_life_stats after;
    uint32_t completed = 0U;
    CHECK(cgai_life_get_stats(owner, &before) == CGAI_LIFE_OK);
    CHECK(cgai_life_evaluate_step(owner, 3U, &completed) == CGAI_LIFE_OK);
    CHECK(completed == 3U);
    CHECK(cgai_life_get_stats(owner, &after) == CGAI_LIFE_OK);
    CHECK(after.generation == before.generation + 3U);
    CHECK(after.policy_hash == before.policy_hash);
    CHECK(after.replay_records == before.replay_records);
    CHECK(after.collision_records == before.collision_records);
    CHECK(after.training_updates == before.training_updates);
    CHECK(after.policy_decisions == before.policy_decisions);
    CHECK(memcmp(after.group_steps, before.group_steps, sizeof(before.group_steps)) == 0);
}

static void cleanup(void) {
    remove(SNAPSHOT);
    remove(SNAPSHOT ".policy");
    remove(BAD_SNAPSHOT);
}

int main(void) {
    remove(SNAPSHOT ".policy");
    cgai_life *original = new_owner();
    cgai_life *restored = new_owner();
    cgai_life_config config = cgai_life_config_default();
    CHECK(cgai_life_hash(original) == cgai_life_hash(restored));
    CHECK(cgai_life_create(&config, &original) == CGAI_LIFE_INVALID_ARGUMENT);
    invalid_configs();
    invalid_scenarios();
    invalid_calls(original);
    learns_in_isolation(original, restored);
    exact_restart(original, restored);
    overwritten_checkpoint(original, restored);
    rejected_restore(restored);
    frozen_evaluation(original);
    cgai_life_destroy(original);
    cgai_life_destroy(restored);
    cgai_life_destroy(NULL);
    cleanup();
    puts("Public Life ownership, training, audit and continuation checks passed");
    return 0;
}
