/** @file abi_spatial.c @brief Fixed-width ownership and JSON boundary for immutable spatial
 * queries. */
#include "centroid_gai_abi.h"
#include "centroid_gai_spatial.h"
#include "internal/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Bounded JSON cursor: 100 result rows fit well within this fixed capacity. */
typedef struct spatial_json {
    char *data;  /**< Owned 16384-byte allocation until published. */
    size_t used; /**< Bytes already written, excluding the terminator. */
} spatial_json;

/**
 * @brief Copy caller coordinates into an independently owned core index.
 * @param vectors Borrowed count*dimensions finite doubles.
 * @param categories Borrowed count-element category array in 0..63.
 * @param count Input row count, at most 4096.
 * @param dimensions Coordinate count, at most 4096; product is bounded by the core.
 * @param output Required empty ownership slot, reset before construction.
 * @return OK with ownership, INVALID_ARGUMENT for missing output, or ERROR with core diagnostic.
 */
cgai_abi_status cgai_abi_spatial_create(const double *vectors, const uint32_t *categories,
                                        uint32_t count, uint32_t dimensions,
                                        cgai_abi_spatial_index **output) {
    /* Step 1: Validate the ownership slot without reading any input arrays. */
    if (!output) {
        (void)cgai_fail("spatial output is required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    *output = NULL;
    /* Step 2: Transfer the independently copied core index only on successful construction. */
    *output = (cgai_abi_spatial_index *)cgai_spatial_create(vectors, categories, count, dimensions);
    return *output ? CGAI_ABI_OK : CGAI_ABI_ERROR;
}

/**
 * @brief Destroy an ABI spatial owner using the matching core allocator.
 * @param index Owned index or NULL; no concurrent call may retain it.
 */
void cgai_abi_spatial_destroy(cgai_abi_spatial_index *index) {
    /* Step 1: The opaque ABI handle wraps exactly one core owner. */
    cgai_spatial_destroy((cgai_spatial_index *)index);
}

/**
 * @brief Append one bounded neighbor object to the JSON cursor.
 * @param json Mutable owned output cursor.
 * @param hit Borrowed native row and exact distance.
 * @param comma Nonzero when an earlier array element needs a separating comma.
 * @return Nonzero if formatting fit the remaining capacity.
 */
static int append_neighbor(spatial_json *json, const cgai_spatial_hit *hit, int comma) {
    /* Step 1: Format without exceeding the fixed allocation. */
    const int written = snprintf(json->data + json->used, 16384U - json->used,
                                 "%s{\"index\":%zu,\"squaredDistance\":%.17g}", comma ? "," : "",
                                 hit->row, hit->squared_distance);
    if (written < 0 || (size_t)written >= 16384U - json->used)
        return 0;
    /* Step 2: Advance only after a complete object was written. */
    json->used += (size_t)written;
    return 1;
}

/**
 * @brief Complete a JSON cursor and transfer its allocation to the caller.
 * @param json Owned cursor, consumed on success and failure.
 * @param stats Borrowed completed query counters.
 * @param output Empty output descriptor receiving ownership on success.
 * @return OK on success, otherwise ERROR after releasing the cursor allocation.
 */
static cgai_abi_status finish_json(spatial_json json, const cgai_spatial_stats *stats,
                                   cgai_abi_buffer *output) {
    /* Step 1: Append counters while retaining a terminator within the fixed allocation. */
    const int written = snprintf(json.data + json.used, 16384U - json.used,
                                 "],\"comparisons\":%zu,\"visitedNodes\":%zu}", stats->comparisons,
                                 stats->visited_nodes);
    if (written < 0 || (size_t)written >= 16384U - json.used) {
        free(json.data);
        (void)cgai_fail("spatial JSON formatting failed");
        return CGAI_ABI_ERROR;
    }
    /* Step 2: Publish ownership and text length together. */
    output->data = (uint8_t *)json.data;
    output->size = json.used + (size_t)written;
    return CGAI_ABI_OK;
}

/**
 * @brief Serialize native results into an ABI-owned allocation.
 * @param hits Borrowed sorted hit array of stats->count entries.
 * @param stats Borrowed completed query counters.
 * @param output Empty output descriptor receiving ownership on success.
 * @return OK on success, otherwise ERROR after freeing temporary storage.
 */
static cgai_abi_status serialize_hits(const cgai_spatial_hit *hits, const cgai_spatial_stats *stats,
                                      cgai_abi_buffer *output) {
    /* Step 1: Allocate a bounded buffer and seed its array prefix. */
    spatial_json json = {calloc(16384U, 1U), 14U};
    if (!json.data) {
        (void)cgai_fail("could not allocate spatial JSON");
        return CGAI_ABI_ERROR;
    }
    memcpy(json.data, "{\"neighbors\":[", 14U);
    /* Step 2: Append bounded results, releasing temporary storage on formatting failure. */
    for (size_t i = 0; i < stats->count; ++i) {
        if (!append_neighbor(&json, &hits[i], i != 0U)) {
            free(json.data);
            (void)cgai_fail("spatial JSON exceeded its budget");
            return CGAI_ABI_ERROR;
        }
    }
    /* Step 3: Complete the object and transfer the owned allocation. */
    return finish_json(json, stats, output);
}

/**
 * @brief Translate fixed-width query options and return exact neighbors as JSON.
 * @param index Borrowed immutable spatial owner.
 * @param vector Borrowed dimension-sized query vector.
 * @param limit Requested result capacity from 1 through 100.
 * @param category Category 0..63 or UINT32_MAX for all.
 * @param exclude Original row index or UINT32_MAX for none.
 * @param output Required empty buffer descriptor, reset before validation.
 * @return OK with owned JSON, INVALID_ARGUMENT for NULL output, or ERROR with diagnostic.
 */
cgai_abi_status cgai_abi_spatial_query(const cgai_abi_spatial_index *index, const double *vector,
                                       uint32_t limit, uint32_t category, uint32_t exclude,
                                       cgai_abi_buffer *output) {
    /* Step 1: Clear the caller's descriptor and map only the ABI exclusion sentinel. */
    if (!output) {
        (void)cgai_fail("spatial JSON output is required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    output->data = NULL;
    output->size = 0U;
    const cgai_spatial_request request = {vector, limit, category,
                                          exclude == UINT32_MAX ? CGAI_SPATIAL_NO_EXCLUSION
                                                                : (size_t)exclude};
    cgai_spatial_hit hits[100];
    cgai_spatial_stats stats;
    /* Step 2: Keep results on this call's stack and serialize only a successful query. */
    if (cgai_spatial_query((const cgai_spatial_index *)index, &request, hits, &stats) !=
        CGAI_STATUS_OK)
        return CGAI_ABI_ERROR;
    return serialize_hits(hits, &stats, output);
}
