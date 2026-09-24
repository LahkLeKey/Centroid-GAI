/** @file model_embedding.c @brief Token hashing and weighted context embeddings. */

#include "internal/model_embedding.h"
#include "internal/model_random.h"
#include <math.h>
#include <string.h>

static const uint64_t CGAI_HASH_OFFSET_BASIS = UINT64_C(1469598103934665603);
static const uint64_t CGAI_HASH_PRIME = UINT64_C(1099511628211);
static const uint64_t CGAI_LOW_BIT_MASK = UINT64_C(1);

/**
 * @brief Combine a token's byte spelling and model seed into an initial embedding state.
 *
 * Hashing operates on bytes up to the first NUL, not Unicode code points. Equal spellings and seeds
 * produce equal states, so the model does not need to store an embedding matrix. The unsigned
 * 64-bit multiply intentionally wraps, as in FNV-style accumulation.
 *
 * @param token Non-NULL borrowed NUL-terminated vocabulary spelling.
 * @param seed Model seed used to distinguish deterministic embedding spaces.
 * @return Deterministic hash word without allocations or changes to the input.
 */
static uint64_t hash_token(const char *token, uint64_t seed) {
    /* Mix the model seed into the initial hash so models remain independently reproducible. */
    /* Step 1: Combine the fixed offset basis with the model's seed. */
    uint64_t hash = CGAI_HASH_OFFSET_BASIS ^ seed;
    /* Step 2: Accumulate each unsigned spelling byte and multiply to advance the hash. */
    while (*token != '\0') {
        /* Consume one UTF-8 byte at a time; tokenization owns character boundaries. */
        hash ^= (unsigned char)*token++;
        /* FNV-style multiplication advances the rolling hash. */
        hash *= CGAI_HASH_PRIME;
    }
    /* Step 3: Return the token-specific starting state for sign generation. */
    return hash;
}

/**
 * @brief Fill a vector with deterministic positive/negative components and normalize it.
 *
 * Each dimension receives either +1 or -1 before scaling. Dividing by the square root of the
 * number of components gives this sign vector unit length, so longer configured vectors do not
 * increase a token's magnitude. All components are regenerated from the spelling and seed.
 *
 * @param model Non-NULL model supplying a positive dimension count and seed.
 * @param token Borrowed NUL-terminated spelling to embed.
 * @param embedding Writable array of at least config.dimensions floats.
 */
static void embed_token(const cgai_model *model, const char *token, float *embedding) {
    /* The token hash is the deterministic starting state for every dimension. */
    /* Step 1: Derive the starting state from the token bytes and model seed. */
    uint64_t state = hash_token(token, model->config.seed);
    /* Sign vectors have equal magnitude before normalization. */
    double norm = 0.0;
    /* Step 2: Use each dimension index to obtain a distinct deterministic sign. */
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        /* Use the dimension index to derive a distinct pseudo-random component. */
        state += d;
        state = cgai_random_next(&state);
        /* The low bit chooses one of the two signed basis values. */
        embedding[d] = (state & CGAI_LOW_BIT_MASK) != 0U ? 1.0F : -1.0F;
        /* Accumulate squared length for later normalization. */
        norm += 1.0;
    }
    /* Scale the vector to unit length so dimensions do not change token weight. */
    /* Step 3: Compute the common scale needed to turn the sign vector into a unit vector. */
    const float scale = (float)(1.0 / sqrt(norm));
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        /* Apply the common scale after all signs have been generated. */
        /* Step 4: Apply that scale to each component already written into the caller's array. */
        embedding[d] *= scale;
    }
}

/**
 * @brief Accumulate one vocabulary token's scaled vector into a context sum.
 *
 * The temporary scratch vector is overwritten for every token, avoiding a fresh allocation inside
 * the context loop. Output already contains the contributions of earlier tokens. The weight
 * expresses recency; normalization of the accumulated sum happens separately.
 *
 * @param model Non-NULL model owning the vocabulary used by token.
 * @param token Non-NULL pointer to a valid vocabulary ID.
 * @param weight Contribution multiplier for this token.
 * @param output Writable dimension-sized running sum, distinct from scratch.
 * @param scratch Writable dimension-sized temporary vector.
 */
static void add_weighted_embedding(const cgai_model *model, const cgai_token_id *token,
                                   float weight, float *output, float *scratch) {
    /* Step 1: Recreate the token vector in reusable scratch storage. */
    embed_token(model, model->vocabulary[token->value], scratch);
    /* Step 2: Add every weighted component to the caller's existing context sum. */
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        output[d] += scratch[d] * weight;
    }
}

/**
 * @brief Convert a weighted vector sum to its weighted average.
 *
 * The divisor is the sum of recency weights, not a Euclidean vector norm. Consequently the context
 * average is not necessarily a unit vector. With no contributing history, weight_sum is zero and
 * the already-zero output is left untouched.
 *
 * @param model Non-NULL model supplying the dimension count.
 * @param output Writable dimension-sized accumulated vector.
 * @param weight_sum Sum of weights used to construct output; zero means no contributors.
 */
static void normalize_embedding(const cgai_model *model, float *output, double weight_sum) {
    /* Step 1: Avoid division when there was no contributing context. */
    if (weight_sum <= 0.0) {
        return;
    }
    /* Step 2: Divide each accumulated component by the same positive total weight. */
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        output[d] = (float)((double)output[d] / weight_sum);
    }
}

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
                            size_t history_count, float *output, float *scratch) {
    /* Start from zero because callers reuse this output buffer for every step. */
    /* Step 1: Reset the result because the same allocation serves many context calculations. */
    memset(output, 0, model->config.dimensions * sizeof(float));
    /* The newest context token receives the largest linear weight. */
    double weight_sum = 0.0;
    /* Ignore history older than the configured context window. */
    /* Step 2: Choose the start of the newest suffix within the configured window. */
    const size_t begin = history_count > model->config.context_window
                             ? history_count - model->config.context_window
                             : 0U;
    /* Step 3: Accumulate token embeddings with increasing recency weights and track their total. */
    for (size_t i = begin; i < history_count; ++i) {
        /* Convert the 1-based recency into a floating-point accumulation weight. */
        const float weight = (float)(i - begin + 1U);
        /* Add the token's normalized embedding to the shared context vector. */
        add_weighted_embedding(model, &history[i], weight, output, scratch);
        /* Track the divisor needed to convert the weighted sum into an average. */
        weight_sum += weight;
    }
    /* An empty history remains the zero vector; otherwise normalize the weighted sum. */
    /* Step 4: Divide by the total weight; an empty history remains a zero vector. */
    normalize_embedding(model, output, weight_sum);
}
