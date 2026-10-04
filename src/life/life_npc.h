/** @file life_npc.h @brief Private complete native episode and learner owner. */
#ifndef CGAI_LIFE_NPC_INTERNAL_H
#define CGAI_LIFE_NPC_INTERNAL_H
#include "centroid_life_npc.h"
#include "life_domain.h"
#include "life_npc_world.h"

struct cgai_life_npc {
    cgai_life_domain *domain;
    life_npc_world world;
    life_npc_memory memory;
    uint32_t actions[LIFE_NPC_MAX_TICKS];
    uint64_t episode_cursor, decisions, successes, deaths, timeouts;
    uint64_t host_parent_version, host_context_hash;
    double host_probability;
};

int life_npc_owner_valid(const cgai_life_npc *owner);
int life_npc_owner_start(cgai_life_npc *owner);
int life_npc_make_task(const cgai_life_domain *domain, const life_npc_world *world,
                       const life_npc_memory *memory, uint64_t episode, const uint32_t *actions,
                       cgai_life_domain_task *task);
uint64_t life_npc_recipe_hash(void);
uint64_t life_npc_host_hash(const cgai_life_npc *owner);
int life_npc_owner_reconstruct(cgai_life_npc *owner, uint32_t ticks);
#endif
