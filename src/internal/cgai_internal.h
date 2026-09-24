/** @file cgai_internal.h @brief Shared private model representation and domain identifiers. */

#ifndef CGAI_INTERNAL_H
#define CGAI_INTERNAL_H

#include "centroid_gai.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Vocabulary index wrapped in its own C type.
 *
 * The wrapper separates token IDs from centroid IDs at compile time. It adds no
 * ownership: copying an ID copies an index, not a string. SIZE_MAX means lookup
 * or insertion failure; other values still require model-specific bounds.
 */
typedef struct cgai_token_id {
    size_t value; /**< Index in vocabulary, or SIZE_MAX for the invalid sentinel. */
} cgai_token_id;

/** A centroid-row index kept distinct from a vocabulary index by the type system. */
typedef struct cgai_centroid_id {
    size_t value; /**< Row index; callers require it to be below initialized_centroids. */
} cgai_centroid_id;

/**
 * @brief Wrap a raw vocabulary index in the token-ID domain type.
 *
 * A struct containing one size_t prevents the compiler from silently accepting a centroid ID in
 * place of a token ID. This constructor only wraps a value; it does not check a particular model's
 * vocabulary bounds. Callers must supply an appropriate index or use the invalid sentinel deliberately.
 *
 * @param value Raw vocabulary index to wrap.
 * @return Token-ID value with no allocation or ownership.
 */
static inline cgai_token_id cgai_token_id_from_size(size_t value) {
    /* Step 1: Initialize the domain-specific wrapper with the raw index. */
    const cgai_token_id id = {value};
    /* Step 2: Return the wrapper by value so no pointer to a local variable escapes. */
    return id;
}

/**
 * @brief Wrap a raw centroid-row index in the centroid-ID domain type.
 *
 * The distinct struct type prevents accidental interchange with vocabulary identifiers. This
 * helper performs no bounds check against initialized_centroids; callers establish that condition
 * before using the returned value for array indexing.
 *
 * @param value Raw centroid row index to wrap.
 * @return Centroid-ID value requiring no cleanup.
 */
static inline cgai_centroid_id cgai_centroid_id_from_size(size_t value) {
    /* Step 1: Construct the centroid-specific wrapper around the raw row index. */
    const cgai_centroid_id id = {value};
    /* Step 2: Return a value copy rather than a pointer to stack storage. */
    return id;
}

/**
 * @brief Construct the token-ID sentinel used for lookup or insertion failure.
 *
 * SIZE_MAX is the largest size_t value and is reserved here to mean no token. Valid vocabulary
 * indices occupy the model's much smaller initialized prefix. Returning a typed sentinel keeps
 * failure signaling inside the same domain type as successful lookup.
 *
 * @return Token ID whose value is SIZE_MAX.
 */
static inline cgai_token_id cgai_token_id_invalid(void) {
    /* Step 1: Wrap the dedicated invalid index in the token-ID type. */
    return cgai_token_id_from_size(SIZE_MAX);
}

/**
 * @brief Check whether a token ID is different from the invalid sentinel.
 *
 * This is a sentinel test, not a complete bounds check: it cannot prove id.value is below the
 * vocabulary size of a particular model because no model is supplied. Callers still need valid
 * model-specific provenance before using an ID to index storage.
 *
 * @param id Token-ID value returned by a trusted lookup or insertion path.
 * @return Nonzero when the value is not SIZE_MAX, otherwise zero.
 */
static inline int cgai_token_id_is_valid(cgai_token_id id) { /* Step 1: Distinguish an ordinary index from the reserved lookup-failure sentinel. */
 return id.value != SIZE_MAX; }

/** Reserved insertion-order IDs shared by tokenization consumers and the codec. */
enum {
    CGAI_TOKEN_BOS = 0, /**< Beginning-of-sequence context before any real token. */
    CGAI_TOKEN_EOS = 1, /**< End-of-sequence target; selecting it stops generation. */
    CGAI_TOKEN_UNKNOWN = 2 /**< Replacement for prompt spellings absent from vocabulary. */
};

/**
 * @brief Complete private state owned by one model handle.
 *
 * The public headers expose only an incomplete type so callers cannot depend on
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
    cgai_config config; /**< Copied shape/seed settings; owns no pointers. */
    char **vocabulary; /**< Owned pointer array; each occupied slot owns a C string. */
    size_t vocabulary_size; /**< Occupied vocabulary entries and count-matrix row width. */
    size_t vocabulary_capacity; /**< Allocated pointer slots, possibly more than occupied entries. */
    float *centroids; /**< Owned centroid_count-by-dimensions matrix of learned mean vectors. */
    uint64_t *cluster_sizes; /**< Owned count of observations assigned to each centroid. */
    uint64_t *token_counts; /**< Owned centroid_count-by-vocabulary_size frequency matrix. */
    size_t initialized_centroids; /**< Learned prefix of centroid rows available for lookup. */
    size_t examples_seen; /**< Total learned transitions, including each corpus's EOS target. */
};

#endif
