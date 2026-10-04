/** @file test_groups.c @brief Configured ownership, authentic high contacts and wire restart. */
#include "centroid_life.h"
#include "life_context.h"
#include "life_io.h"
#include "test_utils.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, "configured Life ownership check failed")

static void block(life_world *world, uint8_t mask) {
    memset(world->cells, 0, sizeof(world->cells));
    memset(world->conflicts, 0, sizeof(world->conflicts));
    for (int y = 10; y < 12; ++y)
        for (int x = 10; x < 12; ++x)
            CHECK(life_world_set(world, x, y, mask) == LIFE_OK);
}

static life_policy_info policy_stats(const life_run *run) {
    life_policy_info info;
    CHECK(life_policy_inspect(run->policy, &info));
    return info;
}

static void group_configurations(void) {
    cgai_life_config config = cgai_life_config_default();
    for (uint32_t count = CGAI_LIFE_MIN_GROUPS; count <= CGAI_LIFE_GROUPS; ++count) {
        cgai_life *owner = NULL;
        cgai_life_stats stats;
        config.group_count = count;
        CHECK(cgai_life_create(&config, &owner) == CGAI_LIFE_OK);
        CHECK(cgai_life_get_stats(owner, &stats) == CGAI_LIFE_OK);
        CHECK(stats.group_count == count && stats.active_group_mask == (1U << count) - 1U);
        for (size_t group = count; group < CGAI_LIFE_GROUPS; ++group)
            CHECK(stats.group_uids[group] == 0U && stats.group_hashes[group] == 0U &&
                  stats.group_steps[group] == 0U);
        cgai_life_destroy(owner);
    }
}

static void legacy_source_default(void) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life *explicit_owner = NULL, *zero_owner = NULL;
    CHECK(config.group_count == CGAI_LIFE_DEFAULT_GROUPS);
    CHECK(cgai_life_create(&config, &explicit_owner) == CGAI_LIFE_OK);
    config.group_count = 0U;
    CHECK(cgai_life_create(&config, &zero_owner) == CGAI_LIFE_OK);
    CHECK(cgai_life_hash(explicit_owner) == cgai_life_hash(zero_owner));
    cgai_life_destroy(explicit_owner);
    cgai_life_destroy(zero_owner);
    config.group_count = 1U;
    CHECK(cgai_life_create(&config, &zero_owner) == CGAI_LIFE_INVALID_ARGUMENT);
    config.group_count = CGAI_LIFE_GROUPS + 1U;
    CHECK(cgai_life_create(&config, &zero_owner) == CGAI_LIFE_INVALID_ARGUMENT);
}

static void high_world(void) {
    life_world world;
    life_frame frame;
    CHECK(life_world_clear_groups(&world, 42U, 8U) == LIFE_OK);
    block(&world, UINT8_C(240));
    CHECK(life_prepare(&world, &frame) == LIFE_OK);
    CHECK(frame.patch_count == 1U && frame.patches[0].module_mask == 240U);
    CHECK(life_apply(&world, &frame, NULL) == LIFE_OK);
    CHECK(life_population(&world, UINT8_C(240)) == 4U);
    CHECK(life_world_merge(&world, UINT8_C(240)) == LIFE_OK);
    CHECK(world.entities[4].ancestry == 240U && world.entities[4].uid == 9U);
    CHECK(world.group_count == 8U && world.entities[0].uid == 1U);
    for (size_t group = 5U; group < 8U; ++group)
        CHECK(world.entities[group].active == 0U);
}

static void isolated_high_update(const life_policy_info *before, const life_policy_info *after) {
    CHECK(before->group_count == 8U && after->group_count == 8U);
    CHECK(before->shared_hash == after->shared_hash);
    for (size_t group = 0U; group < 4U; ++group)
        CHECK(before->module_hash[group] == after->module_hash[group] &&
              before->module_steps[group] == after->module_steps[group]);
    for (size_t group = 4U; group < 8U; ++group)
        CHECK(before->module_hash[group] != after->module_hash[group] &&
              before->module_steps[group] < after->module_steps[group]);
}

static void high_training(life_run *run) {
    const life_run_config config = {42U, 3U, 1U, LIFE_MODE_LEARNED, 0, 8U};
    CHECK(life_run_init(run, &config));
    block(&run->world, UINT8_C(3));
    CHECK(life_run_tick(run) && run->record_count > 0U);
    const life_policy_info before = policy_stats(run);
    CHECK(before.module_steps[0] != 0U && before.module_steps[1] != 0U);
    block(&run->world, UINT8_C(240));
    CHECK(life_run_tick(run));
    CHECK(run->last_frame.patch_count == 1U && run->last_frame.patches[0].module_mask == 240U);
    const life_policy_info after = policy_stats(run);
    isolated_high_update(&before, &after);
}

static void snapshot_restart(life_run *run) {
    const char *path = "test-eight-groups.snapshot";
    life_run resumed = {0};
    CHECK(life_snapshot_save(path, run));
    CHECK(life_snapshot_load(path, &resumed));
    CHECK(life_run_hash(run) == life_run_hash(&resumed));
    CHECK(resumed.world.group_count == 8U && resumed.config.group_count == 8U);
    for (size_t tick = 0U; tick < 3U; ++tick) {
        CHECK(life_run_tick(run) && life_run_tick(&resumed));
        CHECK(life_run_hash(run) == life_run_hash(&resumed));
    }
    life_run_destroy(&resumed);
    CHECK(remove(path) == 0);
}

static cgai_life_context *context_owner(void) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life_context *owner = NULL;
    config.group_count = 8U;
    config.training_epochs = 1U;
    config.enable_merges = 0U;
    CHECK(cgai_life_context_create(&config, &owner) == CGAI_LIFE_OK);
    return owner;
}

static void high_head(cgai_life_context *owner) {
    const uint32_t actions[] = {2U, 3U};
    const cgai_life_context_feedback feedback = {CGAI_LIFE_CONTEXT_TRAIN, 1U, 0U, 733U, 1.0};
    cgai_life_context_choice decision;
    cgai_life_context_choice_stats stats;
    CHECK(cgai_life_context_choice_begin(owner, 0U, "high ownership contact", actions, 2U, 2U,
                                         &decision) == CGAI_LIFE_OK);
    CHECK(decision.participant_mask == 240U);
    CHECK(cgai_life_context_choice_observe(owner, &decision, &feedback) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_choice_get_stats(owner, &stats) == CGAI_LIFE_OK);
    CHECK(stats.group_count == 8U && stats.version == 1U);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        CHECK(stats.group_steps[group] == (group < 4U ? 0U : 1U));
}

static void high_context(cgai_life_context *owner) {
    const cgai_life_context_input input = {"native/eight-owner-proposal",
                                           "high ownership contact centroid context",
                                           0U,
                                           0U,
                                           73U,
                                           CGAI_LIFE_CONTEXT_LLM,
                                           CGAI_LIFE_CONTEXT_TRAIN,
                                           1U,
                                           240U};
    cgai_life_context_stats stats;
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    block(&owner->run.world, UINT8_C(240));
    CHECK(cgai_life_context_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_get_stats(owner, &stats) == CGAI_LIFE_OK);
    CHECK(stats.group_count == 8U && stats.trained_records == 1U);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        CHECK(stats.group_updates[group] == (group < 4U ? 0U : 1U));
    high_head(owner);
}

static void context_restart(cgai_life_context *owner) {
    const char *path = "test-eight-groups.context";
    cgai_life_context *resumed = context_owner();
    CHECK(cgai_life_context_save(owner, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_load(resumed, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_hash(owner) == cgai_life_context_hash(resumed));
    CHECK(cgai_life_context_train_step(owner, 33U, NULL) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_train_step(resumed, 33U, NULL) == CGAI_LIFE_OK);
    CHECK(owner->run.world.group_count == 8U && resumed->run.world.group_count == 8U);
    CHECK(cgai_life_context_hash(owner) == cgai_life_context_hash(resumed));
    cgai_life_context_destroy(resumed);
    CHECK(remove(path) == 0);
}

int main(int argc, char **argv) {
    life_run run = {0};
    (void)argc;
    (void)argv;
    group_configurations();
    legacy_source_default();
    high_world();
    high_training(&run);
    snapshot_restart(&run);
    life_run_destroy(&run);
    cgai_life_context *owner = context_owner();
    high_context(owner);
    context_restart(owner);
    cgai_life_context_destroy(owner);
    return 0;
}
