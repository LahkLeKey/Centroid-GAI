/** @file centroid_gai_knowledge.h @brief Compiled static centroid knowledge index. */
#ifndef CENTROID_GAI_KNOWLEDGE_H
#define CENTROID_GAI_KNOWLEDGE_H

#include "centroid_gai_spatial.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CGAI_STATIC_KNOWLEDGE_DIMENSIONS 32U /**< Components in each compiled centroid vector. */

/** Read-only compiled centroid row; row order is ascending stable ID. */
typedef struct cgai_static_knowledge_centroid {
    const char *id;          /**< Catalog ID such as `database:lookup`; not a file path. */
    uint32_t category;       /**< Index into cgai_static_knowledge_category_name(). */
    uint32_t cluster;        /**< Canonical geometric cluster; exact float32 duplicates share it. */
    uint64_t observations;   /**< Training transitions; zero for manually authored records. */
    const char *description; /**< Human-readable description returned with this record. */
    const float *vector; /**< Borrowed immutable 32-component coordinates, shared across aliases. */
} cgai_static_knowledge_centroid;

/** Canonical cluster metadata; every source ID remains available as an alias row. */
typedef struct cgai_static_knowledge_cluster {
    size_t row;            /**< Global row of the earliest stable ID representing this cluster. */
    size_t aliases;        /**< Number of original rows represented by this vector. */
    uint64_t observations; /**< Sum of observations across aliases, preserving provenance. */
    uint64_t categories;   /**< Union of category membership bits across aliases. */
} cgai_static_knowledge_cluster;

/** Count distinct exact float32 vectors.
 * @return Number of canonical clusters.
 */
size_t cgai_static_knowledge_cluster_count(void);
/** Inspect aggregate metadata for a canonical cluster.
 * @param cluster Dense canonical index from zero to cluster_count minus one.
 * @param output Required writable metadata, cleared on failure.
 * @return OK on success, otherwise ERROR with a diagnostic.
 */
cgai_status cgai_static_knowledge_cluster_get(size_t cluster,
                                              cgai_static_knowledge_cluster *output);
/** Create a standard spatial index with one row per canonical cluster.
 * @return Owned index, or NULL on failure; destroy with cgai_spatial_destroy().
 * Row IDs are cluster IDs. Category filters use the union of all alias memberships.
 */
cgai_spatial_index *cgai_static_knowledge_create_cluster_index(void);

/** Immutable category metadata, owned by its compiled C module. */
typedef struct cgai_static_knowledge_category {
    uint32_t index;          /**< Stable category index for filtered neighbor queries. */
    const char *key;         /**< Stable machine-readable key. */
    const char *name;        /**< Human-readable category name. */
    const char *description; /**< Description of the category's records. */
    size_t count;            /**< Number of category-local centroid rows. */
} cgai_static_knowledge_category;

/**
 * @brief Inspect a category without constructing a search index.
 * @param category Zero-based category index.
 * @return Borrowed immutable metadata, or NULL when out of range.
 */
const cgai_static_knowledge_category *cgai_static_knowledge_category_at(size_t category);
/**
 * @brief Resolve a stable category key without allocation.
 * @param key Borrowed NUL-terminated category key, or NULL.
 * @return Category index, or SIZE_MAX when absent.
 */
size_t cgai_static_knowledge_category_find(const char *key);
/**
 * @brief Read a sorted row inside a category module.
 * @param category Zero-based category index.
 * @param row Zero-based category-local row index.
 * @return Borrowed immutable centroid, or NULL for invalid bounds.
 */
const cgai_static_knowledge_centroid *cgai_static_knowledge_category_centroid_at(size_t category,
                                                                                 size_t row);
/**
 * @brief Look up an exact stable ID within one category by binary search.
 * @param category Zero-based category index.
 * @param id Borrowed stable centroid ID, or NULL.
 * @return Borrowed immutable centroid, or NULL for an unknown ID or category.
 */
const cgai_static_knowledge_centroid *cgai_static_knowledge_category_centroid_find(size_t category,
                                                                                   const char *id);

/** Number of centroids compiled into this library.
 * @return Number of compiled centroid rows.
 */
size_t cgai_static_knowledge_centroid_count(void);

/** Borrow one sorted centroid row, or return NULL when row is out of range.
 * @param row Zero-based sorted row index.
 * @return Borrowed immutable row, or NULL when out of range.
 */
const cgai_static_knowledge_centroid *cgai_static_knowledge_centroid_at(size_t row);

/** Number of category identifiers compiled into this library.
 * @return Number of compiled categories.
 */
size_t cgai_static_knowledge_category_count(void);

/** Borrow a category name by ID, or return NULL when category is out of range.
 * @param category Zero-based category index.
 * @return Borrowed display name, or NULL when out of range.
 */
const char *cgai_static_knowledge_category_name(size_t category);

/** Borrow a stable category key (for example native-core), or NULL when out of range.
 * @param category Zero-based category index.
 * @return Borrowed stable key, or NULL when out of range.
 */
const char *cgai_static_knowledge_category_key(size_t category);

/** Borrow a human-readable description for a category, or NULL when out of range.
 * @param category Zero-based category index.
 * @return Borrowed description, or NULL when out of range.
 */
const char *cgai_static_knowledge_category_description(size_t category);

/** SHA-256 of an imported release manifest, or an empty string for a manual baseline.
 * @return Borrowed NUL-terminated release SHA-256 or empty string.
 */
const char *cgai_static_knowledge_release_sha256(void);

/**
 * @brief Create an owned exact spatial index over all compiled centroid vectors.
 *
 * The returned index is an ordinary immutable spatial index and must be released with
 * cgai_spatial_destroy(). Its row IDs map directly to cgai_static_knowledge_centroid_at().
 * @return Owned spatial index, or NULL with cgai_last_error() on allocation or validation failure.
 */
cgai_spatial_index *cgai_static_knowledge_create_index(void);

/** Owned compiled-knowledge search context. Reuse it across read-only queries. */
typedef struct cgai_static_knowledge_index cgai_static_knowledge_index;

/** Open an owned search context, or NULL with cgai_last_error() on failure.
 * @return Owned context, or NULL with cgai_last_error() on failure.
 */
cgai_static_knowledge_index *cgai_static_knowledge_open(void);

/** Release a search context after all queries finish; NULL is accepted.
 * @param index Owned context or NULL; no concurrent queries may remain.
 */
void cgai_static_knowledge_close(cgai_static_knowledge_index *index);

/** Find a stable centroid ID by binary search; return SIZE_MAX for NULL or an unknown ID.
 * @param id Borrowed stable ID or NULL.
 * @return Sorted row index, or SIZE_MAX if absent.
 */
size_t cgai_static_knowledge_find(const char *id);

/**
 * @brief Find other compiled centroids nearest to a stable centroid ID.
 *
 * Results exclude the source cluster and sort by squared distance, then stable ID.
 * Each hit names a representative global row; resolve it with centroid_at() to
 * read its metadata and vector. Only stats->count entries in hits are valid. Queries
 * allocate no memory and may run concurrently with separate hit arrays and statistics.
 * @param index Borrowed context returned by cgai_static_knowledge_open().
 * @param id Borrowed NUL-terminated stable centroid ID.
 * @param limit Result capacity from 1 through 100.
 * @param category Compiled category ID or CGAI_SPATIAL_ALL_CATEGORIES.
 * @param hits Writable array with room for limit entries; rows map to centroid_at().
 * @param stats Required writable counters; cleared on failure.
 * @return CGAI_STATUS_OK on success, otherwise an error with cgai_last_error().
 */
cgai_status cgai_static_knowledge_neighbors(const cgai_static_knowledge_index *index,
                                            const char *id, size_t limit, uint32_t category,
                                            cgai_spatial_hit *hits, cgai_spatial_stats *stats);

#ifdef __cplusplus
}
#endif
#endif
