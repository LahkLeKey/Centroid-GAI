/** @file cgai_internal.h @brief Private count-model state and centroid row identifiers. */
#ifndef CGAI_INTERNAL_H
#define CGAI_INTERNAL_H

#include "centroid_contract.h"
#include "model/model_contract.h"
#include "token_contract.h"

/**
 * @brief Complete private state owned by one model handle.
 *
 * The private domain contract exposes an incomplete type; numerical code owns
 * this layout. Each pointer below owns a separate heap allocation. Vocabulary
 * additionally owns every string referenced by its occupied entries. Creation
 * establishes that ownership; cgai_model_destroy() releases it from the inside out.
 *
 * Two-dimensional tables are flattened into contiguous one-dimensional arrays:
 * @code
 * centroids[centroid_index * config.dimensions + component_index]
 * token_counts[centroid_index * vocabulary_size + token_index]
 * @endcode
 * Multiplying by the row width skips complete rows; adding a column selects one
 * element within the chosen row. Vocabulary growth changes the token-count row
 * width, so existing rows must be copied into a wider replacement allocation.
 *
 * Training mutates this state. Generation only reads it and owns separate temporary
 * buffers. No mutex is embedded here; callers coordinate training/destruction
 * against any other operation using the same model.
 */
struct cgai_model {
    cgai_config config;     /**< Copied shape/seed settings; owns no pointers. */
    char **vocabulary;      /**< Owned pointer array; each occupied slot owns a C string. */
    size_t vocabulary_size; /**< Occupied vocabulary entries and count-matrix row width. */
    size_t
        vocabulary_capacity; /**< Allocated pointer slots, possibly more than occupied entries. */
    float *centroids; /**< Owned centroid_count-by-dimensions matrix of learned mean vectors. */
    uint64_t *cluster_sizes;      /**< Owned count of observations assigned to each centroid. */
    uint64_t *token_counts;       /**< Owned centroid_count-by-vocabulary_size frequency matrix. */
    size_t initialized_centroids; /**< Learned prefix of centroid rows available for lookup. */
    size_t examples_seen; /**< Total learned transitions, including each corpus's EOS target. */
};

#endif
