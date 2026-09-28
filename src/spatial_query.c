/** @file spatial_query.c @brief Exact read-only spatial traversal with stable ties and category
 * pruning. */
#include "internal/error.h"
#include "internal/spatial.h"
#include <math.h>
#include <string.h>

/** Borrowed per-query state; no shared storage is modified during traversal. */
typedef struct spatial_query {
    const cgai_spatial_index *index;     /**< Borrowed immutable index. */
    const cgai_spatial_request *request; /**< Borrowed query options. */
    cgai_spatial_hit *hits;              /**< Borrowed caller-owned bounded result array. */
    cgai_spatial_stats *stats;           /**< Borrowed caller-owned counters. */
} spatial_query;

/**
 * @brief Measure the squared distance to the closest point in a node's bounding box.
 * @param query Borrowed valid per-call workspace.
 * @param id Valid node ID.
 * @return Nonnegative lower bound on the distance to any point in the node.
 */
static double box_distance(const spatial_query *query, size_t id) {
    /* Step 1: Sum only the components outside the bounding intervals. */
    const cgai_spatial_index *index = query->index;
    double distance = 0.0;
    for (size_t d = 0; d < index->dimensions; ++d) {
        const double value = query->request->vector[d];
        const double delta = fmax(0.0, fmax(index->minimum[id * index->dimensions + d] - value,
                                            value - index->maximum[id * index->dimensions + d]));
        distance += delta * delta;
    }
    return distance;
}

/**
 * @brief Evaluate one full point distance and count the work in this query only.
 * @param query Mutable per-call workspace; borrowed index remains immutable.
 * @param row Original input row ID.
 * @return Squared Euclidean distance, accumulated in coordinate order.
 */
static double point_distance(spatial_query *query, size_t row) {
    /* Step 1: Count the evaluation and accumulate bounded finite squared components. */
    double distance = 0.0;
    ++query->stats->comparisons;
    for (size_t d = 0; d < query->index->dimensions; ++d) {
        const double delta =
            query->request->vector[d] - query->index->vectors[row * query->index->dimensions + d];
        distance += delta * delta;
    }
    return distance;
}

/**
 * @brief Order hits by exact distance and then original row ID.
 * @param left Candidate hit.
 * @param right Existing hit.
 * @return Nonzero if left sorts before right.
 */
static int hit_before(cgai_spatial_hit left, cgai_spatial_hit right) {
    /* Step 1: Preserve deterministic ties even when tree traversal changes row order. */
    return left.squared_distance < right.squared_distance ||
           (left.squared_distance == right.squared_distance && left.row < right.row);
}

/**
 * @brief Insert a candidate into the fixed-capacity sorted result prefix.
 * @param query Mutable per-call workspace with space for request->limit hits.
 * @param row Original row ID of the candidate.
 */
static void insert_hit(spatial_query *query, size_t row) {
    /* Step 1: Locate insertion without changing existing results. */
    const cgai_spatial_hit hit = {row, point_distance(query, row)};
    size_t position = 0;
    while (position < query->stats->count && !hit_before(hit, query->hits[position]))
        ++position;
    if (position == query->request->limit)
        return;
    /* Step 2: Grow only within capacity, shift the suffix, and publish the candidate. */
    if (query->stats->count < query->request->limit)
        ++query->stats->count;
    for (size_t i = query->stats->count - 1U; i > position; --i)
        query->hits[i] = query->hits[i - 1U];
    query->hits[position] = hit;
}

/**
 * @brief Inspect eligible leaf rows without treating excluded rows as distance evaluations.
 * @param query Mutable per-call workspace.
 * @param node Borrowed leaf descriptor.
 */
static void visit_leaf(spatial_query *query, const cgai_spatial_node *node) {
    /* Step 1: Apply self exclusion and optional category equality before vector arithmetic. */
    for (size_t i = node->begin; i < node->begin + node->count; ++i) {
        const size_t row = query->index->order[i];
        if (row != query->request->exclude &&
            (query->request->category == CGAI_SPATIAL_ALL_CATEGORIES ||
             (query->index->categories[row] & (UINT64_C(1) << query->request->category))))
            insert_hit(query, row);
    }
}

/**
 * @brief Decide whether category membership and distance bounds permit useful candidates.
 * @param query Mutable per-call workspace; node visits are counted after category filtering.
 * @param id Valid node ID.
 * @return Nonzero if the node may improve the current result set.
 */
static int should_visit(spatial_query *query, size_t id) {
    /* Step 1: Reject whole categories without evaluating any vectors. */
    const uint32_t category = query->request->category;
    if (category != CGAI_SPATIAL_ALL_CATEGORIES &&
        !(query->index->nodes[id].categories & (UINT64_C(1) << category)))
        return 0;
    ++query->stats->visited_nodes;
    /* Step 2: Keep ties and a conservative rounding margin at the box boundary. */
    if (query->stats->count < query->request->limit)
        return 1;
    const double worst = query->hits[query->stats->count - 1U].squared_distance;
    return box_distance(query, id) <= worst + 1e-12 * fmax(1.0, worst);
}

/**
 * @brief Traverse the nearer child first and prune only provably unhelpful subtrees.
 * @param query Mutable query workspace owning no heap storage.
 * @param id Valid root node for this recursive visit.
 */
static void visit_node(spatial_query *query, size_t id) {
    /* Step 1: Prune against current results, or consume a small leaf. */
    if (!should_visit(query, id))
        return;
    const cgai_spatial_node *node = &query->index->nodes[id];
    if (node->left == SIZE_MAX) {
        visit_leaf(query, node);
        return;
    }
    /* Step 2: Re-evaluate the second child's bound after visiting the first. */
    const int left_first = box_distance(query, node->left) <= box_distance(query, node->right);
    visit_node(query, left_first ? node->left : node->right);
    visit_node(query, left_first ? node->right : node->left);
}

/**
 * @brief Validate a request before changing hits or traversing index memory.
 * @param index Borrowed live index, or NULL.
 * @param request Borrowed request, or NULL.
 * @param hits Caller output array, or NULL.
 * @return Nonzero if pointers, options, and all coordinates are valid.
 */
static int valid_request(const cgai_spatial_index *index, const cgai_spatial_request *request,
                         const cgai_spatial_hit *hits) {
    /* Step 1: Reject malformed scalar options before dereferencing the vector. */
    if (!index || !request || !hits || !request->limit || request->limit > 100U ||
        (request->category >= 64U && request->category != CGAI_SPATIAL_ALL_CATEGORIES) ||
        (request->exclude >= index->count && request->exclude != CGAI_SPATIAL_NO_EXCLUSION))
        return 0;
    /* Step 2: Validate finite bounded coordinates for this index's dimensions. */
    return cgai_spatial_finite(request->vector, index->dimensions);
}

/*
 * @brief Find exact neighbors without allocating or mutating the index.
 * @param index Borrowed live immutable index.
 * @param request Borrowed options and query coordinates.
 * @param hits Writable storage for request->limit hits, unchanged on argument failure.
 * @param stats Required writable counters, zeroed even on argument failure.
 * @return CGAI_STATUS_OK on success, otherwise CGAI_STATUS_ERROR with cgai_last_error().
 */
cgai_status cgai_spatial_query(const cgai_spatial_index *index, const cgai_spatial_request *request,
                               cgai_spatial_hit *hits, cgai_spatial_stats *stats) {
    /* Step 1: Clear the output count before validating all other arguments. */
    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (!stats || !valid_request(index, request, hits))
        return cgai_fail("invalid spatial query");
    /* Step 2: Keep all mutable state on this caller's stack and output arrays. */
    spatial_query query = {index, request, hits, stats};
    visit_node(&query, 0U);
    return CGAI_STATUS_OK;
}
