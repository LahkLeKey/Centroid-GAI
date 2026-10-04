/** @file npc_optimizer.h @brief Private exact role-conditioned v3 optimizer. */
#ifndef CGAI_NPC_V3_OPTIMIZER_H
#define CGAI_NPC_V3_OPTIMIZER_H
#include "gameplay/gameplay_contract.h"

/** Frozen multiplier of the observed-role module-routing cross entropy. */
#define NPC_ROUTING_BALANCE 0.2
/** Frozen assigned-role probability, with the other module receiving its complement. */
#define NPC_ROLE_TARGET 0.95

/** @brief Differentiate conditional action NLL plus observed-role routing cross entropy.
 * @param session Exclusive scratch borrowing a compatible two-module model.
 * @param example Borrowed target matching that forward state and task.
 * @param role Original full-record observed role, zero or one, independent of ablation.
 * @param gradient Writable complete parameter_count scalar array, overwritten.
 * @return OK for finite exact combined derivatives, ERROR otherwise. */
cgai_status npc_v3_optimizer_gradient(cgai_gameplay_session *session,
                                      const cgai_gameplay_example *example, uint32_t role,
                                      double *gradient);

/** Private settings for one admitted event's numerical update; no schedule or dataset ownership. */
typedef struct npc_v3_update_settings {
    double learning_rate; /**< Finite scalar step size, greater than zero and at most one. */
    double gradient_clip; /**< Positive finite complete-gradient norm cap, at most1000. */
    double weight_decay;  /**< Finite decoupled shrinkage, zero through one. */
} npc_v3_update_settings;

/** @brief Apply one conditional-action and role-routing update for one admitted event.
 * @param model Exclusive mutable two-module model with coherent model-owned moments.
 * @param example Borrowed complete verified target; both task modules must be eligible.
 * @param role Original observed role, zero or one, independent of model predictions.
 * @param settings Borrowed bounded scalar update settings.
 * @return OK after one complete update, ERROR without changing weights, moments or counters.
 * The caller owns event admission and scheduling. No epochs or shuffle state advance here. */
cgai_status npc_v3_optimizer_step(cgai_gameplay_model *model, const cgai_gameplay_example *example,
                                  uint32_t role, const npc_v3_update_settings *settings);
#endif
