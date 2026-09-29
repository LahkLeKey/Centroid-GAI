/** @file test_knowledge.c @brief Compiled knowledge query, ownership, and ABI regressions. */
#include "centroid_gai_abi.h"
#include "centroid_gai_knowledge.h"
#include "test_utils.h"
#include <stdlib.h>
#include <string.h>

/** Reference order matches the public distance-then-stable-ID contract.
 * @param left Borrowed left hit.
 * @param right Borrowed right hit.
 * @return Negative, zero, or positive according to stable hit order.
 */
static int compare_hits(const void *left, const void *right) {
    const cgai_spatial_hit *a = left, *b = right;
    if (a->squared_distance != b->squared_distance)
        return a->squared_distance < b->squared_distance ? -1 : 1;
    return a->row < b->row ? -1 : a->row != b->row;
}

/** Calculate reference distances directly from the immutable compiled rows.
 * @param left Valid first row.
 * @param right Valid second row.
 * @return Exact squared Euclidean distance.
 */
static double distance(size_t left, size_t right) {
    const cgai_static_knowledge_centroid *a = cgai_static_knowledge_centroid_at(left);
    const cgai_static_knowledge_centroid *b = cgai_static_knowledge_centroid_at(right);
    double result = 0.0;
    for (size_t d = 0U; d < CGAI_STATIC_KNOWLEDGE_DIMENSIONS; ++d) {
        const double delta = (double)a->vector[d] - (double)b->vector[d];
        result += delta * delta;
    }
    return result;
}

/** Build independently sorted exhaustive candidates in caller-owned storage.
 * @param source Valid source row to exclude.
 * @param category Category index or all-category sentinel.
 * @param reference Writable array with room for every compiled row.
 * @return Number of sorted eligible candidates.
 */
static size_t reference_hits(size_t source, uint32_t category, cgai_spatial_hit *reference) {
    const size_t total = cgai_static_knowledge_centroid_count();
    size_t count = 0U;
    for (size_t row = 0U; row < total; ++row) {
        if (row != source && (category == CGAI_SPATIAL_ALL_CATEGORIES ||
                              cgai_static_knowledge_centroid_at(row)->category == category))
            reference[count++] = (cgai_spatial_hit){row, distance(source, row)};
    }
    qsort(reference, count, sizeof(*reference), compare_hits);
    return count;
}

/** Compare native search with independently sorted exhaustive candidates.
 * @param index Borrowed compiled search context.
 * @param source Valid source row.
 * @param category Category index or all-category sentinel.
 */
static void check_query(cgai_static_knowledge_index *index, size_t source, uint32_t category) {
    cgai_spatial_hit *reference =
        malloc(cgai_static_knowledge_centroid_count() * sizeof(*reference));
    TEST_CHECK(reference != NULL, "reference allocation failed");
    const size_t count = reference_hits(source, category, reference);
    cgai_spatial_hit hits[100];
    cgai_spatial_stats stats;
    TEST_CHECK(cgai_static_knowledge_neighbors(index, cgai_static_knowledge_centroid_at(source)->id,
                                               100U, category, hits, &stats) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(stats.count == (count < 100U ? count : 100U), "neighbor count mismatch");
    for (size_t i = 0U; i < stats.count; ++i)
        TEST_CHECK(hits[i].row == reference[i].row &&
                       hits[i].squared_distance == reference[i].squared_distance,
                   "compiled query disagrees with exhaustive search");
    free(reference);
}

/** Exercise both cross-category and filtered traversal with a representative from every category.
 * @param index Borrowed compiled search context.
 */
static void check_categories(cgai_static_knowledge_index *index) {
    uint32_t previous = UINT32_MAX;
    for (size_t row = 0U; row < cgai_static_knowledge_centroid_count(); ++row) {
        const cgai_static_knowledge_centroid *point = cgai_static_knowledge_centroid_at(row);
        TEST_CHECK(cgai_static_knowledge_find(point->id) == row, "stable ID lookup mismatch");
        if (point->category != previous) {
            check_query(index, row, CGAI_SPATIAL_ALL_CATEGORIES);
            check_query(index, row, point->category);
            previous = point->category;
        }
    }
}

/** Reject malformed requests and clear observable counters on failure.
 * @param index Borrowed compiled search context.
 */
static void check_invalid(cgai_static_knowledge_index *index) {
    cgai_spatial_hit hits[1];
    cgai_spatial_stats stats = {1U, 1U, 1U};
    const char *id = cgai_static_knowledge_centroid_at(0U)->id;
    TEST_CHECK(cgai_static_knowledge_find(NULL) == SIZE_MAX &&
                   cgai_static_knowledge_find("absent") == SIZE_MAX,
               "invalid ID lookup");
    TEST_CHECK(cgai_static_knowledge_neighbors(index, "absent", 1U, 0U, hits, &stats) !=
                   CGAI_STATUS_OK,
               "unknown ID accepted");
    TEST_CHECK(stats.count == 0U && stats.comparisons == 0U && stats.visited_nodes == 0U,
               "stale stats");
    TEST_CHECK(cgai_static_knowledge_neighbors(index, id, 0U, 0U, hits, &stats) != CGAI_STATUS_OK,
               "zero limit accepted");
    TEST_CHECK(cgai_static_knowledge_neighbors(index, id, 101U, 0U, hits, &stats) != CGAI_STATUS_OK,
               "oversized limit accepted");
    TEST_CHECK(cgai_static_knowledge_neighbors(index, id, 1U, 64U, hits, &stats) != CGAI_STATUS_OK,
               "unknown category accepted");
    TEST_CHECK(cgai_static_knowledge_neighbors(NULL, id, 1U, 0U, hits, &stats) != CGAI_STATUS_OK,
               "missing context accepted");
}

/** Check ABI ownership, bounded output, and error reset behavior. */
static void check_abi(void) {
    cgai_abi_knowledge_index *index = NULL;
    cgai_abi_buffer json = {0};
    TEST_CHECK(cgai_abi_knowledge_open(&index) == CGAI_ABI_OK, "ABI open failed");
    TEST_CHECK(cgai_abi_knowledge_catalog(&json) == CGAI_ABI_OK, "ABI catalog failed");
    TEST_CHECK(strstr((const char *)json.data, "native-core") != NULL, "stable category absent");
    cgai_abi_buffer_free(&json);
    TEST_CHECK(cgai_abi_knowledge_page(UINT32_MAX, 10U, &json) == CGAI_ABI_OK, "empty page failed");
    TEST_CHECK(strstr((const char *)json.data, "\"items\":[]") != NULL, "invalid empty page");
    cgai_abi_buffer_free(&json);
    TEST_CHECK(cgai_abi_knowledge_neighbors(index, cgai_static_knowledge_centroid_at(0U)->id, 3U,
                                            UINT32_MAX, &json) == CGAI_ABI_OK,
               "ABI query failed");
    cgai_abi_buffer_free(&json);
    TEST_CHECK(cgai_abi_knowledge_neighbors(index, NULL, 3U, UINT32_MAX, &json) != CGAI_ABI_OK &&
                   json.data == NULL && json.size == 0U,
               "ABI error did not reset output");
    cgai_abi_knowledge_close(index);
    cgai_abi_knowledge_close(NULL);
}

/** Verify all category-local rows resolve to the same native global records.
 * @param category Valid category index.
 */
static void check_module(size_t category) {
    const cgai_static_knowledge_category *metadata = cgai_static_knowledge_category_at(category);
    TEST_CHECK(metadata != NULL && metadata->index == category && metadata->count == 3U,
               "invalid category module metadata");
    TEST_CHECK(cgai_static_knowledge_category_find(metadata->key) == category,
               "category key lookup mismatch");
    for (size_t row = 0U; row < metadata->count; ++row) {
        const cgai_static_knowledge_centroid *point =
            cgai_static_knowledge_category_centroid_at(category, row);
        TEST_CHECK(point && point->category == category, "category-local row mismatch");
        const size_t key_length = strlen(metadata->key);
        TEST_CHECK(strncmp(point->id, metadata->key, key_length) == 0 &&
                       point->id[key_length] == ':',
                   "compiled record ID must belong to its baseline category");
        TEST_CHECK(cgai_static_knowledge_category_centroid_find(category, point->id) == point,
                   "category-local binary lookup mismatch");
        TEST_CHECK(cgai_static_knowledge_centroid_at(cgai_static_knowledge_find(point->id)) ==
                       point,
                   "global span lookup mismatch");
    }
    TEST_CHECK(cgai_static_knowledge_category_centroid_at(category, metadata->count) == NULL,
               "category-local bounds failure");
    TEST_CHECK(cgai_static_knowledge_category_centroid_find(category, "absent") == NULL,
               "unknown category-local ID accepted");
}

/** Exercise every category module and reject invalid category lookups. */
static void check_modules(void) {
    size_t total = 0U;
    for (size_t i = 0U; i < cgai_static_knowledge_category_count(); ++i) {
        check_module(i);
        total += cgai_static_knowledge_category_at(i)->count;
    }
    TEST_CHECK(total == cgai_static_knowledge_centroid_count(), "category module counts mismatch");
    TEST_CHECK(cgai_static_knowledge_category_at(SIZE_MAX) == NULL &&
                   cgai_static_knowledge_category_find(NULL) == SIZE_MAX &&
                   cgai_static_knowledge_category_find("absent") == SIZE_MAX &&
                   cgai_static_knowledge_category_centroid_find(SIZE_MAX, "build:compile") ==
                       NULL &&
                   cgai_static_knowledge_category_centroid_find(0U, NULL) == NULL,
               "invalid category lookup accepted");
}

/** Check the manual baseline's documented feature geometry and result records.
 * @param index Borrowed compiled search context.
 */
static void check_manual_baseline(cgai_static_knowledge_index *index) {
    const size_t category = cgai_static_knowledge_category_find("database");
    cgai_spatial_hit hits[3];
    cgai_spatial_stats stats;
    TEST_CHECK(cgai_static_knowledge_neighbors(index, "database:lookup", 3U, (uint32_t)category,
                                               hits, &stats) == CGAI_STATUS_OK,
               "manual database lookup failed");
    TEST_CHECK(stats.count == 2U, "source record must be excluded");
    const cgai_static_knowledge_centroid *schema = cgai_static_knowledge_centroid_at(hits[0].row);
    const cgai_static_knowledge_centroid *write = cgai_static_knowledge_centroid_at(hits[1].row);
    TEST_CHECK(strcmp(schema->id, "database:schema") == 0 && hits[0].squared_distance == 1.25,
               "schema should be the nearest database operation");
    TEST_CHECK(strcmp(write->id, "database:write") == 0 && hits[1].squared_distance == 2.0,
               "write should be the second database operation");
    TEST_CHECK(strstr(schema->description, "fields, types, and constraints") != NULL,
               "hit must resolve to readable stored data");
    for (size_t row = 0; row < cgai_static_knowledge_centroid_count(); ++row) {
        const cgai_static_knowledge_centroid *point = cgai_static_knowledge_centroid_at(row);
        TEST_CHECK(point->observations == 0U, "manual record claims learned observations");
        for (size_t axis = 8U; axis < CGAI_STATIC_KNOWLEDGE_DIMENSIONS; ++axis)
            TEST_CHECK(point->vector[axis] == 0.0F, "reserved feature must be zero");
    }
}

/** Run native query and ABI integration regressions.
 * @return Zero after all checks pass.
 */
int main(void) {
    check_modules();
    cgai_static_knowledge_index *index = cgai_static_knowledge_open();
    TEST_CHECK(index != NULL, cgai_last_error());
    check_manual_baseline(index);
    check_categories(index);
    check_invalid(index);
    cgai_static_knowledge_close(index);
    cgai_static_knowledge_close(NULL);
    check_abi();
    return 0;
}
