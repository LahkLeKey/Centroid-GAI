/** @file model_composition.c @brief Non-mutating model unions and weighted compaction. */
#include "centroid_gai.h"
#include "internal/constants.h"
#include "internal/error.h"
#include "internal/model_centroid.h"
#include "internal/model_validation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MERGE_MAX_TOKENS ((size_t)200000U)
#define MERGE_MAX_BYTES ((size_t)64U * 1024U * 1024U)
#define MERGE_MAX_DISTANCE_WORK UINT64_C(100000000)

typedef struct merge_workspace {
    cgai_config config;
    size_t rows, tokens, examples, unique;
    const char **spellings;
    size_t *mapping;
} merge_workspace;

static cgai_status validate_vector(const cgai_model *model, size_t c) {
    for (size_t d = 0; d < model->config.dimensions; ++d)
        if (!isfinite(model->centroids[c * model->config.dimensions + d]))
            return cgai_fail("non-finite centroid vector");
    return CGAI_STATUS_OK;
}

static cgai_status validate_row(const cgai_model *model, size_t c, uint64_t *total) {
    uint64_t row = 0U;
    for (size_t t = 0; t < model->vocabulary_size; ++t) {
        const uint64_t value = model->token_counts[c * model->vocabulary_size + t];
        if (UINT64_MAX - row < value)
            return cgai_fail("token count overflow");
        row += value;
    }
    if (row != model->cluster_sizes[c] ||
        (c < model->initialized_centroids ? row == 0U : row != 0U) || UINT64_MAX - *total < row)
        return cgai_fail("inconsistent model observation counts");
    *total += row;
    return validate_vector(model, c);
}

cgai_status cgai_model_validate_statistics(const cgai_model *model) {
    if (!model || model->vocabulary_size < 3U ||
        model->initialized_centroids > model->config.centroid_count ||
        strcmp(model->vocabulary[0], CGAI_TOKEN_BOS_TEXT) ||
        strcmp(model->vocabulary[1], CGAI_TOKEN_EOS_TEXT) ||
        strcmp(model->vocabulary[2], CGAI_TOKEN_UNKNOWN_TEXT))
        return cgai_fail("invalid model vocabulary or centroid count");
    uint64_t total = 0U;
    for (size_t c = 0; c < model->config.centroid_count; ++c)
        if (validate_row(model, c, &total) != CGAI_STATUS_OK)
            return CGAI_STATUS_ERROR;
    return total == model->examples_seen ? CGAI_STATUS_OK : cgai_fail("inconsistent examples seen");
}

static int compare_spellings(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Ordinary union spellings are sorted; control tokens keep their original IDs. */
static size_t find_token(const cgai_model *model, const char *token) {
    for (size_t i = 0; i < 3U; ++i)
        if (!strcmp(model->vocabulary[i], token))
            return i;
    size_t low = 3U, high = model->vocabulary_size;
    if (high > model->vocabulary_capacity)
        return SIZE_MAX;
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        if (strcmp(model->vocabulary[middle], token) < 0)
            low = middle + 1U;
        else
            high = middle;
    }
    return low < model->vocabulary_size && !strcmp(model->vocabulary[low], token) ? low : SIZE_MAX;
}

static int collect_source(merge_workspace *work, const cgai_model *source) {
    if (!source) {
        (void)cgai_fail("merge source must not be null");
        return 0;
    }
    if (cgai_model_validate_statistics(source) != CGAI_STATUS_OK)
        return 0;
    if (!source->initialized_centroids || source->config.dimensions != work->config.dimensions ||
        source->config.context_window != work->config.context_window ||
        source->config.seed != work->config.seed) {
        (void)cgai_fail(
            "merge sources must be trained and share dimensions, context window, and seed");
        return 0;
    }
    if (SIZE_MAX - work->examples < source->examples_seen ||
        source->vocabulary_size > MERGE_MAX_TOKENS - work->tokens) {
        (void)cgai_fail("merge counter or vocabulary limit exceeded");
        return 0;
    }
    work->examples += source->examples_seen;
    work->tokens += source->vocabulary_size;
    work->rows += source->initialized_centroids; /* <= 32 * 65536 */
    return 1;
}

static int collect_sources(merge_workspace *work, const cgai_model *const *sources, size_t count) {
    if (!sources || count == 0U || count > 32U || !sources[0]) {
        (void)cgai_fail("merge requires 1 to 32 source models");
        return 0;
    }
    work->config = sources[0]->config;
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < i; ++j)
            if (sources[i] == sources[j]) {
                return (int)cgai_fail("duplicate merge source");
            }
        if (!collect_source(work, sources[i]))
            return 0;
    }
    return 1;
}

static int choose_capacity(merge_workspace *work, size_t target) {
    const size_t capacity = target ? target : work->rows;
    if (capacity == 0U || capacity > work->rows || capacity > CGAI_MAX_CENTROID_COUNT) {
        (void)cgai_fail(
            "target centroid count must fit the active source rows and native capacity");
        return 0;
    }
    if (target && (uint64_t)(work->rows - capacity) * capacity * work->config.dimensions >
                      MERGE_MAX_DISTANCE_WORK) {
        (void)cgai_fail("merge distance-work limit exceeded; use smaller source groups");
        return 0;
    }
    work->config.centroid_count = capacity;
    return 1;
}

static size_t gather_spellings(merge_workspace *work, const cgai_model *const *sources,
                               size_t count) {
    size_t used = 0U;
    for (size_t i = 0; i < count; ++i)
        for (size_t t = 3U; t < sources[i]->vocabulary_size; ++t)
            work->spellings[used++] = sources[i]->vocabulary[t];
    return used;
}

static int collect_vocabulary(merge_workspace *work, const cgai_model *const *sources,
                              size_t count) {
    work->spellings = (const char **)malloc(work->tokens * sizeof(*work->spellings));
    work->mapping = (size_t *)malloc(work->tokens * sizeof(*work->mapping));
    if (!work->spellings || !work->mapping) {
        (void)cgai_fail("could not allocate merge vocabulary");
        return 0;
    }
    const size_t used = gather_spellings(work, sources, count);
    qsort(work->spellings, used, sizeof(*work->spellings), compare_spellings);
    for (size_t i = 0; i < used; ++i)
        if (work->unique == 0U || strcmp(work->spellings[i], work->spellings[work->unique - 1U]))
            work->spellings[work->unique++] = work->spellings[i];
    return 1;
}

static int copy_spellings(cgai_model *output, const merge_workspace *work) {
    for (size_t i = 0; i < work->unique; ++i) {
        const size_t length = strlen(work->spellings[i]) + 1U;
        char *copy = (char *)malloc(length);
        if (!copy)
            return 0;
        memcpy(copy, work->spellings[i], length);
        output->vocabulary[output->vocabulary_size++] = copy;
    }
    return 1;
}

/* Allocate the union count matrix once instead of resizing it for every token. */
static int install_vocabulary(cgai_model *output, const merge_workspace *work) {
    const size_t vocabulary = work->unique + 3U;
    char **vocab = (char **)realloc(output->vocabulary, vocabulary * sizeof(char *));
    if (!vocab)
        return 0;
    output->vocabulary = vocab;
    output->vocabulary_capacity = vocabulary;
    if (!copy_spellings(output, work))
        return 0;
    free(output->token_counts);
    output->token_counts =
        (uint64_t *)calloc(work->config.centroid_count * vocabulary, sizeof(uint64_t));
    return output->token_counts != NULL;
}

static cgai_model *create_union(const merge_workspace *work) {
    const uint64_t bytes = (uint64_t)work->config.centroid_count *
                           (work->config.dimensions * sizeof(float) + sizeof(uint64_t) +
                            (work->unique + 3U) * sizeof(uint64_t));
    if (bytes > MERGE_MAX_BYTES) {
        (void)cgai_fail("merged numeric storage exceeds 64 MiB");
        return NULL;
    }
    cgai_model *output = cgai_model_create(&work->config);
    if (output && !install_vocabulary(output, work)) {
        cgai_model_destroy(output);
        (void)cgai_fail("could not allocate merged model");
        return NULL;
    }
    return output;
}

static void blend_vector(cgai_model *output, size_t destination, const float *vector,
                         uint64_t weight) {
    const double ratio = (double)weight / (double)(output->cluster_sizes[destination] + weight);
    for (size_t d = 0; d < output->config.dimensions; ++d) {
        float *mean = &output->centroids[destination * output->config.dimensions + d];
        *mean = (float)((double)*mean + ((double)vector[d] - *mean) * ratio);
    }
}

static size_t assign_vector(cgai_model *output, const float *vector, uint64_t weight) {
    size_t destination;
    if (output->initialized_centroids < output->config.centroid_count) {
        destination = output->initialized_centroids++;
        memcpy(output->centroids + destination * output->config.dimensions, vector,
               output->config.dimensions * sizeof(float));
    } else {
        destination = cgai_nearest_centroid(output, vector).value;
        blend_vector(output, destination, vector, weight);
    }
    output->cluster_sizes[destination] += weight;
    return destination;
}

static int merge_source(cgai_model *output, const cgai_model *source, size_t *mapping) {
    for (size_t t = 0; t < source->vocabulary_size; ++t) {
        mapping[t] = find_token(output, source->vocabulary[t]);
        if (mapping[t] == SIZE_MAX) {
            return (int)cgai_fail("merge vocabulary remapping failed");
        }
    }
    for (size_t c = 0; c < source->initialized_centroids; ++c) {
        const float *vector = source->centroids + c * source->config.dimensions;
        const size_t destination = assign_vector(output, vector, source->cluster_sizes[c]);
        for (size_t t = 0; t < source->vocabulary_size; ++t)
            output->token_counts[destination * output->vocabulary_size + mapping[t]] +=
                source->token_counts[c * source->vocabulary_size + t];
    }
    return 1;
}

static cgai_model *build_union(const merge_workspace *work, const cgai_model *const *sources,
                               size_t count) {
    cgai_model *output = create_union(work);
    if (!output)
        return NULL;
    for (size_t i = 0; i < count; ++i)
        if (!merge_source(output, sources[i], work->mapping)) {
            cgai_model_destroy(output);
            return NULL;
        }
    output->examples_seen = work->examples;
    return output;
}

cgai_model *cgai_model_merge(const cgai_model *const *sources, size_t count, size_t target) {
    cgai_error_clear();
    merge_workspace work = {0};
    cgai_model *output = NULL;
    if (collect_sources(&work, sources, count) && choose_capacity(&work, target) &&
        collect_vocabulary(&work, sources, count))
        output = build_union(&work, sources, count);
    free(work.mapping);
    free(work.spellings);
    return output;
}
