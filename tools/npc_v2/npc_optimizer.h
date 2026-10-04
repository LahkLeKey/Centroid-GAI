/** @file npc_optimizer.h @brief Private exact routing-balanced v2 optimizer. */
#ifndef CGAI_NPC_V2_OPTIMIZER_H
#define CGAI_NPC_V2_OPTIMIZER_H
#include "gameplay/gameplay_contract.h"

/** Frozen multiplier of the reverse uniform module-routing KL objective. */
#define NPC_ROUTING_BALANCE 0.2

/** @brief Differentiate mixture NLL plus reverse uniform routing KL after matching forward.
 * @param session Exclusive successful matching forward with both modules active.
 * @param example Borrowed target matching that forward state and task.
 * @param gradient Writable complete parameter_count scalar array, overwritten.
 * @return OK for finite exact combined derivatives, ERROR otherwise. */
cgai_status npc_v2_optimizer_gradient(cgai_gameplay_session *session,
                                      const cgai_gameplay_example *example, double *gradient);

/** Private settings for one admitted event's numerical update; no schedule or dataset ownership. */
typedef struct npc_v2_update_settings {
    double learning_rate; /**< Finite scalar step size, greater than zero and at most one. */
    double gradient_clip; /**< Positive finite complete-gradient norm cap, at most1000. */
    double weight_decay;  /**< Finite decoupled shrinkage, zero through one. */
} npc_v2_update_settings;

/** @brief Apply one routing-balanced numerical update for one admitted event.
 * @param model Exclusive mutable two-module model with coherent model-owned moments.
 * @param example Borrowed complete verified target; both task modules must be eligible.
 * @param settings Borrowed bounded scalar update settings.
 * @return OK after one complete update, ERROR without changing weights, moments or counters.
 * The caller owns event admission and scheduling. No epochs or shuffle state advance here. */
cgai_status npc_v2_optimizer_step(cgai_gameplay_model *model, const cgai_gameplay_example *example,
                                  const npc_v2_update_settings *settings);
#endif
