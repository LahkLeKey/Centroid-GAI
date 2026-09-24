/** @file test_vocabulary.c @brief Unit tests for vocabulary growth and lookup. */

#include "centroid_gai.h"
#include "internal/cgai_internal.h"
#include "internal/vocabulary.h"
#include "test_utils.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief Force vocabulary capacity growth by inserting many distinct spellings.
 *
 * The stack token buffer is reused every iteration; successful insertion must therefore copy its
 * contents. After forty additions, the model must have grown beyond its initial pointer capacity.
 * The helper mutates but does not destroy the caller-owned fixture.
 *
 * @param model Borrowed mutable fixture already containing reserved tokens and the example entry.
 */
static void test_vocabulary_growth(cgai_model *model) {
    /* Step 1: Construct and insert forty unique spellings using the same temporary character
     * buffer. */
    for (size_t i = 0; i < 40U; ++i) {
        char token[32];
        (void)snprintf(token, sizeof(token), "token-%zu", i);
        TEST_CHECK(cgai_token_id_is_valid(cgai_vocabulary_add(model, token)),
                   "vocabulary growth failed");
    }
    /* Step 2: Confirm the occupied vocabulary count reflects growth beyond the original capacity.
     */
    TEST_CHECK(model->vocabulary_size >= 44U, "vocabulary did not grow past initial capacity");
}

/**
 * @brief Check reserved IDs, lookup failure, deduplication, and ownership during growth.
 *
 * A freshly created model contains the three control spellings. Adding the same ordinary spelling
 * twice must return one stable ID, and its stored bytes must survive independently of input
 * storage. The growth helper then exercises reallocation and count-table expansion.
 *
 * @return Zero after all vocabulary invariants are checked.
 */
int test_vocabulary(void) {
    /* Step 1: Create a fixture and verify reserved entries and the invalid-ID lookup sentinel. */
    cgai_model *model = cgai_model_create(NULL);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(model->vocabulary_size == 3U, "special vocabulary entries are missing");
    TEST_CHECK(cgai_vocabulary_find(model, "<bos>").value == CGAI_TOKEN_BOS,
               "BOS identifier is unstable");
    TEST_CHECK(cgai_vocabulary_find(model, "missing").value == SIZE_MAX, "missing token was found");

    /* Step 2: Insert the same spelling twice and compare identifiers, stored bytes, and count
     * storage. */
    const cgai_token_id first = cgai_vocabulary_add(model, "example");
    const cgai_token_id second = cgai_vocabulary_add(model, "example");
    TEST_CHECK(cgai_token_id_is_valid(first), "new token was rejected");
    TEST_CHECK(first.value == second.value, "duplicate token changed identifier");
    TEST_CHECK(strcmp(model->vocabulary[first.value], "example") == 0,
               "vocabulary text was not preserved");
    TEST_CHECK(model->token_counts != NULL, "token counts were not allocated");

    /* Step 3: Exercise larger growth, then destroy the entire fixture. */
    test_vocabulary_growth(model);

    cgai_model_destroy(model);
    return 0;
}
