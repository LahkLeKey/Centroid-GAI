/** @file life_api.c @brief Transactional native ownership for the Life engine. */
#include "centroid_life.h"
#include "life_events.h"
#include "life_io.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define LIFE_API_LAST_START (UINT32_MAX - 9U)

struct cgai_life {
    life_run run;
    cgai_life_events events;
};

_Static_assert(CGAI_LIFE_GROUPS == LIFE_MODULES, "Public Life group capacity must match engine");
_Static_assert(CGAI_LIFE_WIDTH == LIFE_WIDTH, "Public Life width must match engine");
_Static_assert(CGAI_LIFE_HEIGHT == LIFE_HEIGHT, "Public Life height must match engine");
_Static_assert((int)CGAI_LIFE_CONWAY == (int)LIFE_MODE_CONWAY &&
                   (int)CGAI_LIFE_TEACHER == (int)LIFE_MODE_TEACHER &&
                   (int)CGAI_LIFE_LEARNED == (int)LIFE_MODE_LEARNED,
               "Public Life modes must match engine");

cgai_life_config cgai_life_config_default(void) {
    const cgai_life_config config = {42U, 3U, 1U, CGAI_LIFE_LEARNED, 1U, CGAI_LIFE_DEFAULT_GROUPS};
    return config;
}

static int valid_config(const cgai_life_config *config) {
    return config != NULL && config->seed <= UINT32_MAX && config->scenario <= 3U &&
           config->training_epochs <= CGAI_LIFE_MAX_TRAINING_EPOCHS &&
           config->mode >= CGAI_LIFE_CONWAY && config->mode <= CGAI_LIFE_LEARNED &&
           config->enable_merges <= 1U &&
           (config->group_count == 0U || (config->group_count >= CGAI_LIFE_MIN_GROUPS &&
                                          config->group_count <= CGAI_LIFE_GROUPS));
}

static life_run_config private_config(const cgai_life_config *config) {
    const life_run_config result = {config->seed,
                                    config->scenario,
                                    config->training_epochs,
                                    (life_mode)config->mode,
                                    (int)config->enable_merges,
                                    config->group_count};
    return result;
}

cgai_life_status cgai_life_create(const cgai_life_config *config, cgai_life **output) {
    cgai_life *candidate;
    life_run_config internal;
    if (!valid_config(config) || output == NULL || *output != NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    candidate = (cgai_life *)calloc(1U, sizeof(*candidate));
    if (candidate == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    internal = private_config(config);
    if (!life_run_init(&candidate->run, &internal)) {
        cgai_life_destroy(candidate);
        return CGAI_LIFE_OUT_OF_MEMORY;
    }
    *output = candidate;
    return CGAI_LIFE_OK;
}

void cgai_life_destroy(cgai_life *owner) {
    if (owner != NULL) {
        life_run_destroy(&owner->run);
        free(owner);
    }
}

static cgai_life_status advance_generation(cgai_life *owner, int evaluate) {
    life_run candidate = owner->run;
    int ok;
    if (candidate.world.tick > LIFE_API_LAST_START)
        return CGAI_LIFE_LIMIT_REACHED;
    candidate.policy = life_policy_clone(owner->run.policy);
    if (candidate.policy == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    ok = evaluate ? life_run_evaluate_tick(&candidate) : life_run_tick(&candidate);
    if (!ok) {
        life_run_destroy(&candidate);
        return CGAI_LIFE_ENGINE_ERROR;
    }
    life_events_capture(&owner->run, &candidate, evaluate, &owner->events);
    life_run_destroy(&owner->run);
    owner->run = candidate;
    return CGAI_LIFE_OK;
}

static cgai_life_status advance(cgai_life *owner, uint32_t generations, uint32_t *completed,
                                int evaluate) {
    cgai_life_status status = CGAI_LIFE_OK;
    uint32_t count = 0U;
    if (owner == NULL || generations == 0U || generations > CGAI_LIFE_MAX_STEP_GENERATIONS)
        return CGAI_LIFE_INVALID_ARGUMENT;
    while (count < generations) {
        status = advance_generation(owner, evaluate);
        if (status != CGAI_LIFE_OK)
            break;
        ++count;
    }
    if (completed != NULL)
        *completed = count;
    return status;
}

cgai_life_status cgai_life_train_step(cgai_life *owner, uint32_t generations, uint32_t *completed) {
    return advance(owner, generations, completed, 0);
}

cgai_life_status cgai_life_evaluate_step(cgai_life *owner, uint32_t generations,
                                         uint32_t *completed) {
    return advance(owner, generations, completed, 1);
}

static void world_stats(const life_world *world, cgai_life_stats *stats) {
    stats->group_count = world->group_count;
    stats->generation = world->tick;
    stats->population = life_population(world, (uint8_t)((1U << CGAI_LIFE_GROUPS) - 1U));
    stats->collisions = world->stats.collisions;
    stats->separated = world->stats.separated;
    stats->coupled = world->stats.coupled;
    stats->absorbed = world->stats.absorbed;
    stats->extinct = world->stats.extinct;
    stats->merged = world->stats.merged;
    for (size_t i = 0U; i < LIFE_MAX_CONFLICTS; ++i)
        stats->active_conflicts += world->conflicts[i].active != 0U ? 1U : 0U;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i) {
        stats->group_uids[i] = world->entities[i].uid;
        stats->group_populations[i] = life_population(world, (uint8_t)(1U << i));
    }
}

static void policy_stats(const life_policy_info *info, const life_policy *policy,
                         cgai_life_stats *stats) {
    stats->active_group_mask = info->active_mask;
    stats->model_version = info->version;
    stats->policy_hash = life_policy_hash(policy);
    stats->shared_hash = info->shared_hash;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i) {
        stats->group_steps[i] = info->module_steps[i];
        stats->group_hashes[i] = info->module_hash[i];
        stats->group_mass[i] = info->module_mass[i];
    }
}

static void training_stats(const life_run *run, cgai_life_stats *stats) {
    stats->replay_records = (uint32_t)run->record_count;
    stats->collision_records = run->stats.collision_records;
    stats->training_updates = run->stats.training_updates;
    stats->edited_cells = run->stats.edited_cells;
    stats->fallback_calls = run->stats.fallback_calls;
    stats->merge_attempts = run->stats.merge_attempts;
    stats->accepted_merges = run->stats.accepted_merges;
    stats->rejected_merges = run->stats.rejected_merges;
    stats->teacher_agreements = run->stats.teacher_agreements;
    stats->policy_decisions = run->stats.policy_decisions;
    stats->loss_before = run->stats.loss_before;
    stats->loss_after = run->stats.loss_after;
}

cgai_life_status cgai_life_get_stats(const cgai_life *owner, cgai_life_stats *output) {
    life_policy_info info;
    cgai_life_stats stats = {0};
    if (owner == NULL || output == NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    if (!life_policy_inspect(owner->run.policy, &info))
        return CGAI_LIFE_ENGINE_ERROR;
    world_stats(&owner->run.world, &stats);
    policy_stats(&info, owner->run.policy, &stats);
    training_stats(&owner->run, &stats);
    *output = stats;
    return CGAI_LIFE_OK;
}

cgai_life_status cgai_life_get_events(const cgai_life *owner, cgai_life_events *output) {
    if (owner == NULL || output == NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    memcpy(output, &owner->events, sizeof(*output));
    return CGAI_LIFE_OK;
}

uint64_t cgai_life_hash(const cgai_life *owner) {
    return owner != NULL ? life_run_hash(&owner->run) : 0U;
}

static int valid_path(const char *path) { return path != NULL && path[0] != '\0'; }

cgai_life_status cgai_life_save(const cgai_life *owner, const char *path) {
    if (owner == NULL || !valid_path(path))
        return CGAI_LIFE_INVALID_ARGUMENT;
    return life_snapshot_save(path, &owner->run) ? CGAI_LIFE_OK : CGAI_LIFE_IO_ERROR;
}

cgai_life_status cgai_life_load(cgai_life *owner, const char *path) {
    life_run candidate = {0};
    if (owner == NULL || !valid_path(path))
        return CGAI_LIFE_INVALID_ARGUMENT;
    if (!life_snapshot_load(path, &candidate)) {
        life_run_destroy(&candidate);
        return CGAI_LIFE_IO_ERROR;
    }
    life_run_destroy(&owner->run);
    owner->run = candidate;
    memset(&owner->events, 0, sizeof(owner->events));
    return CGAI_LIFE_OK;
}
