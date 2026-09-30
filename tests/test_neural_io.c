/** @file test_neural_io.c @brief Neural serialization, inference preservation, and corruption
 * tests. */
#include "internal/file_utils.h"
#include "test_neural.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Compare generation and evaluation from two independently owned models.
 *
 * Seeded sampling must preserve the exact sequence after loading, while greedy
 * inference and teacher-forced losses must also remain identical.
 * @param original Borrowed model saved to disk.
 * @param loaded Borrowed independent model loaded from that artifact. */
static void cgai_test_neural_same_inference(const cgai_neural_model *original,
                                            const cgai_neural_model *loaded) {
    /* Step 1: Compare seeded sampled continuations with an unknown prompt word. */
    char first[512];
    char second[512];
    cgai_neural_metrics before;
    cgai_neural_metrics after;
    TEST_CHECK(cgai_neural_generate(original, "a unseen", 24U, 0.8, 91U, first, sizeof(first)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_generate(loaded, "a unseen", 24U, 0.8, 91U, second, sizeof(second)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(strcmp(first, second) == 0, "sampled output changed after reload");
    /* Step 2: Compare greedy continuations independently of the sampling seed. */
    TEST_CHECK(cgai_neural_generate(original, "b", 24U, 0.0, 1U, first, sizeof(first)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_generate(loaded, "b", 24U, 0.0, 2U, second, sizeof(second)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(strcmp(first, second) == 0, "greedy output changed after reload");
    /* Step 3: Compare scalar evaluation and unknown-word accounting. */
    TEST_CHECK(cgai_neural_evaluate(original, "a b unseen", &before) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_evaluate(loaded, "a b unseen", &after) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(before.cross_entropy == after.cross_entropy && before.accuracy == after.accuracy,
               "evaluation changed after reload");
    TEST_CHECK(before.unknown_tokens == after.unknown_tokens, "unknown accounting changed");
}

/** @brief Train, save, and load an artifact while preserving all model state.
 *
 * The caller retains the original model and output file; this helper releases
 * only its newly loaded model after verifying the complete flat parameter block.
 * @param model Borrowed mutable fixture trained before saving.
 * @param path Borrowed temporary artifact path owned by the test runner. */
static void cgai_test_neural_roundtrip(cgai_neural_model *model, const char *path) {
    /* Step 1: Train a few epochs so serialization covers updated parameter groups. */
    cgai_neural_training training = cgai_neural_default_training();
    training.epochs = 3U;
    TEST_CHECK(cgai_neural_train(model, "a b left a b left", &training) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_save(model, path) == CGAI_STATUS_OK, cgai_last_error());
    /* Step 2: Load independent ownership and compare persisted numeric state. */
    cgai_neural_model *loaded = cgai_neural_load(path);
    TEST_CHECK(loaded != NULL, cgai_last_error());
    TEST_CHECK(model->parameter_count == loaded->parameter_count &&
                   model->vocabulary_size == loaded->vocabulary_size,
               "model shape changed after reload");
    TEST_CHECK(memcmp(model->parameters, loaded->parameters,
                      model->parameter_count * sizeof(*model->parameters)) == 0,
               "parameters changed after reload");
    /* Step 3: Compare observable inference before releasing the loaded handle. */
    cgai_test_neural_same_inference(model, loaded);
    cgai_neural_destroy(loaded);
}

/** @brief Require a deliberately malformed artifact to fail validation.
 *
 * The byte span is borrowed and written exactly as supplied. The test terminates
 * on any unexpected successful load, so later operations cannot trust bad state.
 * @param bytes Borrowed payload with at least count readable bytes.
 * @param count Payload bytes to write, permitting zero for an empty file. */
static void cgai_test_neural_reject_bytes(const uint8_t *bytes, size_t count) {
    /* Step 1: Write the malformed bytes to a test-specific temporary artifact. */
    const char *path = "centroid_gai_neural_malformed.cgnn";
    TEST_CHECK(cgai_file_write_all(path, bytes, count) == CGAI_STATUS_OK, cgai_last_error());
    /* Step 2: Require rejection, then remove the temporary file. */
    TEST_CHECK(cgai_neural_load(path) == NULL, "malformed neural artifact was accepted");
    TEST_CHECK(cgai_last_error()[0] != '\0', "load failure omitted a diagnostic");
    TEST_CHECK(remove(path) == 0, "could not remove malformed artifact");
}

/** @brief Reject truncated artifacts and distinguish their dedicated magic bytes.
 *
 * The complete byte allocation includes a separate sentinel byte, allowing a
 * one-byte trailing-data case without reading outside allocated storage.
 * @param bytes Borrowed mutable complete artifact followed by one allocated byte.
 * @param count Number of actual artifact bytes, excluding that extra byte. */
static void cgai_test_neural_corrupt_bytes(uint8_t *bytes, size_t count) {
    /* Step 1: Try truncation at the empty, header, middle, and final-byte boundaries. */
    const size_t truncations[] = {0U, 8U, count / 2U, count - 1U};
    for (size_t index = 0U; index < sizeof(truncations) / sizeof(truncations[0]); ++index) {
        cgai_test_neural_reject_bytes(bytes, truncations[index]);
    }
    /* Step 2: Corrupt the magic and restore it before the trailing-data check. */
    const uint8_t saved = bytes[0];
    bytes[0] ^= 0xffU;
    cgai_test_neural_reject_bytes(bytes, count);
    bytes[0] = saved;
    /* Step 3: Require exact artifact length rather than silently accepting an extra byte. */
    bytes[count] = 0x7fU;
    cgai_test_neural_reject_bytes(bytes, count + 1U);
}

/** @brief Replace one small serialized field and then restore its exact bytes.
 *
 * The caller supplies an in-bounds replacement of at most sixteen bytes. Saving
 * the original representation keeps later corruption cases independent.
 * @param bytes Borrowed mutable complete valid artifact.
 * @param count Number of artifact bytes.
 * @param offset Zero-based replacement byte offset.
 * @param replacement Borrowed replacement representation with length bytes.
 * @param length Number of bytes to replace, no greater than sixteen. */
static void cgai_test_neural_replace(uint8_t *bytes, size_t count, size_t offset,
                                     const void *replacement, size_t length) {
    /* Step 1: Check the test's own bounds and retain the original representation. */
    uint8_t saved[16];
    TEST_CHECK(length <= sizeof(saved) && offset <= count && length <= count - offset,
               "invalid corruption fixture bounds");
    memcpy(saved, bytes + offset, length);
    /* Step 2: Require rejection of the replacement, then restore the field. */
    memcpy(bytes + offset, replacement, length);
    cgai_test_neural_reject_bytes(bytes, count);
    memcpy(bytes + offset, saved, length);
}

/** @brief Reject unsupported versions, malicious lengths, and nonfinite parameters.
 *
 * Version-one stores individual native scalar fields without structure padding;
 * these offsets follow that documented format rather than a C struct layout.
 * @param bytes Borrowed mutable complete valid version-one artifact.
 * @param count Number of artifact bytes. */
static void cgai_test_neural_bad_fields(uint8_t *bytes, size_t count) {
    /* Step 1: Reject version, shape, count, and first-spelling length corruptions. */
    const uint32_t version = 99U;
    const uint64_t zero = 0U;
    const uint64_t excessive = UINT64_MAX;
    const double nonfinite = NAN;
    const size_t config_offset = 8U + sizeof(uint32_t);
    const size_t count_offset = config_offset + 5U * sizeof(uint64_t) + sizeof(double);
    const size_t spelling_offset = count_offset + 2U * sizeof(uint64_t);
    cgai_test_neural_replace(bytes, count, 8U, &version, sizeof(version));
    cgai_test_neural_replace(bytes, count, config_offset, &zero, sizeof(zero));
    cgai_test_neural_replace(bytes, count, count_offset, &excessive, sizeof(excessive));
    cgai_test_neural_replace(bytes, count, count_offset + sizeof(uint64_t), &zero, sizeof(zero));
    cgai_test_neural_replace(bytes, count, spelling_offset, &excessive, sizeof(excessive));
    /* Step 2: Reject NaN in the routing temperature and in the last parameter. */
    cgai_test_neural_replace(bytes, count, config_offset + 5U * sizeof(uint64_t), &nonfinite,
                             sizeof(nonfinite));
    cgai_test_neural_replace(bytes, count, count - sizeof(double), &nonfinite, sizeof(nonfinite));
}

/** @brief Locate a vocabulary spelling within a trusted valid test artifact.
 *
 * This helper still checks each length against the known byte allocation before
 * advancing, preventing mistakes in the corruption tests from hiding codec bugs.
 * @param bytes Borrowed valid artifact bytes.
 * @param count Number of artifact bytes.
 * @param token Zero-based vocabulary ID known to exist in the fixture.
 * @return Zero-based byte offset of that spelling's first byte. */
static size_t cgai_test_neural_spelling_offset(const uint8_t *bytes, size_t count, size_t token) {
    /* Step 1: Skip the fixed version-one header to the first token length. */
    size_t offset = 8U + sizeof(uint32_t) + 7U * sizeof(uint64_t) + sizeof(double);
    /* Step 2: Follow only checked length-prefixed entries to the requested token. */
    for (size_t index = 0U; index <= token; ++index) {
        uint64_t length = 0U;
        TEST_CHECK(offset <= count && sizeof(length) <= count - offset, "invalid length offset");
        memcpy(&length, bytes + offset, sizeof(length));
        offset += sizeof(length);
        TEST_CHECK(length <= count - offset, "invalid spelling length");
        if (index == token)
            return offset;
        offset += (size_t)length;
    }
    return 0U;
}

/** @brief Reject invalid control IDs and duplicate or nonnormalized spellings.
 *
 * The fixture's ordinary spellings start with single-byte a and b, allowing
 * independent malformed-string scenarios without changing serialized lengths.
 * @param bytes Borrowed mutable complete valid artifact.
 * @param count Number of artifact bytes. */
static void cgai_test_neural_bad_spellings(uint8_t *bytes, size_t count) {
    /* Step 1: Locate fixed controls and the first two ordinary token spellings. */
    const size_t bos = cgai_test_neural_spelling_offset(bytes, count, 0U);
    const size_t first = cgai_test_neural_spelling_offset(bytes, count, 3U);
    const size_t second = cgai_test_neural_spelling_offset(bytes, count, 4U);
    /* Step 2: Reject changed controls, embedded zeros, uppercase, and duplicates. */
    cgai_test_neural_replace(bytes, count, bos, "x", 1U);
    cgai_test_neural_replace(bytes, count, first, "", 1U);
    cgai_test_neural_replace(bytes, count, first, "A", 1U);
    cgai_test_neural_replace(bytes, count, second, "a", 1U);
}

/** @brief Check neural artifact round trips and malformed artifact rejection.
 *
 * The successful artifact is reused for corruption tests through a separately
 * owned byte copy. Every successful path releases models, buffers, and files. */
void cgai_test_neural_io(void) {
    /* Step 1: Check the public path validation and create a round-trip artifact. */
    const char *path = "centroid_gai_neural_roundtrip.cgnn";
    cgai_neural_model *model = cgai_test_neural_fixture();
    TEST_CHECK(cgai_neural_save(model, NULL) == CGAI_STATUS_ERROR, "null save path accepted");
    TEST_CHECK(cgai_neural_load(NULL) == NULL, "null load path accepted");
    cgai_test_neural_roundtrip(model, path);
    /* Step 2: Read independent bytes and exercise structural rejection. */
    uint8_t *bytes = NULL;
    size_t count = 0U;
    TEST_CHECK(cgai_file_read_all(path, &bytes, &count) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(count > 8U, "saved artifact omitted its header");
    cgai_test_neural_corrupt_bytes(bytes, count);
    cgai_test_neural_bad_fields(bytes, count);
    cgai_test_neural_bad_spellings(bytes, count);
    /* Step 3: Release the byte copy, model, and successful artifact. */
    free(bytes);
    cgai_neural_destroy(model);
    TEST_CHECK(remove(path) == 0, "could not remove round-trip artifact");
}
