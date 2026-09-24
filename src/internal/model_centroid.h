/** @file model_centroid.h @brief Private centroid operations. */
#ifndef CGAI_MODEL_CENTROID_H
#define CGAI_MODEL_CENTROID_H
#include "cgai_internal.h"

/**
 * @brief Find the initialized centroid nearest to a context vector.
 *
 * A centroid is a learned mean vector representing similar contexts. Its row in the flat array
 * starts at centroid_index * dimensions. Squared Euclidean distance sums squared component
 * differences; taking a square root is unnecessary because it would preserve the ordering.
 * Strictly smaller distances replace the winner, so ties keep the lowest earlier index.
 *
 * @param model Non-NULL borrowed model with at least one initialized centroid and consistent arrays.
 * @param embedding Borrowed vector containing config.dimensions finite float components.
 * @return Typed index of the nearest initialized centroid. No allocation or model mutation occurs.
 */
cgai_centroid_id cgai_nearest_centroid(const cgai_model *model, const float *embedding);
#endif
