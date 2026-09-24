/** @file test_model_math.c @brief Unit tests for embedding and centroid math. */

#include "centroid_gai.h"
#include "internal/cgai_internal.h"
#include "internal/model_centroid.h"
#include "internal/model_embedding.h"
#include "internal/model_random.h"
#include "internal/vocabulary.h"
#include "test_utils.h"

#include <math.h>

/**
 * @brief Verify equal RNG states produce the same next sample.
 *
 * The two state variables are independent stack values initialized identically. The RNG advances
 * each through its pointer, so comparing returned values checks reproducibility without sharing
 * mutable state between the two calls.
 *
 */
static void test_random_sequence(void) {
    /* Step 1: Initialize two independent states to the same seed. */
    uint64_t first = 7U;
    uint64_t second = 7U;
    /* Step 2: Advance each once and require equal returned words. */
    TEST_CHECK(cgai_random_next(&first) == cgai_random_next(&second),
               "random generator is not deterministic");
}

/**
 * @brief Check a context vector and the centroid selected from it.
 *
 * The fixture must have eight dimensions because both local float arrays have that length.
 * A BOS/UNKNOWN history exercises reserved-token embeddings. Squared length is accumulated in
 * double precision and must be finite and positive before the nearest-centroid result is checked.
 *
 * @param model Borrowed trained fixture configured for eight dimensions.
 */
static void test_context_embedding(const cgai_model *model) {
    /* Step 1: Build a short history from reserved IDs and prepare dimension-sized stack arrays. */
    const cgai_token_id history[] = {cgai_token_id_from_size(CGAI_TOKEN_BOS),
                                     cgai_token_id_from_size(CGAI_TOKEN_UNKNOWN)};
    float output[8];
    float scratch[8];
    /* Step 2: Compute the context vector into caller-owned storage. */
    cgai_context_embedding(model, history, 2U, output, scratch);
    /* Step 3: Sum squared components and verify that the vector and chosen centroid are usable. */
    double length = 0.0;
    for (size_t i = 0; i < model->config.dimensions; ++i) {
        length += (double)output[i] * (double)output[i];
    }
    TEST_CHECK(isfinite(length) && length > 0.0, "context embedding was empty");
    TEST_CHECK(cgai_nearest_centroid(model, output).value < model->initialized_centroids,
               "nearest centroid was not initialized");
}

/**
 * @brief Build a small fixture for context, centroid, and RNG checks.
 *
 * Training initializes the centroid rows required by nearest-centroid lookup. The test helpers
 * borrow the model, while the runner retains responsibility for cleanup. RNG testing uses its own
 * state variables and does not alter model state.
 *
 * @return Zero after all mathematical checks pass.
 */
int test_model_math(void) {
    /* Step 1: Configure an eight-dimensional fixture with a small centroid capacity. */
    cgai_config config = cgai_default_config();
    config.dimensions = 8U;
    config.centroid_count = 3U;
    /* Step 2: Create and train the model before testing nearest-centroid behavior. */
    cgai_model *model = cgai_model_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(cgai_model_train_text(model, "red fox blue bird") == CGAI_STATUS_OK,
               cgai_last_error());

    /* Step 3: Run context/centroid checks and independent RNG reproducibility checks. */
    test_context_embedding(model);

    test_random_sequence();

    /* Step 4: Release the fixture after all checks complete. */
    cgai_model_destroy(model);
    return 0;
}
