/** @file test_utils.h @brief Shared assertions for native C tests. */

#ifndef CGAI_TEST_UTILS_H
#define CGAI_TEST_UTILS_H

/** Prints one failed assertion with its source location and library error. */
void test_fail(const char *expression, const char *file, int line, const char *error_message);

/** Fails the current test function when a condition is false. */
#define TEST_CHECK(condition, error_message)                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            test_fail(#condition, __FILE__, __LINE__, (error_message));                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#endif