/** @file test_events.c @brief Committed collision provenance and transactional feed checks. */
#include "centroid_life.h"
#include "life_events.h"
#include "life_io.h"
#include "test_utils.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, "Committed Life event check failed")
#define EVENT_SNAPSHOT "test-life-events.snapshot"
#define LIMIT_SNAPSHOT "test-life-events-limit.snapshot"

/** Independent action table: fallback, no change, single toggles, then lexicographic pairs. */
static const uint32_t action_bits[] = {0U,  0U, 1U,  2U,  4U,  8U,  16U, 32U, 3U,  5U,  9U, 17U,
                                       33U, 6U, 10U, 18U, 34U, 12U, 20U, 36U, 24U, 40U, 48U};

/** Saved causal state independent of the private run's subsequent mutations. */
typedef struct event_source {
    life_world world;
    uint64_t policy_version;
} event_source;

static cgai_life_config event_config(cgai_life_mode mode, uint32_t epochs, uint32_t merges) {
    return (cgai_life_config){42U, 3U, epochs, mode, merges};
}

static cgai_life *new_owner(const cgai_life_config *config) {
    cgai_life *owner = NULL;
    CHECK(cgai_life_create(config, &owner) == CGAI_LIFE_OK);
    return owner;
}

static void init_reference(life_run *run, const cgai_life_config *config) {
    const life_run_config internal = {config->seed,
                                      config->scenario,
                                      config->training_epochs,
                                      (life_mode)config->mode,
                                      (int)config->enable_merges,
                                      config->group_count};
    CHECK(life_run_init(run, &internal));
}

static event_source source_state(const life_run *run) {
    return (event_source){run->world, life_policy_version(run->policy)};
}

static void same_identities(const cgai_life_collision_event *first,
                            const cgai_life_collision_event *second) {
    CHECK(memcmp(first->group_uids, second->group_uids, sizeof(first->group_uids)) == 0);
    CHECK(memcmp(first->group_ancestry, second->group_ancestry, sizeof(first->group_ancestry)) ==
          0);
    CHECK(memcmp(first->frontier_cells, second->frontier_cells, sizeof(first->frontier_cells)) ==
          0);
}

static void same_event(const cgai_life_collision_event *first,
                       const cgai_life_collision_event *second) {
    CHECK(first->generation == second->generation && first->conflict_id == second->conflict_id);
    CHECK(first->conflict_age == second->conflict_age &&
          first->participant_mask == second->participant_mask);
    CHECK(first->frontier_count == second->frontier_count &&
          first->selected_output == second->selected_output);
    CHECK(first->toggle_bits == second->toggle_bits &&
          first->legal_output_mask == second->legal_output_mask);
    CHECK(first->outcome == second->outcome &&
          first->teacher_target_valid == second->teacher_target_valid);
    CHECK(first->teacher_target == second->teacher_target);
    CHECK(first->source_world_hash == second->source_world_hash &&
          first->result_world_hash == second->result_world_hash);
    CHECK(first->source_policy_version == second->source_policy_version &&
          first->result_policy_version == second->result_policy_version);
    same_identities(first, second);
}

static void same_batch(const cgai_life_events *first, const cgai_life_events *second) {
    CHECK(first->generation_valid == second->generation_valid &&
          first->generation == second->generation && first->event_count == second->event_count);
    for (size_t i = 0U; i < CGAI_LIFE_MAX_COLLISION_EVENTS; ++i)
        same_event(&first->events[i], &second->events[i]);
}

static void empty_feed(const cgai_life *owner) {
    cgai_life_events actual;
    const cgai_life_events empty = {0};
    CHECK(cgai_life_get_events(owner, &actual) == CGAI_LIFE_OK);
    same_batch(&actual, &empty);
}

static uint32_t legal_mask(uint32_t cells) {
    const uint32_t slots = (UINT32_C(1) << cells) - 1U;
    uint32_t legal = 0U;
    for (size_t i = 0U; i < sizeof(action_bits) / sizeof(action_bits[0]); ++i)
        if ((action_bits[i] & ~slots) == 0U)
            legal |= UINT32_C(1) << i;
    return legal;
}

static void check_actions(const cgai_life_collision_event *event, const life_patch *patch,
                          uint32_t output) {
    CHECK(event->frontier_count == patch->cell_count &&
          event->frontier_count <= CGAI_LIFE_MAX_FRONTIER_CELLS);
    CHECK(event->selected_output == output && output < LIFE_OUTPUTS);
    CHECK(event->toggle_bits == action_bits[output]);
    CHECK(event->legal_output_mask == legal_mask(event->frontier_count));
    CHECK((event->legal_output_mask & (UINT32_C(1) << output)) != 0U);
    for (size_t i = 0U; i < CGAI_LIFE_MAX_FRONTIER_CELLS; ++i) {
        const uint32_t expected = i < patch->cell_count ? patch->cells[i] : 0U;
        CHECK(event->frontier_cells[i] == expected && event->frontier_cells[i] < LIFE_CELLS);
    }
}

static void check_identities(const cgai_life_collision_event *event, const life_world *source) {
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i) {
        const int participant = (event->participant_mask & (1U << i)) != 0U;
        CHECK(event->group_uids[i] == (participant ? source->entities[i].uid : 0U));
        CHECK(event->group_ancestry[i] == (participant ? source->entities[i].ancestry : 0U));
    }
}

static cgai_life_collision_outcome expected_outcome(const life_world *world, uint32_t id) {
    for (size_t i = 0U; i < LIFE_MAX_CONFLICTS; ++i)
        if (world->conflicts[i].id == id)
            return (cgai_life_collision_outcome)world->conflicts[i].outcome;
    return CGAI_LIFE_COLLISION_UNKNOWN;
}

static void check_event(const cgai_life_collision_event *event, const event_source *source,
                        const life_run *result, size_t index, int evaluate) {
    const life_patch *patch = &result->last_frame.patches[index];
    const uint32_t has_target = !evaluate && result->config.mode != LIFE_MODE_CONWAY ? 1U : 0U;
    CHECK(event->generation == result->world.tick && event->conflict_id == patch->conflict_id);
    CHECK(event->conflict_age == patch->age && event->participant_mask == patch->module_mask);
    CHECK(event->outcome == expected_outcome(&result->world, patch->conflict_id));
    CHECK(event->source_world_hash == life_world_hash(&source->world));
    CHECK(event->result_world_hash == life_world_hash(&result->world));
    CHECK(event->source_policy_version == source->policy_version);
    CHECK(event->result_policy_version == life_policy_version(result->policy));
    CHECK(event->teacher_target_valid == has_target);
    CHECK(event->teacher_target == (has_target ? result->last_targets[index] : 0U));
    check_identities(event, &source->world);
    check_actions(event, patch, result->last_outputs[index]);
}

static cgai_life_events check_feed(const cgai_life *owner, const event_source *source,
                                   const life_run *result, int evaluate) {
    const uint64_t before = cgai_life_hash(owner);
    const cgai_life_collision_event empty = {0};
    cgai_life_events batch;
    CHECK(cgai_life_get_events(owner, &batch) == CGAI_LIFE_OK);
    CHECK(batch.generation_valid == 1U && batch.generation == result->world.tick);
    CHECK(batch.event_count == result->last_frame.patch_count &&
          batch.event_count <= CGAI_LIFE_MAX_COLLISION_EVENTS);
    for (size_t i = 0U; i < batch.event_count; ++i)
        check_event(&batch.events[i], source, result, i, evaluate);
    for (size_t i = batch.event_count; i < CGAI_LIFE_MAX_COLLISION_EVENTS; ++i)
        same_event(&batch.events[i], &empty);
    CHECK(cgai_life_hash(owner) == before && before == life_run_hash(result));
    return batch;
}

static cgai_life_events advance_pair(cgai_life *owner, life_run *reference, int evaluate) {
    const event_source source = source_state(reference);
    uint32_t completed = 0U;
    const cgai_life_status status = evaluate ? cgai_life_evaluate_step(owner, 1U, &completed)
                                             : cgai_life_train_step(owner, 1U, &completed);
    CHECK(status == CGAI_LIFE_OK && completed == 1U);
    CHECK(evaluate ? life_run_evaluate_tick(reference) : life_run_tick(reference));
    return check_feed(owner, &source, reference, evaluate);
}

static void invalid_getters(const cgai_life *owner) {
    cgai_life_events untouched;
    unsigned char expected[sizeof(untouched)];
    memset(&untouched, 0x5a, sizeof(untouched));
    memcpy(expected, &untouched, sizeof(expected));
    CHECK(cgai_life_get_events(NULL, &untouched) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(memcmp(expected, &untouched, sizeof(expected)) == 0);
    CHECK(cgai_life_get_events(owner, NULL) == CGAI_LIFE_INVALID_ARGUMENT);
}

static void invalid_steps(cgai_life *owner, const cgai_life_events *previous) {
    const uint64_t before = cgai_life_hash(owner);
    uint32_t completed = 77U;
    cgai_life_events after;
    CHECK(cgai_life_train_step(owner, 0U, &completed) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_evaluate_step(owner, CGAI_LIFE_MAX_STEP_GENERATIONS + 1U, &completed) ==
          CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(completed == 77U && cgai_life_hash(owner) == before);
    CHECK(cgai_life_load(owner, "test-life-events-no-such.snapshot") == CGAI_LIFE_IO_ERROR);
    CHECK(cgai_life_get_events(owner, &after) == CGAI_LIFE_OK);
    same_batch(previous, &after);
}

static void training_and_reload(void) {
    const cgai_life_config config = event_config(CGAI_LIFE_LEARNED, 1U, 0U);
    cgai_life *owner = new_owner(&config);
    life_run reference;
    init_reference(&reference, &config);
    empty_feed(owner);
    invalid_getters(owner);
    const cgai_life_events previous = advance_pair(owner, &reference, 0);
    CHECK(previous.event_count != 0U);
    invalid_steps(owner, &previous);
    const uint64_t hash = cgai_life_hash(owner);
    CHECK(cgai_life_save(owner, EVENT_SNAPSHOT) == CGAI_LIFE_OK);
    CHECK(cgai_life_load(owner, EVENT_SNAPSHOT) == CGAI_LIFE_OK);
    CHECK(cgai_life_hash(owner) == hash);
    empty_feed(owner);
    (void)advance_pair(owner, &reference, 0);
    CHECK(remove(EVENT_SNAPSHOT) == 0);
    life_run_destroy(&reference);
    cgai_life_destroy(owner);
}

static void same_training_stats(const life_run_stats *before, const life_run_stats *after) {
    CHECK(before->collision_records == after->collision_records);
    CHECK(before->training_updates == after->training_updates);
    CHECK(before->edited_cells == after->edited_cells &&
          before->fallback_calls == after->fallback_calls);
    CHECK(before->merge_attempts == after->merge_attempts &&
          before->accepted_merges == after->accepted_merges);
    CHECK(before->rejected_merges == after->rejected_merges);
    CHECK(before->teacher_agreements == after->teacher_agreements &&
          before->policy_decisions == after->policy_decisions);
    CHECK(before->loss_before == after->loss_before && before->loss_after == after->loss_after);
}

static void same_replay(const life_run *before, const life_run *after) {
    CHECK(before->record_count == after->record_count &&
          before->record_cursor == after->record_cursor);
    CHECK(before->last_merge_attempt == after->last_merge_attempt);
    CHECK(memcmp(before->last_targets, after->last_targets, sizeof(before->last_targets)) == 0);
    for (size_t i = 0U; i < before->record_count; ++i) {
        CHECK(before->records[i].target == after->records[i].target &&
              before->records[i].module_mask == after->records[i].module_mask);
        CHECK(memcmp(before->records[i].state.values, after->records[i].state.values,
                     sizeof(before->records[i].state.values)) == 0);
    }
    same_training_stats(&before->stats, &after->stats);
}

static void frozen_feed(void) {
    const cgai_life_config config = event_config(CGAI_LIFE_LEARNED, 1U, 0U);
    cgai_life *owner = new_owner(&config);
    life_run reference;
    init_reference(&reference, &config);
    const cgai_life_events trained = advance_pair(owner, &reference, 0);
    CHECK(trained.event_count != 0U && trained.events[0].teacher_target_valid == 1U);
    CHECK(trained.events[0].teacher_target != 0U);
    const life_run training_before = reference;
    const uint64_t policy_hash = life_policy_hash(reference.policy);
    const cgai_life_events frozen = advance_pair(owner, &reference, 1);
    CHECK(frozen.event_count != 0U);
    CHECK(life_policy_hash(reference.policy) == policy_hash);
    same_replay(&training_before, &reference);
    life_run_destroy(&reference);
    cgai_life_destroy(owner);
}

static void multiple_generations(void) {
    const cgai_life_config config = event_config(CGAI_LIFE_TEACHER, 0U, 0U);
    cgai_life *owner = new_owner(&config);
    life_run reference;
    event_source source;
    uint32_t completed = 0U;
    init_reference(&reference, &config);
    CHECK(cgai_life_train_step(owner, 3U, &completed) == CGAI_LIFE_OK && completed == 3U);
    for (size_t i = 0U; i < 3U; ++i) {
        source = source_state(&reference);
        CHECK(life_run_tick(&reference));
    }
    const cgai_life_events batch = check_feed(owner, &source, &reference, 0);
    CHECK(batch.generation == 3U);
    life_run_destroy(&reference);
    cgai_life_destroy(owner);
}

static void stable_contact(life_world *world, uint32_t seed) {
    life_world_clear(world, seed);
    for (int y = 10; y < 12; ++y)
        for (int x = 10; x < 12; ++x)
            CHECK(life_world_set(world, x, y, 3U) == LIFE_OK);
}

static cgai_life *limit_fixture(life_run *reference) {
    const cgai_life_config config = event_config(CGAI_LIFE_CONWAY, 0U, 0U);
    init_reference(reference, &config);
    stable_contact(&reference->world, (uint32_t)config.seed);
    reference->world.tick = UINT32_MAX - 10U;
    CHECK(life_snapshot_save(LIMIT_SNAPSHOT, reference));
    cgai_life *owner = new_owner(&config);
    CHECK(cgai_life_load(owner, LIMIT_SNAPSHOT) == CGAI_LIFE_OK);
    CHECK(remove(LIMIT_SNAPSHOT) == 0);
    empty_feed(owner);
    return owner;
}

static void failed_generation(cgai_life *owner, const cgai_life_events *previous) {
    const uint64_t hash = cgai_life_hash(owner);
    cgai_life_events after;
    uint32_t completed = 77U;
    CHECK(cgai_life_train_step(owner, 1U, &completed) == CGAI_LIFE_LIMIT_REACHED);
    CHECK(completed == 0U && cgai_life_hash(owner) == hash);
    CHECK(cgai_life_get_events(owner, &after) == CGAI_LIFE_OK);
    same_batch(previous, &after);
}

static void partially_committed_call(void) {
    life_run reference;
    cgai_life *owner = limit_fixture(&reference);
    uint32_t completed = 0U;
    CHECK(cgai_life_train_step(owner, 3U, &completed) == CGAI_LIFE_LIMIT_REACHED);
    CHECK(completed == 2U);
    CHECK(life_run_tick(&reference));
    const event_source source = source_state(&reference);
    CHECK(life_run_tick(&reference));
    const cgai_life_events batch = check_feed(owner, &source, &reference, 0);
    CHECK(batch.event_count != 0U && batch.generation == UINT32_MAX - 8U);
    failed_generation(owner, &batch);
    life_run_destroy(&reference);
    cgai_life_destroy(owner);
}

/** Deterministic observer fixture complements model-gated merge coverage below. */
static void physical_merge_provenance(void) {
    const cgai_life_config config = event_config(CGAI_LIFE_CONWAY, 0U, 0U);
    life_run source, candidate;
    cgai_life_events batch;
    init_reference(&source, &config);
    stable_contact(&source.world, (uint32_t)config.seed);
    candidate = source;
    candidate.policy = life_policy_clone(source.policy);
    CHECK(candidate.policy != NULL && life_run_tick(&candidate));
    CHECK(candidate.last_frame.patch_count == 1U);
    CHECK(life_world_merge(&candidate.world, 3U) == LIFE_OK);
    life_events_capture(&source, &candidate, 0, &batch);
    const event_source before = source_state(&source);
    check_event(&batch.events[0], &before, &candidate, 0U, 0);
    CHECK(batch.events[0].outcome == CGAI_LIFE_COLLISION_MERGED);
    CHECK(batch.events[0].group_uids[0] != candidate.world.entities[0].uid);
    CHECK(batch.events[0].group_ancestry[0] != candidate.world.entities[0].ancestry);
    CHECK(batch.events[0].group_uids[1] != 0U && candidate.world.entities[1].active == 0U);
    life_run_destroy(&candidate);
    life_run_destroy(&source);
}

static uint32_t accepted_merge_generation(cgai_life *owner, life_run *reference) {
    const life_world parents = reference->world;
    const uint64_t accepted_before = reference->stats.accepted_merges;
    const cgai_life_events batch = advance_pair(owner, reference, 0);
    uint32_t covered = 0U;
    if (reference->stats.accepted_merges == accepted_before)
        return 0U;
    for (size_t i = 0U; i < batch.event_count; ++i) {
        if (batch.events[i].outcome == CGAI_LIFE_COLLISION_MERGED) {
            check_identities(&batch.events[i], &parents);
            ++covered;
        }
    }
    return covered;
}

static void model_merge_provenance(void) {
    const cgai_life_config config = event_config(CGAI_LIFE_LEARNED, 8U, 1U);
    cgai_life *owner = new_owner(&config);
    life_run reference;
    uint32_t covered = 0U;
    init_reference(&reference, &config);
    for (size_t i = 0U; i < 64U; ++i)
        covered += accepted_merge_generation(owner, &reference);
    CHECK(reference.stats.accepted_merges != 0U && covered != 0U);
    life_run_destroy(&reference);
    cgai_life_destroy(owner);
}

int main(void) {
    training_and_reload();
    frozen_feed();
    multiple_generations();
    partially_committed_call();
    physical_merge_provenance();
    model_merge_provenance();
    puts("Committed Life event metadata, provenance and publication checks passed");
    return 0;
}
