/* Curated static knowledge baseline; maintain these C sources directly. */
#include "knowledge_catalog.h"

const cgai_knowledge_provider cgai_knowledge_catalog_modules[] = {
    cgai_knowledge_build,
    cgai_knowledge_database,
    cgai_knowledge_native_core,
};
const size_t cgai_knowledge_catalog_category_count = 3U;

/* Category source: knowledge_catalog/00-build-and-configuration.c */
/* Category source: knowledge_catalog/01-database-and-persistence.c */
/* Category source: knowledge_catalog/02-native-c-core.c */

const cgai_knowledge_span cgai_knowledge_catalog_spans[] = {
    {0U, 3U, 0U, 0U},
    {3U, 3U, 1U, 0U},
    {6U, 3U, 2U, 0U},
};
const size_t cgai_knowledge_catalog_span_count = 3U;
const size_t cgai_knowledge_catalog_row_count = 9U;
const size_t cgai_knowledge_catalog_cluster_count = 9U;
const size_t cgai_knowledge_catalog_dimensions = 32U;
const char cgai_knowledge_catalog_release_sha256[] = "";
