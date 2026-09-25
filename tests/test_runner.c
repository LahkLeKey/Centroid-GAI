/** @file test_runner.c @brief Runner for focused internal C module tests. */

#include "test_internal.h"

#include <stdio.h>

/**
 * @brief Run the focused internal-module suites and report success to CTest.
 *
 * The logical OR chain stops after a nonzero suite result. Assertion helpers may also terminate
 * the process directly, which CTest observes as failure. The success message is emitted only after
 * all suites complete and return zero.
 *
 * @return Zero for complete success, one for a suite failure.
 */
int main(void) {
    /* Step 1: Run tokenizer, vocabulary, math, codec, sampling, and file-I/O suites in sequence. */
    if (test_tokenizer() != 0 || test_vocabulary() != 0 || test_model_math() != 0 ||
        test_model_io() != 0 || test_sampling() != 0 || test_file_utils() != 0 ||
        test_composition() != 0) {
        return 1;
    }
    /* Step 2: Announce success only after every suite has completed. */
    puts("all internal tests passed");
    return 0;
}
