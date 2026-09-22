/** @file tokenizer.h @brief Private tokenization interface. */

#ifndef CGAI_TOKENIZER_H
#define CGAI_TOKENIZER_H

#include "centroid_gai.h"

#include <stddef.h>

/**
 * @brief Growable list of normalized token strings.
 * @ownership The list owns every string and the pointer array until destruction.
 */
typedef struct cgai_token_list {
    char **items;    /**< Caller-owned NUL-terminated token strings. */
    size_t count;    /**< Number of initialized entries in @p items. */
    size_t capacity; /**< Allocated entry capacity of @p items. */
} cgai_token_list;

/**
 * @brief Tokenizes text into lowercase words and punctuation tokens.
 * @param text NUL-terminated input; it remains caller-owned.
 * @param tokens Empty or reusable list receiving allocated token strings.
 * @return CGAI_STATUS_OK, or an error after cleaning partially created tokens.
 * @note Apostrophes remain inside word tokens and high-bit bytes are preserved.
 */
cgai_status cgai_tokenize(const char *text, cgai_token_list *tokens);

/**
 * @brief Releases every string and array owned by a token list.
 * @param tokens List to clear; NULL is not accepted.
 * @post All fields are reset to zero, making repeated cleanup safe.
 */
void cgai_token_list_destroy(cgai_token_list *tokens);

#endif
