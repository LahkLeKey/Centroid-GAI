/** @file cli_knowledge.c @brief Native catalog, exact lookup, and neighbor commands. */
#include "centroid_gai_knowledge.h"
#include "internal/cli_commands.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/** Print a borrowed centroid's stable identity and learned-context description.
 * @param point Valid immutable compiled centroid.
 */
static void print_centroid(const cgai_static_knowledge_centroid *point) {
    printf("%s\t%s\t%" PRIu64 "\t%s\n", point->id,
           cgai_static_knowledge_category_key(point->category), point->observations,
           point->description);
}

/** List every registered native category implementation.
 * @return Zero after printing the catalog.
 */
static int list_categories(void) {
    printf("%zu centroids in %zu categories; release %s\n", cgai_static_knowledge_centroid_count(),
           cgai_static_knowledge_category_count(), cgai_static_knowledge_release_sha256());
    for (size_t i = 0U; i < cgai_static_knowledge_category_count(); ++i) {
        const cgai_static_knowledge_category *category = cgai_static_knowledge_category_at(i);
        printf("%s\t%zu\t%s\n", category->key, category->count, category->name);
    }
    return 0;
}

/** List all rows supplied by one category module.
 * @param key Borrowed stable category key.
 * @return Zero on success, two for an unknown category.
 */
static int list_centroids(const char *key) {
    const size_t category = cgai_static_knowledge_category_find(key);
    const cgai_static_knowledge_category *metadata = cgai_static_knowledge_category_at(category);
    if (!metadata) {
        fprintf(stderr, "Unknown knowledge category: %s\n", key);
        return 2;
    }
    for (size_t row = 0U; row < metadata->count; ++row)
        print_centroid(cgai_static_knowledge_category_centroid_at(category, row));
    return 0;
}

/** Resolve one stable ID with the native binary lookup.
 * @param id Borrowed stable centroid ID.
 * @return Zero on success, two for an unknown ID.
 */
static int find_centroid(const char *id) {
    const cgai_static_knowledge_centroid *point =
        cgai_static_knowledge_centroid_at(cgai_static_knowledge_find(id));
    if (!point) {
        fprintf(stderr, "Unknown knowledge centroid: %s\n", id);
        return 2;
    }
    print_centroid(point);
    return 0;
}

/** Execute a native neighbor lookup and release the search context on every path.
 * @param id Borrowed source stable ID.
 * @param limit Result capacity from 1 through 100.
 * @param category Category ID or the all-category sentinel.
 * @return Zero on success, one on native failure.
 */
static int query_neighbors(const char *id, size_t limit, uint32_t category) {
    cgai_static_knowledge_index *index = cgai_static_knowledge_open();
    cgai_spatial_hit hits[100];
    cgai_spatial_stats stats;
    const cgai_status status =
        cgai_static_knowledge_neighbors(index, id, limit, category, hits, &stats);
    cgai_static_knowledge_close(index);
    if (status != CGAI_STATUS_OK) {
        fprintf(stderr, "%s\n", cgai_last_error());
        return 1;
    }
    for (size_t i = 0U; i < stats.count; ++i) {
        printf("%.17g\t", hits[i].squared_distance);
        print_centroid(cgai_static_knowledge_centroid_at(hits[i].row));
    }
    return 0;
}

/** Parse bounded nearest-neighbor options before constructing any index.
 * @param argc Validated argument count from four through six.
 * @param argv Borrowed arguments with source ID at index three.
 * @return Command status; two denotes invalid options.
 */
static int nearest_command(int argc, char **argv) {
    size_t limit = 5U;
    if (argc >= 5 && (!cgai_cli_parse_size(argv[4], &limit) || limit > 100U)) {
        fprintf(stderr, "Knowledge neighbor limit must be 1-100\n");
        return 2;
    }
    const size_t category = argc == 6 ? cgai_static_knowledge_category_find(argv[5]) : UINT32_MAX;
    if (argc == 6 && category == SIZE_MAX) {
        fprintf(stderr, "Unknown knowledge category: %s\n", argv[5]);
        return 2;
    }
    return query_neighbors(argv[3], limit, (uint32_t)category);
}

int cgai_cli_knowledge(int argc, char **argv) {
    if (argc == 2 || (argc == 3 && strcmp(argv[2], "categories") == 0))
        return list_categories();
    if (argc == 4 && strcmp(argv[2], "list") == 0)
        return list_centroids(argv[3]);
    if (argc == 4 && strcmp(argv[2], "find") == 0)
        return find_centroid(argv[3]);
    if (argc >= 4 && argc <= 6 && strcmp(argv[2], "nearest") == 0)
        return nearest_command(argc, argv);
    fprintf(stderr, "Usage: cgai knowledge [categories | list <category-key> | find <id> | "
                    "nearest <id> [limit] [category-key]]\n");
    return 2;
}
