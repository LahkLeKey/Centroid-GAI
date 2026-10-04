/** @file static_knowledge.c @brief Accessors and native index construction for compiled knowledge.
 */
#include "knowledge/knowledge_contract.h"

#include "internal/error.h"
#include "internal/knowledge_module.h"
#include "internal/size_utils.h"

#include <stdlib.h>
#include <string.h>

size_t cgai_static_knowledge_centroid_count(void) { return cgai_knowledge_catalog_row_count; }

const cgai_static_knowledge_centroid *cgai_static_knowledge_centroid_at(size_t row) {
    size_t begin = 0U, end = cgai_knowledge_catalog_span_count;
    if (row >= cgai_knowledge_catalog_row_count)
        return NULL;
    while (begin < end) {
        const size_t middle = begin + (end - begin) / 2U;
        const cgai_knowledge_span *span = &cgai_knowledge_catalog_spans[middle];
        if (row < span->first)
            end = middle;
        else if (row - span->first >= span->count)
            begin = middle + 1U;
        else
            return cgai_static_knowledge_category_centroid_at(span->category,
                                                              span->local + row - span->first);
    }
    return NULL;
}

const cgai_static_knowledge_category *cgai_static_knowledge_category_at(size_t category) {
    return category < cgai_knowledge_catalog_category_count
               ? &cgai_knowledge_catalog_modules[category]()->category
               : NULL;
}

size_t cgai_static_knowledge_category_find(const char *key) {
    if (key)
        for (size_t i = 0U; i < cgai_knowledge_catalog_category_count; ++i)
            if (strcmp(cgai_static_knowledge_category_at(i)->key, key) == 0)
                return i;
    return SIZE_MAX;
}

const cgai_static_knowledge_centroid *cgai_static_knowledge_category_centroid_at(size_t category,
                                                                                 size_t row) {
    if (category >= cgai_knowledge_catalog_category_count)
        return NULL;
    const cgai_knowledge_module *module = cgai_knowledge_catalog_modules[category]();
    return row < module->category.count ? &module->rows[row] : NULL;
}

const cgai_static_knowledge_centroid *cgai_static_knowledge_category_centroid_find(size_t category,
                                                                                   const char *id) {
    const cgai_static_knowledge_category *metadata = cgai_static_knowledge_category_at(category);
    if (!metadata || !id)
        return NULL;
    size_t begin = 0U, end = metadata->count;
    while (begin < end) {
        const size_t middle = begin + (end - begin) / 2U;
        const cgai_static_knowledge_centroid *point =
            cgai_static_knowledge_category_centroid_at(category, middle);
        const int order = strcmp(point->id, id);
        if (order == 0)
            return point;
        if (order < 0)
            begin = middle + 1U;
        else
            end = middle;
    }
    return NULL;
}

size_t cgai_static_knowledge_category_count(void) { return cgai_knowledge_catalog_category_count; }

const char *cgai_static_knowledge_category_key(size_t category) {
    const cgai_static_knowledge_category *metadata = cgai_static_knowledge_category_at(category);
    return metadata ? metadata->key : NULL;
}

const char *cgai_static_knowledge_category_name(size_t category) {
    const cgai_static_knowledge_category *metadata = cgai_static_knowledge_category_at(category);
    return metadata ? metadata->name : NULL;
}

const char *cgai_static_knowledge_category_description(size_t category) {
    const cgai_static_knowledge_category *metadata = cgai_static_knowledge_category_at(category);
    return metadata ? metadata->description : NULL;
}

const char *cgai_static_knowledge_release_sha256(void) {
    return cgai_knowledge_catalog_release_sha256;
}

/** Compute checked input sizes.
 * @param vector_bytes Writable checked vector allocation size.
 * @param category_bytes Writable checked category allocation size.
 * @return Nonzero when all size calculations fit.
 */
static int cgai_static_knowledge_input_sizes(size_t *vector_bytes, size_t *category_bytes) {
    size_t cells = 0U;
    return cgai_size_mul(cgai_knowledge_catalog_row_count, cgai_knowledge_catalog_dimensions,
                         &cells) &&
           cgai_size_mul(cells, sizeof(double), vector_bytes) &&
           cgai_size_mul(cgai_knowledge_catalog_row_count, sizeof(uint32_t), category_bytes);
}

/** Copy compiled vectors and categories into owned spatial inputs.
 * @param vectors Writable count-times-dimensions double array.
 * @param categories Writable count-element category array.
 */
static void cgai_static_knowledge_copy_inputs(double *vectors, uint32_t *categories) {
    for (size_t row = 0; row < cgai_knowledge_catalog_row_count; ++row) {
        const cgai_static_knowledge_centroid *centroid = cgai_static_knowledge_centroid_at(row);
        categories[row] = centroid->category;
        for (size_t dimension = 0; dimension < cgai_knowledge_catalog_dimensions; ++dimension) {
            vectors[row * cgai_knowledge_catalog_dimensions + dimension] =
                (double)centroid->vector[dimension];
        }
    }
}

cgai_spatial_index *cgai_static_knowledge_create_index(void) {
    size_t vector_bytes = 0U;
    size_t category_bytes = 0U;
    if (!cgai_static_knowledge_input_sizes(&vector_bytes, &category_bytes) || vector_bytes == 0U ||
        category_bytes == 0U) {
        (void)cgai_fail("compiled static knowledge dimensions overflow");
        return NULL;
    }
    double *vectors = (double *)malloc(vector_bytes);
    uint32_t *categories = (uint32_t *)malloc(category_bytes);
    if (vectors == NULL || categories == NULL) {
        free(vectors);
        free(categories);
        (void)cgai_fail("could not allocate compiled static knowledge index inputs");
        return NULL;
    }
    cgai_static_knowledge_copy_inputs(vectors, categories);
    cgai_spatial_index *index = cgai_spatial_create(
        vectors, categories, cgai_knowledge_catalog_row_count, cgai_knowledge_catalog_dimensions);
    free(vectors);
    free(categories);
    return index;
}
