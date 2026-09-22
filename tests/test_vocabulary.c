/** @file test_vocabulary.c @brief Unit tests for vocabulary growth and lookup. */

#include "centroid_gai.h"
#include "internal/cgai_internal.h"
#include "internal/vocabulary.h"
#include "test_utils.h"

#include <stdio.h>
#include <string.h>

int test_vocabulary(void) {
    cgai_model *model = cgai_model_create(NULL);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(model->vocabulary_size == 3U, "special vocabulary entries are missing");
    TEST_CHECK(cgai_vocabulary_find(model, "<bos>").value == CGAI_TOKEN_BOS,
               "BOS identifier is unstable");
    TEST_CHECK(cgai_vocabulary_find(model, "missing").value == SIZE_MAX, "missing token was found");

    const cgai_token_id first = cgai_vocabulary_add(model, "example");
    const cgai_token_id second = cgai_vocabulary_add(model, "example");
    TEST_CHECK(cgai_token_id_is_valid(first), "new token was rejected");
    TEST_CHECK(first.value == second.value, "duplicate token changed identifier");
    TEST_CHECK(strcmp(model->vocabulary[first.value], "example") == 0,
               "vocabulary text was not preserved");
    TEST_CHECK(model->token_counts != NULL, "token counts were not allocated");

    for (size_t i = 0; i < 40U; ++i) {
        char token[32];
        (void)snprintf(token, sizeof(token), "token-%zu", i);
        TEST_CHECK(cgai_token_id_is_valid(cgai_vocabulary_add(model, token)),
                   "vocabulary growth failed");
    }
    TEST_CHECK(model->vocabulary_size >= 44U, "vocabulary did not grow past initial capacity");

    cgai_model_destroy(model);
    return 0;
}