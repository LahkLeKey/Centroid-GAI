/** @file test_utils.h @brief Shared assertions for native C tests. */

#ifndef CGAI_TEST_UTILS_H
#define CGAI_TEST_UTILS_H

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
                         const char *error_message);

/**
 * @brief Check a condition exactly once and fail the test process if it is false.
 *
 * The conditional expression evaluates only its chosen arm, so the diagnostic
 * expression is evaluated only on failure. The preprocessor's # operator converts
 * the condition's source spelling into text; __FILE__ and __LINE__ supply location.
 * Unlike the standard assert macro, this check remains active in release builds.
 * @param condition Expression whose nonzero value means the check passed.
 * @param error_message Diagnostic expression evaluated only if condition is false.
 */
#define TEST_CHECK(condition, error_message)                                                       \
    ((condition) ? (void)0 : test_fail(#condition, __FILE__, __LINE__, (error_message)))

#endif
