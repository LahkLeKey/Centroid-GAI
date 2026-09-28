/** @file test_spatial.c @brief Independent exhaustive, lifetime, and ABI checks for spatial
 * indexes. */
#include "centroid_gai_abi.h"
#include "centroid_gai_knowledge.h"
#include "centroid_gai_spatial.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Small synthetic corpus with three categories and deliberately identical coordinates. */
typedef struct spatial_fixture {
    double vectors[192];     /**< Sixty-four three-dimensional rows. */
    uint32_t categories[64]; /**< Alternating category IDs. */
} spatial_fixture;

/**
 * @brief Fill deterministic separated coordinates and an exact tie.
 * @param fixture Writable stack-owned fixture.
 */
static void fill_fixture(spatial_fixture *fixture) {
    /* Step 1: Use ordinary arithmetic rather than the index implementation's random state. */
    for (size_t i = 0; i < 64U; ++i) {
        fixture->vectors[i * 3U] = (double)(i / 2U) * 10.0;
        fixture->vectors[i * 3U + 1U] = (double)(i % 3U) * 0.1;
        fixture->vectors[i * 3U + 2U] = 0.0;
        fixture->categories[i] = (uint32_t)(i % 3U);
    }
    /* Step 2: Two different category rows intentionally occupy exactly the same location. */
    memcpy(fixture->vectors + 3U, fixture->vectors, 3U * sizeof(double));
}

/**
 * @brief Supply qsort with the public distance/row tie contract.
 * @param first Borrowed cgai_spatial_hit.
 * @param second Borrowed cgai_spatial_hit.
 * @return Negative, zero, or positive according to exact distance then row ID.
 */
static int compare_hits(const void *first, const void *second) {
    /* Step 1: Compare distance independently of the index's insertion algorithm. */
    const cgai_spatial_hit *a = first;
    const cgai_spatial_hit *b = second;
    if (a->squared_distance < b->squared_distance)
        return -1;
    if (a->squared_distance > b->squared_distance)
        return 1;
    return (a->row > b->row) - (a->row < b->row);
}

/**
 * @brief Exhaustively score every eligible fixture row and sort the complete result set.
 * @param fixture Borrowed unmodified source coordinates.
 * @param request Borrowed valid query options.
 * @param hits Writable storage for all 64 fixture rows.
 * @return Total eligible rows before truncating to the requested limit.
 */
static size_t reference_hits(const spatial_fixture *fixture, const cgai_spatial_request *request,
                             cgai_spatial_hit *hits) {
    /* Step 1: Scan every row, applying only the externally specified filters. */
    size_t count = 0;
    for (size_t row = 0; row < 64U; ++row) {
        if (row == request->exclude ||
            (request->category != UINT32_MAX && fixture->categories[row] != request->category))
            continue;
        cgai_spatial_hit hit = {row, 0.0};
        for (size_t d = 0; d < 3U; ++d) {
            const double delta = request->vector[d] - fixture->vectors[row * 3U + d];
            hit.squared_distance += delta * delta;
        }
        hits[count++] = hit;
    }
    /* Step 2: Sort all eligible rows instead of imitating bounded tree search. */
    qsort(hits, count, sizeof(*hits), compare_hits);
    return count;
}

/**
 * @brief Compare a single indexed result and its work count against exhaustive search.
 * @param index Borrowed immutable native index.
 * @param fixture Borrowed reference data from before construction.
 * @param request Borrowed valid query.
 */
static void check_query(const cgai_spatial_index *index, const spatial_fixture *fixture,
                        const cgai_spatial_request *request) {
    /* Step 1: Compute the independent full-sort reference and execute the native query. */
    cgai_spatial_hit expected[64], actual[100];
    const size_t total = reference_hits(fixture, request, expected);
    cgai_spatial_stats stats;
    TEST_CHECK(cgai_spatial_query(index, request, actual, &stats) == CGAI_STATUS_OK,
               cgai_last_error());
    const size_t count = total < request->limit ? total : request->limit;
    /* Step 2: Require exact distances, stable identities, and no duplicate distance evaluations. */
    TEST_CHECK(stats.count == count && stats.comparisons <= total, "spatial count/work mismatch");
    for (size_t i = 0; i < count; ++i) {
        TEST_CHECK(actual[i].row == expected[i].row, "spatial row mismatch");
        TEST_CHECK(actual[i].squared_distance == expected[i].squared_distance,
                   "spatial distance mismatch");
    }
}

/**
 * @brief Exercise arbitrary queries, category masks, exclusions, and copied-input lifetime.
 * All mutable query output lives on the stack; the native index never borrows the fixture.
 * @param index Borrowed immutable spatial index.
 * @param fixture Borrowed reference coordinates and categories.
 */
static void check_selection_policies(cgai_spatial_index *index, const spatial_fixture *fixture) {
    const size_t limits[] = {1U, 5U, 100U};
    for (size_t i = 0; i < 65U; ++i) {
        const double vector[] = {(double)i * 5.1, 0.05, 0.25};
        for (uint32_t category = 0; category < 5U; ++category) {
            for (size_t k = 0; k < 3U; ++k) {
                const cgai_spatial_request request = {vector, limits[k],
                                                      category == 4U ? UINT32_MAX : category,
                                                      i == 64U ? SIZE_MAX : i};
                check_query(index, fixture, &request);
            }
        }
    }
}

/** Exercise arbitrary queries after overwriting the input passed to construction. */
static void test_exact_queries(void) {
    spatial_fixture fixture;
    fill_fixture(&fixture);
    spatial_fixture copied = fixture;
    cgai_spatial_index *index = cgai_spatial_create(copied.vectors, copied.categories, 64U, 3U);
    TEST_CHECK(index != NULL, cgai_last_error());
    memset(&copied, 0, sizeof(copied));
    check_selection_policies(index, &fixture);
    cgai_spatial_destroy(index);
}

/**
 * @brief Require earliest-row ties, measurable pruning, and defined invalid-query outputs.
 */
static void test_ties_and_failures(void) {
    /* Step 1: Query the duplicated origin with and without its first row. */
    spatial_fixture fixture;
    fill_fixture(&fixture);
    cgai_spatial_index *index = cgai_spatial_create(fixture.vectors, fixture.categories, 64U, 3U);
    TEST_CHECK(index != NULL, cgai_last_error());
    cgai_spatial_request request = {fixture.vectors, 1U, UINT32_MAX, SIZE_MAX};
    cgai_spatial_hit hit = {99U, -1.0};
    cgai_spatial_stats stats;
    TEST_CHECK(cgai_spatial_query(index, &request, &hit, &stats) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(hit.row == 0U && stats.comparisons < 64U, "tie/pruning failed");
    request.exclude = 0U;
    TEST_CHECK(cgai_spatial_query(index, &request, &hit, &stats) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(hit.row == 1U, "self exclusion failed");
    /* Step 2: Invalid limits must clear stats without touching prior hit data. */
    request.limit = 101U;
    TEST_CHECK(cgai_spatial_query(index, &request, &hit, &stats) == CGAI_STATUS_ERROR,
               "invalid limit accepted");
    TEST_CHECK(stats.count == 0U && stats.comparisons == 0U && hit.row == 1U,
               "failure mutated output");
    cgai_spatial_destroy(index);
}

/**
 * @brief Reject invalid coordinates, categories, and allocation limits before unsafe reads.
 */
static void test_invalid_construction(void) {
    /* Step 1: Check tiny arrays against impossible shapes without reading beyond them. */
    const double value = 0.0;
    const uint32_t category = 0U;
    TEST_CHECK(!cgai_spatial_create(&value, &category, 0U, 1U), "zero rows accepted");
    TEST_CHECK(!cgai_spatial_create(&value, &category, 4097U, 1U), "excessive rows accepted");
    TEST_CHECK(!cgai_spatial_create(&value, &category, 4096U, 4096U), "excessive cells accepted");
    TEST_CHECK(!cgai_spatial_create(NULL, &category, 1U, 1U), "NULL vectors accepted");
    /* Step 2: Validate scalar values after copying, before category shifts or distance arithmetic.
     */
    const double invalid = NAN;
    const uint32_t invalid_category = 64U;
    TEST_CHECK(!cgai_spatial_create(&invalid, &category, 1U, 1U), "NaN accepted");
    TEST_CHECK(!cgai_spatial_create(&value, &invalid_category, 1U, 1U),
               "category overflow accepted");
    cgai_spatial_destroy(NULL);
}

/**
 * @brief Exercise the shared-library ownership and JSON boundary independently of Node.
 */
static void test_spatial_abi(void) {
    /* Step 1: Create a one-row owner and query through fixed-width ABI declarations. */
    const double vector[] = {1.0, 2.0};
    const uint32_t category = 2U;
    cgai_abi_spatial_index *index = NULL;
    TEST_CHECK(cgai_abi_spatial_create(vector, &category, 1U, 2U, &index) == CGAI_ABI_OK,
               "ABI create failed");
    cgai_abi_buffer output = {0};
    TEST_CHECK(cgai_abi_spatial_query(index, vector, 5U, UINT32_MAX, UINT32_MAX, &output) ==
                   CGAI_ABI_OK,
               "ABI query failed");
    TEST_CHECK(strstr((const char *)output.data, "\"index\":0,\"squaredDistance\":0") != NULL,
               "ABI JSON mismatch");
    cgai_abi_buffer_free(&output);
    /* Step 2: Invalid queries return empty descriptors and leave owner lifetime explicit. */
    TEST_CHECK(cgai_abi_spatial_query(index, vector, 0U, UINT32_MAX, UINT32_MAX, &output) ==
                   CGAI_ABI_ERROR,
               "ABI accepted bad limit");
    TEST_CHECK(output.data == NULL && output.size == 0U, "ABI failure output not empty");
    cgai_abi_spatial_destroy(index);
    cgai_abi_spatial_destroy(NULL);
}

/** @brief Exercise the compiled static knowledge table and its native exact-query index. */
static void test_compiled_metadata(void) {
    const size_t count = cgai_static_knowledge_centroid_count();
    const size_t category_count = cgai_static_knowledge_category_count();
    TEST_CHECK(count > 0U && count <= 4096U, "compiled knowledge centroid count mismatch");
    TEST_CHECK(category_count > 0U && category_count <= 64U,
               "compiled knowledge category count mismatch");
    const char *release_hash = cgai_static_knowledge_release_sha256();
    TEST_CHECK(release_hash != NULL && strlen(release_hash) == 64U,
               "compiled knowledge release hash mismatch");
    const cgai_static_knowledge_centroid *first = cgai_static_knowledge_centroid_at(0U);
    TEST_CHECK(first != NULL && first->observations > 0U && first->category < category_count &&
                   cgai_static_knowledge_category_name(first->category) != NULL &&
                   strchr(cgai_static_knowledge_category_name(first->category), '/') == NULL &&
                   cgai_static_knowledge_category_description(first->category) != NULL &&
                   strlen(cgai_static_knowledge_category_description(first->category)) > 20U &&
                   first->description != NULL && strlen(first->description) > 20U,
               "compiled knowledge first row mismatch");
    TEST_CHECK(cgai_static_knowledge_centroid_at(count) == NULL &&
                   cgai_static_knowledge_category_name(category_count) == NULL,
               "compiled knowledge bounds mismatch");
}

/** Check sorted rows, readable descriptions, and encyclopedia coverage. */
static void test_compiled_rows(void) {
    const size_t count = cgai_static_knowledge_centroid_count();
    const size_t category_count = cgai_static_knowledge_category_count();
    int found_encyclopedia = 0;
    for (size_t row = 1U; row < count; ++row) {
        const cgai_static_knowledge_centroid *previous =
            cgai_static_knowledge_centroid_at(row - 1U);
        const cgai_static_knowledge_centroid *current = cgai_static_knowledge_centroid_at(row);
        TEST_CHECK(previous != NULL && current != NULL && strcmp(previous->id, current->id) < 0,
                   "compiled knowledge rows are not sorted");
        TEST_CHECK(current->category < category_count &&
                       cgai_static_knowledge_category_name(current->category) != NULL &&
                       strchr(cgai_static_knowledge_category_name(current->category), '/') ==
                           NULL &&
                       cgai_static_knowledge_category_description(current->category) != NULL &&
                       current->description != NULL && strlen(current->description) > 20U,
                   "compiled knowledge category reference is invalid");
        if (strncmp(current->id, "encyclopedia:", 13U) == 0) {
            found_encyclopedia = 1;
            TEST_CHECK(strstr(current->description, "article-title cues include") != NULL,
                       "encyclopedia row is missing human-readable topic labels");
            TEST_CHECK(strstr(cgai_static_knowledge_category_description(current->category),
                              "article-title cues include") != NULL,
                       "encyclopedia category is missing human-readable topic labels");
        }
    }
    TEST_CHECK(found_encyclopedia, "compiled encyclopedia centroids are missing");
}

/** Verify exact lookup in the low-level spatial index exposed for native vector callers. */
static void test_compiled_vector_query(void) {
    const cgai_static_knowledge_centroid *first = cgai_static_knowledge_centroid_at(0U);
    double vector[CGAI_STATIC_KNOWLEDGE_DIMENSIONS];
    for (size_t dimension = 0U; dimension < CGAI_STATIC_KNOWLEDGE_DIMENSIONS; ++dimension)
        vector[dimension] = (double)first->vector[dimension];
    const cgai_spatial_request request = {vector, 5U, CGAI_SPATIAL_ALL_CATEGORIES,
                                          CGAI_SPATIAL_NO_EXCLUSION};
    cgai_spatial_hit hits[5];
    cgai_spatial_stats stats;
    cgai_spatial_index *index = cgai_static_knowledge_create_index();
    TEST_CHECK(index != NULL, cgai_last_error());
    TEST_CHECK(cgai_spatial_query(index, &request, hits, &stats) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(stats.count == 5U && hits[0].row == 0U && hits[0].squared_distance == 0.0,
               "compiled knowledge query did not return its exact source row");
    cgai_spatial_destroy(index);
}

/**
 * @brief Run native spatial correctness, lifetime, invalid-input, and ABI regressions.
 * @return Zero after all assertions pass; assertions terminate on failure.
 */
int main(void) {
    /* Step 1: Run independent scenarios before returning success to CTest. */
    test_exact_queries();
    test_ties_and_failures();
    test_invalid_construction();
    test_spatial_abi();
    test_compiled_metadata();
    test_compiled_rows();
    test_compiled_vector_query();
    return 0;
}
