/** @file test_model_io.c @brief Unit tests for in-memory model serialization. */

#include "centroid_gai.h"
#include "internal/model_io.h"
#include "test_utils.h"

#include <stdlib.h>
#include <string.h>

int test_model_io(void) {
    TEST_CHECK(cgai_model_decode(NULL, 0U) == NULL, "NULL model bytes were accepted");
    cgai_model *model = cgai_model_create(NULL);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(cgai_model_train_text(model, "model bytes survive a round trip") == CGAI_STATUS_OK,
               cgai_last_error());

    size_t encoded_size = 0U;
    TEST_CHECK(cgai_model_encode(model, NULL, 0U, &encoded_size) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(encoded_size > 0U, "empty model encoding");
    uint8_t *encoded = (uint8_t *)malloc(encoded_size);
    TEST_CHECK(encoded != NULL, "encoded model allocation failed");
    TEST_CHECK(cgai_model_encode(model, encoded, encoded_size, &encoded_size) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_model_encode(model, NULL, 1U, &encoded_size) == CGAI_STATUS_ERROR,
               "nonzero capacity with NULL output was accepted");
    TEST_CHECK(cgai_model_encode(model, encoded, encoded_size - 1U, &encoded_size) ==
                   CGAI_STATUS_ERROR,
               "short output buffer was accepted");

    cgai_model *decoded = cgai_model_decode(encoded, encoded_size);
    TEST_CHECK(decoded != NULL, cgai_last_error());
    TEST_CHECK(cgai_model_vocabulary_size(decoded) == cgai_model_vocabulary_size(model),
               "vocabulary changed during round trip");
    TEST_CHECK(cgai_model_examples_seen(decoded) == cgai_model_examples_seen(model),
               "example count changed during round trip");
    cgai_model_destroy(decoded);

    TEST_CHECK(cgai_model_decode(encoded, encoded_size - 1U) == NULL,
               "truncated model was accepted");
    encoded[0] = 'X';
    TEST_CHECK(cgai_model_decode(encoded, encoded_size) == NULL, "bad model magic was accepted");
    free(encoded);
    cgai_model_destroy(model);
    return 0;
}