/** @file tokenizer.h @brief Private tokenization interface. */

#ifndef CGAI_TOKENIZER_H
#define CGAI_TOKENIZER_H

#include "centroid_gai.h"

#include <stddef.h>

/**
 * @brief Growable list of normalized token strings.
 * @note Ownership: The list owns every string and the pointer array until destruction.
 */
typedef struct cgai_token_list {
    char **items;    /**< List-owned pointer array containing independently owned C strings. */
    size_t count;    /**< Number of initialized entries in @p items. */
    size_t capacity; /**< Allocated entry capacity of @p items. */
} cgai_token_list;

/**
 * @brief Append normalized word and punctuation tokens from a C string.
 *
 * Callers normally pass a zero-initialized list. Scanning borrows input bytes, while each emitted
 * spelling is copied into list-owned storage. On allocation failure the entire supplied list is
 * destroyed and reset, including any entries it already contained. Empty or whitespace-only input
 * succeeds without adding entries; the training layer separately rejects an empty corpus.
 *
 * @param text Non-NULL NUL-terminated source string, borrowed for this call.
 * @param tokens Non-NULL initialized list receiving owned strings.
 * @return CGAI_STATUS_OK after scanning, otherwise CGAI_STATUS_ERROR with an empty destination
 * list.
 */
cgai_status cgai_tokenize(const char *text, cgai_token_list *tokens);

/**
 * @brief Free all token spellings and reset their owning list.
 *
 * Each item is an independent heap string, and items is another allocation holding those pointers.
 * The list structure itself remains caller-owned. Zeroing its fields after cleanup allows reuse
 * and repeated destruction of that same list, including a partially filled list.
 *
 * @param tokens Non-NULL initialized or zero-initialized list whose allocations belong to this
 * caller.
 */
void cgai_token_list_destroy(cgai_token_list *tokens);

#endif
