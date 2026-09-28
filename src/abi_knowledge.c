/** @file abi_knowledge.c @brief Owned ABI queries and bounded inspection of compiled knowledge. */
#include "centroid_gai_abi.h"
#include "centroid_gai_knowledge.h"
#include "internal/error.h"
#include "internal/json_writer.h"
#include <inttypes.h>
#include <stdlib.h>

cgai_abi_status cgai_abi_knowledge_open(cgai_abi_knowledge_index **output) {
    if (!output) {
        (void)cgai_fail("knowledge output is required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    *output = (cgai_abi_knowledge_index *)cgai_static_knowledge_open();
    return *output ? CGAI_ABI_OK : CGAI_ABI_ERROR;
}

void cgai_abi_knowledge_close(cgai_abi_knowledge_index *index) {
    cgai_static_knowledge_close((cgai_static_knowledge_index *)index);
}

/** Transfer a finished writer to an empty ABI buffer, freeing failed output.
 * @param writer Owned writer consumed on all paths.
 * @param output Empty ABI descriptor receiving ownership.
 * @return OK on success, ERROR after cleanup on failure.
 */
static cgai_abi_status finish(cgai_json_writer writer, cgai_abi_buffer *output) {
    if (writer.failed) {
        free(writer.data);
        (void)cgai_fail("could not serialize compiled knowledge");
        return CGAI_ABI_ERROR;
    }
    output->data = (uint8_t *)writer.data;
    output->size = writer.size;
    return CGAI_ABI_OK;
}

/** Append a category's stable identity and readable metadata.
 * @param writer Mutable JSON writer.
 * @param category Valid compiled category index.
 */
static void category_json(cgai_json_writer *writer, size_t category) {
    cgai_json_append(writer, "{\"index\":%zu,\"key\":", category);
    cgai_json_string(writer, cgai_static_knowledge_category_key(category));
    cgai_json_text(writer, ",\"name\":");
    cgai_json_string(writer, cgai_static_knowledge_category_name(category));
    cgai_json_text(writer, ",\"description\":");
    cgai_json_string(writer, cgai_static_knowledge_category_description(category));
    cgai_json_text(writer, "}");
}

/** Append one immutable row's metadata without copying coordinates across the ABI.
 * @param writer Mutable JSON writer.
 * @param row Valid compiled centroid row.
 */
static void row_json(cgai_json_writer *writer, size_t row) {
    const cgai_static_knowledge_centroid *point = cgai_static_knowledge_centroid_at(row);
    cgai_json_append(writer, "{\"row\":%zu,\"id\":", row);
    cgai_json_string(writer, point->id);
    cgai_json_text(writer, ",\"category\":");
    cgai_json_string(writer, cgai_static_knowledge_category_key(point->category));
    cgai_json_append(writer,
                     ",\"observations\":\"%" PRIu64 "\",\"description\":", point->observations);
    cgai_json_string(writer, point->description);
    cgai_json_text(writer, "}");
}

cgai_abi_status cgai_abi_knowledge_catalog(cgai_abi_buffer *output) {
    if (!output)
        return CGAI_ABI_INVALID_ARGUMENT;
    *output = (cgai_abi_buffer){0};
    cgai_json_writer writer = {0};
    cgai_json_append(&writer, "{\"centroids\":%zu,\"dimensions\":%u,\"releaseSha256\":",
                     cgai_static_knowledge_centroid_count(), CGAI_STATIC_KNOWLEDGE_DIMENSIONS);
    cgai_json_string(&writer, cgai_static_knowledge_release_sha256());
    cgai_json_text(&writer, ",\"categories\":[");
    for (size_t i = 0U; i < cgai_static_knowledge_category_count(); ++i) {
        if (i)
            cgai_json_text(&writer, ",");
        category_json(&writer, i);
    }
    cgai_json_text(&writer, "]}");
    return finish(writer, output);
}

cgai_abi_status cgai_abi_knowledge_page(uint32_t offset, uint32_t limit, cgai_abi_buffer *output) {
    if (!output)
        return CGAI_ABI_INVALID_ARGUMENT;
    *output = (cgai_abi_buffer){0};
    if (limit == 0U || limit > 100U) {
        (void)cgai_fail("knowledge page limit must be 1-100");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    cgai_json_writer writer = {0};
    cgai_json_append(&writer, "{\"total\":%zu,\"offset\":%u,\"items\":[",
                     cgai_static_knowledge_centroid_count(), offset);
    for (size_t row = offset; row < cgai_static_knowledge_centroid_count() && row - offset < limit;
         ++row) {
        if (row != offset)
            cgai_json_text(&writer, ",");
        row_json(&writer, row);
    }
    cgai_json_text(&writer, "]}");
    return finish(writer, output);
}

/** Serialize native neighbors and work counters with exact decimal observation counts.
 * @param id Borrowed source stable ID.
 * @param hits Borrowed sorted neighbor rows.
 * @param stats Borrowed completed query counters.
 * @param output Empty ABI descriptor receiving JSON ownership.
 * @return OK on success, ERROR on serialization failure.
 */
static cgai_abi_status neighbors_json(const char *id, const cgai_spatial_hit *hits,
                                      const cgai_spatial_stats *stats, cgai_abi_buffer *output) {
    cgai_json_writer writer = {0};
    cgai_json_text(&writer, "{\"id\":");
    cgai_json_string(&writer, id);
    cgai_json_text(&writer, ",\"engine\":\"native-c\",\"neighbors\":[");
    for (size_t i = 0U; i < stats->count; ++i) {
        cgai_json_append(&writer, "%s{\"squaredDistance\":%.17g,\"centroid\":", i ? "," : "",
                         hits[i].squared_distance);
        row_json(&writer, hits[i].row);
        cgai_json_text(&writer, "}");
    }
    cgai_json_append(&writer, "],\"comparisons\":%zu,\"visitedNodes\":%zu}", stats->comparisons,
                     stats->visited_nodes);
    return finish(writer, output);
}

cgai_abi_status cgai_abi_knowledge_neighbors(const cgai_abi_knowledge_index *index, const char *id,
                                             uint32_t limit, uint32_t category,
                                             cgai_abi_buffer *output) {
    if (!output)
        return CGAI_ABI_INVALID_ARGUMENT;
    *output = (cgai_abi_buffer){0};
    cgai_spatial_hit hits[100];
    cgai_spatial_stats stats;
    if (cgai_static_knowledge_neighbors((const cgai_static_knowledge_index *)index, id, limit,
                                        category, hits, &stats) != CGAI_STATUS_OK)
        return CGAI_ABI_ERROR;
    return neighbors_json(id, hits, &stats, output);
}
