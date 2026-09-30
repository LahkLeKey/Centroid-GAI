/** @file tokenizer.c @brief Minimal UTF-8-preserving word and punctuation tokenizer. */

#include "internal/tokenizer.h"

#include "internal/constants.h"
#include "internal/error.h"
#include "internal/size_utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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
void cgai_token_list_destroy(cgai_token_list *tokens) {
    /* Step 1: Release every string recorded in the occupied part of the pointer array. */
    for (size_t i = 0; i < tokens->count; ++i) {
        /* Each token string is independently allocated by make_token(). */
        free(tokens->items[i]);
    }
    /* The pointer array is separate from the strings it references. */
    /* Step 2: Release the array of string addresses after the strings are gone. */
    free(tokens->items);
    /* Reset all fields so repeated cleanup is harmless and observable. */
    /* Step 3: Reset count, capacity, and pointer so the descriptor is empty again. */
    memset(tokens, 0, sizeof(*tokens));
}

/**
 * @brief Ensure that appending one more token has an available pointer slot.
 *
 * The list distinguishes occupied count from allocated capacity. Capacity grows geometrically
 * to avoid reallocating on every append. realloc may move the allocation; keeping its result in
 * a temporary pointer preserves the old allocation when growth fails.
 *
 * @param tokens Non-NULL mutable list with count no greater than its current capacity.
 * @return CGAI_STATUS_OK if a slot exists, otherwise CGAI_STATUS_ERROR with existing storage
 * retained.
 */
static cgai_status ensure_token_capacity(cgai_token_list *tokens) {
    /* Reuse existing capacity whenever possible. */
    /* Step 1: Reuse an existing free slot without allocating. */
    if (tokens->count < tokens->capacity) {
        return CGAI_STATUS_OK;
    }
    /* Step 2: Choose initial capacity or double it, then check the byte-size calculation. */
    size_t capacity = tokens->capacity == 0U ? CGAI_INITIAL_TOKEN_CAPACITY : tokens->capacity * 2U;
    /* Compute bytes through the checked helper before calling realloc(). */
    size_t bytes = 0U;
    if ((tokens->capacity != 0U && tokens->capacity > SIZE_MAX / 2U) ||
        !cgai_size_mul(capacity, sizeof(char *), &bytes) || bytes == 0U) {
        return cgai_fail("token list is too large");
    }
    /* Step 3: Attempt growth without overwriting the list's only pointer to existing storage. */
    char **items = (char **)realloc(tokens->items, bytes);
    if (items == NULL) {
        return cgai_fail("could not allocate tokens");
    }
    /* Step 4: Publish the new pointer and capacity only after allocation succeeds. */
    tokens->items = items;
    /* Publish the new pointer and capacity only after realloc succeeds. */
    tokens->capacity = capacity;
    return CGAI_STATUS_OK;
}

/**
 * @brief Copy one source span into an owned, terminated token string.
 *
 * The source span may end before the source string's terminator. This helper therefore copies
 * exactly length bytes and supplies a new NUL terminator. Character conversion receives an
 * unsigned-char value, avoiding undefined ctype behavior for negative signed char bytes.
 * Normalization follows the process C locale; this is not full Unicode case folding.
 *
 * @param start Borrowed readable span containing at least length bytes.
 * @param length Number of source bytes to copy, excluding the new terminator.
 * @return Owned token string to free(), or NULL on size/allocation failure.
 */
static char *make_token(const char *start, size_t length) {
    /* Include one byte for the string terminator in the allocation request. */
    /* Step 1: Account for the extra NUL byte using checked addition. */
    size_t token_bytes = 0U;
    if (!cgai_size_add(length, 1U, &token_bytes)) {
        (void)cgai_fail("token is too large");
        return NULL;
    }
    /* Step 2: Allocate storage owned by the eventual token list. */
    char *token = (char *)malloc(token_bytes);
    if (token == NULL) {
        (void)cgai_fail("could not allocate token");
        return NULL;
    }
    /* Step 3: Copy and lowercase each byte using the C character-conversion rules. */
    for (size_t i = 0; i < length; ++i) {
        /* Normalize ASCII letters while preserving non-ASCII bytes verbatim. */
        token[i] = (char)tolower((unsigned char)start[i]);
    }
    /* Step 4: Terminate the new spelling so string functions can find its end. */
    token[length] = '\0';
    return token;
}

/**
 * @brief Locate the next token without allocating or changing the input.
 *
 * Whitespace is skipped. A word groups alphanumeric bytes, high bytes used in UTF-8 encodings,
 * and apostrophes; other bytes become single-character punctuation tokens. This byte-oriented
 * scanner preserves ordinary UTF-8 sequences in the default C locale but does not validate UTF-8
 * or implement Unicode word segmentation. The returned pointer resumes the next scan.
 *
 * @param cursor Non-NULL position in a borrowed NUL-terminated input string.
 * @param start Non-NULL output receiving the first byte of the selected span.
 * @param length Non-NULL output receiving the span's byte length; zero means end of input.
 * @return Borrowed pointer immediately after the selected token, or to the input terminator.
 */
static const char *next_token_span(const char *cursor, const char **start, size_t *length) {
    /* Whitespace is a separator and never becomes a token. */
    /* Step 1: Advance over separators without producing whitespace tokens. */
    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    /* Step 2: Report an empty span when no non-whitespace byte remains. */
    if (*cursor == '\0') {
        /* Signal end-of-input with an empty span. */
        /* Step 3: Remember the start, then choose word scanning or one-byte punctuation scanning.
         */
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
    /* Step 4: Convert the distance between two pointers into the span's byte count. */
    *length = (size_t)(cursor - *start);
    /* Return the first cursor position not included in this token. */
    return cursor;
}

/**
 * @brief Append an owned copy of a token span to a mutable token list.
 *
 * Capacity is obtained first so the new spelling can be published without another fallible step.
 * The caller's source bytes stay borrowed. Once items[count] receives the new pointer, ownership
 * of that string moves to the list and its destructor becomes responsible for freeing it.
 *
 * @param tokens Non-NULL mutable owning list.
 * @param start Borrowed span with at least length readable bytes.
 * @param length Length of that span, excluding a terminator.
 * @return CGAI_STATUS_OK after append, otherwise CGAI_STATUS_ERROR without adding an item.
 */
static cgai_status token_list_push(cgai_token_list *tokens, const char *start, size_t length) {
    /* Step 1: Reserve a pointer slot before creating a new spelling. */
    if (ensure_token_capacity(tokens) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 2: Allocate and normalize the spelling, returning early if allocation fails. */
    char *token = make_token(start, length);
    if (token == NULL) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 3: Store the owned pointer and advance the occupied-item count together. */
    tokens->items[tokens->count++] = token;
    return CGAI_STATUS_OK;
}

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
cgai_status cgai_tokenize(const char *text, cgai_token_list *tokens) {
    /* Step 1: Start a read-only cursor at the beginning of the source string. */
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *start = cursor;
        size_t length = 0U;
        /* Step 2: Skip separators and identify the next byte span without allocating it. */
        cursor = next_token_span(cursor, &start, &length);
        /* Step 3: Copy nonempty spans; on failure release every token accumulated in the list. */
        if (length != 0U && token_list_push(tokens, start, length) != CGAI_STATUS_OK) {
            cgai_token_list_destroy(tokens);
            return CGAI_STATUS_ERROR;
        }
    }
    /* Step 4: Report success once the cursor reaches the terminating NUL. */
    return CGAI_STATUS_OK;
}
