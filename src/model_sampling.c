/** @file model_sampling.c @brief Greedy and temperature-weighted token selection. */

#include "internal/model_sampling.h"

#include "internal/model_math.h"

#include <math.h>

typedef struct sampling_mass {
    cgai_token_id best;
    uint64_t best_count;
    double total;
} sampling_mass;

/** Computes the greedy fallback and total temperature-weighted mass. */
static sampling_mass calculate_mass(const cgai_model *model, cgai_centroid_id cluster,
                                    double temperature) {
    sampling_mass mass = {cgai_token_id_from_size(CGAI_TOKEN_EOS), 0U, 0.0};
    for (size_t token = CGAI_TOKEN_EOS; token < model->vocabulary_size; ++token) {
        const uint64_t count = model->token_counts[cluster.value * model->vocabulary_size + token];
        if (count > mass.best_count) {
            mass.best_count = count;
            mass.best = cgai_token_id_from_size(token);
        }
        if (temperature > 0.0 && count > 0U) {
            mass.total += pow((double)count, 1.0 / temperature);
        }
    }
    return mass;
}

/** Selects the token whose probability interval contains the random threshold. */
static cgai_token_id consume_threshold(const cgai_model *model, cgai_centroid_id cluster,
                                       double temperature, double threshold,
                                       cgai_token_id fallback) {
    for (size_t token = CGAI_TOKEN_EOS; token < model->vocabulary_size; ++token) {
        const uint64_t count = model->token_counts[cluster.value * model->vocabulary_size + token];
        if (count > 0U) {
            threshold -= pow((double)count, 1.0 / temperature);
            if (threshold <= 0.0) {
                return cgai_token_id_from_size(token);
            }
        }
    }
    return fallback;
}

/** Selects the highest-count token or samples counts using temperature weights. */
cgai_token_id cgai_select_token(const cgai_model *model, cgai_centroid_id cluster,
                                double temperature, uint64_t *random_state) {
    const sampling_mass mass = calculate_mass(model, cluster, temperature);
    if (temperature <= 0.0 || mass.total == 0.0) {
        return mass.best;
    }
    const double unit =
        (double)(cgai_random_next(random_state) >> 11U) * (1.0 / 9007199254740992.0);
    return consume_threshold(model, cluster, temperature, unit * mass.total, mass.best);
}