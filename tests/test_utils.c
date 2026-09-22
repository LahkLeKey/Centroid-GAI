/** @file test_utils.c @brief Shared assertions for native C tests. */

#include "test_utils.h"

#include <stdio.h>

void test_fail(const char *expression, const char *file, int line, const char *error_message) {
    fprintf(stderr, "check failed at %s:%d: %s (%s)\n", file, line, expression,
            error_message != NULL ? error_message : "no error");
}