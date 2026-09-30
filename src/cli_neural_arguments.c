/** @file cli_neural_arguments.c @brief Strict neural CLI numeric parsing and corpus loading. */
#include "internal/cli_neural.h"
#include "internal/file_utils.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Parse decimal digits with an explicit upper bound before multiplication.
 *
 * Manual decimal accumulation rejects signs, whitespace, suffixes, and overflow uniformly
 * across platforms whose unsigned long long or size_t widths may differ.
 * @param text Borrowed terminated numeric argument, never NULL.
 * @param maximum Largest accepted result.
 * @param value Borrowed writable destination, changed only on success.
 * @return Nonzero for a nonempty valid decimal argument, otherwise zero.
 */
int cgai_cli_neural_unsigned(const char *text, uint64_t maximum, uint64_t *value) {
    /* Step 1: Accumulate only digits while proving the next multiply/add is bounded. */
    uint64_t parsed = 0U;
    if (*text == '\0')
        return 0;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9')
            return 0;
        const uint64_t digit = (uint64_t)(*cursor - '0');
        if (digit > maximum || parsed > (maximum - digit) / 10U)
            return 0;
        parsed = parsed * 10U + digit;
    }
    /* Step 2: Publish only after the complete argument passed validation. */
    *value = parsed;
    return 1;
}

/**
 * @brief Parse a fully consumed nonnegative finite floating-point value.
 *
 * Range errors, nonfinite spellings, signs, and leading whitespace are rejected. Scientific
 * notation remains available for small learning rates such as 1e-3.
 * @param text Borrowed terminated numeric argument, never NULL.
 * @param maximum Inclusive upper bound.
 * @param value Borrowed writable destination, unchanged on failure.
 * @return Nonzero after an accepted conversion, otherwise zero.
 */
int cgai_cli_neural_real(const char *text, double maximum, double *value) {
    /* Step 1: Require an unsigned numeric start before asking strtod to parse. */
    if ((*text < '0' || *text > '9') && *text != '.')
        return 0;
    char *end = NULL;
    errno = 0;
    const double parsed = strtod(text, &end);
    /* Step 2: Reject incomplete, nonfinite, underflowed, or out-of-range results. */
    if (end == text || *end != '\0' || errno != 0 || !isfinite(parsed) || parsed < 0.0 ||
        parsed > maximum)
        return 0;
    *value = parsed;
    return 1;
}

/**
 * @brief Read a text file into owned storage that can safely be passed to string APIs.
 *
 * The shared reader appends a terminator. Embedded NUL bytes would silently truncate training
 * or evaluation, so this helper rejects them and releases the allocation.
 * @param path Borrowed terminated corpus path.
 * @return Owned terminated text to free(), or NULL after a stderr diagnostic.
 */
char *cgai_cli_neural_text(const char *path) {
    /* Step 1: Acquire the whole-file buffer and its payload length. */
    uint8_t *data = NULL;
    size_t size = 0U;
    if (cgai_file_read_all(path, &data, &size) != CGAI_STATUS_OK) {
        fprintf(stderr, "could not read %s: %s\n", path, cgai_last_error());
        return NULL;
    }
    /* Step 2: Preserve exact corpus boundaries by rejecting an early string terminator. */
    if (memchr(data, '\0', size) != NULL) {
        fprintf(stderr, "text corpus contains a NUL byte: %s\n", path);
        free(data);
        return NULL;
    }
    return (char *)data;
}
