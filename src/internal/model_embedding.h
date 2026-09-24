/** @file model_embedding.h @brief Private embedding operations. */
#ifndef CGAI_MODEL_EMBEDDING_H
#define CGAI_MODEL_EMBEDDING_H
#include "cgai_internal.h"

/**
 * @brief Compute a recency-weighted average of the newest history tokens.
 *
 * Only the configured context window contributes. Within that suffix, weights increase from one
 * for the oldest token to the suffix length for the newest. Output and scratch are reused by the
 * training/generation loop, so clearing output here prevents accumulation across separate steps.
 *
 * @param model Non-NULL borrowed model with valid vocabulary and dimensions.
 * @param history Readable array of valid token IDs; may be NULL only when history_count is zero.
 * @param history_count Number of initialized IDs available in history.
 * @param output Writable dimension-sized result vector, distinct from scratch.
 * @param scratch Writable dimension-sized temporary token vector.
 */
void cgai_context_embedding(const cgai_model *model, const cgai_token_id *history,
                            size_t history_count, float *output, float *scratch);
#endif
