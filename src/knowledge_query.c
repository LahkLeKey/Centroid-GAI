/** @file knowledge_query.c @brief Reusable native queries over compiled centroid knowledge. */
#include "centroid_gai_knowledge.h"
#include "internal/error.h"
#include "internal/knowledge_module.h"
#include <stdlib.h>
#include <string.h>

/** Owns the spatial index whose rows refer exclusively to compiled knowledge. */
struct cgai_static_knowledge_index {
    cgai_spatial_index *spatial; /**< Owned immutable spatial search tree. */
    cgai_static_knowledge_cluster *clusters; /**< Owned canonical metadata, indexed by cluster ID. */
};

cgai_static_knowledge_index *cgai_static_knowledge_open(void) {
    cgai_static_knowledge_index *index = calloc(1U, sizeof(*index));
    if (!index) {
        (void)cgai_fail("could not allocate compiled knowledge context");
        return NULL;
    }
    index->clusters = cgai_knowledge_clusters_create();
    if (index->clusters)
        index->spatial = cgai_knowledge_cluster_index(index->clusters);
    if (!index->spatial) {
        cgai_static_knowledge_close(index);
        return NULL;
    }
    return index;
}

void cgai_static_knowledge_close(cgai_static_knowledge_index *index) {
    if (index) {
        cgai_spatial_destroy(index->spatial);
        free(index->clusters);
        free(index);
    }
}

size_t cgai_static_knowledge_find(const char *id) {
    if (!id)
        return SIZE_MAX;
    size_t begin = 0U, end = cgai_static_knowledge_centroid_count();
    while (begin < end) {
        const size_t middle = begin + (end - begin) / 2U;
        const int order = strcmp(cgai_static_knowledge_centroid_at(middle)->id, id);
        if (order == 0)
            return middle;
        if (order < 0)
            begin = middle + 1U;
        else
            end = middle;
    }
    return SIZE_MAX;
}

cgai_status cgai_static_knowledge_neighbors(const cgai_static_knowledge_index *index,
                                            const char *id, size_t limit, uint32_t category,
                                            cgai_spatial_hit *hits, cgai_spatial_stats *stats) {
    if (stats)
        memset(stats, 0, sizeof(*stats));
    const size_t row = cgai_static_knowledge_find(id);
    if (!index || !hits || !stats || row == SIZE_MAX || limit == 0U || limit > 100U ||
        (category != CGAI_SPATIAL_ALL_CATEGORIES &&
         category >= cgai_static_knowledge_category_count()))
        return cgai_fail("invalid compiled knowledge query: check ID, category, and limit (1-100)");
    const cgai_static_knowledge_centroid *point = cgai_static_knowledge_centroid_at(row);
    double vector[CGAI_STATIC_KNOWLEDGE_DIMENSIONS];
    for (size_t d = 0U; d < CGAI_STATIC_KNOWLEDGE_DIMENSIONS; ++d)
        vector[d] = (double)point->vector[d];
    const cgai_spatial_request request = {vector, limit, category, point->cluster};
    const cgai_status status = cgai_spatial_query(index->spatial, &request, hits, stats);
    if (status == CGAI_STATUS_OK)
        for (size_t i = 0U; i < stats->count; ++i)
            hits[i].row = index->clusters[hits[i].row].row;
    return status;
}
