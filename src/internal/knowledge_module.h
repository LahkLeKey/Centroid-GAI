/** @file knowledge_module.h @brief Private contract implemented by each compiled category. */
#ifndef CGAI_KNOWLEDGE_MODULE_H
#define CGAI_KNOWLEDGE_MODULE_H
#include "centroid_gai_knowledge.h"

extern const size_t cgai_knowledge_catalog_cluster_count; /**< Distinct exact vectors. */
/** Build aggregate metadata for all canonical clusters in one pass.
 * @return Owned array of cluster_count records, or NULL with a diagnostic; release with free().
 */
cgai_static_knowledge_cluster *cgai_knowledge_clusters_create(void);
/** Construct a standard spatial index from validated canonical metadata.
 * @param clusters Borrowed complete aggregate metadata from cgai_knowledge_clusters_create().
 * @return Owned index, or NULL with a diagnostic.
 */
cgai_spatial_index *cgai_knowledge_cluster_index(const cgai_static_knowledge_cluster *clusters);

/** Private module storage; callers use the public category and lookup operations. */
typedef struct cgai_knowledge_module {
    cgai_static_knowledge_category category;    /**< Immutable category identity and row count. */
    const cgai_static_knowledge_centroid *rows; /**< Private rows sorted by stable ID. */
} cgai_knowledge_module;

/** Category implementation entry point returning process-lifetime immutable storage. */
typedef const cgai_knowledge_module *(*cgai_knowledge_provider)(void);

/** Consecutive sorted global rows mapped to part of a category module. */
typedef struct cgai_knowledge_span {
    size_t first;    /**< First global row. */
    size_t count;    /**< Number of consecutive rows. */
    size_t category; /**< Category registry index. */
    size_t local;    /**< First category-local row. */
} cgai_knowledge_span;

extern const cgai_knowledge_provider
    cgai_knowledge_catalog_modules[]; /**< Category implementations. */
extern const cgai_knowledge_span cgai_knowledge_catalog_spans[]; /**< Sorted global row spans. */
extern const size_t cgai_knowledge_catalog_span_count; /**< Number of spans. */
extern const size_t cgai_knowledge_catalog_category_count; /**< Number of modules. */
extern const size_t cgai_knowledge_catalog_row_count; /**< Total compiled rows. */
extern const size_t cgai_knowledge_catalog_dimensions; /**< Dimensions per vector. */
extern const char cgai_knowledge_catalog_release_sha256[]; /**< Source release fingerprint. */
#endif
