/** @file model_math.h @brief Private embedding, centroid, and random operations. */

#ifndef CGAI_MODEL_MATH_H
#define CGAI_MODEL_MATH_H

#include "cgai_internal.h"

/**
 * @brief Computes a weighted normalized embedding for recent token history.
 * @param model Model supplying dimensions, vocabulary text, and context limit.
 * @param history Token IDs in chronological order.
 * @param history_count Number of valid IDs in @p history.
 * @param output Caller-owned dimension-sized result vector.
 * @param scratch Caller-owned dimension-sized temporary vector.
 * @note Only the newest context_window entries contribute; newer entries have larger weights.
 */
void cgai_context_embedding(const cgai_model *model, const cgai_token_id *history,
                            size_t history_count, float *output, float *scratch);

/**
 * @brief Returns the nearest initialized centroid for an embedding.
 * @param model Model containing initialized centroid vectors.
 * @param embedding Dimension-sized normalized vector to compare.
 * @return A typed centroid ID; callers must provide a trained model.
 */
cgai_centroid_id cgai_nearest_centroid(const cgai_model *model, const float *embedding);

/**
 * @brief Advances a deterministic 64-bit pseudo-random state.
 * @param state Mutable state owned by the caller.
 * @return The next pseudo-random word; equal starting states produce equal sequences.
 */
uint64_t cgai_random_next(uint64_t *state);

#endif
