/** @file abi_inspection.c @brief Bounded artifact inspection and composition ABI. */
#include "centroid_gai_abi.h"
#include "internal/abi_utils.h"
#include "internal/error.h"
#include "internal/model_embedding.h"
#include "internal/model_generation.h"
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Owned expandable JSON output and sticky allocation failure state. */
typedef struct json_writer {
    char *data;      /**< Owned UTF-8 buffer. */
    size_t size;     /**< Written bytes excluding NUL. */
    size_t capacity; /**< Allocated byte capacity. */
    int failed;      /**< Nonzero after an allocation or formatting error. */
} json_writer;
/** @brief Token identifier paired with its observation frequency. */
typedef struct token_rank {
    size_t id;      /**< Vocabulary token ID. */
    uint64_t count; /**< Observed target frequency. */
} token_rank;
/** @brief Validated selection for bounded artifact inspection. */
typedef struct inspection_page {
    uint64_t offset;   /**< Starting result offset. */
    uint32_t limit;    /**< Maximum result entries. */
    uint32_t centroid; /**< Selected active centroid ID. */
} inspection_page;

/**
 * @brief Grow the owned JSON buffer while checking allocation capacity.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param needed Required allocation capacity in bytes.
 * @return One on success, or zero on validation/allocation failure.
 */
static int reserve(json_writer *writer, size_t needed) {
    if (needed <= writer->capacity)
        return 1;
    size_t capacity = writer->capacity ? writer->capacity : 1024U;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    char *data = (char *)realloc(writer->data, capacity);
    if (!data)
        return 0;
    writer->data = data;
    writer->capacity = capacity;
    return 1;
}

/**
 * @brief Append formatted JSON while keeping both variadic traversals local and balanced.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param format Borrowed printf-compatible format string.
 */
static void append(json_writer *writer, const char *format, ...) {
    if (writer->failed)
        return;
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (length < 0 || (size_t)length > SIZE_MAX - writer->size - 1U ||
        !reserve(writer, writer->size + (size_t)length + 1U)) {
        writer->failed = 1;
    } else {
        va_start(args, format);
        (void)vsnprintf(writer->data + writer->size, writer->capacity - writer->size, format, args);
        va_end(args);
        writer->size += (size_t)length;
    }
}

/**
 * @brief Append literal UTF-8 bytes without variadic argument handling.
 * @param writer Mutable JSON output buffer.
 * @param value Borrowed NUL-terminated text to copy.
 */
static void append_text(json_writer *writer, const char *value) {
    if (writer->failed)
        return;
    const size_t length = strlen(value);
    if (length > SIZE_MAX - writer->size - 1U || !reserve(writer, writer->size + length + 1U)) {
        writer->failed = 1;
        return;
    }
    memcpy(writer->data + writer->size, value, length + 1U);
    writer->size += length;
}

/**
 * @brief Append a quoted JSON string, escaping control bytes and delimiters.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param value Borrowed input value to validate or serialize.
 */
static void string_value(json_writer *writer, const char *value) {
    append_text(writer, "\"");
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\')
            append(writer, "\\%c", (int)*p);
        else if (*p < 32U)
            append(writer, "\\u%04x", (unsigned int)*p);
        else
            append(writer, "%c", (int)*p);
    }
    append_text(writer, "\"");
}

/**
 * @brief Order token ranks by descending observations, then ascending token ID.
 * @param left Borrowed left comparison operand.
 * @param right Borrowed right comparison operand.
 * @return Negative, zero, or positive according to the requested ordering.
 */
static int compare_counts(const void *left, const void *right) {
    const token_rank *a = (const token_rank *)left, *b = (const token_rank *)right;
    if (a->count != b->count)
        return a->count > b->count ? -1 : 1;
    return a->id < b->id ? -1 : a->id != b->id;
}

/**
 * @brief Append one vocabulary entry and its exact decimal observation count.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 * @param id Vocabulary or centroid index for the selected entry.
 * @param count Number of entries supplied to this operation.
 */
static void token_json(json_writer *writer, const cgai_model *model, size_t id, uint64_t count) {
    append(writer, "{\"id\":%zu,\"token\":", id);
    string_value(writer, model->vocabulary[id]);
    append(writer, ",\"count\":\"%" PRIu64 "\"}", count);
}

/**
 * @brief Append configuration, occupancy, and numeric storage totals.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 */
static void summary_json(json_writer *writer, const cgai_model *model) {
    const uint64_t vectors =
        (uint64_t)model->config.centroid_count * model->config.dimensions * sizeof(float);
    const uint64_t counts =
        (uint64_t)model->config.centroid_count * model->vocabulary_size * sizeof(uint64_t);
    const uint64_t sizes = (uint64_t)model->config.centroid_count * sizeof(uint64_t);
    append(
        writer,
        "{\"dimensions\":%zu,\"centroidCount\":%zu,\"initializedCentroids\":%zu,\"contextWindow\":%"
        "zu,\"seed\":\"%" PRIu64
        "\",\"vocabularySize\":\"%zu\",\"examplesSeen\":\"%zu\",\"storage\":{\"vectors\":\"%" PRIu64
        "\",\"tokenCounts\":\"%" PRIu64 "\",\"clusterSizes\":\"%" PRIu64 "\"}}",
        model->config.dimensions, model->config.centroid_count, model->initialized_centroids,
        model->config.context_window, model->config.seed, model->vocabulary_size,
        model->examples_seen, vectors, counts, sizes);
}

/**
 * @brief Append a centroid observation count, vector norm, and target diversity.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 * @param id Vocabulary or centroid index for the selected entry.
 */
static void centroid_json(json_writer *writer, const cgai_model *model, size_t id) {
    double norm = 0.0;
    size_t targets = 0U;
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        const double v = model->centroids[id * model->config.dimensions + d];
        norm += v * v;
    }
    for (size_t t = 0; t < model->vocabulary_size; ++t)
        if (model->token_counts[id * model->vocabulary_size + t])
            targets++;
    append(writer,
           "{\"id\":%zu,\"observations\":\"%" PRIu64 "\",\"norm\":%.9g,\"distinctTargets\":%zu}",
           id, model->cluster_sizes[id], sqrt(norm), targets);
}

/**
 * @brief Sum a token across active rows and append its vocabulary entry.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 * @param id Vocabulary or centroid index for the selected entry.
 */
static void vocabulary_json(json_writer *writer, const cgai_model *model, size_t id) {
    uint64_t count = 0U;
    for (size_t c = 0; c < model->initialized_centroids; ++c)
        count += model->token_counts[c * model->vocabulary_size + id];
    token_json(writer, model, id, count);
}

/**
 * @brief Append a bounded vocabulary or centroid page.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 * @param section Inspection section selector.
 * @param page Validated pagination and centroid selection.
 */
static void page_json(json_writer *writer, const cgai_model *model, uint32_t section,
                      inspection_page page) {
    const size_t total = section == 1U ? model->vocabulary_size : model->initialized_centroids;
    const size_t begin = page.offset > total ? total : (size_t)page.offset;
    const size_t end = total - begin > page.limit ? begin + page.limit : total;
    append(writer, "{\"total\":%zu,\"offset\":%" PRIu64 ",\"limit\":%u,\"items\":[", total,
           page.offset, page.limit);
    for (size_t i = begin; i < end; ++i) {
        if (i != begin)
            append_text(writer, ",");
        if (section == 1U)
            vocabulary_json(writer, model, i);
        else
            centroid_json(writer, model, i);
    }
    append_text(writer, "]}");
}

/**
 * @brief Collect nonzero target counts and sort them by frequency.
 * @param model Borrowed model, kept alive for the operation.
 * @param centroid Index of the active centroid.
 * @param ranks Writable or borrowed token ranks, as required by this operation.
 * @return Number of initialized result entries.
 */
static size_t rank_tokens(const cgai_model *model, size_t centroid, token_rank *ranks) {
    size_t total = 0U;
    for (size_t t = 0; t < model->vocabulary_size; ++t) {
        const uint64_t count = model->token_counts[centroid * model->vocabulary_size + t];
        if (count) {
            ranks[total].id = t;
            ranks[total++].count = count;
        }
    }
    qsort(ranks, total, sizeof(*ranks), compare_counts);
    return total;
}

/**
 * @brief Append a bounded page of ranked target tokens.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 * @param page Validated pagination and centroid selection.
 * @param ranks Writable or borrowed token ranks, as required by this operation.
 * @param total Total available entries or mutable accumulated observations.
 */
static void ranked_page_json(json_writer *writer, const cgai_model *model, inspection_page page,
                             const token_rank *ranks, size_t total) {
    const size_t begin = page.offset > total ? total : (size_t)page.offset;
    const size_t end = total - begin > page.limit ? begin + page.limit : total;
    append(writer, "{\"total\":%zu,\"offset\":%" PRIu64 ",\"limit\":%u,\"items\":[", total,
           page.offset, page.limit);
    for (size_t i = begin; i < end; ++i) {
        if (i != begin)
            append_text(writer, ",");
        token_json(writer, model, ranks[i].id, ranks[i].count);
    }
    append_text(writer, "]}");
}

/**
 * @brief Append the actual centroid vector and ranked target distribution.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 * @param page Validated pagination and centroid selection.
 */
static void detail_json(json_writer *writer, const cgai_model *model, inspection_page page) {
    token_rank *ranks = (token_rank *)malloc(model->vocabulary_size * sizeof(*ranks));
    if (!ranks) {
        writer->failed = 1;
        return;
    }
    const size_t total = rank_tokens(model, page.centroid, ranks);
    append(writer, "{\"id\":%u,\"observations\":\"%" PRIu64 "\",\"vector\":[", page.centroid,
           model->cluster_sizes[page.centroid]);
    for (size_t d = 0; d < model->config.dimensions; ++d)
        append(writer, "%s%.9g", d ? "," : "",
               (double)model->centroids[(size_t)page.centroid * model->config.dimensions + d]);
    append_text(writer, "],\"tokens\":");
    ranked_page_json(writer, model, page, ranks, total);
    append_text(writer, "}");
    free(ranks);
}

/**
 * @brief Sum ordinary target tokens across all active centroids.
 * @param model Borrowed model, kept alive for the operation.
 * @param ranks Writable or borrowed token ranks, as required by this operation.
 */
static void aggregate_targets(const cgai_model *model, token_rank *ranks) {
    for (size_t t = 3U; t < model->vocabulary_size; ++t) {
        ranks[t - 3U].id = t;
        for (size_t c = 0; c < model->initialized_centroids; ++c)
            ranks[t - 3U].count += model->token_counts[c * model->vocabulary_size + t];
    }
}

/**
 * @brief Append frequent ordinary tokens for discovery without exposing control tokens.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 * @param limit Maximum number of result entries.
 */
static void highlights_json(json_writer *writer, const cgai_model *model, uint32_t limit) {
    token_rank *ranks = (token_rank *)calloc(model->vocabulary_size, sizeof(*ranks));
    if (!ranks) {
        writer->failed = 1;
        return;
    }
    aggregate_targets(model, ranks);
    const size_t total = model->vocabulary_size - 3U;
    qsort(ranks, total, sizeof(*ranks), compare_counts);
    const inspection_page page = {0U, limit, 0U};
    append_text(writer, "{\"tokens\":");
    ranked_page_json(writer, model, page, ranks, total);
    append_text(writer, "}");
    free(ranks);
}

/**
 * @brief Dispatch an inspection section and transfer the completed JSON allocation.
 * @param model Borrowed model, kept alive for the operation.
 * @param section Inspection section selector.
 * @param page Validated pagination and centroid selection.
 * @param output Writable destination; receives the result or owned output allocation.
 * @return CGAI_ABI_OK on success, otherwise an ABI error with a diagnostic.
 */
static cgai_abi_status inspect_section(const cgai_model *model, uint32_t section,
                                       inspection_page page, cgai_abi_buffer *output) {
    json_writer writer = {0};
    if (section == 0U)
        summary_json(&writer, model);
    else if (section < 3U)
        page_json(&writer, model, section, page);
    else if (section == 3U)
        detail_json(&writer, model, page);
    else
        highlights_json(&writer, model, page.limit);
    if (writer.failed) {
        free(writer.data);
        (void)cgai_fail("could not allocate inspection JSON");
        return CGAI_ABI_OUT_OF_MEMORY;
    }
    output->data = (uint8_t *)writer.data;
    output->size = writer.size;
    return CGAI_ABI_OK;
}

cgai_abi_status cgai_abi_model_inspect_json(const cgai_abi_model *handle, uint32_t section,
                                            uint64_t offset, uint32_t limit, uint32_t centroid_id,
                                            cgai_abi_buffer *output) {
    if (!output) {
        (void)cgai_fail("inspection output is required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    output->data = NULL;
    output->size = 0U;
    const cgai_model *model = cgai_abi_const_core_model(handle);
    if (!model || section > 4U || limit == 0U || limit > 100U || offset > SIZE_MAX ||
        (section == 3U && centroid_id >= model->initialized_centroids)) {
        (void)cgai_fail("invalid inspection section, page, or centroid");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    const inspection_page page = {offset, limit, centroid_id};
    return inspect_section(model, section, page, output);
}

cgai_abi_status cgai_abi_model_merge(const cgai_abi_model *const *sources, uint32_t count,
                                     uint32_t target_centroids, cgai_abi_model **output) {
    if (!output) {
        (void)cgai_fail("merge output is required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    *output = NULL;
    if (!sources || count == 0U || count > 32U) {
        (void)cgai_fail("merge requires 1 to 32 sources");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    const cgai_model *core[32];
    for (uint32_t i = 0; i < count; ++i)
        core[i] = cgai_abi_const_core_model(sources[i]);
    *output = (cgai_abi_model *)cgai_model_merge(core, count, target_centroids);
    return *output ? CGAI_ABI_OK : CGAI_ABI_ERROR;
}

/** @brief Candidate centroid and its distance from an input context. */
typedef struct centroid_match {
    size_t id;       /**< Active centroid ID. */
    double distance; /**< Squared Euclidean distance. */
} centroid_match;

/**
 * @brief Measure squared Euclidean distance from a query embedding to a centroid.
 * @param model Borrowed model, kept alive for the operation.
 * @param centroid Index of the active centroid.
 * @param embedding Borrowed query vector with one value per model dimension.
 * @return Nonnegative squared distance in the model embedding space.
 */
static double centroid_distance(const cgai_model *model, size_t centroid, const float *embedding) {
    double distance = 0.0;
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        const double delta =
            (double)embedding[d] - model->centroids[centroid * model->config.dimensions + d];
        distance += delta * delta;
    }
    return distance;
}

/**
 * @brief Insert a candidate into a bounded stable list of nearest centroids.
 * @param matches Writable or borrowed nearest-centroid entries.
 * @param used Number of initialized entries in the result list.
 * @param limit Maximum number of result entries.
 * @param id Vocabulary or centroid index for the selected entry.
 * @param distance Squared Euclidean distance of the candidate.
 * @return Number of initialized result entries.
 */
static size_t insert_match(centroid_match *matches, size_t used, size_t limit, size_t id,
                           double distance) {
    size_t position = 0U;
    while (position < used && matches[position].distance <= distance)
        position++;
    if (position >= limit)
        return used;
    if (used < limit)
        used++;
    for (size_t i = used - 1U; i > position; --i)
        matches[i] = matches[i - 1U];
    matches[position].id = id;
    matches[position].distance = distance;
    return used;
}

/**
 * @brief Rank initialized centroids with ties resolved by ascending centroid ID.
 * @param model Borrowed model, kept alive for the operation.
 * @param embedding Borrowed query vector with one value per model dimension.
 * @param limit Maximum number of result entries.
 * @param matches Writable or borrowed nearest-centroid entries.
 * @return Number of initialized result entries.
 */
static size_t nearest_matches(const cgai_model *model, const float *embedding, size_t limit,
                              centroid_match *matches) {
    size_t used = 0U;
    for (size_t c = 0; c < model->initialized_centroids; ++c)
        used = insert_match(matches, used, limit, c, centroid_distance(model, c, embedding));
    return used;
}

/**
 * @brief Append the used context suffix and count unknown vocabulary mappings.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 * @param workspace Prepared temporary context and vector storage.
 */
static void matched_context_json(json_writer *writer, const cgai_model *model,
                                 const generation_workspace *workspace) {
    const size_t count = workspace->prompt_tokens.count;
    const size_t begin =
        count > model->config.context_window ? count - model->config.context_window : 0U;
    size_t unknown = 0U;
    for (size_t i = begin; i < count; ++i)
        if (workspace->history[i].value == CGAI_TOKEN_UNKNOWN)
            unknown++;
    append(writer, "{\"inputTokens\":%zu,\"unknownTokens\":%zu,\"context\":[", count, unknown);
    for (size_t i = begin; i < count; ++i) {
        append(writer, "%s{\"token\":", i == begin ? "" : ",");
        string_value(writer, workspace->prompt_tokens.items[i]);
        append(writer, ",\"known\":%s}",
               workspace->history[i].value == CGAI_TOKEN_UNKNOWN ? "false" : "true");
    }
    append_text(writer, "],\"matches\":[");
}

/**
 * @brief Append nearest centroid distances and their observed target distributions.
 * @param writer Mutable JSON buffer; records allocation failure for its caller.
 * @param model Borrowed model, kept alive for the operation.
 * @param matches Writable or borrowed nearest-centroid entries.
 * @param count Number of entries supplied to this operation.
 */
static void matched_rows_json(json_writer *writer, const cgai_model *model,
                              const centroid_match *matches, size_t count) {
    token_rank *ranks = (token_rank *)malloc(model->vocabulary_size * sizeof(*ranks));
    if (!ranks) {
        writer->failed = 1;
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const size_t id = matches[i].id;
        const size_t total = rank_tokens(model, id, ranks);
        append(writer,
               "%s{\"centroidId\":%zu,\"squaredDistance\":%.17g,\"observations\":\"%" PRIu64
               "\",\"targets\":",
               i ? "," : "", id, matches[i].distance, model->cluster_sizes[id]);
        const inspection_page page = {0U, 10U, (uint32_t)id};
        ranked_page_json(writer, model, page, ranks, total);
        append_text(writer, "}");
    }
    free(ranks);
}

/**
 * @brief Embed a prepared context and serialize its nearest learned patterns.
 * @param model Borrowed model, kept alive for the operation.
 * @param workspace Prepared temporary context and vector storage.
 * @param limit Maximum number of result entries.
 * @param output Writable destination; receives the result or owned output allocation.
 * @return CGAI_ABI_OK on success, otherwise an ABI error with a diagnostic.
 */
static cgai_abi_status match_json(const cgai_model *model, generation_workspace *workspace,
                                  uint32_t limit, cgai_abi_buffer *output) {
    size_t history_count = 0U;
    cgai_generation_prepare_history(model, workspace, &history_count);
    cgai_context_embedding(model, workspace->history, history_count, workspace->embedding,
                           workspace->scratch);
    centroid_match matches[10];
    const size_t count = nearest_matches(model, workspace->embedding, limit, matches);
    json_writer writer = {0};
    matched_context_json(&writer, model, workspace);
    matched_rows_json(&writer, model, matches, count);
    append_text(&writer, "]}");
    if (writer.failed) {
        free(writer.data);
        (void)cgai_fail("could not allocate pattern match JSON");
        return CGAI_ABI_OUT_OF_MEMORY;
    }
    output->data = (uint8_t *)writer.data;
    output->size = writer.size;
    return CGAI_ABI_OK;
}

/**
 * @brief Validate model training, text size, result limit, and distance-work budget.
 * @param model Borrowed model, kept alive for the operation.
 * @param text Borrowed NUL-terminated context text.
 * @param limit Maximum number of result entries.
 * @return One on success, or zero on validation/allocation failure.
 */
static int valid_match(const cgai_model *model, const char *text, uint32_t limit) {
    if (!model || !text || strlen(text) > 16384U || !limit || limit > 10U ||
        !model->initialized_centroids) {
        (void)cgai_fail(
            "matching requires a trained model, text up to 16384 bytes, and limit 1..10");
        return 0;
    }
    if ((uint64_t)model->initialized_centroids * model->config.dimensions > UINT64_C(100000000)) {
        (void)cgai_fail("pattern matching distance-work limit exceeded");
        return 0;
    }
    return 1;
}

/**
 * @brief Own temporary tokenization and embedding storage for one read-only match.
 * @param model Borrowed model, kept alive for the operation.
 * @param text Borrowed NUL-terminated context text.
 * @param limit Maximum number of result entries.
 * @param output Writable destination; receives the result or owned output allocation.
 * @return CGAI_ABI_OK on success, otherwise an ABI error with a diagnostic.
 */
static cgai_abi_status prepare_match(const cgai_model *model, const char *text, uint32_t limit,
                                     cgai_abi_buffer *output) {
    generation_workspace workspace = {0};
    cgai_abi_status status = CGAI_ABI_ERROR;
    if (cgai_generation_prepare(model, text, 0U, &workspace) == CGAI_STATUS_OK) {
        if (!workspace.prompt_tokens.count) {
            (void)cgai_fail("pattern matching text must contain tokens");
            status = CGAI_ABI_INVALID_ARGUMENT;
        } else {
            status = match_json(model, &workspace, limit, output);
        }
    }
    cgai_generation_workspace_destroy(&workspace);
    return status;
}

cgai_abi_status cgai_abi_model_match_json(const cgai_abi_model *handle, const char *text,
                                          uint32_t limit, cgai_abi_buffer *output) {
    cgai_error_clear();
    if (!output) {
        (void)cgai_fail("pattern match output is required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    output->data = NULL;
    output->size = 0U;
    const cgai_model *model = cgai_abi_const_core_model(handle);
    return valid_match(model, text, limit) ? prepare_match(model, text, limit, output)
                                           : CGAI_ABI_INVALID_ARGUMENT;
}
