/** @file life_events.c @brief Bounded metadata for committed collision frontiers. */
#include "life_events.h"
#include <string.h>

_Static_assert(CGAI_LIFE_MAX_COLLISION_EVENTS == LIFE_MAX_PATCHES,
               "Public collision event capacity must match engine patches");
_Static_assert(CGAI_LIFE_MAX_FRONTIER_CELLS == LIFE_PATCH_CELLS,
               "Public collision frontier capacity must match engine cells");
_Static_assert((int)CGAI_LIFE_COLLISION_UNRESOLVED == (int)LIFE_UNRESOLVED &&
                   (int)CGAI_LIFE_COLLISION_SEPARATED == (int)LIFE_SEPARATED &&
                   (int)CGAI_LIFE_COLLISION_COUPLED == (int)LIFE_COUPLED &&
                   (int)CGAI_LIFE_COLLISION_ABSORBED == (int)LIFE_ABSORBED &&
                   (int)CGAI_LIFE_COLLISION_EXTINCT == (int)LIFE_EXTINCT &&
                   (int)CGAI_LIFE_COLLISION_MERGED == (int)LIFE_MERGED,
               "Public collision outcomes must match physical records");

static cgai_life_collision_outcome committed_outcome(const life_world *world, uint32_t id) {
    for (size_t i = 0U; i < LIFE_MAX_CONFLICTS; ++i) {
        if (world->conflicts[i].id == id)
            return (cgai_life_collision_outcome)world->conflicts[i].outcome;
    }
    return CGAI_LIFE_COLLISION_UNKNOWN;
}

static void capture_identities(const life_world *source, const life_patch *patch,
                               cgai_life_collision_event *event) {
    for (size_t i = 0U; i < LIFE_MODULES; ++i) {
        if ((patch->module_mask & (1U << i)) != 0U) {
            event->group_uids[i] = source->entities[i].uid;
            event->group_ancestry[i] = source->entities[i].ancestry;
        }
    }
    for (size_t i = 0U; i < patch->cell_count; ++i)
        event->frontier_cells[i] = patch->cells[i];
}

static void capture_frontier(const life_run *source, const life_run *result, size_t index,
                             int evaluate, cgai_life_collision_event *event) {
    const life_patch *patch = &result->last_frame.patches[index];
    event->generation = result->world.tick;
    event->conflict_id = patch->conflict_id;
    event->conflict_age = patch->age;
    event->participant_mask = patch->module_mask;
    event->frontier_count = patch->cell_count;
    event->selected_output = result->last_outputs[index];
    event->toggle_bits = life_output_bits(event->selected_output);
    event->legal_output_mask = life_allowed_outputs(patch);
    event->outcome = committed_outcome(&result->world, patch->conflict_id);
    event->teacher_target_valid = !evaluate && result->config.mode != LIFE_MODE_CONWAY ? 1U : 0U;
    if (event->teacher_target_valid != 0U)
        event->teacher_target = result->last_targets[index];
    capture_identities(&source->world, patch, event);
}

static void capture_versions(const life_run *source, const life_run *result,
                             cgai_life_events *batch) {
    const uint64_t source_hash = life_world_hash(&source->world);
    const uint64_t result_hash = life_world_hash(&result->world);
    const uint64_t source_version = life_policy_version(source->policy);
    const uint64_t result_version = life_policy_version(result->policy);
    for (size_t i = 0U; i < batch->event_count; ++i) {
        batch->events[i].source_world_hash = source_hash;
        batch->events[i].result_world_hash = result_hash;
        batch->events[i].source_policy_version = source_version;
        batch->events[i].result_policy_version = result_version;
    }
}

void life_events_capture(const life_run *source, const life_run *result, int evaluate,
                         cgai_life_events *output) {
    memset(output, 0, sizeof(*output));
    output->generation_valid = 1U;
    output->generation = result->world.tick;
    output->event_count = result->last_frame.patch_count;
    for (size_t i = 0U; i < output->event_count; ++i)
        capture_frontier(source, result, i, evaluate, &output->events[i]);
    capture_versions(source, result, output);
}
