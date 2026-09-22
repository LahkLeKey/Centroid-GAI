/** @file model_math.c @brief Hashed embeddings and nearest-centroid calculations. */

#include "internal/model_math.h"

#include <math.h>
#include <string.h>

static const uint64_t CGAI_HASH_OFFSET_BASIS = UINT64_C(1469598103934665603);
static const uint64_t CGAI_HASH_PRIME = UINT64_C(1099511628211);
static const uint64_t CGAI_MIX_MULTIPLIER_A = UINT64_C(0xbf58476d1ce4e5b9);
static const uint64_t CGAI_MIX_MULTIPLIER_B = UINT64_C(0x94d049bb133111eb);
static const uint64_t CGAI_MIX_INCREMENT = UINT64_C(0x9e3779b97f4a7c15);
static const unsigned CGAI_MIX_SHIFT_A = 30U;
static const unsigned CGAI_MIX_SHIFT_B = 27U;
static const unsigned CGAI_MIX_SHIFT_C = 31U;
static const uint64_t CGAI_LOW_BIT_MASK = UINT64_C(1);

/** Applies the splitmix64 avalanche transform. */
static uint64_t mix64(uint64_t value) {
    /* XOR-shift, multiply, and XOR-shift steps avalanche nearby input bits. */
    value ^= value >> CGAI_MIX_SHIFT_A;
    /* The first odd multiplier spreads the high-quality bits across the word. */
    value *= CGAI_MIX_MULTIPLIER_A;
    /* A second shift/multiply pair removes remaining linear structure. */
    value ^= value >> CGAI_MIX_SHIFT_B;
    value *= CGAI_MIX_MULTIPLIER_B;
    /* The final shift makes all output bits depend on the complete state. */
    return value ^ (value >> CGAI_MIX_SHIFT_C);
}

/** Hashes a token with the model seed using FNV-style byte accumulation. */
static uint64_t hash_token(const char *token, uint64_t seed) {
    /* Mix the model seed into the initial hash so models remain independently reproducible. */
    uint64_t hash = CGAI_HASH_OFFSET_BASIS ^ seed;
    while (*token != '\0') {
        /* Consume one UTF-8 byte at a time; tokenization owns character boundaries. */
        hash ^= (unsigned char)*token++;
        /* FNV-style multiplication advances the rolling hash. */
        hash *= CGAI_HASH_PRIME;
    }
    return hash;
}

/** Creates a normalized deterministic sign embedding for one token. */
static void embed_token(const cgai_model *model, const char *token, float *embedding) {
    /* The token hash is the deterministic starting state for every dimension. */
    uint64_t state = hash_token(token, model->config.seed);
    /* Sign vectors have equal magnitude before normalization. */
    double norm = 0.0;
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        /* Use the dimension index to derive a distinct pseudo-random component. */
        state = mix64(state + d + CGAI_MIX_INCREMENT);
        /* The low bit chooses one of the two signed basis values. */
        embedding[d] = (state & CGAI_LOW_BIT_MASK) != 0U ? 1.0F : -1.0F;
        /* Accumulate squared length for later normalization. */
        norm += 1.0;
    }
    /* Scale the vector to unit length so dimensions do not change token weight. */
    const float scale = (float)(1.0 / sqrt(norm));
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        /* Apply the common scale after all signs have been generated. */
        embedding[d] *= scale;
    }
}

/** Adds one weighted token embedding to an output vector. */
static void add_weighted_embedding(const cgai_model *model, const cgai_token_id *token,
                                   float weight, float *output, float *scratch) {
    embed_token(model, model->vocabulary[token->value], scratch);
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        output[d] += scratch[d] * weight;
    }
}

/** Divides a context vector by its accumulated weight when nonempty. */
static void normalize_embedding(const cgai_model *model, float *output, double weight_sum) {
    if (weight_sum <= 0.0) {
        return;
    }
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        output[d] = (float)((double)output[d] / weight_sum);
    }
}

/** Combines recent token embeddings into one weighted context vector. */
void cgai_context_embedding(const cgai_model *model, const cgai_token_id *history,
                            size_t history_count, float *output, float *scratch) {
    /* Start from zero because callers reuse this output buffer for every step. */
    memset(output, 0, model->config.dimensions * sizeof(float));
    /* The newest context token receives the largest linear weight. */
    double weight_sum = 0.0;
    /* Ignore history older than the configured context window. */
    const size_t begin = history_count > model->config.context_window
                             ? history_count - model->config.context_window
                             : 0U;
    for (size_t i = begin; i < history_count; ++i) {
        /* Convert the 1-based recency into a floating-point accumulation weight. */
        const float weight = (float)(i - begin + 1U);
        /* Add the token's normalized embedding to the shared context vector. */
        add_weighted_embedding(model, &history[i], weight, output, scratch);
        /* Track the divisor needed to convert the weighted sum into an average. */
        weight_sum += weight;
    }
    /* An empty history remains the zero vector; otherwise normalize the weighted sum. */
    normalize_embedding(model, output, weight_sum);
}

/** Finds the initialized centroid with minimum squared Euclidean distance. */
cgai_centroid_id cgai_nearest_centroid(const cgai_model *model, const float *embedding) {
    /* Start with centroid zero; training guarantees at least one initialized centroid. */
    cgai_centroid_id best = cgai_centroid_id_from_size(0U);
    double best_distance = HUGE_VAL;
    for (size_t c = 0; c < model->initialized_centroids; ++c) {
        /* Distance is accumulated in double precision even though stored values are floats. */
        double distance = 0.0;
        for (size_t d = 0; d < model->config.dimensions; ++d) {
            /* Compare one embedding component with the same centroid component. */
            const double delta =
                (double)embedding[d] - (double)model->centroids[c * model->config.dimensions + d];
            /* Squared distance avoids a costly square root and preserves ordering. */
            distance += delta * delta;
        }
        if (distance < best_distance) {
            /* Keep the closest centroid seen so far. */
            best_distance = distance;
            best = cgai_centroid_id_from_size(c);
        }
    }
    return best;
}

/** Advances the deterministic model random-number state. */
uint64_t cgai_random_next(uint64_t *state) {
    *state += CGAI_MIX_INCREMENT;
    return mix64(*state);
}
