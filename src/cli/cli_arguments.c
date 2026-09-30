/** @file cli_arguments.c @brief Shared numeric command-line parsing. */

#include "internal/cli_commands.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * @brief Convert a fully consumed decimal argument into a positive native size.
 *
 * strtoull returns both a numeric result and an end pointer. errno detects reported conversion
 * range errors; comparing end with text detects an input with no digits. This function follows
 * strtoull's lexical rules, including accepted whitespace/sign prefixes, rather than implementing
 * a digits-only parser. The destination is left unchanged unless all checks pass.
 *
 * @param text Non-NULL borrowed NUL-terminated numeric text.
 * @param value Non-NULL writable destination for a positive size_t value.
 * @return One after storing an accepted value, otherwise zero.
 */
int cgai_cli_parse_size(const char *text, size_t *value) {
    /* Step 1: Prepare an end pointer and clear stale errno before conversion. */
    char *end = NULL;
    errno = 0;
    /* Step 2: Parse in base ten and retain the wider intermediate value for range checks. */
    const unsigned long long parsed = strtoull(text, &end, 10);
    /* Step 3: Require successful conversion, complete consumption, a nonzero value, and size_t fit.
     */
    if (errno != 0 || end == text || *end != '\0' || parsed == 0U || parsed > SIZE_MAX) {
        return 0;
    }
    /* Step 4: Publish the narrowed value only after validation. */
    *value = (size_t)parsed;
    return 1;
}
