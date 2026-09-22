/** @file test_runner.c @brief Runner for focused internal C module tests. */

#include "test_internal.h"

#include <stdio.h>

int main(void) {
    if (test_tokenizer() != 0 || test_vocabulary() != 0 || test_model_math() != 0 ||
        test_model_io() != 0 || test_sampling() != 0 || test_file_utils() != 0) {
        return 1;
    }
    puts("all internal tests passed");
    return 0;
}