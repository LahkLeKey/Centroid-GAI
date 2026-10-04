/** @file test_encoded_generation.c @brief Encoded prefix, stopping and failure boundaries. */
#include "internal/neural_internal.h"
#include "neural/neural_generation.h"
#include "test_utils.h"
#include <string.h>

/** @brief Create two experts selected by the first encoded position alone.
 * @return Owned fixture whose positive prefix selects ordinary output over EOS. */
static cgai_neural_model *prefix_model(void) {
    const cgai_neural_config config = {1U, 1U, 2U, 3U, 42U, 1.0};
    cgai_neural_model *model = cgai_neural_create(&config, "prefix emit");
    TEST_CHECK(model != NULL, cgai_last_error());
    memset(model->parameters, 0, model->parameter_count * sizeof(*model->parameters));
    model->embeddings[cgai_neural_lookup(model, "prefix").value] = 1.0;
    model->embeddings[cgai_neural_lookup(model, "emit").value] = -1.0;
    model->encoder[0] = 1.0;
    model->centroids[0] = 1.0;
    model->centroids[1] = -1.0;
    model->logits[cgai_neural_lookup(model, "emit").value] = 30.0;
    model->logits[model->vocabulary_size + CGAI_TOKEN_EOS] = 30.0;
    return model;
}

/** @brief Construct one valid borrowed request with a persistent prefix.
 * @param context Borrowed three-slot initialized context.
 * @param output Writable independent destination.
 * @param capacity Positive output byte capacity.
 * @return Request borrowing its arguments without allocation. */
static cgai_neural_encoded_request encoded_request(const cgai_token_id *context, char *output,
                                                   size_t capacity) {
    return (cgai_neural_encoded_request){context, 1U, 12U, 0.0, 42U, output, capacity};
}

/** @brief Verify the fixed prefix survives every shift and repetition is bounded.
 * @param model Borrowed context-sensitive immutable fixture.
 * @param context Borrowed original context that generation must not overwrite. */
static void fixed_prefix(const cgai_neural_model *model, const cgai_token_id *context) {
    cgai_token_id saved[3];
    memcpy(saved, context, sizeof(saved));
    char output[128];
    const cgai_neural_encoded_request request = encoded_request(context, output, sizeof(output));
    cgai_neural_encoded_result result = {0};
    TEST_CHECK(cgai_neural_generate_encoded(model, &request, &result), cgai_last_error());
    TEST_CHECK(strcmp(output, "emit emit emit emit emit emit emit emit") == 0 &&
                   result.generated_tokens == 8U && result.forward_passes == 8U &&
                   result.finish == CGAI_NEURAL_ENCODED_REPETITION,
               "generation shifted the fixed prefix or lost bounded repetition");
    TEST_CHECK(memcmp(context, saved, sizeof(saved)) == 0, "generation modified borrowed context");
}

/** @brief Contrast rolling-only history and zero-work stopping accounting.
 * @param model Borrowed context-sensitive immutable fixture.
 * @param context Borrowed positive-prefix initial context. */
static void rolling_and_zero(const cgai_neural_model *model, const cgai_token_id *context) {
    char output[128];
    cgai_neural_encoded_request request = encoded_request(context, output, sizeof(output));
    cgai_neural_encoded_result result = {0};
    request.fixed = 0U;
    TEST_CHECK(cgai_neural_generate_encoded(model, &request, &result), cgai_last_error());
    TEST_CHECK(strcmp(output, "emit") == 0 && result.generated_tokens == 1U &&
                   result.forward_passes == 2U && result.finish == CGAI_NEURAL_ENCODED_EOS,
               "rolling continuation did not consume its stopping EOS forward pass");
    request.max_tokens = 0U;
    TEST_CHECK(cgai_neural_generate_encoded(model, &request, &result), cgai_last_error());
    TEST_CHECK(output[0] == '\0' && result.generated_tokens == 0U && result.forward_passes == 0U &&
                   result.finish == CGAI_NEURAL_ENCODED_LIMIT,
               "zero-token request performed forward work or retained old output");
}

/** @brief Preserve full-token prefixes and success-only result ownership on failure.
 * @param model Borrowed context-sensitive immutable fixture.
 * @param context Borrowed positive-prefix initial context. */
static void capacity_failure(const cgai_neural_model *model, const cgai_token_id *context) {
    char output[6] = "old";
    const cgai_neural_encoded_request request = encoded_request(context, output, sizeof(output));
    cgai_neural_encoded_result result = {91U, 92U, CGAI_NEURAL_ENCODED_EOS};
    TEST_CHECK(!cgai_neural_generate_encoded(model, &request, &result),
               "accepted a destination too small for the second token");
    TEST_CHECK(strcmp(output, "emit") == 0 && result.generated_tokens == 91U &&
                   result.forward_passes == 92U && result.finish == CGAI_NEURAL_ENCODED_EOS,
               "failed append damaged the terminated prefix or published success accounting");
}

/** @brief Reject a fixed window that leaves no rolling slot before touching outputs.
 * @param model Borrowed immutable fixture.
 * @param context Borrowed initialized source context. */
static void invalid_window(const cgai_neural_model *model, const cgai_token_id *context) {
    char output[16] = "unchanged";
    cgai_neural_encoded_request request = encoded_request(context, output, sizeof(output));
    cgai_neural_encoded_result result = {91U, 92U, CGAI_NEURAL_ENCODED_EOS};
    request.fixed = model->config.context_window;
    TEST_CHECK(!cgai_neural_generate_encoded(model, &request, &result),
               "accepted a fixed prefix without a rolling slot");
    TEST_CHECK(strcmp(output, "unchanged") == 0 && result.generated_tokens == 91U &&
                   result.forward_passes == 92U && result.finish == CGAI_NEURAL_ENCODED_EOS,
               "invalid request changed output or accounting");
}

/** @brief Run private encoded-generation boundary checks.
 * @return Zero when the independent fixture expectations hold. */
int main(void) {
    cgai_neural_model *model = prefix_model();
    const cgai_token_id context[] = {
        cgai_neural_lookup(model, "prefix"), {CGAI_TOKEN_BOS}, {CGAI_TOKEN_BOS}};
    fixed_prefix(model, context);
    rolling_and_zero(model, context);
    capacity_failure(model, context);
    invalid_window(model, context);
    cgai_neural_destroy(model);
    return 0;
}
