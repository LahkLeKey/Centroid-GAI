/** @file model_sampling.h @brief Private token selection for generation. */

#ifndef CGAI_MODEL_SAMPLING_H
#define CGAI_MODEL_SAMPLING_H

#include "cgai_internal.h"

/**
 * @brief Choose the highest-count next token or sample the centroid's distribution.
 *
 * This helper borrows all model state and mutates only the caller's random-state variable when a
 * sample is needed. Shifting a 64-bit random word right by eleven keeps 53 bits; multiplying by
 * 1/2^53 maps that integer to a double in [0, 1). Greedy selection consumes no random word.
 *
 * @param model Non-NULL borrowed model with learned counts.
 * @param cluster Valid initialized centroid ID.
 * @param temperature Finite nonnegative temperature; zero chooses the greedy token.
 * @param random_state Non-NULL writable RNG state when sampling is needed.
 * @return Typed selected token ID, which may be EOS to end generation.
 */
cgai_token_id cgai_select_token(const cgai_model *model, cgai_centroid_id cluster,
                                double temperature, uint64_t *random_state);

#endif