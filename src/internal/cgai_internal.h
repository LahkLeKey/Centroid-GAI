/** @file cgai_internal.h @brief Shared private model representation and domain identifiers. */

#ifndef CGAI_INTERNAL_H
#define CGAI_INTERNAL_H

#include "centroid_gai.h"

#include <stddef.h>
#include <stdint.h>

/** Strong identifier for an entry in the vocabulary. */
typedef struct cgai_token_id {
    size_t value;
} cgai_token_id;

/** Strong identifier for a learned centroid. */
typedef struct cgai_centroid_id {
    size_t value;
} cgai_centroid_id;

/** Converts an array index into a token identifier. */
static inline cgai_token_id cgai_token_id_from_size(size_t value) {
    const cgai_token_id id = {value};
    return id;
}

/** Converts an array index into a centroid identifier. */
static inline cgai_centroid_id cgai_centroid_id_from_size(size_t value) {
    const cgai_centroid_id id = {value};
    return id;
}

/** Returns the sentinel token identifier used for lookup failure. */
static inline cgai_token_id cgai_token_id_invalid(void) {
    return cgai_token_id_from_size(SIZE_MAX);
}

/** Tests whether a token identifier refers to a vocabulary entry. */
static inline int cgai_token_id_is_valid(cgai_token_id id) { return id.value != SIZE_MAX; }

enum { CGAI_TOKEN_BOS = 0, CGAI_TOKEN_EOS = 1, CGAI_TOKEN_UNKNOWN = 2 };

struct cgai_model {
    cgai_config config;
    char **vocabulary;
    size_t vocabulary_size;
    size_t vocabulary_capacity;
    float *centroids;
    uint64_t *cluster_sizes;
    uint64_t *token_counts;
    size_t initialized_centroids;
    size_t examples_seen;
};

#endif
