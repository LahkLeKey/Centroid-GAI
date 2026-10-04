/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Build and Configuration; centroids: 3. */
#include "../knowledge_catalog.h"

/* Immutable search records, sorted by ID. Coordinates rank squared Euclidean
 * distance. See docs/architecture.md for catalog context and feature axes. */
static const cgai_static_knowledge_centroid centroids[] = {
    {.id = "build:compile",
     .category = 0U,
     .observations = UINT64_C(0),
     .description = "Compile C source files into object files and link them into an executable.",
     .cluster = 0U,
     .vector =
         (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
             [0 /* configuration */] = 1.0F,
             [4 /* validation */] = 0.5F,
         }},
    {.id = "build:configure",
     .category = 0U,
     .observations = UINT64_C(0),
     .description = "Select compiler options and source files before building a C application.",
     .cluster = 1U,
     .vector =
         (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
             [0 /* configuration */] = 1.0F,
             [4 /* validation */] = 0.25F,
         }},
    {.id = "build:test",
     .category = 0U,
     .observations = UINT64_C(0),
     .description = "Run test cases and report whether the observed results match expectations.",
     .cluster = 2U,
     .vector =
         (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
             [0 /* configuration */] = 0.5F,
             [4 /* validation */] = 1.0F,
         }},
};

static const cgai_knowledge_module module = {
    .category = {.index = 0U,
                 .key = "build",
                 .name = "Build and Configuration",
                 .description = "Configure, compile, and validate a C application",
                 .count = sizeof(centroids) / sizeof(centroids[0])},
    .rows = centroids};

/** Return borrowed category metadata and rows, valid for the process lifetime.
 * No allocation, file access, database query, or vector search occurs here. */
const cgai_knowledge_module *cgai_knowledge_build(void) { return &module; }
