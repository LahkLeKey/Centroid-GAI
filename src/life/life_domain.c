/** @file life_domain.c @brief Public native typed ownership and authentic round transactions. */
#include "life_domain.h"
#include "life_events.h"
#include <stdlib.h>
#include <string.h>

static uint32_t configured_groups(const cgai_life_config *config) {
    return config->group_count == 0U ? CGAI_LIFE_DEFAULT_GROUPS : config->group_count;
}

int life_domain_kind_valid(cgai_life_domain_kind kind) {
    return kind == CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE || kind == CGAI_LIFE_DOMAIN_NATIVE_NPC;
}

static int config_valid(const cgai_life_config *config, cgai_life_domain_kind kind) {
    return config != NULL && life_domain_kind_valid(kind) &&
           configured_groups(config) >= CGAI_LIFE_MIN_GROUPS &&
           configured_groups(config) <= CGAI_LIFE_GROUPS && config->seed <= UINT32_MAX &&
           config->scenario <= 3U && config->training_epochs <= CGAI_LIFE_MAX_TRAINING_EPOCHS &&
           config->mode == CGAI_LIFE_LEARNED && config->enable_merges == 0U;
}

cgai_life_status cgai_life_domain_create(const cgai_life_config *config, cgai_life_domain_kind kind,
                                         cgai_life_domain **output) {
    if (!config_valid(config, kind) || output == NULL || *output != NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    cgai_life_domain *owner = calloc(1U, sizeof(*owner));
    if (owner == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    const life_run_config recipe = {config->seed,
                                    config->scenario,
                                    config->training_epochs,
                                    LIFE_MODE_LEARNED,
                                    0,
                                    configured_groups(config)};
    if (!life_run_init(&owner->run, &recipe) ||
        !life_probe_init_kind(&owner->probe, config->seed, configured_groups(config), kind)) {
        cgai_life_domain_destroy(owner);
        return CGAI_LIFE_OUT_OF_MEMORY;
    }
    owner->kind = kind;
    owner->next_task_id = 1U;
    *output = owner;
    return CGAI_LIFE_OK;
}

void cgai_life_domain_destroy(cgai_life_domain *owner) {
    if (owner != NULL) {
        life_run_destroy(&owner->run);
        life_probe_destroy(&owner->probe);
        free(owner);
    }
}

cgai_life_domain *life_domain_clone(const cgai_life_domain *owner) {
    cgai_life_domain *candidate = malloc(sizeof(*candidate));
    if (candidate == NULL)
        return NULL;
    *candidate = *owner;
    candidate->run.policy = life_policy_clone(owner->run.policy);
    memset(&candidate->probe, 0, sizeof(candidate->probe));
    if (candidate->run.policy == NULL || !life_probe_clone(&candidate->probe, &owner->probe)) {
        cgai_life_domain_destroy(candidate);
        return NULL;
    }
    return candidate;
}

static int queued_context_conflicts(const cgai_life_domain *owner,
                                    const cgai_life_domain_task *task) {
    if (owner->kind != CGAI_LIFE_DOMAIN_NATIVE_NPC)
        return 0;
    for (size_t i = 0U; i < owner->queue_count; ++i)
        if (owner->queue[i].task.episode_id == task->episode_id &&
            owner->queue[i].task.tick == task->tick)
            return 1;
    return 0;
}

static int queued_task_id(const cgai_life_domain *owner, uint64_t hash, uint64_t *task_id) {
    for (size_t i = 0U; i < owner->queue_count; ++i)
        if (life_probe_task_hash(&owner->queue[i].task) == hash) {
            *task_id = owner->queue[i].id;
            return 1;
        }
    return 0;
}

cgai_life_status cgai_life_domain_enqueue(cgai_life_domain *owner,
                                          const cgai_life_domain_task *task, uint64_t *task_id) {
    if (owner == NULL || task_id == NULL || !life_domain_kind_valid(owner->kind) ||
        owner->kind != owner->probe.kind || !life_probe_task_valid(&owner->probe, task, 1))
        return CGAI_LIFE_INVALID_ARGUMENT;
    const uint64_t hash = life_probe_task_hash(task);
    if (queued_task_id(owner, hash, task_id))
        return CGAI_LIFE_OK;
    if (queued_context_conflicts(owner, task))
        return CGAI_LIFE_INVALID_ARGUMENT;
    if (owner->queue_count == CGAI_LIFE_DOMAIN_QUEUE || owner->next_task_id == UINT64_MAX)
        return CGAI_LIFE_LIMIT_REACHED;
    owner->queue[owner->queue_count++] = (life_domain_queued){owner->next_task_id, *task};
    *task_id = owner->next_task_id++;
    return CGAI_LIFE_OK;
}

static void publish(cgai_life_domain *owner, cgai_life_domain *candidate) {
    life_run_destroy(&owner->run);
    life_probe_destroy(&owner->probe);
    *owner = *candidate;
    free(candidate);
}

static int renew_encounter(cgai_life_domain *owner) {
    life_world renewed;
    const life_world *world = &owner->run.world;
    if (world->tick == 0U || world->tick % 32U != 0U)
        return 1;
    if (life_world_init_groups(&renewed, (uint32_t)owner->run.config.seed,
                               owner->run.config.scenario, world->group_count) != LIFE_OK)
        return 0;
    renewed.tick = world->tick;
    renewed.stats = world->stats;
    memcpy(renewed.entities, world->entities, sizeof(renewed.entities));
    renewed.next_uid = world->next_uid;
    renewed.next_conflict_id = world->next_conflict_id;
    owner->run.world = renewed;
    ++owner->encounter_renewals;
    return 1;
}

static int training_tick(const cgai_life_domain *owner, cgai_life_domain *candidate) {
    life_run source = owner->run;
    cgai_life_events events;
    cgai_life_domain_boundary before;
    if (!life_domain_boundary_capture(owner, &before) || !renew_encounter(candidate))
        return 0;
    source.world = candidate->run.world;
    if (!life_run_tick(&candidate->run))
        return 0;
    life_events_capture(&source, &candidate->run, 0, &events);
    if (!life_domain_collect(candidate, &events))
        return 0;
    candidate->round.before = before;
    return life_domain_learn(candidate) &&
           life_domain_boundary_capture(candidate, &candidate->round.after);
}

static cgai_life_status generation(cgai_life_domain *owner, int evaluate) {
    cgai_life_domain *candidate = life_domain_clone(owner);
    if (candidate == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    const int okay =
        evaluate ? life_run_evaluate_tick(&candidate->run) : training_tick(owner, candidate);
    if (!okay || !life_domain_valid(candidate)) {
        cgai_life_domain_destroy(candidate);
        return CGAI_LIFE_ENGINE_ERROR;
    }
    publish(owner, candidate);
    return CGAI_LIFE_OK;
}

int life_domain_boundary_capture(const cgai_life_domain *owner,
                                 cgai_life_domain_boundary *boundary) {
    life_policy_info cell;
    memset(boundary, 0, sizeof(*boundary));
    if (!life_policy_inspect(owner->run.policy, &cell))
        return 0;
    boundary->shared_hash = life_probe_slice_hash(&owner->probe, CGAI_LIFE_GROUPS);
    boundary->cell_shared_hash = cell.shared_hash;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i) {
        boundary->group_steps[i] = owner->probe.steps[i];
        boundary->group_hashes[i] = life_probe_slice_hash(&owner->probe, i);
        boundary->cell_group_steps[i] = cell.module_steps[i];
        boundary->cell_group_hashes[i] = cell.module_hash[i];
    }
    return 1;
}

static cgai_life_status advance(cgai_life_domain *owner, uint32_t generations, uint32_t *completed,
                                int evaluate) {
    uint32_t done = 0U;
    cgai_life_status status = CGAI_LIFE_OK;
    if (owner == NULL || generations == 0U || generations > CGAI_LIFE_MAX_STEP_GENERATIONS ||
        !life_domain_valid(owner))
        return CGAI_LIFE_INVALID_ARGUMENT;
    while (done < generations) {
        if (owner->run.world.tick > UINT32_MAX - 9U || owner->version == UINT64_MAX ||
            owner->rounds == UINT64_MAX ||
            owner->observations > UINT64_MAX - CGAI_LIFE_DOMAIN_ROUND_RECORDS ||
            owner->deferred > UINT64_MAX - CGAI_LIFE_DOMAIN_ROUND_RECORDS) {
            status = CGAI_LIFE_LIMIT_REACHED;
            break;
        }
        status = generation(owner, evaluate);
        if (status != CGAI_LIFE_OK)
            break;
        ++done;
    }
    if (completed != NULL)
        *completed = done;
    return status;
}

cgai_life_status cgai_life_domain_train_step(cgai_life_domain *owner, uint32_t generations,
                                             uint32_t *completed) {
    return advance(owner, generations, completed, 0);
}

cgai_life_status cgai_life_domain_evaluate_step(cgai_life_domain *owner, uint32_t generations,
                                                uint32_t *completed) {
    return advance(owner, generations, completed, 1);
}

cgai_life_status cgai_life_domain_predict(const cgai_life_domain *owner,
                                          const cgai_life_domain_task *task, const uint32_t *uids,
                                          size_t count, cgai_life_domain_prediction *output) {
    cgai_life_domain_prediction prediction = {0};
    if (owner == NULL || output == NULL || !life_domain_kind_valid(owner->kind) ||
        owner->kind != owner->probe.kind || !life_probe_task_valid(&owner->probe, task, 0))
        return CGAI_LIFE_INVALID_ARGUMENT;
    const uint32_t mask = life_probe_uid_mask(&owner->probe, uids, count);
    const uint32_t eligible =
        life_probe_uid_mask(&owner->probe, task->eligible_uids, task->eligible_count);
    if (mask == 0U || (mask & ~eligible) != 0U)
        return CGAI_LIFE_INVALID_ARGUMENT;
    if (!life_probe_predict(&owner->probe, task, mask, &prediction))
        return CGAI_LIFE_ENGINE_ERROR;
    prediction.model_version = owner->version;
    prediction.participant_count = (uint32_t)count;
    *output = prediction;
    return CGAI_LIFE_OK;
}

cgai_life_status cgai_life_domain_get_round(const cgai_life_domain *owner,
                                            cgai_life_domain_round *output) {
    if (owner == NULL || output == NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    *output = owner->round;
    return CGAI_LIFE_OK;
}

static void stat_counters(const cgai_life_domain *owner, cgai_life_domain_stats *output) {
    cgai_life_domain_stats stats = {0};
    stats.generation = owner->run.world.tick;
    stats.group_count = (uint32_t)owner->probe.model->config.module_count;
    stats.expert_count =
        stats.group_count * (uint32_t)owner->probe.model->config.centroids_per_module;
    stats.queued = owner->queue_count;
    stats.replay_records = owner->replay_count;
    stats.version = owner->version;
    stats.rounds = owner->rounds;
    stats.observations = owner->observations;
    stats.deferred = owner->deferred;
    stats.training_updates = owner->probe.model->training_step;
    stats.encounter_renewals = owner->encounter_renewals;
    stats.cell_policy_hash = life_policy_hash(owner->run.policy);
    *output = stats;
}

static void stat_groups(const cgai_life_domain *owner, const cgai_life_domain_boundary *boundary,
                        cgai_life_domain_stats *stats) {
    stats->shared_hash = boundary->shared_hash;
    stats->cell_shared_hash = boundary->cell_shared_hash;
    memcpy(stats->group_visits, owner->group_visits, sizeof(stats->group_visits));
    memcpy(stats->group_deferred, owner->group_deferred, sizeof(stats->group_deferred));
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i) {
        stats->group_uids[i] = owner->probe.uids[i];
        stats->group_steps[i] = boundary->group_steps[i];
        stats->group_hashes[i] = boundary->group_hashes[i];
        stats->cell_group_steps[i] = boundary->cell_group_steps[i];
        stats->cell_group_hashes[i] = boundary->cell_group_hashes[i];
    }
}

cgai_life_status cgai_life_domain_get_stats(const cgai_life_domain *owner,
                                            cgai_life_domain_stats *output) {
    cgai_life_domain_stats stats;
    cgai_life_domain_boundary boundary;
    if (owner == NULL || output == NULL || !life_domain_boundary_capture(owner, &boundary))
        return CGAI_LIFE_INVALID_ARGUMENT;
    stat_counters(owner, &stats);
    stat_groups(owner, &boundary, &stats);
    *output = stats;
    return CGAI_LIFE_OK;
}

uint64_t cgai_life_domain_hash(const cgai_life_domain *owner) {
    if (owner == NULL)
        return 0U;
    const uint64_t payload = life_domain_payload_hash(owner);
    return payload == 0U ? 0U : life_domain_hash_word(life_run_hash(&owner->run), payload);
}
