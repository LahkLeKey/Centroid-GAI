/** @file vocabulary.c @brief Vocabulary storage and token-distribution resizing. */

#include "internal/vocabulary.h"

#include "internal/constants.h"
#include "internal/error.h"
#include "internal/size_utils.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** Allocates an owned copy of a vocabulary spelling. */
static char *duplicate_string(const char *value) {
    /* Copy the terminator as well as the spelling so ownership is self-contained. */
    size_t length = 0U;
    if (!cgai_size_add(strlen(value), 1U, &length)) {
        return NULL;
    }
    char *copy = (char *)malloc(length);
    if (copy != NULL) {
        memcpy(copy, value, length);
    }
    return copy;
}

/** Grows the vocabulary pointer array when it has no free slot. */
static cgai_status ensure_vocabulary_capacity(cgai_model *model) {
    /* Existing slots are stable and do not need reallocation. */
    if (model->vocabulary_size < model->vocabulary_capacity) {
        return CGAI_STATUS_OK;
    }
    size_t capacity = model->vocabulary_capacity == 0U ? CGAI_INITIAL_VOCABULARY_CAPACITY
                                                       : model->vocabulary_capacity * 2U;
    size_t bytes = 0U;
    /* Check both capacity doubling and pointer-array byte multiplication. */
    if ((model->vocabulary_capacity != 0U && model->vocabulary_capacity > SIZE_MAX / 2U) ||
        !cgai_size_mul(capacity, sizeof(char *), &bytes)) {
        return cgai_fail("vocabulary is too large");
    }
    char **items = (char **)realloc(model->vocabulary, bytes);
    if (items == NULL) {
        return cgai_fail("could not grow vocabulary");
    }
    model->vocabulary = items;
    /* Only publish the new capacity after the pointer remains valid. */
    model->vocabulary_capacity = capacity;
    return CGAI_STATUS_OK;
}

/** Finds a token by linear scan and returns its stable identifier. */
cgai_token_id cgai_vocabulary_find(const cgai_model *model, const char *token) {
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        /* Vocabulary IDs are array indexes and remain stable after insertion. */
        if (strcmp(model->vocabulary[i], token) == 0) {
            return cgai_token_id_from_size(i);
        }
    }
    return cgai_token_id_invalid();
}

/** Resizes every centroid's token-count row while preserving old counts. */
static cgai_status resize_counts(cgai_model *model, size_t old_size, size_t new_size) {
    /* Allocate a new rectangular table because each centroid row gains one column. */
    size_t count_values = 0U;
    size_t count_bytes = 0U;
    if (!cgai_size_mul(model->config.centroid_count, new_size, &count_values) ||
        !cgai_size_mul(count_values, sizeof(uint64_t), &count_bytes)) {
        return cgai_fail("vocabulary is too large");
    }
    uint64_t *counts = (uint64_t *)calloc(1U, count_bytes);
    if (counts == NULL) {
        return cgai_fail("could not grow token counts");
    }
    if (model->token_counts != NULL) {
        for (size_t c = 0; c < model->config.centroid_count; ++c) {
            /* Copy each old row into its new, wider row without shifting neighbors. */
            memcpy(counts + c * new_size, model->token_counts + c * old_size,
                   old_size * sizeof(uint64_t));
        }
        free(model->token_counts);
    }
    model->token_counts = counts;
    return CGAI_STATUS_OK;
}

/** Adds a vocabulary spelling and expands token-count storage when needed. */
cgai_token_id cgai_vocabulary_add(cgai_model *model, const char *token) {
    /* Avoid duplicate strings and preserve the first assigned identifier. */
    const cgai_token_id existing = cgai_vocabulary_find(model, token);
    if (cgai_token_id_is_valid(existing)) {
        return existing;
    }
    if (ensure_vocabulary_capacity(model) != CGAI_STATUS_OK) {
        return cgai_token_id_invalid();
    }
    size_t new_size = 0U;
    /* The new count-table width is the old vocabulary size plus this token. */
    if (!cgai_size_add(model->vocabulary_size, 1U, &new_size)) {
        (void)cgai_fail("vocabulary is too large");
        return cgai_token_id_invalid();
    }
    char *copy = duplicate_string(token);
    if (copy == NULL || resize_counts(model, model->vocabulary_size, new_size) != CGAI_STATUS_OK) {
        /* Neither the string nor the table is published when either allocation fails. */
        free(copy);
        return cgai_token_id_invalid();
    }
    model->vocabulary[model->vocabulary_size] = copy;
    /* Publish the spelling only after all dependent storage exists. */
    return cgai_token_id_from_size(model->vocabulary_size++);
}
