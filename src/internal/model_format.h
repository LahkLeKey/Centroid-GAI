/** @file model_format.h @brief Private serialized model header layout. */

#ifndef CGAI_MODEL_FORMAT_H
#define CGAI_MODEL_FORMAT_H

/**
 * Positions in the seven-element uint64_t header array after the magic bytes.
 * Encoding and decoding must use this exact order. These are field indices, not
 * byte offsets; native uint64_t representation determines each field's byte span.
 */
enum {
    CGAI_HEADER_DIMENSIONS = 0,        /**< Components in one embedding/centroid vector. */
    CGAI_HEADER_CENTROID_COUNT,        /**< Allocated centroid-row capacity. */
    CGAI_HEADER_CONTEXT_WINDOW,        /**< Recent token count contributing to a context. */
    CGAI_HEADER_SEED,                  /**< Deterministic token-embedding seed. */
    CGAI_HEADER_VOCABULARY_SIZE,       /**< Number of following length-prefixed spelling records. */
    CGAI_HEADER_INITIALIZED_CENTROIDS, /**< Learned prefix of the centroid array. */
    CGAI_HEADER_EXAMPLES_SEEN          /**< Total learned context-to-target transitions. */
};

#endif
