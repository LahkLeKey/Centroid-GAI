/** @file test_domain.c @brief Native probe output, frozen rounds, isolation and exact restart. */
#include "centroid_life_domain.h"
#include "life_domain.h"
#include "life_io.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CHECK(condition) TEST_CHECK(condition, "Native Life domain probe check failed")
#define PATH_BYTES 4097U

static cgai_life_domain *new_owner(uint32_t groups, uint32_t epochs) {
    cgai_life_domain *owner = NULL;
    cgai_life_config config = cgai_life_config_default();
    config.group_count = groups;
    config.training_epochs = epochs;
    config.enable_merges = 0U;
    CHECK(cgai_life_domain_create(&config, CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE, &owner) ==
          CGAI_LIFE_OK);
    CHECK(owner != NULL && life_domain_valid(owner));
    return owner;
}

static cgai_life_domain_task task(uint32_t mask, uint32_t example) {
    cgai_life_domain_task result = {0};
    result.version = CGAI_LIFE_DOMAIN_VERSION;
    result.family = example + 1U;
    result.split = CGAI_LIFE_CONTEXT_TRAIN;
    result.reviewed = 1U;
    result.source_hash = UINT64_C(10001) + example;
    result.x = example % 2U == 0U ? 1U : 0U;
    result.y = example % 2U == 0U ? 0U : 2U;
    result.observed_fields = 3U;
    result.legal_actions = 7U;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i)
        if ((mask & (1U << i)) != 0U)
            result.eligible_uids[result.eligible_count++] = (uint32_t)i + 1U;
    return result;
}

static uint64_t enqueue(cgai_life_domain *owner, const cgai_life_domain_task *input) {
    uint64_t id = 0U;
    CHECK(cgai_life_domain_enqueue(owner, input, &id) == CGAI_LIFE_OK && id != 0U);
    return id;
}

static void enqueue_pair(cgai_life_domain *owner, uint32_t mask, uint32_t family) {
    const cgai_life_domain_task first = task(mask, family), second = task(mask, family + 1U);
    CHECK(enqueue(owner, &first) != enqueue(owner, &second));
}

static cgai_life_domain_stats stats(const cgai_life_domain *owner) {
    cgai_life_domain_stats result;
    CHECK(cgai_life_domain_get_stats(owner, &result) == CGAI_LIFE_OK);
    return result;
}

static void reset_world(cgai_life_domain *owner) {
    life_world next;
    const life_world *previous = &owner->run.world;
    CHECK(life_world_clear_groups(&next, previous->seed, previous->group_count) == LIFE_OK);
    next.tick = previous->tick;
    next.stats = previous->stats;
    next.next_uid = previous->next_uid;
    next.next_conflict_id = previous->next_conflict_id;
    memcpy(next.entities, previous->entities, sizeof(next.entities));
    owner->run.world = next;
}

static void block(cgai_life_domain *owner, int x, int y, uint8_t mask) {
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
            CHECK(life_world_set(&owner->run.world, x + dx, y + dy, mask) == LIFE_OK);
}

static void contact(cgai_life_domain *owner, uint8_t mask) {
    reset_world(owner);
    block(owner, 10, 10, mask);
    uint32_t completed = 77U;
    CHECK(cgai_life_domain_train_step(owner, 1U, &completed) == CGAI_LIFE_OK && completed == 1U);
    CHECK(life_domain_valid(owner));
}

static uint32_t predict(const cgai_life_domain *owner, const cgai_life_domain_task *input) {
    cgai_life_domain_prediction prediction;
    const uint64_t before = cgai_life_domain_hash(owner);
    CHECK(cgai_life_domain_predict(owner, input, input->eligible_uids, input->eligible_count,
                                   &prediction) == CGAI_LIFE_OK);
    CHECK(cgai_life_domain_hash(owner) == before);
    CHECK(prediction.context_hash == life_probe_task_hash(input));
    CHECK(prediction.model_version == owner->version &&
          prediction.participant_count == input->eligible_count);
    CHECK(isfinite(prediction.probability) && prediction.probability >= 0.0 &&
          prediction.probability <= 1.0);
    return prediction.action;
}

static void freeze_record(const cgai_life_domain_record *record, uint32_t mask) {
    CHECK(record->participant_mask == mask && record->parent_version == 0U);
    CHECK(record->verified == 1U && record->target == 1U && record->executed_action == 0U);
    CHECK(record->teacher_identity == LIFE_PROBE_TEACHER && record->evidence_hash != 0U);
    CHECK(record->admission == CGAI_LIFE_DOMAIN_ADMITTED);
    CHECK(record->task.split == CGAI_LIFE_CONTEXT_TRAIN &&
          record->context_hash == life_probe_task_hash(&record->task));
    CHECK(record->contact.teacher_target_valid == 1U && record->contact.participant_mask == mask);
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i) {
        CHECK(record->proposals[i] == 0U);
        CHECK(record->participant_uids[i] == ((mask & (1U << i)) != 0U ? (uint32_t)i + 1U : 0U));
        CHECK((mask & (1U << i)) != 0U ? fabs(record->probabilities[i] - 1.0 / 3.0) < 1e-12
                                       : record->probabilities[i] == 0.0);
    }
}

static void cold_round(const cgai_life_domain *owner, uint32_t count, uint32_t mask) {
    const cgai_life_domain_round *round = &owner->round;
    CHECK(round->count == count && round->parent_version == 0U && round->result_version == 1U);
    for (size_t i = 0U; i < count; ++i)
        freeze_record(&round->records[i], mask);
}

static void low_unchanged(const cgai_life_domain_stats *before,
                          const cgai_life_domain_stats *after) {
    CHECK(before->shared_hash == after->shared_hash &&
          before->cell_shared_hash == after->cell_shared_hash);
    for (size_t i = 0U; i < 4U; ++i) {
        CHECK(before->group_hashes[i] == after->group_hashes[i]);
        CHECK(before->group_steps[i] == after->group_steps[i]);
        CHECK(before->cell_group_hashes[i] == after->cell_group_hashes[i]);
        CHECK(before->cell_group_steps[i] == after->cell_group_steps[i]);
    }
}

static void high_steps(const cgai_life_domain_stats *before, const cgai_life_domain_stats *after) {
    for (size_t i = 4U; i < 8U; ++i) {
        CHECK(after->group_steps[i] == 8U && before->group_hashes[i] != after->group_hashes[i]);
        CHECK(after->cell_group_steps[i] > before->cell_group_steps[i]);
        CHECK(after->group_visits[i] == 2U && after->group_deferred[i] == 0U);
    }
}

static void high_learning(void) {
    cgai_life_domain *owner = new_owner(8U, 4U);
    const cgai_life_domain_task input = task(240U, 0U);
    CHECK(predict(owner, &input) == 0U);
    const cgai_life_domain_stats before = stats(owner);
    enqueue_pair(owner, 240U, 0U);
    contact(owner, 240U);
    const cgai_life_domain_stats after = stats(owner);
    cold_round(owner, 2U, 240U);
    low_unchanged(&before, &after);
    CHECK(after.version == 1U && after.observations == 2U && after.training_updates == 8U);
    CHECK(after.queued == 0U && after.replay_records == 2U && after.expert_count == 32U);
    high_steps(&before, &after);
    CHECK(predict(owner, &input) == 1U);
    reset_world(owner);
    CHECK(life_population(&owner->run.world, 240U) == 0U && predict(owner, &input) == 1U);
    cgai_life_domain_destroy(owner);
}

static void existing_low_replay(void) {
    cgai_life_domain *owner = new_owner(8U, 4U);
    enqueue_pair(owner, 3U, 0U);
    contact(owner, 3U);
    const cgai_life_domain_stats before = stats(owner);
    CHECK(before.group_steps[0] != 0U && before.cell_group_steps[0] != 0U);
    enqueue_pair(owner, 240U, 2U);
    contact(owner, 240U);
    const cgai_life_domain_stats after = stats(owner);
    low_unchanged(&before, &after);
    CHECK(after.training_updates == before.training_updates + 8U);
    for (size_t i = 4U; i < 8U; ++i)
        CHECK(after.group_steps[i] == 8U);
    cgai_life_domain_destroy(owner);
}

static void disjoint_contacts(void) {
    cgai_life_domain *owner = new_owner(8U, 1U);
    enqueue_pair(owner, 240U, 0U);
    reset_world(owner);
    block(owner, 4, 4, 48U);
    block(owner, 20, 20, 192U);
    CHECK(cgai_life_domain_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(owner->round.count == 4U && owner->round.parent_version == 0U);
    CHECK(owner->round.records[0].participant_mask == 48U &&
          owner->round.records[2].participant_mask == 192U);
    CHECK(owner->round.records[0].task_id == owner->round.records[2].task_id);
    for (size_t i = 0U; i < owner->round.count; ++i)
        freeze_record(&owner->round.records[i], i < 2U ? 48U : 192U);
    for (size_t i = 4U; i < 8U; ++i)
        CHECK(owner->probe.steps[i] == 2U);
    cgai_life_domain_destroy(owner);
}

static void uid_remapping(void) {
    cgai_life_domain *owner = new_owner(8U, 2U);
    const cgai_life_domain_task input = task(48U, 0U);
    enqueue(owner, &input);
    reset_world(owner);
    const life_entity fifth = owner->run.world.entities[4];
    owner->run.world.entities[4] = owner->run.world.entities[7];
    owner->run.world.entities[7] = fifth;
    block(owner, 10, 10, 160U);
    CHECK(cgai_life_domain_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(owner->round.count == 1U && owner->round.records[0].contact.participant_mask == 160U);
    CHECK(owner->round.records[0].participant_mask == 48U);
    CHECK(owner->probe.steps[4] == 2U && owner->probe.steps[5] == 2U &&
          owner->probe.steps[7] == 0U);
    CHECK(predict(owner, &input) == 1U);
    cgai_life_domain_destroy(owner);
}

static void zero_epochs(void) {
    cgai_life_domain *owner = new_owner(8U, 0U);
    const cgai_life_domain_stats before = stats(owner);
    const cgai_life_domain_task input = task(240U, 0U);
    enqueue_pair(owner, 240U, 0U);
    contact(owner, 240U);
    const cgai_life_domain_stats after = stats(owner);
    CHECK(after.version == 0U && after.observations == 2U && after.replay_records == 2U);
    CHECK(after.training_updates == 0U && after.cell_policy_hash == before.cell_policy_hash);
    for (size_t i = 0U; i < 8U; ++i) {
        CHECK(after.group_hashes[i] == before.group_hashes[i] && after.group_steps[i] == 0U);
        CHECK(owner->round.before.group_hashes[i] == owner->round.after.group_hashes[i]);
    }
    CHECK(predict(owner, &input) == 0U);
    cgai_life_domain_destroy(owner);
}

static void deferred_round(void) {
    cgai_life_domain *owner = new_owner(8U, 2U);
    enqueue_pair(owner, 240U, 0U);
    contact(owner, 240U);
    cgai_life_domain_task unsupported = task(240U, 4U);
    unsupported.legal_actions = 1U;
    const cgai_life_domain_stats before = stats(owner);
    enqueue(owner, &unsupported);
    contact(owner, 240U);
    const cgai_life_domain_stats after = stats(owner);
    CHECK(after.deferred == 1U && after.observations == before.observations);
    CHECK(after.version == before.version && after.training_updates == before.training_updates);
    CHECK(owner->round.records[0].admission == CGAI_LIFE_DOMAIN_UNSUPPORTED_TARGET);
    for (size_t i = 0U; i < 8U; ++i)
        CHECK(after.group_hashes[i] == before.group_hashes[i]);
    cgai_life_domain_destroy(owner);
}

static void rejected_task(cgai_life_domain *owner, const cgai_life_domain_task *input) {
    const uint64_t before = cgai_life_domain_hash(owner);
    uint64_t id = 77U;
    CHECK(cgai_life_domain_enqueue(owner, input, &id) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(id == 77U && cgai_life_domain_hash(owner) == before);
}

static void invalid_inputs(void) {
    cgai_life_domain *owner = new_owner(8U, 2U);
    cgai_life_domain_task input = task(240U, 0U);
    const uint64_t first = enqueue(owner, &input);
    const uint64_t before = cgai_life_domain_hash(owner);
    CHECK(enqueue(owner, &input) == first && cgai_life_domain_hash(owner) == before);
    input.split = CGAI_LIFE_CONTEXT_DEVELOPMENT;
    rejected_task(owner, &input);
    CHECK(predict(owner, &input) == 0U);
    input.split = CGAI_LIFE_CONTEXT_TRAIN;
    input.observed_fields = 1U;
    rejected_task(owner, &input);
    input.observed_fields = 3U;
    input.eligible_uids[0] = 99U;
    rejected_task(owner, &input);
    uint32_t completed = 77U;
    CHECK(cgai_life_domain_train_step(owner, 0U, &completed) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(completed == 77U && cgai_life_domain_hash(owner) == before);
    cgai_life_domain_destroy(owner);
}

static void frozen_evaluation(void) {
    cgai_life_domain *owner = new_owner(8U, 2U);
    enqueue_pair(owner, 240U, 0U);
    contact(owner, 240U);
    enqueue_pair(owner, 240U, 2U);
    const uint64_t before = life_domain_payload_hash(owner),
                   cell = life_policy_hash(owner->run.policy);
    const uint64_t updates = owner->run.stats.training_updates;
    const uint32_t generation = owner->run.world.tick;
    CHECK(cgai_life_domain_evaluate_step(owner, 3U, NULL) == CGAI_LIFE_OK);
    CHECK(owner->run.world.tick == generation + 3U);
    CHECK(life_domain_payload_hash(owner) == before && life_policy_hash(owner->run.policy) == cell);
    CHECK(owner->run.stats.training_updates == updates && owner->queue_count == 2U);
    cgai_life_domain_destroy(owner);
}

static void encounter_renewal(void) {
    cgai_life_domain *owner = new_owner(8U, 0U);
    CHECK(cgai_life_domain_evaluate_step(owner, 32U, NULL) == CGAI_LIFE_OK);
    const life_entity fifth = owner->run.world.entities[4];
    owner->run.world.entities[4] = owner->run.world.entities[7];
    owner->run.world.entities[7] = fifth;
    cgai_life_domain *frozen = life_domain_clone(owner);
    CHECK(frozen != NULL);
    const uint64_t payload = life_domain_payload_hash(frozen);
    enqueue_pair(owner, 240U, 0U);
    CHECK(cgai_life_domain_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(owner->run.world.tick == 33U && owner->encounter_renewals == 1U);
    CHECK(owner->run.world.entities[7].uid == 5U && owner->run.world.entities[4].uid == 8U);
    CHECK(owner->probe.model->training_step == 0U && owner->observations != 0U);
    CHECK(cgai_life_domain_evaluate_step(frozen, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(frozen->run.world.tick == 33U && frozen->encounter_renewals == 0U);
    CHECK(life_domain_payload_hash(frozen) == payload);
    cgai_life_domain_destroy(owner);
    cgai_life_domain_destroy(frozen);
}

static void path_name(char *path, const char *directory, const char *label) {
#ifdef _WIN32
    const unsigned int pid = (unsigned int)GetCurrentProcessId();
#else
    const unsigned int pid = (unsigned int)getpid();
#endif
    const int written = snprintf(path, PATH_BYTES, "%s/domain-%u-%s.bin", directory, pid, label);
    CHECK(written > 0 && (size_t)written < PATH_BYTES);
}

static void changed_file(const char *path) {
    FILE *file = fopen(path, "r+b");
    CHECK(file != NULL);
    const int first = fgetc(file);
    CHECK(first != EOF && fseek(file, 0L, SEEK_SET) == 0);
    CHECK(fputc(first ^ 1, file) != EOF && fclose(file) == 0);
}

static void fixture_word(FILE *file, uint64_t value) {
    unsigned char bytes[8];
    for (size_t i = 0U; i < 8U; ++i)
        bytes[i] = (unsigned char)(value >> (8U * i));
    CHECK(fwrite(bytes, 1U, sizeof(bytes), file) == sizeof(bytes));
}

/* Deliberately malformed complete owner: test-only writer skips public validation.
 * Production save never accepts these states, including self-consistent hashes. */
static void malformed_owner(const char *path, const cgai_life_domain *owner) {
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    fixture_word(file, UINT64_C(0x4c444f4d41494e31));
    fixture_word(file, CGAI_LIFE_DOMAIN_VERSION);
    CHECK(life_snapshot_write(file, &owner->run));
    CHECK(life_domain_payload_write(file, owner));
    fixture_word(file, cgai_life_domain_hash(owner));
    fixture_word(file, UINT64_C(0x454e44444f4d4149));
    CHECK(fclose(file) == 0);
}

static void load_rejected(cgai_life_domain *owner, const char *path) {
    const uint64_t before = cgai_life_domain_hash(owner);
    CHECK(cgai_life_domain_load(owner, path) == CGAI_LIFE_IO_ERROR);
    CHECK(cgai_life_domain_hash(owner) == before);
}

static void semantic_corruption(cgai_life_domain *owner, const char *path) {
    cgai_life_domain *broken = life_domain_clone(owner);
    CHECK(broken != NULL);
    ++broken->version;
    broken->round.result_version = broken->version;
    malformed_owner(path, broken);
    load_rejected(owner, path);
    broken->version = owner->version;
    broken->round.result_version = owner->version;
    broken->probe.steps[4] = 0U;
    malformed_owner(path, broken);
    load_rejected(owner, path);
    cgai_life_domain_destroy(broken);
}

static void receipt_corruption(cgai_life_domain *owner, const char *path) {
    cgai_life_domain *broken = life_domain_clone(owner);
    CHECK(broken != NULL);
    broken->round.records[1] = broken->round.records[0];
    malformed_owner(path, broken);
    load_rejected(owner, path);
    broken->round = owner->round;
    broken->replay[0].task.split = CGAI_LIFE_CONTEXT_AUDIT;
    broken->replay[0].context_hash = life_probe_task_hash(&broken->replay[0].task);
    broken->replay[0].evidence_hash = life_domain_evidence(&broken->replay[0]);
    malformed_owner(path, broken);
    load_rejected(owner, path);
    cgai_life_domain_destroy(broken);
}

static void boundary_corruption(cgai_life_domain *owner, const char *path) {
    cgai_life_domain *broken = life_domain_clone(owner);
    CHECK(broken != NULL);
    /* This UID really learned earlier, but it is unrelated to the latest round.
     * A fabricated smaller before clock must not claim it learned this round. */
    CHECK(broken->probe.steps[0] != 0U && (broken->round.records[0].participant_mask & 1U) == 0U);
    --broken->round.before.group_steps[0];
    malformed_owner(path, broken);
    load_rejected(owner, path);
    broken->round = owner->round;
    broken->group_visits[4] = 0U;
    malformed_owner(path, broken);
    load_rejected(owner, path);
    cgai_life_domain_destroy(broken);
}

static void nonfinite_file(const char *path, const cgai_life_domain *owner) {
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    fixture_word(file, UINT64_C(0x4c444f4d41494e31));
    fixture_word(file, CGAI_LIFE_DOMAIN_VERSION);
    CHECK(life_snapshot_write(file, &owner->run));
    const long payload_start = ftell(file);
    CHECK(payload_start >= 0L && life_domain_payload_write(file, owner));
    fixture_word(file, cgai_life_domain_hash(owner));
    fixture_word(file, UINT64_C(0x454e44444f4d4149));
    /* controls13, parametercount1, UIDs8, clocks/mass16 precede weights. */
    CHECK(fseek(file, payload_start + 38L * 8L, SEEK_SET) == 0);
    fixture_word(file, UINT64_C(0x7ff8000000000001));
    CHECK(fclose(file) == 0);
}

static void zero_epoch_restart(const char *directory) {
    cgai_life_domain *owner = new_owner(8U, 0U), *restored = new_owner(2U, 1U);
    char path[PATH_BYTES];
    path_name(path, directory, "zero-epochs");
    enqueue_pair(owner, 240U, 0U);
    contact(owner, 240U);
    CHECK(cgai_life_domain_save(owner, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_domain_load(restored, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_domain_hash(owner) == cgai_life_domain_hash(restored));
    const life_entity fifth = owner->run.world.entities[4];
    owner->run.world.entities[4] = owner->run.world.entities[7];
    owner->run.world.entities[7] = fifth;
    CHECK(cgai_life_domain_save(owner, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_domain_load(restored, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_domain_hash(owner) == cgai_life_domain_hash(restored));
    CHECK(restored->probe.model->training_step == 0U && restored->observations == 2U);
    CHECK(restored->run.world.entities[7].uid == 5U && restored->probe.uids[4] == 5U);
    CHECK(remove(path) == 0);
    cgai_life_domain_destroy(owner);
    cgai_life_domain_destroy(restored);
}

static void checkpoint_adversaries(const char *directory) {
    cgai_life_domain *owner = new_owner(8U, 2U);
    char path[PATH_BYTES];
    path_name(path, directory, "malformed");
    enqueue_pair(owner, 3U, 2U);
    contact(owner, 3U);
    enqueue_pair(owner, 240U, 0U);
    contact(owner, 240U);
    semantic_corruption(owner, path);
    receipt_corruption(owner, path);
    boundary_corruption(owner, path);
    nonfinite_file(path, owner);
    load_rejected(owner, path);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL && fputs("truncated", file) >= 0 && fclose(file) == 0);
    load_rejected(owner, path);
    CHECK(remove(path) == 0);
    cgai_life_domain_destroy(owner);
}

static void continuation(const char *directory) {
    cgai_life_domain *whole = new_owner(8U, 2U), *restored = new_owner(2U, 0U);
    char path[PATH_BYTES];
    enqueue_pair(whole, 240U, 0U);
    contact(whole, 240U);
    enqueue_pair(whole, 240U, 2U);
    path_name(path, directory, "resume");
    CHECK(cgai_life_domain_save(whole, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_domain_load(restored, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_domain_hash(whole) == cgai_life_domain_hash(restored));
    CHECK(memcmp(&whole->round, &restored->round, sizeof(whole->round)) == 0);
    contact(whole, 240U);
    contact(restored, 240U);
    CHECK(cgai_life_domain_hash(whole) == cgai_life_domain_hash(restored));
    changed_file(path);
    const uint64_t before = cgai_life_domain_hash(restored);
    CHECK(cgai_life_domain_load(restored, path) == CGAI_LIFE_IO_ERROR);
    CHECK(cgai_life_domain_hash(restored) == before);
    CHECK(remove(path) == 0);
    cgai_life_domain_destroy(whole);
    cgai_life_domain_destroy(restored);
}

static void failed_generation(void) {
    cgai_life_domain *owner = new_owner(8U, 1U);
    enqueue_pair(owner, 240U, 0U);
    reset_world(owner);
    block(owner, 10, 10, 240U);
    owner->run.world.next_uid = 0U;
    const uint64_t before = cgai_life_domain_hash(owner);
    uint32_t completed = 77U;
    CHECK(cgai_life_domain_train_step(owner, 1U, &completed) == CGAI_LIFE_ENGINE_ERROR);
    CHECK(completed == 0U && cgai_life_domain_hash(owner) == before);
    CHECK(owner->queue_count == 2U && owner->observations == 0U && owner->version == 0U);
    cgai_life_domain_destroy(owner);
}

static void configured_counts(void) {
    for (uint32_t groups = 2U; groups <= 8U; ++groups) {
        cgai_life_domain *owner = new_owner(groups, 1U);
        const cgai_life_domain_stats info = stats(owner);
        CHECK(info.group_count == groups && info.expert_count == groups * 4U);
        CHECK(owner->run.world.group_count == groups &&
              life_policy_group_count(owner->run.policy) == groups);
        cgai_life_domain_destroy(owner);
    }
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    configured_counts();
    high_learning();
    existing_low_replay();
    disjoint_contacts();
    uid_remapping();
    zero_epochs();
    deferred_round();
    invalid_inputs();
    frozen_evaluation();
    encounter_renewal();
    failed_generation();
    continuation(argv[1]);
    checkpoint_adversaries(argv[1]);
    zero_epoch_restart(argv[1]);
    puts("Native Life domain: typed verified probe output, frozen rounds, UID isolation and "
         "continuation pass");
    return 0;
}
