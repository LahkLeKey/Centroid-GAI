/** @file tokenizer.c @brief Minimal UTF-8-preserving word and punctuation tokenizer. */

#include "internal/tokenizer.h"

#include "internal/constants.h"
#include "internal/error.h"
#include "internal/size_utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void cgai_token_list_destroy(cgai_token_list *tokens) {
    for (size_t i = 0; i < tokens->count; ++i) {
        /* Each token string is independently allocated by make_token(). */
        free(tokens->items[i]);
    }
    /* The pointer array is separate from the strings it references. */
    free(tokens->items);
    /* Reset all fields so repeated cleanup is harmless and observable. */
    memset(tokens, 0, sizeof(*tokens));
}

/** Grows the token-pointer array when the next token needs a slot. */
static cgai_status ensure_token_capacity(cgai_token_list *tokens) {
    /* Reuse existing capacity whenever possible. */
    if (tokens->count < tokens->capacity) {
        return CGAI_STATUS_OK;
    }
    size_t capacity = tokens->capacity == 0U ? CGAI_INITIAL_TOKEN_CAPACITY : tokens->capacity * 2U;
    /* Compute bytes through the checked helper before calling realloc(). */
    size_t bytes = 0U;
    if ((tokens->capacity != 0U && tokens->capacity > SIZE_MAX / 2U) ||
        !cgai_size_mul(capacity, sizeof(char *), &bytes)) {
        return cgai_fail("token list is too large");
    }
    char **items = (char **)realloc(tokens->items, bytes);
    if (items == NULL) {
        return cgai_fail("could not allocate tokens");
    }
    tokens->items = items;
    /* Publish the new pointer and capacity only after realloc succeeds. */
    tokens->capacity = capacity;
    return CGAI_STATUS_OK;
}

/** Allocates and normalizes one source token span. */
static char *make_token(const char *start, size_t length) {
    /* Include one byte for the string terminator in the allocation request. */
    size_t token_bytes = 0U;
    if (!cgai_size_add(length, 1U, &token_bytes)) {
        (void)cgai_fail("token is too large");
        return NULL;
    }
    char *token = (char *)malloc(token_bytes);
    if (token == NULL) {
        (void)cgai_fail("could not allocate token");
        return NULL;
    }
    for (size_t i = 0; i < length; ++i) {
        /* Normalize ASCII letters while preserving non-ASCII bytes verbatim. */
        token[i] = (char)tolower((unsigned char)start[i]);
    }
    token[length] = '\0';
    return token;
}

/** Advances past whitespace and returns the next nonempty token span. */
static const char *next_token_span(const char *cursor, const char **start, size_t *length) {
    /* Whitespace is a separator and never becomes a token. */
    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor == '\0') {
        /* Signal end-of-input with an empty span. */
        *start = cursor;
        *length = 0U;
        return cursor;
    }
    *start = cursor;
    if (isalnum((unsigned char)*cursor) || (unsigned char)*cursor >= CGAI_NON_ASCII_BYTE ||
        *cursor == '\'') {
        /* Word spans include letters, high-byte UTF-8 data, and apostrophes. */
        do {
            ++cursor;
        } while (isalnum((unsigned char)*cursor) || (unsigned char)*cursor >= CGAI_NON_ASCII_BYTE ||
                 *cursor == '\'');
    } else {
        /* Every other byte is emitted as its own punctuation token. */
        ++cursor;
    }
    *length = (size_t)(cursor - *start);
    /* Return the first cursor position not included in this token. */
    return cursor;
}

/** Copies one source span into the token list in normalized form. */
static cgai_status token_list_push(cgai_token_list *tokens, const char *start, size_t length) {
    if (ensure_token_capacity(tokens) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    char *token = make_token(start, length);
    if (token == NULL) {
        return CGAI_STATUS_ERROR;
    }
    tokens->items[tokens->count++] = token;
    return CGAI_STATUS_OK;
}

/** Splits text into lowercase word, UTF-8 byte, and punctuation tokens. */
cgai_status cgai_tokenize(const char *text, cgai_token_list *tokens) {
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *start = cursor;
        size_t length = 0U;
        cursor = next_token_span(cursor, &start, &length);
        if (length != 0U && token_list_push(tokens, start, length) != CGAI_STATUS_OK) {
            cgai_token_list_destroy(tokens);
            return CGAI_STATUS_ERROR;
        }
    }
    return CGAI_STATUS_OK;
}
