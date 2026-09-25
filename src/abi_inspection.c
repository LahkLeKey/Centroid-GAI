/** @file abi_inspection.c @brief Bounded artifact inspection and composition ABI. */
#include "centroid_gai_abi.h"
#include "internal/abi_utils.h"
#include "internal/error.h"
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct json_writer {
    char *data;
    size_t size;
    size_t capacity;
    int failed;
} json_writer;
typedef struct token_rank {
    size_t id;
    uint64_t count;
} token_rank;
typedef struct inspection_page {
    uint64_t offset;
    uint32_t limit;
    uint32_t centroid;
} inspection_page;

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

static void append_args(json_writer *writer, const char *format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    const int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0 || (size_t)length > SIZE_MAX - writer->size - 1U ||
        !reserve(writer, writer->size + (size_t)length + 1U)) {
        writer->failed = 1;
        return;
    }
    (void)vsnprintf(writer->data + writer->size, writer->capacity - writer->size, format, args);
    writer->size += (size_t)length;
}

static void append(json_writer *writer, const char *format, ...) {
    if (writer->failed)
        return;
    va_list args;
    va_start(args, format);
    append_args(writer, format, args);
    va_end(args);
}

static void string_value(json_writer *writer, const char *value) {
    append(writer, "\"");
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\')
            append(writer, "\\%c", (int)*p);
        else if (*p < 32U)
            append(writer, "\\u%04x", (unsigned int)*p);
        else
            append(writer, "%c", (int)*p);
    }
    append(writer, "\"");
}

static int compare_counts(const void *left, const void *right) {
    const token_rank *a = (const token_rank *)left, *b = (const token_rank *)right;
    if (a->count != b->count)
        return a->count > b->count ? -1 : 1;
    return a->id < b->id ? -1 : a->id != b->id;
}

static void token_json(json_writer *writer, const cgai_model *model, size_t id, uint64_t count) {
    append(writer, "{\"id\":%zu,\"token\":", id);
    string_value(writer, model->vocabulary[id]);
    append(writer, ",\"count\":\"%" PRIu64 "\"}", count);
}

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

static void vocabulary_json(json_writer *writer, const cgai_model *model, size_t id) {
    uint64_t count = 0U;
    for (size_t c = 0; c < model->initialized_centroids; ++c)
        count += model->token_counts[c * model->vocabulary_size + id];
    token_json(writer, model, id, count);
}

static void page_json(json_writer *writer, const cgai_model *model, uint32_t section,
                      inspection_page page) {
    const size_t total = section == 1U ? model->vocabulary_size : model->initialized_centroids;
    const size_t begin = page.offset > total ? total : (size_t)page.offset;
    const size_t end = total - begin > page.limit ? begin + page.limit : total;
    append(writer, "{\"total\":%zu,\"offset\":%" PRIu64 ",\"limit\":%u,\"items\":[", total,
           page.offset, page.limit);
    for (size_t i = begin; i < end; ++i) {
        if (i != begin)
            append(writer, ",");
        if (section == 1U)
            vocabulary_json(writer, model, i);
        else
            centroid_json(writer, model, i);
    }
    append(writer, "]}");
}

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

static void ranked_page_json(json_writer *writer, const cgai_model *model, inspection_page page,
                             const token_rank *ranks, size_t total) {
    const size_t begin = page.offset > total ? total : (size_t)page.offset;
    const size_t end = total - begin > page.limit ? begin + page.limit : total;
    append(writer, "{\"total\":%zu,\"offset\":%" PRIu64 ",\"limit\":%u,\"items\":[", total,
           page.offset, page.limit);
    for (size_t i = begin; i < end; ++i) {
        if (i != begin)
            append(writer, ",");
        token_json(writer, model, ranks[i].id, ranks[i].count);
    }
    append(writer, "]}");
}

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
    append(writer, "],\"tokens\":");
    ranked_page_json(writer, model, page, ranks, total);
    append(writer, "}");
    free(ranks);
}

static cgai_abi_status inspect_section(const cgai_model *model, uint32_t section,
                                       inspection_page page, cgai_abi_buffer *output) {
    json_writer writer = {0};
    if (section == 0U)
        summary_json(&writer, model);
    else if (section < 3U)
        page_json(&writer, model, section, page);
    else
        detail_json(&writer, model, page);
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
    if (!model || section > 3U || limit == 0U || limit > 100U || offset > SIZE_MAX ||
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
