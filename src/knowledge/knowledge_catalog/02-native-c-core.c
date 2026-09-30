/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Native C Core; centroids: 3. */
#include "../knowledge_catalog.h"

/* Immutable search records, sorted by ID. Coordinates rank squared Euclidean
 * distance. See docs/architecture.md for catalog context and feature axes. */
static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "native-core:allocate",
        .category = 2U,
        .observations = UINT64_C(0),
        .description = "Allocate a buffer; return an owned pointer or NULL, and release it with free.",
        .cluster = 6U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            [4 /* validation */] = 0.5F,
            [5 /* memory */] = 1.0F,
        }
    },
    {
        .id = "native-core:distance",
        .category = 2U,
        .observations = UINT64_C(0),
        .description = "Sum squared coordinate differences; return a nonnegative numeric distance.",
        .cluster = 7U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            [2 /* read */] = 0.5F,
            [6 /* numeric */] = 1.0F,
        }
    },
    {
        .id = "native-core:serialize",
        .category = 2U,
        .observations = UINT64_C(0),
        .description = "Encode a record into bytes; return an owned buffer and its length or an error.",
        .cluster = 8U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            [3 /* write */] = 0.5F,
            [4 /* validation */] = 0.5F,
            [7 /* serialization */] = 1.0F,
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 2U,
        .key = "native-core",
        .name = "Native C Core",
        .description = "C memory ownership, numeric distance, and byte serialization",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

/** Return borrowed category metadata and rows, valid for the process lifetime.
 * No allocation, file access, database query, or vector search occurs here. */
const cgai_knowledge_module *cgai_knowledge_native_core(void) {
    return &module;
}
