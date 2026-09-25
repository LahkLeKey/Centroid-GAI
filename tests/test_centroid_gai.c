/** @file test_centroid_gai.c @brief Public model API integration tests. */

#include "centroid_gai.h"
#include "test_utils.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, cgai_last_error())

/**
 * @brief Verify saving and loading preserves a known greedy continuation.
 *
 * The original fixture is borrowed. This helper owns a loaded copy and creates a temporary artifact
 * in the test working directory. Removing that file also enables an explicit missing-file check.
 * A successful comparison shows observable generation survives the file round trip.
 *
 * @param model Borrowed trained model used to produce the expected continuation.
 * @param first Borrowed NUL-terminated expected continuation for prompt 'the red'.
 */
static void test_persistence(cgai_model *model, const char *first) {
    /* Step 1: Reserve comparison output and choose the temporary model path. */
    char second[256];
    const char *path = "centroid_gai_test_model.cgai";
    /* Step 2: Save, reload, and compare the loaded model's vocabulary and greedy continuation. */
    CHECK(cgai_model_save(model, path));
    cgai_model *loaded = cgai_model_load(path);
    CHECK(loaded != NULL);
    CHECK(cgai_model_vocabulary_size(loaded) == cgai_model_vocabulary_size(model));
    CHECK(cgai_model_generate(loaded, "the red", 8U, 0.0, 1U, second, sizeof(second)));
    CHECK(strcmp(first, second) == 0);

    /* Step 3: Remove the artifact and exercise missing/null path handling. */
    (void)remove(path);
    CHECK(cgai_model_load(path) == NULL);
    CHECK(cgai_model_load(NULL) == NULL);
    CHECK(cgai_model_save(model, NULL) == CGAI_STATUS_ERROR);
    /* Step 4: Release only the loaded copy; the parent owns the original fixture. */
    cgai_model_destroy(loaded);
}

/**
 * @brief Verify a second corpus adds learned transitions to existing model state.
 *
 * Capturing the counter before training distinguishes accumulating state from replacing the model.
 * The fixture is intentionally mutated and kept alive for subsequent scenarios. An assertion
 * failure terminates this test executable before it uses an invalid result.
 *
 * @param model Borrowed mutable trained fixture.
 */
static void test_incremental_training(cgai_model *model) {
    /* Step 1: Capture the learned-transition count before the second corpus. */
    const size_t examples_before = cgai_model_examples_seen(model);
    /* Step 2: Train another corpus and require the counter to increase. */
    CHECK(cgai_model_train_text(model, "another short example") == CGAI_STATUS_OK);
    CHECK(cgai_model_examples_seen(model) > examples_before);
}

/**
 * @brief Reject zero and unrepresentably large dimension requests.
 *
 * Each constructor failure is expected to return NULL rather than an owned object requiring
 * cleanup. The second scenario starts from defaults again, so it isolates the oversized-dimension
 * case from the preceding zero value.
 *
 * @return Zero after both invalid configurations are rejected.
 */
static int test_invalid_configuration(void) {
    /* Step 1: Start with defaults and change dimensions to the invalid zero value. */
    cgai_config /* Step 2: Reset the fixture and test the largest size_t value as a dimension
                   request. */
        config = cgai_default_config();
    config.dimensions = 0U;
    CHECK(cgai_model_create(&config) == NULL);
    config = cgai_default_config();
    config.dimensions = SIZE_MAX;
    CHECK(cgai_model_create(&config) == NULL);
    return 0;
}

/**
 * @brief Check core failure paths for pointers, temperature, and output capacity.
 *
 * Initial checks require no model. Later cases use a trained temporary fixture so failures are
 * attributable to generation arguments or buffer size rather than an untrained model. The small
 * output array lives on the stack and needs no heap cleanup.
 *
 * @return Zero after all rejection checks pass; failed assertions terminate the executable.
 */
static int test_argument_validation(void) {
    /* Step 1: Reserve a deliberately small output and test calls with missing models/text. */
    char output[8];
    CHECK(cgai_model_train_text(NULL, "text") == CGAI_STATUS_ERROR);
    CHECK(cgai_model_train_text(NULL, NULL) == CGAI_STATUS_ERROR);
    CHECK(cgai_model_generate(NULL, "prompt", 1U, 0.0, 0U, output, sizeof(output)) ==
          CGAI_STATUS_ERROR);
    /* Step 2: Create and train a valid model for the generation-error scenarios. */
    cgai_model *model = cgai_model_create(NULL);
    CHECK(model != NULL);
    CHECK(cgai_model_train_text(model, "one two three") == CGAI_STATUS_OK);
    /* Step 3: Check negative temperature, zero capacity, and insufficient text space. */
    CHECK(cgai_model_generate(model, "prompt", 1U, -1.0, 0U, output, sizeof(output)) ==
          CGAI_STATUS_ERROR);
    CHECK(cgai_model_generate(model, "prompt", 1U, 0.0, 0U, output, 0U) == CGAI_STATUS_ERROR);
    CHECK(cgai_model_generate(model, "prompt", 8U, 0.0, 0U, output, sizeof(output)) ==
          CGAI_STATUS_ERROR);
    /* Step 4: Release the temporary fixture after its error paths have been exercised. */
    cgai_model_destroy(model);
    return 0;
}

/**
 * @brief Reject history-size overflow and accept an empty zero-token continuation.
 *
 * The huge token counts must fail checked arithmetic before attempting a correspondingly huge
 * allocation. Checking the diagnostic distinguishes that rejection from unrelated errors. The final
 * case verifies a trained model still accepts zero output tokens after those failed requests.
 *
 * @param model Borrowed trained fixture used without mutation.
 */
static void test_generation_capacity(const cgai_model *model) {
    /* Step 1: Provide a small real output buffer while testing impossible history capacities. */
    char output[16];
    /* Step 2: Exercise addition overflow and then multiplication overflow with a smaller huge
     * count. */
    CHECK(cgai_model_generate(model, "the red", SIZE_MAX, 0.0, 0U, output, sizeof(output)) ==
          CGAI_STATUS_ERROR);
    CHECK(strstr(cgai_last_error(), "history is too large") != NULL);
    CHECK(cgai_model_generate(model, "the red", SIZE_MAX / 2U, 0.0, 0U, output, sizeof(output)) ==
          CGAI_STATUS_ERROR);
    CHECK(strstr(cgai_last_error(), "history is too large") != NULL);
    /* Step 3: Check that a valid zero-token request produces only the NUL terminator. */
    CHECK(cgai_model_generate(model, "", 0U, 0.0, 0U, output, sizeof(output)) == CGAI_STATUS_OK);
    CHECK(output[0] == '\0');
}

/**
 * @brief Create the owned fixture shared by the public-API integration scenarios.
 *
 * The small fixed configuration makes the test quick and repeatable. Training text contains
 * repeated contexts and several ordinary spellings, so both vocabulary and learned-example checks
 * have useful nonzero expectations. The caller is responsible for destroying the returned model.
 *
 * @return Owned trained model after prerequisite assertions succeed.
 */
static cgai_model *trained_model(void) {
    /* Step 1: Choose small dimensions, centroid capacity, and context length. */
    cgai_config config = cgai_default_config();
    config.dimensions = 12U;
    config.centroid_count = 4U;
    config.context_window = 2U;

    /* Step 2: Create the fixture, train its corpus, and confirm that useful state was learned. */
    cgai_model *model = cgai_model_create(&config);
    CHECK(model != NULL);
    CHECK(cgai_model_train_text(model, "the red fox runs. the blue fox sleeps. "
                                       "the red bird sings. the blue bird flies."));
    CHECK(cgai_model_vocabulary_size(model) >= 10U);
    CHECK(cgai_model_examples_seen(model) > 0U);
    /* Step 3: Transfer fixture cleanup responsibility to the test runner. */
    return model;
}

/**
 * @brief Run public core API validation, generation, and persistence scenarios.
 *
 * The shared model is deliberately trained again before comparisons. Greedy generation should not
 * depend on the supplied random seed, so two different seeds must produce equal continuations.
 * The persistence helper uses that same continuation as its reference.
 *
 * @return Zero after all scenarios pass; assertions terminate on failure.
 */
int main(void) {
    /* Step 1: Run argument/configuration checks before constructing the shared fixture. */
    CHECK(test_invalid_configuration() == 0);
    CHECK(test_argument_validation() == 0);
    /* Step 2: Build the fixture and exercise incremental training and capacity validation. */
    cgai_model *model = trained_model();
    test_incremental_training(model);
    test_generation_capacity(model);

    /* Step 3: Compare two greedy continuations generated with different random seeds. */
    char first[256];
    char second[256];
    CHECK(cgai_model_generate(model, "the red", 8U, 0.0, 42U, first, sizeof(first)));
    CHECK(cgai_model_generate(model, "the red", 8U, 0.0, 99U, second, sizeof(second)));
    CHECK(strcmp(first, second) == 0);

    /* Step 4: Check file round-trip behavior, then destroy the shared model. */
    test_persistence(model, first);

    cgai_model_destroy(model);
    return 0;
}
