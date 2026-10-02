/** @file test_neural.c @brief Neural model fixtures, input validation, and test entry point. */
#include "test_neural.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Create an owned small deterministic model for numerical checks.
 *
 * The compact shape keeps a full parameter-by-parameter gradient check inexpensive.
 * The vocabulary includes controls and three normalized ordinary spellings.
 * @return Owned initialized model; assertions terminate on allocation failure. */
cgai_neural_model *cgai_test_neural_fixture(void) {
    /* Step 1: Select a small shape and reproducible seed. */
    cgai_neural_config config = cgai_neural_default_config();
    config.embedding_dimensions = 2U;
    config.hidden_dimensions = 3U;
    config.centroid_count = 3U;
    config.context_window = 2U;
    config.seed = 73U;
    /* Step 2: Construct and publish the owned fixture after checking allocation. */
    cgai_neural_model *model = cgai_neural_create(&config, "a b left");
    TEST_CHECK(model != NULL, cgai_last_error());
    return model;
}

/** @brief Reject invalid dimensions and nonfinite routing settings before allocation.
 *
 * Every constructor call is expected to fail; no model ownership escapes this helper. */
static void cgai_test_neural_bad_config(void) {
    /* Step 1: Check invalid dimensions and scalar routing temperatures independently. */
    cgai_neural_config config = cgai_neural_default_config();
    config.embedding_dimensions = 0U;
    TEST_CHECK(cgai_neural_create(&config, "a") == NULL, "zero dimensions accepted");
    config.embedding_dimensions = 65U;
    TEST_CHECK(cgai_neural_create(&config, "a") == NULL, "oversized dimensions accepted");
    config = cgai_neural_default_config();
    config.routing_temperature = NAN;
    TEST_CHECK(cgai_neural_create(&config, "a") == NULL, "NaN routing accepted");
    /* Step 2: Check missing vocabulary text and null accessors. */
    TEST_CHECK(cgai_neural_create(NULL, NULL) == NULL, "null training text accepted");
    TEST_CHECK(cgai_neural_create(NULL, "") == NULL, "empty vocabulary accepted");
    TEST_CHECK(cgai_neural_vocabulary_size(NULL) == 0U, "null vocabulary size was nonzero");
    cgai_neural_destroy(NULL);
}

/** @brief Reject invalid operations without consuming the valid fixture.
 *
 * Invalid numeric controls and empty training sequences must report failure.
 * @param model Borrowed initialized fixture which remains owned by its caller. */
static void cgai_test_neural_bad_training(cgai_neural_model *model) {
    /* Step 1: Reject invalid learning controls before updating the model. */
    cgai_neural_training training = cgai_neural_default_training();
    training.learning_rate = NAN;
    TEST_CHECK(cgai_neural_train(model, "a", &training) == CGAI_STATUS_ERROR,
               "NaN learning rate accepted");
    training = cgai_neural_default_training();
    training.epochs = 0U;
    TEST_CHECK(cgai_neural_train(model, "a", &training) == CGAI_STATUS_ERROR,
               "zero epochs accepted");
    training = cgai_neural_default_training();
    training.gradient_clip = 0.0;
    TEST_CHECK(cgai_neural_train(model, "a", &training) == CGAI_STATUS_ERROR, "zero clip accepted");
    /* Step 2: Reject missing handles and empty sequences. */
    TEST_CHECK(cgai_neural_train(model, "", NULL) == CGAI_STATUS_ERROR,
               "empty training sequence accepted");
    TEST_CHECK(cgai_neural_train(NULL, "a", NULL) == CGAI_STATUS_ERROR,
               "null training handle accepted");
}

/** @brief Check generation limits, buffers, and an empty prompt.
 *
 * A zero-token request must return an empty terminated string. Invalid calls are
 * required to return errors without reading beyond caller-owned arrays.
 * @param model Borrowed initialized fixture which remains owned by its caller. */
static void cgai_test_neural_generation(cgai_neural_model *model) {
    /* Step 1: Verify the documented zero-token case with an empty prompt. */
    char output[128] = "sentinel";
    TEST_CHECK(cgai_neural_generate(model, "", 0U, 0.0, 3U, output, sizeof(output)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(output[0] == '\0', "zero-token generation produced text");
    /* Step 2: Reject invalid scalar and buffer arguments. */
    TEST_CHECK(cgai_neural_generate(model, "a", 2U, NAN, 3U, output, sizeof(output)) ==
                   CGAI_STATUS_ERROR,
               "NaN sampling accepted");
    TEST_CHECK(cgai_neural_generate(model, "a", 2U, -1.0, 3U, output, sizeof(output)) ==
                   CGAI_STATUS_ERROR,
               "negative sampling accepted");
    TEST_CHECK(cgai_neural_generate(model, "a", 2U, 0.0, 3U, output, 0U) == CGAI_STATUS_ERROR,
               "zero-size buffer accepted");
    TEST_CHECK(cgai_neural_generate(model, NULL, 2U, 0.0, 3U, output, sizeof(output)) ==
                   CGAI_STATUS_ERROR,
               "null prompt accepted");
}

/** @brief Verify evaluation uses a frozen vocabulary and normalized token spellings.
 *
 * The held-out spelling must count as unknown without adding a vocabulary entry.
 * Only successful evaluations may publish metrics into caller-owned output storage.
 * @param model Borrowed initialized fixture which remains owned by its caller. */
static void cgai_test_neural_evaluation(cgai_neural_model *model) {
    /* Step 1: Evaluate a known uppercase spelling and one unseen spelling. */
    cgai_neural_metrics metrics = {0U, 0U, 0.0, 0.0, 0.0};
    const size_t vocabulary_size = cgai_neural_vocabulary_size(model);
    TEST_CHECK(cgai_neural_evaluate(model, "A unseen", &metrics) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(metrics.tokens == 3U && metrics.unknown_tokens == 1U,
               "target or unknown accounting is incorrect");
    TEST_CHECK(cgai_neural_vocabulary_size(model) == vocabulary_size,
               "held-out text leaked into vocabulary");
    TEST_CHECK(isfinite(metrics.cross_entropy) && metrics.cross_entropy > 0.0,
               "invalid evaluation loss");
    TEST_CHECK(fabs(metrics.perplexity - exp(metrics.cross_entropy)) < 1e-10,
               "perplexity does not match cross entropy");
    /* Step 2: Reject invalid outputs and empty evaluation without publishing metrics. */
    const cgai_neural_metrics saved = metrics;
    TEST_CHECK(cgai_neural_evaluate(model, "", &metrics) == CGAI_STATUS_ERROR,
               "empty evaluation accepted");
    TEST_CHECK(metrics.tokens == saved.tokens && metrics.cross_entropy == saved.cross_entropy,
               "failed evaluation published metrics");
    TEST_CHECK(cgai_neural_evaluate(model, "a", NULL) == CGAI_STATUS_ERROR,
               "null metrics accepted");
}

/** @brief Run every neural model scenario independently of the legacy engine tests.
 *
 * Shared assertions remain active in release builds and terminate on the first
 * failure, so no later helper uses an invalid prerequisite.
 * @return Zero after all assertions pass. */
int main(void) {
    /* Step 1: Check public validation and ownership using a compact model. */
    cgai_test_neural_bad_config();
    cgai_neural_model *model = cgai_test_neural_fixture();
    cgai_test_neural_bad_training(model);
    cgai_test_neural_generation(model);
    cgai_test_neural_evaluation(model);
    cgai_neural_destroy(model);
    /* Step 2: Exercise calculus, actual learning, and durable model state. */
    cgai_test_neural_math();
    cgai_test_neural_training();
    cgai_test_neural_io();
    cgai_test_neural_session();
    return 0;
}
