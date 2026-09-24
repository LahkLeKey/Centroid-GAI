/** @file test_model_io.c @brief Unit tests for in-memory model serialization. */

#include "centroid_gai.h"
#include "internal/model_io.h"
#include "test_utils.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Compare decoded state and reject truncated or corrupted artifacts.
 *
 * The decoded model belongs to this helper. The original model and byte allocation remain owned
 * by the caller. Corrupting the first magic byte is deliberate and permanent for this supplied
 * buffer; callers must not expect it to remain a valid artifact afterward.
 *
 * @param model Borrowed original fixture for counter comparisons.
 * @param encoded Borrowed writable artifact buffer that will be deliberately corrupted.
 * @param encoded_size Complete artifact length, greater than zero.
 */
static void test_decode_round_trip(const cgai_model *model, uint8_t *encoded, size_t encoded_size) {
    /* Step 1: Decode independent model state and compare its vocabulary and learned-transition counters. */
    cgai_model *decoded = cgai_model_decode(encoded, encoded_size);
    TEST_CHECK(decoded != NULL, cgai_last_error());
    TEST_CHECK(cgai_model_vocabulary_size(decoded) == cgai_model_vocabulary_size(model),
               "vocabulary changed during round trip");
    TEST_CHECK(cgai_model_examples_seen(decoded) == cgai_model_examples_seen(model),
               "example count changed during round trip");
    /* Step 2: Release the successfully decoded model before rejection scenarios. */
    cgai_model_destroy(decoded);

    /* Step 3: Check a truncated payload, then overwrite magic and check corruption rejection. */
    TEST_CHECK(cgai_model_decode(encoded, encoded_size - 1U) == NULL,
               "truncated model was accepted");
    encoded[0] = 'X';
    TEST_CHECK(cgai_model_decode(encoded, encoded_size) == NULL, "bad model magic was accepted");
}

/**
 * @brief Exercise size-query encoding, exact buffers, and decoding failures.
 *
 * The encoder's size-query mode allows exact allocation without guessing. Invalid destination
 * combinations and a one-byte-short buffer must fail. The decode helper subsequently corrupts the
 * artifact on purpose, after which the buffer is only freed.
 *
 * @return Zero after all codec checks pass.
 */
int test_model_io(void) {
    /* Step 1: Check missing input and construct a trained source fixture. */
    TEST_CHECK(cgai_model_decode(NULL, 0U) == NULL, "NULL model bytes were accepted");
    cgai_model *model = cgai_model_create(NULL);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(cgai_model_train_text(model, "model bytes survive a round trip") == CGAI_STATUS_OK,
               cgai_last_error());

    /* Step 2: Measure the artifact, allocate exactly enough bytes, and encode it. */
    size_t encoded_size = 0U;
    TEST_CHECK(cgai_model_encode(model, NULL, 0U, &encoded_size) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(encoded_size > 0U, "empty model encoding");
    uint8_t *encoded = (uint8_t *)malloc(encoded_size);
    TEST_CHECK(encoded != NULL, "encoded model allocation failed");
    TEST_CHECK(cgai_model_encode(model, encoded, encoded_size, &encoded_size) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 3: Exercise invalid query capacity and short destination rejection. */
    TEST_CHECK(cgai_model_encode(model, NULL, 1U, &encoded_size) == CGAI_STATUS_ERROR,
               "nonzero capacity with NULL output was accepted");
    TEST_CHECK(cgai_model_encode(model, encoded, encoded_size - 1U, &encoded_size) ==
                   CGAI_STATUS_ERROR,
               "short output buffer was accepted");

    /* Step 4: Verify decode behavior, then release artifact and model ownership. */
    test_decode_round_trip(model, encoded, encoded_size);

    free(encoded);
    cgai_model_destroy(model);
    return 0;
}
