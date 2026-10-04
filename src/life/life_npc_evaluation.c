/** @file life_npc_evaluation.c @brief Frozen unguarded native own-history outcomes. */
#include "life_npc.h"
#include <string.h>

typedef struct evaluation_actor {
    life_npc_world world;
    life_npc_memory memory;
    uint32_t actions[LIFE_NPC_MAX_TICKS];
} evaluation_actor;

static int actor_start(evaluation_actor *actor, cgai_life_context_split split, uint32_t family,
                       uint32_t variant) {
    life_npc_family recipe;
    memset(actor, 0, sizeof(*actor));
    return life_npc_family_get((life_npc_split)split, family, &recipe) &&
           life_npc_world_init(&actor->world, &recipe, variant);
}

static int actor_decision(const cgai_life_npc *owner, evaluation_actor *actor,
                          cgai_life_npc_evaluation *report) {
    life_npc_observation observation;
    cgai_life_domain_task task;
    cgai_life_domain_prediction prediction;
    life_npc_world_observe(&actor->world, &observation);
    life_npc_memory_observe(&actor->memory, &observation);
    if (!life_npc_make_task(owner->domain, &actor->world, &actor->memory, 1U, actor->actions,
                            &task) ||
        cgai_life_domain_predict(owner->domain, &task, task.eligible_uids, task.eligible_count,
                                 &prediction) != CGAI_LIFE_OK)
        return 0;
    actor->actions[actor->world.ticks] = prediction.action;
    const uint32_t blocked = actor->world.last_outcome == LIFE_NPC_BLOCKED ? 1U : 0U;
    const life_npc_outcome outcome = life_npc_world_step(&actor->world, prediction.action);
    ++report->decisions;
    report->blocked += outcome == LIFE_NPC_BLOCKED ? 1U : 0U;
    report->recovered += blocked != 0U && outcome == LIFE_NPC_MOVED ? 1U : 0U;
    report->fallbacks += prediction.action == LIFE_NPC_FALLBACK ? 1U : 0U;
    return 1;
}

static int evaluate_episode(const cgai_life_npc *owner, cgai_life_context_split split,
                            uint32_t family, uint32_t variant, cgai_life_npc_evaluation *report) {
    evaluation_actor actor;
    if (!actor_start(&actor, split, family, variant))
        return 0;
    while (actor.world.terminal == LIFE_NPC_RUNNING)
        if (!actor_decision(owner, &actor, report))
            return 0;
    const uint32_t mechanic = (uint32_t)actor.world.family.mechanic;
    ++report->episodes;
    ++report->mechanic_episodes[mechanic];
    report->successes += actor.world.terminal == LIFE_NPC_SUCCESS ? 1U : 0U;
    report->deaths += actor.world.terminal == LIFE_NPC_DEATH ? 1U : 0U;
    report->timeouts += actor.world.terminal == LIFE_NPC_TIMEOUT ? 1U : 0U;
    report->mechanic_successes[mechanic] += actor.world.terminal == LIFE_NPC_SUCCESS ? 1U : 0U;
    report->attempted_illegal += actor.world.attempted_illegal;
    report->executed_illegal += actor.world.executed_illegal;
    return 1;
}

static int evaluation_valid(cgai_life_context_split split, uint32_t families, uint32_t variants) {
    return (split == CGAI_LIFE_CONTEXT_TRAIN || split == CGAI_LIFE_CONTEXT_DEVELOPMENT) &&
           families != 0U && families <= LIFE_NPC_FAMILIES_PER_SPLIT && variants != 0U &&
           variants <= LIFE_NPC_VARIANTS_PER_FAMILY;
}

static int evaluate_ranges(const cgai_life_npc *owner, cgai_life_context_split split,
                           uint32_t families, uint32_t variants, cgai_life_npc_evaluation *report) {
    for (uint32_t family = 0U; family < families; ++family)
        for (uint32_t variant = 0U; variant < variants; ++variant)
            if (!evaluate_episode(owner, split, family, variant, report))
                return 0;
    return 1;
}

cgai_life_status cgai_life_npc_evaluate(const cgai_life_npc *owner, cgai_life_context_split split,
                                        uint32_t families, uint32_t variants,
                                        cgai_life_npc_evaluation *output) {
    if (output == NULL || !life_npc_owner_valid(owner) ||
        !evaluation_valid(split, families, variants))
        return CGAI_LIFE_INVALID_ARGUMENT;
    cgai_life_npc_evaluation report = {0};
    report.source_model_hash = cgai_life_npc_hash(owner);
    if (!evaluate_ranges(owner, split, families, variants, &report) ||
        report.source_model_hash != cgai_life_npc_hash(owner))
        return CGAI_LIFE_ENGINE_ERROR;
    *output = report;
    return CGAI_LIFE_OK;
}
