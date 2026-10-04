/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Database and Persistence; centroids: 3. */
#include "../knowledge_catalog.h"

/* Immutable search records, sorted by ID. Coordinates rank squared Euclidean
 * distance. See docs/architecture.md for catalog context and feature axes. */
static const cgai_static_knowledge_centroid centroids[] = {
    {.id = "database:lookup",
     .category = 1U,
     .observations = UINT64_C(0),
     .description =
         "Read a record by key; return the matching record or an explicit not-found result.",
     .cluster = 3U,
     .vector =
         (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
             [1 /* schema */] = 1.0F,
             [2 /* read */] = 1.0F,
             [4 /* validation */] = 0.5F,
         }},
    {.id = "database:schema",
     .category = 1U,
     .observations = UINT64_C(0),
     .description = "Define record fields, types, and constraints used to validate stored data.",
     .cluster = 4U,
     .vector =
         (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
             [1 /* schema */] = 1.0F,
             [4 /* validation */] = 1.0F,
         }},
    {.id = "database:write",
     .category = 1U,
     .observations = UINT64_C(0),
     .description =
         "Validate a record and persist it; return success or an error describing the failure.",
     .cluster = 5U,
     .vector =
         (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
             [1 /* schema */] = 1.0F,
             [3 /* write */] = 1.0F,
             [4 /* validation */] = 0.5F,
         }},
};

static const cgai_knowledge_module module = {
    .category = {.index = 1U,
                 .key = "database",
                 .name = "Database and Persistence",
                 .description = "Database schemas, record lookup, and validated writes",
                 .count = sizeof(centroids) / sizeof(centroids[0])},
    .rows = centroids};

/** Return borrowed category metadata and rows, valid for the process lifetime.
 * No allocation, file access, database query, or vector search occurs here. */
const cgai_knowledge_module *cgai_knowledge_database(void) { return &module; }
