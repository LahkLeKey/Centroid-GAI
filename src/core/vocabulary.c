/** @file vocabulary.c @brief Vocabulary storage and token-distribution resizing. */

#include "internal/vocabulary.h"

#include "internal/constants.h"
#include "internal/error.h"
#include "internal/size_utils.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Allocate an independent copy of a NUL-terminated vocabulary spelling.
 *
 * The terminator is part of the allocation and copy, so the result is usable by strlen and strcmp.
 * The source pointer remains borrowed. This helper returns NULL directly on failure; it does not
 * itself guarantee that the thread-local diagnostic has been updated.
 *
 * @param value Non-NULL borrowed NUL-terminated spelling.
 * @return Owned copy to free(), or NULL when size calculation or allocation fails.
 */
static char *duplicate_string(const char *value) {
    /* Copy the terminator as well as the spelling so ownership is self-contained. */
    /* Step 1: Compute the spelling length plus its terminating byte with checked addition. */
    size_t length = 0U;
    if (!cgai_size_add(strlen(value), 1U, &length)) {
        return NULL;
    }
    /* Step 2: Allocate an independent string and copy bytes only if allocation succeeded. */
    char *copy = (char *)malloc(length);
    if (copy != NULL) {
        memcpy(copy, value, length);
    }
    /* Step 3: Return ownership, or propagate the NULL failure value. */
    return copy;
}

/**
 * @brief Reserve a pointer slot for a new vocabulary entry.
 *
 * Vocabulary capacity counts pointer slots, not string bytes or count-table cells. Existing IDs
 * remain stable because moving the pointer array does not change insertion indices. Successful
 * reallocation may invalidate a previously borrowed pointer to the array itself.
 *
 * @param model Non-NULL mutable model with a consistent vocabulary descriptor.
 * @return CGAI_STATUS_OK if a slot exists, otherwise CGAI_STATUS_ERROR; existing entries remain
 * owned.
 */
static cgai_status ensure_vocabulary_capacity(cgai_model *model) {
    /* Existing slots are stable and do not need reallocation. */
    /* Step 1: Avoid growth when the allocated pointer array already has room. */
    if (model->vocabulary_size < model->vocabulary_capacity) {
        return CGAI_STATUS_OK;
    }
    /* Step 2: Choose initial or doubled capacity and reject invalid byte calculations. */
    size_t capacity = model->vocabulary_capacity == 0U ? CGAI_INITIAL_VOCABULARY_CAPACITY
                                                       : model->vocabulary_capacity * 2U;
    size_t bytes = 0U;
    /* Check both capacity doubling and pointer-array byte multiplication. */
    if ((model->vocabulary_capacity != 0U && model->vocabulary_capacity > SIZE_MAX / 2U) ||
        !cgai_size_mul(capacity, sizeof(char *), &bytes) || bytes == 0U) {
        return cgai_fail("vocabulary is too large");
    }
    /* Step 3: Use a temporary realloc result so failure preserves the old pointer. */
    char **items = (char **)realloc(model->vocabulary, bytes);
    if (items == NULL) {
        return cgai_fail("could not grow vocabulary");
    }
    /* Step 4: Commit the new pointer and capacity after successful allocation. */
    model->vocabulary = items;
    /* Only publish the new capacity after the pointer remains valid. */
    model->vocabulary_capacity = capacity;
    return CGAI_STATUS_OK;
}

/**
 * @brief Find the stable identifier associated with an exact spelling.
 *
 * strcmp returns zero for equal C strings; the search therefore checks equality explicitly rather
 * than treating its result as a success flag. Lookup is a linear scan and does not normalize text,
 * allocate memory, or add missing spellings.
 *
 * @param model Non-NULL borrowed model with initialized vocabulary.
 * @param token Non-NULL borrowed NUL-terminated spelling to match exactly.
 * @return Typed vocabulary index, or the SIZE_MAX token sentinel when no spelling matches.
 */
cgai_token_id cgai_vocabulary_find(const cgai_model *model, const char *token) {
    /* Step 1: Visit occupied vocabulary entries in insertion order. */
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        /* Vocabulary IDs are array indexes and remain stable after insertion. */
        /* Step 2: Compare spelling bytes and return the first matching index. */
        if (strcmp(model->vocabulary[i], token) == 0) {
            return cgai_token_id_from_size(i);
        }
    }
    /* Step 3: Represent a missing spelling with the dedicated invalid token ID. */
    return cgai_token_id_invalid();
}

/**
 * @brief Grow each centroid's token-count row while retaining learned counts.
 *
 * The count table is one flat allocation indexed as row * vocabulary_size + token. Adding a token
 * changes the row stride, so realloc alone cannot preserve that layout: each old row must be copied
 * to a new, wider row. calloc gives every newly added token a starting count of zero.
 *
 * @param model Non-NULL mutable model owning the previous count table.
 * @param old_size Previous number of token columns per centroid.
 * @param new_size New column count, at least old_size and positive for a valid model.
 * @return CGAI_STATUS_OK after replacing the table, or CGAI_STATUS_ERROR with the old table
 * retained.
 */
static cgai_status resize_counts(cgai_model *model, size_t old_size, size_t new_size) {
    /* Allocate a new rectangular table because each centroid row gains one column. */
    /* Step 1: Calculate the new cell count and byte count using overflow-checked multiplication. */
    size_t count_values = 0U;
    size_t count_bytes = 0U;
    if (!cgai_size_mul(model->config.centroid_count, new_size, &count_values) ||
        !cgai_size_mul(count_values, sizeof(uint64_t), &count_bytes) || count_bytes == 0U) {
        return cgai_fail("vocabulary is too large");
    }
    /* Step 2: Allocate and zero the complete replacement matrix. */
    uint64_t *counts = (uint64_t *)calloc(1U, count_bytes);
    if (counts == NULL) {
        return cgai_fail("could not grow token counts");
    }
    /* Step 3: Copy each old row to its new stride, then release the previous matrix. */
    if (model->token_counts != NULL) {
        for (size_t c = 0; c < model->config.centroid_count; ++c) {
            /* Copy each old row into its new, wider row without shifting neighbors. */
            memcpy(counts + c * new_size, model->token_counts + c * old_size,
                   old_size * sizeof(uint64_t));
        }
        free(model->token_counts);
    }
    /* Step 4: Publish the replacement only after all non-failing row copies finish. */
    model->token_counts = counts;
    return CGAI_STATUS_OK;
}

/**
 * @brief Return an existing token ID or append a newly owned vocabulary spelling.
 *
 * An ID is the spelling's insertion index and stays stable for the life of this model. A new
 * entry needs both a copied spelling and a wider count table. These are prepared before the
 * occupied vocabulary size is advanced; capacity may still grow on a later failure. This mutates
 * the model and requires exclusive access.
 *
 * @param model Non-NULL mutable model with consistent vocabulary and count arrays.
 * @param token Non-NULL borrowed NUL-terminated spelling; copied only if it is new.
 * @return Existing/new token ID, or the invalid sentinel on failure.
 */
cgai_token_id cgai_vocabulary_add(cgai_model *model, const char *token) {
    /* Avoid duplicate strings and preserve the first assigned identifier. */
    /* Step 1: Look up duplicates first so repeated words reuse the same identifier. */
    const cgai_token_id existing = cgai_vocabulary_find(model, token);
    if (cgai_token_id_is_valid(existing)) {
        return existing;
    }
    /* Step 2: Reserve a pointer slot and check that the vocabulary count can grow. */
    if (ensure_vocabulary_capacity(model) != CGAI_STATUS_OK) {
        return cgai_token_id_invalid();
    }
    size_t new_size = 0U;
    /* The new count-table width is the old vocabulary size plus this token. */
    if (!cgai_size_add(model->vocabulary_size, 1U, &new_size)) {
        (void)cgai_fail("vocabulary is too large");
        return cgai_token_id_invalid();
    }
    /* Step 3: Prepare an owned spelling and a replacement count matrix before publishing the entry.
     */
    char *copy = duplicate_string(token);
    if (copy == NULL || resize_counts(model, model->vocabulary_size, new_size) != CGAI_STATUS_OK) {
        /* Neither the string nor the table is published when either allocation fails. */
        free(copy);
        return cgai_token_id_invalid();
    }
    /* Step 4: Store the spelling, then return the old size as its ID while incrementing the count.
     */
    model->vocabulary[model->vocabulary_size] = copy;
    /* Publish the spelling only after all dependent storage exists. */
    return cgai_token_id_from_size(model->vocabulary_size++);
}
