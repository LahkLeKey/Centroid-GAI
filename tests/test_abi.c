/** @file test_abi.c @brief Contract tests for the fixed-width native ABI. */

#include "centroid_gai_abi.h"
#include "test_utils.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/** Fail the test immediately when the supplied assertion is false. */
#define CHECK(condition) TEST_CHECK(condition, cgai_abi_last_error())

/**
 * @brief Check stable ABI widths, exported identity, and NULL-handling contracts.
 *
 * Compile-time assertions stop the build if selected ABI layout guarantees change. Runtime CHECK
 * calls verify version/schema queries and rejected pointer arguments. CHECK terminates this test
 * process on failure, so later assertions never rely on a failed prerequisite.
 *
 */
static void test_abi_contract(void) {
    /* Step 1: Verify the status width and buffer data-field offset at compile time. */
    _Static_assert(sizeof(cgai_abi_status) == sizeof(uint32_t), "ABI status width changed");
    _Static_assert(offsetof(cgai_abi_buffer, data) == 0U, "ABI buffer layout changed");
    /* Step 2: Check exported identity, metadata schema presence, and invalid-pointer statuses. */
    CHECK(cgai_abi_version() == CGAI_ABI_VERSION);
    CHECK(strstr(cgai_abi_persistence_schema_json(), "checksumSha256") != NULL);
    CHECK(cgai_abi_default_config(NULL) == CGAI_ABI_INVALID_ARGUMENT);
    CHECK(cgai_abi_model_create(NULL, NULL) == CGAI_ABI_INVALID_ARGUMENT);
    /* Step 3: Exercise the documented no-op cleanup for an absent buffer. */
    cgai_abi_buffer_free(NULL);
}

/**
 * @brief Check scalar metadata from the small trained ABI fixture.
 *
 * The fixture is borrowed and remains alive for the parent test. Assertions use the known fixture
 * dimension and confirm that training produced examples. No ownership is transferred through the
 * metadata structure because it contains only scalar fields.
 *
 * @param model Borrowed trained fixture created with twelve embedding dimensions.
 */
static void test_metadata(cgai_abi_model *model) {
    /* Step 1: Request a snapshot into local stack storage. */
    cgai_abi_model_metadata metadata;
    CHECK(cgai_abi_model_get_metadata(model, &metadata) == CGAI_ABI_OK);
    /* Step 2: Compare format, configuration, and learned-state fields with the fixture
     * expectations. */
    CHECK(metadata.format_version == 1U);
    CHECK(metadata.dimensions == 12U);
    CHECK(metadata.examples_seen > 0U);
}

/**
 * @brief Exercise ABI export, import, and generated-buffer ownership together.
 *
 * A decoded model is independent of the original fixture. The generated buffer's size must exclude
 * its NUL terminator, matching strlen. This helper owns the imported handle and both returned byte
 * allocations; the parent continues owning the original model.
 *
 * @param model Borrowed trained fixture to export.
 */
static void test_artifact_generation(cgai_abi_model *model) {
    /* Step 1: Export a complete artifact from the borrowed fixture. */
    cgai_abi_buffer encoded = {0};
    CHECK(cgai_abi_model_export(model, &encoded) == CGAI_ABI_OK);
    CHECK(encoded.data != NULL && encoded.size > 0U);

    /* Step 2: Import separate model ownership and generate a continuation through that handle. */
    cgai_abi_model *loaded = NULL;
    CHECK(cgai_abi_model_import(encoded.data, encoded.size, &loaded) == CGAI_ABI_OK);
    cgai_abi_buffer generated = {0};
    CHECK(cgai_abi_model_generate(loaded, "native", 8U, 0.0, 42U, &generated) == CGAI_ABI_OK);
    CHECK(generated.data != NULL && generated.size == strlen((char *)generated.data));

    /* Step 3: Release both ABI allocations and the imported model after assertions succeed. */
    cgai_abi_buffer_free(&generated);
    cgai_abi_buffer_free(&encoded);
    cgai_abi_model_destroy(loaded);
}

/**
 * @brief Verify ABI matching buffers and rejected argument paths.
 * @param model Borrowed model, kept alive for the operation.
 */
static void test_pattern_matching(cgai_abi_model *model) {
    cgai_abi_buffer result = {0};
    CHECK(cgai_abi_model_match_json(model, "native models", 3U, &result) == CGAI_ABI_OK);
    CHECK(result.data != NULL && result.size == strlen((char *)result.data));
    CHECK(strstr((char *)result.data, "\"matches\":[") != NULL);
    CHECK(strstr((char *)result.data, "\"unknownTokens\":0") != NULL);
    cgai_abi_buffer_free(&result);
    CHECK(cgai_abi_model_match_json(NULL, "native", 1U, &result) == CGAI_ABI_INVALID_ARGUMENT);
    CHECK(result.data == NULL && result.size == 0U);
    CHECK(cgai_abi_model_match_json(model, "native", 0U, &result) == CGAI_ABI_INVALID_ARGUMENT);
    CHECK(cgai_abi_model_match_json(model, "native", 11U, &result) == CGAI_ABI_INVALID_ARGUMENT);
    CHECK(cgai_abi_model_match_json(model, "  ", 1U, &result) == CGAI_ABI_INVALID_ARGUMENT);
    CHECK(cgai_abi_model_match_json(model, NULL, 1U, &result) == CGAI_ABI_INVALID_ARGUMENT);
    CHECK(cgai_abi_model_match_json(model, "native", 1U, NULL) == CGAI_ABI_INVALID_ARGUMENT);
}

/**
 * @brief Run the ABI integration scenarios against a small reproducible fixture.
 *
 * This executable separates ABI layout and argument checks from operations requiring a trained
 * model. The runner owns that fixture until all scenarios finish. Failed CHECK calls exit the
 * process immediately with a diagnostic and failure status.
 *
 * @return Zero after every check passes; failed assertions terminate the process.
 */
int main(void) {
    /* Step 1: Run checks that do not require a model. */
    test_abi_contract();

    /* Step 2: Initialize the ABI configuration and create/train a compact fixture. */
    cgai_abi_config config;
    CHECK(cgai_abi_default_config(&config) == CGAI_ABI_OK);
    config.dimensions = 12U;
    config.centroid_count = 4U;

    cgai_abi_model *model = NULL;
    CHECK(cgai_abi_model_create(&config, &model) == CGAI_ABI_OK);
    CHECK(cgai_abi_model_train(model, "native models persist through postgres") == CGAI_ABI_OK);

    /* Step 3: Check snapshot values and serialization/generation behavior. */
    test_metadata(model);

    test_artifact_generation(model);
    test_pattern_matching(model);

    /* Step 4: Destroy the shared fixture, then check the invalid import argument path. */
    cgai_abi_model_destroy(model);
    CHECK(cgai_abi_model_import(NULL, 0U, NULL) == CGAI_ABI_INVALID_ARGUMENT);
    return 0;
}
