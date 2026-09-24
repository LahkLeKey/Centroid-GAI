/** @file test_utils.c @brief Shared assertions for native C tests. */

#include "test_utils.h"

#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Print a failed assertion's context and terminate the test process.
 *
 * The assertion macro supplies expression text and source position, while each caller supplies
 * its relevant diagnostic. _Noreturn tells the compiler that control never resumes after this
 * function, so failed prerequisites cannot lead to later dereferences of invalid fixtures.
 *
 * @param expression Borrowed NUL-terminated source text of the failed condition.
 * @param file Borrowed NUL-terminated source filename.
 * @param line One-based source line containing the failed assertion.
 * @param error_message Borrowed diagnostic string, or NULL to print the fallback.
 */
_Noreturn void test_fail(const char *expression, const char *file, int line,
                         const char *error_message) {
    /* Step 1: Print source location, failed condition, and the available diagnostic to stderr. */
    fprintf(stderr, "check failed at %s:%d: %s (%s)\n", file, line, expression,
            error_message != NULL ? error_message : "no error");
    /* Step 2: Terminate this executable so the test runner observes failure immediately. */
    exit(EXIT_FAILURE);
}
