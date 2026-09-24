/** @file model_sampling.c @brief Greedy and temperature-weighted token selection. */

#include "internal/model_sampling.h"

#include "internal/model_random.h"

#include <math.h>

/**
 * @brief One pass over a count row summarized for greedy and weighted selection.
 *
 * Raw counts determine the greedy winner; powered counts determine sampling mass.
 * Keeping both makes a separate probability allocation unnecessary. This is a
 * value-only result with no cleanup or pointers into model storage.
 */
typedef struct sampling_mass {
    cgai_token_id best; /**< Earliest token with greatest raw count, or EOS if none. */
    uint64_t best_count; /**< Raw observation count for the current greedy winner. */
    double total; /**< Sum of count^(1/temperature) for eligible positive counts. */
} sampling_mass;

/**
 * @brief Find the greedy winner and sum temperature-adjusted token weights.
 *
 * Each centroid row contains observed next-token counts. Raising positive counts to 1/temperature
 * sharpens weights below temperature one and flattens them above one. BOS is excluded by starting
 * at EOS; EOS remains selectable because it represents stopping. Tied maxima keep the first ID.
 *
 * @param model Non-NULL model with a consistent token-count matrix.
 * @param cluster Valid centroid row to inspect.
 * @param temperature Nonnegative finite value supplied by generation; zero requests greedy selection.
 * @return Value structure containing the greedy fallback, its raw count, and total sampling weight.
 */
static sampling_mass calculate_mass(const cgai_model *model, cgai_centroid_id cluster,
                                    double temperature) {
    /* Step 1: Use EOS as the fallback when no token has any recorded mass. */
    sampling_mass mass = {cgai_token_id_from_size(CGAI_TOKEN_EOS), 0U, 0.0};
    /* Step 2: Read the selected centroid's row, skipping the BOS control token. */
    for (size_t token = CGAI_TOKEN_EOS; token < model->vocabulary_size; ++token) {
        const uint64_t count = model->token_counts[cluster.value * model->vocabulary_size + token];
        /* Step 3: Track the first token with the greatest observed count. */
        if (count > mass.best_count) {
            mass.best_count = count;
            mass.best = cgai_token_id_from_size(token);
        }
        /* Step 4: Accumulate powered positive counts only when probabilistic sampling is requested. */
        if (temperature > 0.0 && count > 0U) {
            mass.total += pow((double)count, 1.0 / temperature);
        }
    }
    return mass;
}

/**
 * @brief Select the token covering a threshold in cumulative sampling weight.
 *
 * Conceptually each eligible token owns an interval whose width is its powered count. Subtracting
 * those widths walks the threshold through the intervals without constructing a probability array.
 * A fallback handles a threshold that is not consumed, including floating-point rounding effects.
 *
 * @param model Non-NULL model with count storage for the requested row.
 * @param cluster Valid centroid identifier.
 * @param temperature Positive sampling temperature used when computing the total mass.
 * @param threshold Random position in the total mass, normally in [0, total).
 * @param fallback Greedy token ID to return if no interval consumes the threshold.
 * @return Selected token ID, or fallback when no interval is reached.
 */
static cgai_token_id consume_threshold(const cgai_model *model, cgai_centroid_id cluster,
                                       double temperature, double threshold,
                                       cgai_token_id fallback) {
    /* Step 1: Traverse tokens in the same order used to calculate total mass. */
    for (size_t token = CGAI_TOKEN_EOS; token < model->vocabulary_size; ++token) {
        const uint64_t count = model->token_counts[cluster.value * model->vocabulary_size + token];
        if (count > 0U) {
            /* Step 2: Consume the interval belonging to each token with positive count. */
            threshold -= pow((double)count, 1.0 / temperature);
            /* Step 3: Return when the remaining threshold lands in this token's interval. */
            if (threshold <= 0.0) {
                return cgai_token_id_from_size(token);
            }
        }
    }
    /* Step 4: Retain a defined choice if rounding or an unusable mass leaves no selected interval. */
    return fallback;
}

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
                                double temperature, uint64_t *random_state) {
    /* Step 1: Find the greedy fallback and the total powered count mass. */
    const sampling_mass mass = calculate_mass(model, cluster, temperature);
    /* Step 2: Return deterministically for greedy mode or an empty distribution. */
    if (temperature <= 0.0 || mass.total == 0.0) {
        return mass.best;
    }
    /* Step 3: Advance the random state and map its high 53 bits into a unit-interval value. */
    const double unit =
        (double)(cgai_random_next(random_state) >> 11U) * (1.0 / 9007199254740992.0);
    /* Step 4: Scale the value by total mass and locate the corresponding token interval. */
    return consume_threshold(model, cluster, temperature, unit * mass.total, mass.best);
}
