/** @file model_sampling.h @brief Private token selection for generation. */

#ifndef CGAI_MODEL_SAMPLING_H
#define CGAI_MODEL_SAMPLING_H

#include "cgai_internal.h"

/**
 * @brief Selects the next token using greedy or temperature-weighted sampling.
 * @param model Trained model containing one token-count row per centroid.
 * @param cluster Centroid row to sample.
 * @param temperature Zero selects the highest-count token; positive values sample counts.
 * @param random_state Caller-owned deterministic state used only for positive temperatures.
 * @return A token ID, including EOS when the learned distribution selects termination.
 */
cgai_token_id cgai_select_token(const cgai_model *model, cgai_centroid_id cluster,
                                double temperature, uint64_t *random_state);

#endif