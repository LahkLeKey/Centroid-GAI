/** @file knowledge_clusters.c @brief Canonical vectors, provenance aggregation, and shared spatial
 * indexing. */
#include "internal/error.h"
#include "internal/knowledge_module.h"
#include <stdlib.h>
#include <string.h>

size_t cgai_static_knowledge_cluster_count(void) { return cgai_knowledge_catalog_cluster_count; }

/** Aggregate one alias without losing its category or observation count.
 * @param cluster Writable aggregate initialized to zero.
 * @param point Borrowed compiled alias.
 * @param row Global alias row.
 * @return Nonzero on success, zero on invalid metadata or counter overflow.
 */
static int add_alias(cgai_static_knowledge_cluster *cluster,
                     const cgai_static_knowledge_centroid *point, size_t row) {
    if (point->category >= 64U || point->observations > UINT64_MAX - cluster->observations)
        return 0;
    if (cluster->aliases == 0U)
        cluster->row = row;
    cluster->aliases++;
    cluster->observations += point->observations;
    cluster->categories |= UINT64_C(1) << point->category;
    return 1;
}

cgai_status cgai_static_knowledge_cluster_get(size_t cluster,
                                              cgai_static_knowledge_cluster *output) {
    if (output)
        memset(output, 0, sizeof(*output));
    if (!output || cluster >= cgai_static_knowledge_cluster_count())
        return cgai_fail("invalid compiled knowledge cluster");
    for (size_t row = 0U; row < cgai_static_knowledge_centroid_count(); ++row) {
        const cgai_static_knowledge_centroid *point = cgai_static_knowledge_centroid_at(row);
        if (point->cluster == cluster && !add_alias(output, point, row))
            return cgai_fail("compiled cluster observation overflow");
    }
    return CGAI_STATUS_OK;
}

cgai_static_knowledge_cluster *cgai_knowledge_clusters_create(void) {
    const size_t count = cgai_static_knowledge_cluster_count();
    cgai_static_knowledge_cluster *clusters = calloc(count, sizeof(*clusters));
    if (!clusters) {
        (void)cgai_fail("could not allocate compiled cluster metadata");
        return NULL;
    }
    for (size_t row = 0U; row < cgai_static_knowledge_centroid_count(); ++row) {
        const cgai_static_knowledge_centroid *point = cgai_static_knowledge_centroid_at(row);
        if (point->cluster >= count || !add_alias(&clusters[point->cluster], point, row)) {
            free(clusters);
            (void)cgai_fail("invalid compiled cluster metadata");
            return NULL;
        }
    }
    return clusters;
}

/** Copy one vector per cluster and preserve category union masks.
 * @param clusters Borrowed complete canonical metadata.
 * @param vectors Writable cluster-count-times-dimensions array.
 * @param masks Writable cluster-count array.
 */
static void copy_clusters(const cgai_static_knowledge_cluster *clusters, double *vectors,
                          uint64_t *masks) {
    for (size_t i = 0U; i < cgai_static_knowledge_cluster_count(); ++i) {
        const cgai_static_knowledge_centroid *point =
            cgai_static_knowledge_centroid_at(clusters[i].row);
        masks[i] = clusters[i].categories;
        for (size_t d = 0U; d < CGAI_STATIC_KNOWLEDGE_DIMENSIONS; ++d)
            vectors[i * CGAI_STATIC_KNOWLEDGE_DIMENSIONS + d] = point->vector[d];
    }
}

cgai_spatial_index *cgai_knowledge_cluster_index(const cgai_static_knowledge_cluster *clusters) {
    const size_t count = cgai_static_knowledge_cluster_count();
    double *vectors = calloc(count * CGAI_STATIC_KNOWLEDGE_DIMENSIONS, sizeof(*vectors));
    uint64_t *masks = calloc(count, sizeof(*masks));
    cgai_spatial_index *index = NULL;
    if (vectors && masks) {
        copy_clusters(clusters, vectors, masks);
        index = cgai_spatial_create_masked(vectors, masks, count, CGAI_STATIC_KNOWLEDGE_DIMENSIONS);
    } else
        (void)cgai_fail("could not allocate compiled cluster inputs");
    free(vectors);
    free(masks);
    return index;
}

cgai_spatial_index *cgai_static_knowledge_create_cluster_index(void) {
    cgai_static_knowledge_cluster *clusters = cgai_knowledge_clusters_create();
    if (!clusters)
        return NULL;
    cgai_spatial_index *index = cgai_knowledge_cluster_index(clusters);
    free(clusters);
    return index;
}
