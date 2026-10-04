/** @file life_npc.c @brief Authentic own-history decisions with native Life updates. */
#include "life_npc.h"
#include <stdlib.h>
#include <string.h>

#ifndef CGAI_LIFE_NPC_RECIPE_SHA
#define CGAI_LIFE_NPC_RECIPE_SHA "source-identity-unavailable"
#endif

uint64_t life_npc_recipe_hash(void) {
    uint64_t hash = UINT64_C(14695981039346656037);
    const char *identity = CGAI_LIFE_NPC_RECIPE_SHA;
    for (size_t i = 0U; identity[i] != '\0'; ++i)
        hash = (hash ^ (unsigned char)identity[i]) * UINT64_C(1099511628211);
    return hash;
}

int life_npc_owner_start(cgai_life_npc *owner) {
    life_npc_family family;
    const uint32_t index = (uint32_t)(owner->episode_cursor % LIFE_NPC_FAMILIES_PER_SPLIT);
    const uint32_t variant = (uint32_t)((owner->episode_cursor / LIFE_NPC_FAMILIES_PER_SPLIT) %
                                        LIFE_NPC_VARIANTS_PER_FAMILY);
    if (!life_npc_family_get(LIFE_NPC_TRAIN, index, &family) ||
        !life_npc_world_init(&owner->world, &family, variant))
        return 0;
    life_npc_memory_reset(&owner->memory);
    memset(owner->actions, 0, sizeof(owner->actions));
    owner->host_parent_version = 0U;
    owner->host_context_hash = 0U;
    owner->host_probability = 0.0;
    return 1;
}

cgai_life_status cgai_life_npc_create(const cgai_life_config *config, cgai_life_npc **output) {
    if (output == NULL || *output != NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    cgai_life_npc *owner = calloc(1U, sizeof(*owner));
    if (owner == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    const cgai_life_status status =
        cgai_life_domain_create(config, CGAI_LIFE_DOMAIN_NATIVE_NPC, &owner->domain);
    if (status != CGAI_LIFE_OK || !life_npc_owner_start(owner)) {
        cgai_life_npc_destroy(owner);
        return status == CGAI_LIFE_OK ? CGAI_LIFE_ENGINE_ERROR : status;
    }
    *output = owner;
    return CGAI_LIFE_OK;
}

void cgai_life_npc_destroy(cgai_life_npc *owner) {
    if (owner != NULL) {
        cgai_life_domain_destroy(owner->domain);
        free(owner);
    }
}

static uint64_t source_identity(const life_npc_world *world, uint64_t episode,
                                const uint32_t *actions) {
    uint64_t hash = life_domain_hash_word(life_npc_recipe_hash(), episode);
    hash = life_domain_hash_word(hash, world->family.id);
    hash = life_domain_hash_word(hash, world->variant);
    for (uint32_t tick = 0U; tick < world->ticks; ++tick)
        hash = life_domain_hash_word(hash, actions[tick]);
    return hash;
}

int life_npc_make_task(const cgai_life_domain *domain, const life_npc_world *world,
                       const life_npc_memory *memory, uint64_t episode, const uint32_t *actions,
                       cgai_life_domain_task *task) {
    life_npc_observation observation;
    cgai_gameplay_state state;
    memset(task, 0, sizeof(*task));
    life_npc_world_observe(world, &observation);
    life_npc_encode(&observation, memory, 1, &state);
    task->version = CGAI_LIFE_DOMAIN_VERSION;
    task->family = world->family.id + 1U;
    task->split = (cgai_life_context_split)world->family.split;
    task->reviewed = task->split == CGAI_LIFE_CONTEXT_TRAIN ? 1U : 0U;
    task->source_hash = source_identity(world, episode, actions);
    task->observed_fields = UINT32_C(0xffff);
    task->legal_actions = (uint32_t)observation.allowed_actions;
    task->eligible_count = (uint32_t)domain->probe.model->config.module_count;
    memcpy(task->eligible_uids, domain->probe.uids, sizeof(task->eligible_uids));
    task->feature_count = LIFE_NPC_FEATURE_COUNT;
    memcpy(task->features, state.values, sizeof(task->features));
    task->episode_id = episode;
    task->tick = world->ticks;
    return life_probe_task_valid(&domain->probe, task, 0);
}

static cgai_life_npc *clone_owner(const cgai_life_npc *owner) {
    cgai_life_npc *copy = malloc(sizeof(*copy));
    if (copy == NULL)
        return NULL;
    *copy = *owner;
    copy->domain = life_domain_clone(owner->domain);
    if (copy->domain == NULL) {
        free(copy);
        return NULL;
    }
    return copy;
}

static int prepare_episode(cgai_life_npc *owner) {
    if (owner->world.terminal == LIFE_NPC_RUNNING)
        return 1;
    if (owner->episode_cursor == UINT64_MAX - 1U)
        return 0;
    ++owner->episode_cursor;
    return life_npc_owner_start(owner);
}

static int drain_queue(cgai_life_domain *domain) {
    for (uint32_t tick = 0U; domain->queue_count == CGAI_LIFE_DOMAIN_QUEUE && tick < 64U; ++tick)
        if (cgai_life_domain_train_step(domain, 1U, NULL) != CGAI_LIFE_OK)
            return 0;
    return domain->queue_count < CGAI_LIFE_DOMAIN_QUEUE;
}

static void commit_host(cgai_life_npc *owner, const cgai_life_domain_prediction *prediction) {
    owner->actions[owner->world.ticks] = prediction->action;
    owner->host_parent_version = prediction->model_version;
    owner->host_context_hash = prediction->context_hash;
    owner->host_probability = prediction->probability;
    life_npc_world_step(&owner->world, prediction->action);
    ++owner->decisions;
    owner->successes += owner->world.terminal == LIFE_NPC_SUCCESS ? 1U : 0U;
    owner->deaths += owner->world.terminal == LIFE_NPC_DEATH ? 1U : 0U;
    owner->timeouts += owner->world.terminal == LIFE_NPC_TIMEOUT ? 1U : 0U;
}

static int consume_decision(cgai_life_npc *owner) {
    life_npc_observation observation;
    cgai_life_domain_task task;
    cgai_life_domain_prediction prediction;
    uint64_t task_id;
    if (!prepare_episode(owner) || !drain_queue(owner->domain))
        return 0;
    life_npc_world_observe(&owner->world, &observation);
    life_npc_memory_observe(&owner->memory, &observation);
    if (!life_npc_make_task(owner->domain, &owner->world, &owner->memory,
                            owner->episode_cursor + 1U, owner->actions, &task) ||
        cgai_life_domain_predict(owner->domain, &task, task.eligible_uids, task.eligible_count,
                                 &prediction) != CGAI_LIFE_OK ||
        cgai_life_domain_enqueue(owner->domain, &task, &task_id) != CGAI_LIFE_OK ||
        cgai_life_domain_train_step(owner->domain, 1U, NULL) != CGAI_LIFE_OK)
        return 0;
    commit_host(owner, &prediction);
    return 1;
}

static cgai_life_status decision(cgai_life_npc *owner) {
    cgai_life_npc *candidate = clone_owner(owner);
    if (candidate == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    if (!consume_decision(candidate) || !life_npc_owner_valid(candidate)) {
        cgai_life_npc_destroy(candidate);
        return CGAI_LIFE_ENGINE_ERROR;
    }
    cgai_life_domain_destroy(owner->domain);
    *owner = *candidate;
    free(candidate);
    return CGAI_LIFE_OK;
}

cgai_life_status cgai_life_npc_train_step(cgai_life_npc *owner, uint32_t decisions,
                                          uint32_t *completed) {
    uint32_t done = 0U;
    cgai_life_status status = CGAI_LIFE_OK;
    if (!life_npc_owner_valid(owner) || decisions == 0U || decisions > 64U)
        return CGAI_LIFE_INVALID_ARGUMENT;
    while (done < decisions) {
        if (owner->decisions == UINT64_MAX || owner->episode_cursor == UINT64_MAX - 1U) {
            status = CGAI_LIFE_LIMIT_REACHED;
            break;
        }
        status = decision(owner);
        if (status != CGAI_LIFE_OK)
            break;
        ++done;
    }
    if (completed != NULL)
        *completed = done;
    return status;
}

static void stats_host(const cgai_life_npc *owner, cgai_life_npc_stats *stats) {
    stats->episode_id = owner->episode_cursor + 1U;
    stats->decisions = owner->decisions;
    stats->successes = owner->successes;
    stats->deaths = owner->deaths;
    stats->timeouts = owner->timeouts;
    stats->completed_episodes = stats->successes + stats->deaths + stats->timeouts;
    stats->family = owner->world.family.id;
    stats->variant = owner->world.variant;
    stats->tick = owner->world.ticks;
    stats->terminal = (uint32_t)owner->world.terminal;
    stats->action = owner->world.last_action;
    stats->outcome = owner->world.last_outcome;
    stats->host_parent_version = owner->host_parent_version;
    stats->host_context_hash = owner->host_context_hash;
    stats->host_probability = owner->host_probability;
}

cgai_life_status cgai_life_npc_get_stats(const cgai_life_npc *owner, cgai_life_npc_stats *output) {
    if (output == NULL || !life_npc_owner_valid(owner))
        return CGAI_LIFE_INVALID_ARGUMENT;
    cgai_life_npc_stats stats = {0};
    if (cgai_life_domain_get_stats(owner->domain, &stats.domain) != CGAI_LIFE_OK)
        return CGAI_LIFE_ENGINE_ERROR;
    stats_host(owner, &stats);
    stats.recipe_hash = life_npc_recipe_hash();
    stats.state_hash = cgai_life_npc_hash(owner);
    stats.model_version = stats.domain.version;
    stats.domain_observations = stats.domain.observations;
    stats.queued = stats.domain.queued;
    *output = stats;
    return CGAI_LIFE_OK;
}
