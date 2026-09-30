/** @file spatial_index.c @brief Copy, categorize, partition, and own immutable spatial indexes. */
#include "internal/error.h"
#include "internal/spatial.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * @brief Validate coordinates without overflowing subsequent squared distances.
 * @param values Borrowed count-element array, or NULL.
 * @param count Number of values to inspect.
 * @return Nonzero for finite, bounded coordinates; zero otherwise.
 */
int cgai_spatial_finite(const double *values, size_t count) {
    /* Step 1: Reject a missing array before reading any elements. */
    if (values == NULL)
        return 0;
    /* Step 2: Bound coordinates so all allowed dimensional sums fit in a double. */
    for (size_t i = 0; i < count; ++i)
        if (!isfinite(values[i]) || fabs(values[i]) > 1e100)
            return 0;
    return 1;
}

/*
 * @brief Release all owned allocations, including partially constructed indexes.
 * @param index Owned index or NULL; no concurrent query may retain it.
 */
void cgai_spatial_destroy(cgai_spatial_index *index) {
    /* Step 1: Accept NULL and free each independent allocation. */
    if (index == NULL)
        return;
    free(index->vectors);
    free(index->minimum);
    free(index->maximum);
    free(index->categories);
    free(index->order);
    free(index->nodes);
    /* Step 2: Release the descriptor after its children. */
    free(index);
}

/**
 * @brief Allocate and initialize all arrays after public size limits have been checked.
 * @param index Zero-initialized owned descriptor with count and dimensions populated.
 * @return Nonzero if every allocation succeeded; partial storage remains owned by index.
 */
static int allocate_arrays(cgai_spatial_index *index) {
    /* Step 1: The checked product is at most 1048576, making byte multiplication safe. */
    const size_t cells = index->count * index->dimensions;
    index->vectors = calloc(cells, sizeof(double));
    index->minimum = calloc(cells, sizeof(double));
    index->maximum = calloc(cells, sizeof(double));
    index->categories = calloc(index->count, sizeof(uint64_t));
    index->order = calloc(index->count, sizeof(size_t));
    index->nodes = calloc(index->count, sizeof(cgai_spatial_node));
    /* Step 2: Let the caller use one cleanup path for any failed allocation. */
    return index->vectors && index->minimum && index->maximum && index->categories &&
           index->order && index->nodes;
}

/**
 * @brief Compute a node's category union and exact component bounds.
 * @param index Mutable index under construction.
 * @param id Initialized node index whose row range is populated.
 */
static void measure_bounds(cgai_spatial_index *index, size_t id) {
    /* Step 1: Seed each bound from the first row rather than an artificial sentinel. */
    cgai_spatial_node *node = &index->nodes[id];
    double *minimum = index->minimum + id * index->dimensions;
    double *maximum = index->maximum + id * index->dimensions;
    memcpy(minimum, index->vectors + index->order[node->begin] * index->dimensions,
           index->dimensions * sizeof(double));
    memcpy(maximum, minimum, index->dimensions * sizeof(double));
    /* Step 2: Expand the bounds and membership mask over every row in this subtree. */
    for (size_t i = node->begin; i < node->begin + node->count; ++i) {
        const size_t row = index->order[i];
        node->categories |= index->categories[row];
        for (size_t d = 0; d < index->dimensions; ++d) {
            const double value = index->vectors[row * index->dimensions + d];
            minimum[d] = fmin(minimum[d], value);
            maximum[d] = fmax(maximum[d], value);
        }
    }
}

/**
 * @brief Choose the widest component, retaining the first dimension for equal widths.
 * @param index Borrowed index with completed bounds for id.
 * @param id Valid node index.
 * @return Dimension index used to partition this node.
 */
static size_t widest_axis(const cgai_spatial_index *index, size_t id) {
    /* Step 1: Compare widths against the earliest widest dimension. */
    const double *minimum = index->minimum + id * index->dimensions;
    const double *maximum = index->maximum + id * index->dimensions;
    size_t axis = 0;
    for (size_t d = 1; d < index->dimensions; ++d)
        if (maximum[d] - minimum[d] > maximum[axis] - minimum[axis])
            axis = d;
    return axis;
}

/**
 * @brief Compare two rows by one coordinate and then original row ID.
 * @param index Borrowed coordinate storage.
 * @param left First original row ID.
 * @param right Second original row ID.
 * @param axis Valid component index.
 * @return Nonzero if left belongs strictly before right.
 */
static int row_before(const cgai_spatial_index *index, size_t left, size_t right, size_t axis) {
    /* Step 1: Break coordinate ties deterministically without global comparator state. */
    const double a = index->vectors[left * index->dimensions + axis];
    const double b = index->vectors[right * index->dimensions + axis];
    return a < b || (a == b && left < right);
}

/**
 * @brief Sort a bounded row range using stable insertion without global state or scratch
 * allocation.
 *
 * Construction is capped at 4096 rows. This quadratic construction step is separate from immutable
 * query traversal and avoids platform-specific qsort context conventions.
 * @param index Mutable index under construction.
 * @param node Borrowed node specifying the row range.
 * @param axis Coordinate by which to order the range.
 */
static void sort_rows(cgai_spatial_index *index, const cgai_spatial_node *node, size_t axis) {
    /* Step 1: Insert each row into the already sorted prefix. */
    for (size_t i = node->begin + 1U; i < node->begin + node->count; ++i) {
        const size_t row = index->order[i];
        size_t position = i;
        while (position > node->begin &&
               row_before(index, row, index->order[position - 1U], axis)) {
            index->order[position] = index->order[position - 1U];
            --position;
        }
        /* Step 2: Fill the gap with the preserved original row ID. */
        index->order[position] = row;
    }
}

/**
 * @brief Recursively split a median-balanced tree until leaves contain at most eight rows.
 * @param index Mutable index with fixed arrays large enough for count nodes.
 * @param begin First permutation position in this subtree.
 * @param count Number of rows in the subtree, at least one.
 * @return Root node ID of this subtree; recursion is bounded by the row limit.
 */
static size_t build_node(cgai_spatial_index *index, size_t begin, size_t count) {
    /* Step 1: Claim a node from fixed storage and measure its original row range. */
    const size_t id = index->used++;
    cgai_spatial_node *node = &index->nodes[id];
    node->begin = begin;
    node->count = count;
    node->left = SIZE_MAX;
    node->right = SIZE_MAX;
    measure_bounds(index, id);
    /* Step 2: Median splits need fewer than count nodes with this eight-row leaf threshold. */
    if (count > 8U) {
        sort_rows(index, node, widest_axis(index, id));
        node->left = build_node(index, begin, count / 2U);
        node->right = build_node(index, begin + count / 2U, count - count / 2U);
    }
    return id;
}

/**
 * @brief Copy and validate caller data before any category shift or geometry calculation.
 * @param index Allocated mutable index with count and dimensions populated.
 * @param vectors Borrowed row-major coordinate array of count*dimensions elements.
 * @param categories Borrowed count-element category array.
 * @return Nonzero for valid data; copied allocations remain owned on failure.
 */
static int copy_inputs(cgai_spatial_index *index, const double *vectors,
                       const uint64_t *categories) {
    /* Step 1: Copy arrays so later caller edits cannot alter bounds or membership. */
    memcpy(index->vectors, vectors, index->count * index->dimensions * sizeof(double));
    memcpy(index->categories, categories, index->count * sizeof(uint64_t));
    /* Step 2: Validate the owned values before building the tree. */
    for (size_t i = 0; i < index->count; ++i) {
        if (index->categories[i] == 0U)
            return 0;
        index->order[i] = i;
    }
    return cgai_spatial_finite(index->vectors, index->count * index->dimensions);
}

/*
 * @brief Allocate an immutable spatial index and copy the caller's compatible vectors.
 * @param vectors Borrowed row-major count*dimensions doubles, bounded by magnitude 1e100.
 * @param categories Borrowed count category IDs in 0..63.
 * @param count Row count in 1..4096, with count*dimensions at most 1048576.
 * @param dimensions Component count in 1..4096.
 * @return Owned index or NULL with a diagnostic; partial allocations are released on failure.
 */
cgai_spatial_index *cgai_spatial_create_masked(const double *vectors, const uint64_t *categories,
                                               size_t count, size_t dimensions) {
    /* Step 1: Bound every allocation product before multiplication or input reads. */
    if (!vectors || !categories || !count || count > 4096U || !dimensions || dimensions > 4096U ||
        count > 1048576U / dimensions) {
        (void)cgai_fail("invalid spatial inputs or size limit");
        return NULL;
    }
    /* Step 2: Allocate a zeroed owner so every failure can use the ordinary destructor. */
    cgai_spatial_index *index = calloc(1U, sizeof(*index));
    if (!index) {
        (void)cgai_fail("could not allocate spatial index");
        return NULL;
    }
    index->count = count;
    index->dimensions = dimensions;
    if (!allocate_arrays(index) || !copy_inputs(index, vectors, categories)) {
        cgai_spatial_destroy(index);
        (void)cgai_fail("could not allocate spatial arrays or invalid coordinate/category");
        return NULL;
    }
    /* Step 3: Publish only after the complete tree has been built. */
    (void)build_node(index, 0U, count);
    return index;
}

cgai_spatial_index *cgai_spatial_create(const double *vectors, const uint32_t *categories,
                                        size_t count, size_t dimensions) {
    if (!vectors || !categories || !count || count > 4096U || !dimensions || dimensions > 4096U ||
        count > 1048576U / dimensions) {
        (void)cgai_fail("invalid spatial inputs or size limit");
        return NULL;
    }
    uint64_t masks[4096];
    for (size_t row = 0U; row < count; ++row) {
        if (categories[row] >= 64U) {
            (void)cgai_fail("invalid spatial category");
            return NULL;
        }
        masks[row] = UINT64_C(1) << categories[row];
    }
    return cgai_spatial_create_masked(vectors, masks, count, dimensions);
}
