/** @file model_centroid.c @brief Nearest-centroid distance search. */

#include "internal/model_centroid.h"
#include <math.h>

/**
 * @brief Find the initialized centroid nearest to a context vector.
 *
 * A centroid is a learned mean vector representing similar contexts. Its row in the flat array
 * starts at centroid_index * dimensions. Squared Euclidean distance sums squared component
 * differences; taking a square root is unnecessary because it would preserve the ordering.
 * Strictly smaller distances replace the winner, so ties keep the lowest earlier index.
 *
 * @param model Non-NULL borrowed model with at least one initialized centroid and consistent
 * arrays.
 * @param embedding Borrowed vector containing config.dimensions finite float components.
 * @return Typed index of the nearest initialized centroid. No allocation or model mutation occurs.
 */
cgai_centroid_id cgai_nearest_centroid(const cgai_model *model, const float *embedding) {
    /* Start with centroid zero; training guarantees at least one initialized centroid. */
    /* Step 1: Start with centroid zero and infinite distance so the first finite candidate can win.
     */
    cgai_centroid_id best = cgai_centroid_id_from_size(0U);
    double best_distance = HUGE_VAL;
    /* Step 2: Visit only learned centroid rows, ignoring unused allocated rows. */
    for (size_t c = 0; c < model->initialized_centroids; ++c) {
        /* Distance is accumulated in double precision even though stored values are floats. */
        double distance = 0.0;
        /* Step 3: Accumulate squared differences in double precision across the candidate's
         * components. */
        for (size_t d = 0; d < model->config.dimensions; ++d) {
            /* Compare one embedding component with the same centroid component. */
            const double delta =
                (double)embedding[d] - (double)model->centroids[c * model->config.dimensions + d];
            /* Squared distance avoids a costly square root and preserves ordering. */
            distance += delta * delta;
        }
        /* Step 4: Remember the candidate only when it improves on the best distance so far. */
        if (distance < best_distance) {
            /* Keep the closest centroid seen so far. */
            best_distance = distance;
            best = cgai_centroid_id_from_size(c);
        }
    }
    /* Step 5: Return the typed centroid ID for downstream count-table lookup. */
    return best;
}
