/** @file centroid_life_npc.h @brief Native own-history NPC episodes trained through Life. */
#ifndef CENTROID_LIFE_NPC_H
#define CENTROID_LIFE_NPC_H
#include "centroid_life_domain.h"

typedef struct cgai_life_npc cgai_life_npc;
typedef struct cgai_life_npc_stats {
    uint64_t episode_id, decisions, completed_episodes, successes, deaths, timeouts;
    uint64_t recipe_hash, state_hash, model_version, domain_observations;
    uint64_t host_parent_version, host_context_hash;
    double host_probability;
    uint32_t family, variant, tick, terminal, action, outcome, queued;
    cgai_life_domain_stats domain;
} cgai_life_npc_stats;

/** Every requested episode contributes, including death and timeout. No training
 * or teacher repair occurs during evaluation. Confirmation remains reserved. */
typedef struct cgai_life_npc_evaluation {
    uint64_t episodes, successes, deaths, timeouts, decisions;
    uint64_t attempted_illegal, executed_illegal, blocked, recovered, fallbacks;
    uint64_t mechanic_episodes[3], mechanic_successes[3];
    uint64_t source_model_hash;
} cgai_life_npc_evaluation;

/** Creates a fresh reviewed TRAIN episode and a native NPC domain owner. */
cgai_life_status cgai_life_npc_create(const cgai_life_config *config, cgai_life_npc **output);
void cgai_life_npc_destroy(cgai_life_npc *owner);
/** Atomic per host decision: authenticate current own history, freeze prediction,
 * admit visible TRAIN input, advance authentic Life contacts, then execute the
 * chosen host action without repair. A bounded full FIFO drains before admission.
 * Completed episodes restart from a deterministic target-independent recipe. */
cgai_life_status cgai_life_npc_train_step(cgai_life_npc *owner, uint32_t decisions,
                                          uint32_t *completed);
cgai_life_status cgai_life_npc_get_stats(const cgai_life_npc *owner, cgai_life_npc_stats *output);
/** Frozen own-history TRAIN or DEVELOPMENT episodes, first family/variant ranges.
 * AUDIT is reserved and rejected. No queue, model, world or memory mutation. */
cgai_life_status cgai_life_npc_evaluate(const cgai_life_npc *owner, cgai_life_context_split split,
                                        uint32_t families, uint32_t variants,
                                        cgai_life_npc_evaluation *output);
uint64_t cgai_life_npc_hash(const cgai_life_npc *owner);
/** Complete learner, Life world, NPC world, history, cursor and diagnostics.
 * Recipe identity is pinned; publication and load replacement are transactional. */
cgai_life_status cgai_life_npc_save(const cgai_life_npc *owner, const char *path);
cgai_life_status cgai_life_npc_load(cgai_life_npc *owner, const char *path);
#endif
