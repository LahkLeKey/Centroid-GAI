/** @file spatial_contract.h @brief Private immutable spatial indexes over compatible centroid
 * vectors. */
#ifndef CGAI_SPATIAL_CONTRACT_H
#define CGAI_SPATIAL_CONTRACT_H
#include "internal/core_contract.h"
#include <stddef.h>
#include <stdint.h>

/** Opaque index that copies its coordinates and supports concurrent read-only queries. */
typedef struct cgai_spatial_index cgai_spatial_index;
/**
 * @brief Create an index whose rows may belong to multiple categories.
 * @param vectors Borrowed row-major count-times-dimensions finite coordinates.
 * @param categories Nonzero 64-bit membership mask per row; bit N denotes category N.
 * @param count Row count in 1..4096; count-times-dimensions at most 1048576.
 * @param dimensions Components per row in 1..4096.
 * @return Owned immutable copied index, or NULL with diagnostic; destroy with
 * cgai_spatial_destroy().
 */
cgai_spatial_index *cgai_spatial_create_masked(const double *vectors, const uint64_t *categories,
                                               size_t count, size_t dimensions);
#define CGAI_SPATIAL_ALL_CATEGORIES UINT32_MAX /**< Query all categories instead of one ID. */
#define CGAI_SPATIAL_NO_EXCLUSION SIZE_MAX     /**< Do not exclude a source row. */

/** Borrowed query and selection options; input vector length equals index dimensions. */
typedef struct cgai_spatial_request {
    const double
        *vector;  /**< Borrowed dimension-sized finite vector; components have magnitude <=1e100. */
    size_t limit; /**< Output capacity and requested neighbor count, from 1 through 100. */
    uint32_t category; /**< Category ID 0..63 or CGAI_SPATIAL_ALL_CATEGORIES. */
    size_t exclude;    /**< Original input row to omit, or CGAI_SPATIAL_NO_EXCLUSION. */
} cgai_spatial_request;

/** One exact neighbor, ordered by distance then ascending original input row. */
typedef struct cgai_spatial_hit {
    size_t row; /**< Original zero-based row, independent of the tree's internal ordering. */
    double squared_distance; /**< Squared Euclidean distance accumulated in double precision. */
} cgai_spatial_hit;

/** Per-call results and work counters; no mutable counters live in the index. */
typedef struct cgai_spatial_stats {
    size_t count;         /**< Number of valid entries written into the caller's result array. */
    size_t comparisons;   /**< Full vector distance evaluations performed in visited leaves. */
    size_t visited_nodes; /**< Nodes considered after category pruning, including distance-pruned
                             nodes. */
} cgai_spatial_stats;

/**
 * @brief Copy compatible vectors into an immutable median-split bounding-box index.
 *
 * Category IDs describe caller-defined groups; the library does not infer semantic categories or
 * embedding compatibility. Coordinate storage uses doubles so inspected native float coordinates
 * and their JSON representations can be queried without further rounding. No model is mutated.
 * @param vectors Borrowed row-major count*dimensions doubles, all finite with magnitude <=1e100.
 * @param categories Borrowed count-element array of category IDs from 0 through 63.
 * @param count Number of rows, from 1 through 4096; count*dimensions must not exceed 1048576.
 * @param dimensions Number of coordinates per row, from 1 through 4096.
 * @return Owned index, or NULL with cgai_last_error() on invalid input or allocation failure.
 * The caller must destroy the index after all concurrent queries finish. Input arrays may be
 * released as soon as construction returns; concurrent modification during copying is forbidden.
 */
cgai_spatial_index *cgai_spatial_create(const double *vectors, const uint32_t *categories,
                                        size_t count, size_t dimensions);

/**
 * @brief Release an index and all copied coordinates, bounds, categories, and row permutations.
 * @param index Owned index, or NULL. No query may still be using a non-NULL index.
 */
void cgai_spatial_destroy(cgai_spatial_index *index);

/**
 * @brief Query exact nearest rows with category filtering and stable row-ID ties.
 *
 * Bounding-box pruning is conservative at floating-point boundaries. The index remains immutable
 * and queries allocate no memory; concurrent callers must use separate output arrays and stats.
 * @param index Borrowed live index created by cgai_spatial_create().
 * @param request Borrowed options and dimension-sized query vector, kept alive throughout the call.
 * @param hits Writable array of at least request->limit entries; only stats->count are valid.
 * @param stats Required writable counters, reset to zero before argument validation.
 * @return CGAI_STATUS_OK on success (including no matching category), otherwise CGAI_STATUS_ERROR
 * with cgai_last_error(). On failure, non-NULL stats is zeroed and hit storage is unchanged.
 */
cgai_status cgai_spatial_query(const cgai_spatial_index *index, const cgai_spatial_request *request,
                               cgai_spatial_hit *hits, cgai_spatial_stats *stats);
#endif
