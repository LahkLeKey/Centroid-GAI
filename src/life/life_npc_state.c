/** @file life_npc_state.c @brief Reconstruct and authenticate every current episode. */
#include "life_npc.h"
#include <math.h>
#include <string.h>

static int replay_observation(const cgai_life_npc *owner, cgai_life_npc *reconstructed,
                              uint32_t tick) {
    life_npc_observation observation;
    if (reconstructed->world.terminal != LIFE_NPC_RUNNING || owner->actions[tick] >= 7U)
        return 0;
    life_npc_world_observe(&reconstructed->world, &observation);
    if ((observation.allowed_actions & (UINT64_C(1) << owner->actions[tick])) == 0U)
        return 0;
    life_npc_memory_observe(&reconstructed->memory, &observation);
    return 1;
}

static int last_context_valid(const cgai_life_npc *owner, const cgai_life_npc *reconstructed,
                              uint32_t tick) {
    if (tick + 1U != owner->world.ticks || owner->domain == NULL ||
        owner->domain->probe.model == NULL)
        return 1;
    cgai_life_domain_task task;
    return life_npc_make_task(owner->domain, &reconstructed->world, &reconstructed->memory,
                              owner->episode_cursor + 1U, owner->actions, &task) &&
           life_probe_task_hash(&task) == owner->host_context_hash;
}

static int replay_episode(const cgai_life_npc *owner, cgai_life_npc *reconstructed) {
    reconstructed->episode_cursor = owner->episode_cursor;
    if (!life_npc_owner_start(reconstructed) || owner->world.ticks > LIFE_NPC_MAX_TICKS)
        return 0;
    for (uint32_t tick = 0U; tick < owner->world.ticks; ++tick) {
        if (!replay_observation(owner, reconstructed, tick) ||
            !last_context_valid(owner, reconstructed, tick))
            return 0;
        life_npc_world_step(&reconstructed->world, owner->actions[tick]);
    }
    return 1;
}

static int counters_valid(const cgai_life_npc *owner) {
    if (owner->episode_cursor >= UINT64_MAX / LIFE_NPC_MAX_TICKS ||
        owner->successes > owner->decisions || owner->deaths > owner->decisions ||
        owner->timeouts > owner->decisions || owner->decisions > owner->domain->run.world.tick ||
        owner->host_parent_version > owner->domain->version || !isfinite(owner->host_probability) ||
        owner->host_probability < 0.0 || owner->host_probability > 1.0)
        return 0;
    if (owner->world.ticks != 0U && owner->domain->version - owner->host_parent_version > 1U)
        return 0;
    const uint64_t completed = owner->successes + owner->deaths + owner->timeouts;
    if (owner->world.ticks == 0U &&
        (owner->host_parent_version != 0U || owner->host_context_hash != 0U ||
         owner->host_probability != 0.0))
        return 0;
    return completed ==
               owner->episode_cursor + (owner->world.terminal != LIFE_NPC_RUNNING ? 1U : 0U) &&
           owner->decisions >= owner->episode_cursor + owner->world.ticks &&
           owner->decisions <= owner->episode_cursor * LIFE_NPC_MAX_TICKS + owner->world.ticks;
}

static int terminal_valid(const cgai_life_npc *owner) {
    return (owner->world.terminal != LIFE_NPC_SUCCESS || owner->successes != 0U) &&
           (owner->world.terminal != LIFE_NPC_DEATH || owner->deaths != 0U) &&
           (owner->world.terminal != LIFE_NPC_TIMEOUT || owner->timeouts != 0U);
}

static int current_hashes(const cgai_life_npc *owner, uint64_t hashes[LIFE_NPC_MAX_TICKS]) {
    cgai_life_npc reconstructed = {0};
    reconstructed.episode_cursor = owner->episode_cursor;
    if (!life_npc_owner_start(&reconstructed))
        return 0;
    for (uint32_t tick = 0U; tick < owner->world.ticks; ++tick) {
        life_npc_observation observation;
        cgai_life_domain_task task;
        life_npc_world_observe(&reconstructed.world, &observation);
        life_npc_memory_observe(&reconstructed.memory, &observation);
        if (!life_npc_make_task(owner->domain, &reconstructed.world, &reconstructed.memory,
                                owner->episode_cursor + 1U, owner->actions, &task))
            return 0;
        hashes[tick] = life_probe_task_hash(&task);
        life_npc_world_step(&reconstructed.world, owner->actions[tick]);
    }
    return 1;
}

static int source_task_valid(const cgai_life_npc *owner, const cgai_life_domain_task *task,
                             const uint64_t hashes[LIFE_NPC_MAX_TICKS]) {
    life_npc_family family;
    if (task->episode_id == 0U || task->episode_id > owner->episode_cursor + 1U ||
        !life_npc_family_get(LIFE_NPC_TRAIN,
                             (uint32_t)((task->episode_id - 1U) % LIFE_NPC_FAMILIES_PER_SPLIT),
                             &family) ||
        task->family != family.id + 1U)
        return 0;
    return task->episode_id != owner->episode_cursor + 1U ||
           (task->tick < owner->world.ticks && life_probe_task_hash(task) == hashes[task->tick]);
}

static int queued_sources_valid(const cgai_life_npc *owner,
                                const uint64_t hashes[LIFE_NPC_MAX_TICKS]) {
    for (uint32_t i = 0U; i < owner->domain->queue_count; ++i)
        if (!source_task_valid(owner, &owner->domain->queue[i].task, hashes))
            return 0;
    return 1;
}

static int recorded_sources_valid(const cgai_life_npc *owner,
                                  const cgai_life_domain_record *records, uint32_t count,
                                  const uint64_t hashes[LIFE_NPC_MAX_TICKS]) {
    for (uint32_t i = 0U; i < count; ++i)
        if (!source_task_valid(owner, &records[i].task, hashes))
            return 0;
    return 1;
}

static int retained_sources_valid(const cgai_life_npc *owner) {
    uint64_t hashes[LIFE_NPC_MAX_TICKS] = {0};
    return current_hashes(owner, hashes) && queued_sources_valid(owner, hashes) &&
           recorded_sources_valid(owner, owner->domain->replay, owner->domain->replay_count,
                                  hashes) &&
           recorded_sources_valid(owner, owner->domain->round.records, owner->domain->round.count,
                                  hashes);
}

int life_npc_owner_valid(const cgai_life_npc *owner) {
    cgai_life_npc reconstructed = {0};
    if (owner == NULL || owner->domain == NULL ||
        owner->domain->kind != CGAI_LIFE_DOMAIN_NATIVE_NPC || !life_domain_valid(owner->domain) ||
        !counters_valid(owner) || !terminal_valid(owner) ||
        !replay_episode(owner, &reconstructed) || !retained_sources_valid(owner))
        return 0;
    for (size_t i = owner->world.ticks; i < LIFE_NPC_MAX_TICKS; ++i)
        if (owner->actions[i] != 0U)
            return 0;
    return memcmp(&owner->world, &reconstructed.world, sizeof(owner->world)) == 0 &&
           memcmp(&owner->memory, &reconstructed.memory, sizeof(owner->memory)) == 0;
}

static uint64_t world_hash(uint64_t hash, const life_npc_world *world) {
    const uint64_t fields[] = {world->family.id,
                               world->family.split,
                               world->family.mechanic,
                               world->family.layout,
                               world->variant,
                               world->width,
                               world->height,
                               world->x,
                               world->y,
                               world->item_x,
                               world->item_y,
                               world->exit_x,
                               world->exit_y,
                               world->junction_x,
                               world->junction_y,
                               world->cue_direction,
                               world->branch_chosen,
                               world->inventory,
                               world->obstruction_revealed,
                               world->ticks,
                               world->last_action,
                               world->last_outcome,
                               world->attempted_illegal,
                               world->executed_illegal,
                               world->terminal};
    for (size_t i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i)
        hash = life_domain_hash_word(hash, fields[i]);
    for (size_t i = 0U; i < LIFE_NPC_GRID_SIDE * LIFE_NPC_GRID_SIDE; ++i)
        hash = life_domain_hash_word(hash, world->cells[i]);
    return hash;
}

uint64_t life_npc_host_hash(const cgai_life_npc *owner) {
    const life_npc_memory *memory = &owner->memory;
    uint64_t probability;
    memcpy(&probability, &owner->host_probability, sizeof(probability));
    const uint64_t fields[] = {life_npc_recipe_hash(),
                               owner->episode_cursor,
                               owner->decisions,
                               owner->successes,
                               owner->deaths,
                               owner->timeouts,
                               memory->cue,
                               memory->cue_age,
                               memory->last_action,
                               memory->last_outcome,
                               memory->observed_tick,
                               memory->initialized,
                               owner->host_parent_version,
                               owner->host_context_hash,
                               probability};
    uint64_t hash = world_hash(UINT64_C(14695981039346656037), &owner->world);
    for (size_t i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i)
        hash = life_domain_hash_word(hash, fields[i]);
    for (size_t i = 0U; i < LIFE_NPC_MAX_TICKS; ++i)
        hash = life_domain_hash_word(hash, owner->actions[i]);
    return hash;
}

uint64_t cgai_life_npc_hash(const cgai_life_npc *owner) {
    if (!life_npc_owner_valid(owner))
        return 0U;
    const uint64_t domain = cgai_life_domain_hash(owner->domain);
    return domain == 0U ? 0U : life_domain_hash_word(life_npc_host_hash(owner), domain);
}

int life_npc_owner_reconstruct(cgai_life_npc *owner, uint32_t ticks) {
    cgai_life_npc reconstructed = {0};
    owner->world.ticks = ticks;
    if (!replay_episode(owner, &reconstructed))
        return 0;
    owner->world = reconstructed.world;
    owner->memory = reconstructed.memory;
    return 1;
}
