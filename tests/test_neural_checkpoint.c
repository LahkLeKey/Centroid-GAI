/** @file test_neural_checkpoint.c @brief Exact checkpoint round trips and malformed state
 * rejection. */
#include "internal/file_utils.h"
#include "internal/neural_internal.h"
#include "test_utils.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Shared corpus includes punctuation, an apostrophe, UTF-8 bytes and a long spelling. */
static const char checkpoint_corpus[] =
    "alpha beta beta, don't caf\xc3\xa9 "
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
/** Successful checkpoint path, isolated by the CTest working directory. */
static const char checkpoint_path[] = "neural-checkpoint-test.cgcheckpoint";
/** Secondary path for round trips and malformed payloads. */
static const char checkpoint_other[] = "neural-checkpoint-test-other.cgcheckpoint";

/** @brief Create an inexpensive deterministic model for codec checks.
 * @return Owned initialized model; failed prerequisites terminate the test. */
static cgai_neural_model *checkpoint_fixture(void) {
    /* Step 1: Select a small supported shape and a fixed initialization seed. */
    cgai_neural_config config = cgai_neural_default_config();
    config.embedding_dimensions = 2U;
    config.hidden_dimensions = 3U;
    config.centroid_count = 2U;
    config.context_window = 2U;
    config.seed = 17U;
    /* Step 2: Build the owned vocabulary and parameters from training text alone. */
    cgai_neural_model *model = cgai_neural_create(&config, checkpoint_corpus);
    TEST_CHECK(model != NULL, cgai_last_error());
    return model;
}

/** @brief Require exact shape, vocabulary, weights, moments and counters after loading.
 * @param expected Borrowed original standalone model.
 * @param actual Borrowed loaded standalone model. */
static void check_state(const cgai_neural_model *expected, const cgai_neural_model *actual) {
    /* Step 1: Compare scalar configuration without inspecting compiler structure padding. */
    TEST_CHECK(actual->config.embedding_dimensions == expected->config.embedding_dimensions &&
                   actual->config.hidden_dimensions == expected->config.hidden_dimensions &&
                   actual->config.centroid_count == expected->config.centroid_count &&
                   actual->config.context_window == expected->config.context_window &&
                   actual->config.seed == expected->config.seed &&
                   actual->config.routing_temperature == expected->config.routing_temperature,
               "checkpoint configuration changed");
    TEST_CHECK(actual->vocabulary_size == expected->vocabulary_size &&
                   actual->output_size == actual->vocabulary_size &&
                   actual->parameter_count == expected->parameter_count,
               "checkpoint layout changed");
    for (size_t i = 0U; i < expected->vocabulary_size; ++i)
        TEST_CHECK(strcmp(expected->vocabulary[i], actual->vocabulary[i]) == 0,
                   "checkpoint vocabulary changed");
    /* Step 2: Compare all exact double representations, including signed zero. */
    const size_t bytes = expected->parameter_count * sizeof(double);
    TEST_CHECK(memcmp(expected->parameters, actual->parameters, bytes) == 0,
               "checkpoint weights changed");
    TEST_CHECK((expected->adam_first == NULL) == (actual->adam_first == NULL) &&
                   (expected->adam_second == NULL) == (actual->adam_second == NULL),
               "checkpoint optimizer presence changed");
    if (expected->adam_first != NULL) {
        TEST_CHECK(expected->adam_second != NULL && actual->adam_second != NULL,
                   "checkpoint optimizer moments are incomplete");
        TEST_CHECK(memcmp(expected->adam_first, actual->adam_first, bytes) == 0 &&
                       memcmp(expected->adam_second, actual->adam_second, bytes) == 0,
                   "checkpoint moments changed");
    }
    TEST_CHECK(expected->training_step == actual->training_step &&
                   expected->training_epochs == actual->training_epochs &&
                   expected->training_shuffle == actual->training_shuffle,
               "checkpoint continuation counters changed");
}

/** @brief Read an owned terminated checkpoint byte copy for corruption and comparison.
 * @param path Borrowed checkpoint path.
 * @param count Writable physical byte count excluding the sentinel NUL.
 * @return Owned byte allocation to free(); failed prerequisites terminate the test. */
static uint8_t *read_checkpoint(const char *path, size_t *count) {
    /* Step 1: Read a bounded trusted test artifact with independent ownership. */
    uint8_t *bytes = NULL;
    TEST_CHECK(cgai_file_read_all(path, &bytes, count) == CGAI_STATUS_OK, cgai_last_error());
    return bytes;
}

/** @brief Require two saved checkpoints to contain exactly the same canonical bytes.
 * @param first Borrowed first checkpoint path.
 * @param second Borrowed second checkpoint path. */
static void check_identical_files(const char *first, const char *second) {
    /* Step 1: Acquire independent byte copies before comparing physical content. */
    size_t first_count = 0U;
    size_t second_count = 0U;
    uint8_t *first_bytes = read_checkpoint(first, &first_count);
    uint8_t *second_bytes = read_checkpoint(second, &second_count);
    TEST_CHECK(first_count == second_count && memcmp(first_bytes, second_bytes, first_count) == 0,
               "checkpoint reserialization changed text");
    /* Step 2: Release both byte-copy owners. */
    free(second_bytes);
    free(first_bytes);
}

/** @brief Require exact load/save state and canonical byte identity.
 * @param model Borrowed initialized standalone model. */
static void check_roundtrip(const cgai_neural_model *model) {
    /* Step 1: Save, reload and compare the entire standalone training state. */
    TEST_CHECK(cgai_neural_checkpoint_save(model, checkpoint_path) == CGAI_STATUS_OK,
               cgai_last_error());
    cgai_neural_model *loaded = cgai_neural_checkpoint_load(checkpoint_path);
    TEST_CHECK(loaded != NULL, cgai_last_error());
    check_state(model, loaded);
    /* Step 2: Reserialization must retain every canonical text byte. */
    TEST_CHECK(cgai_neural_checkpoint_save(loaded, checkpoint_other) == CGAI_STATUS_OK,
               cgai_last_error());
    check_identical_files(checkpoint_path, checkpoint_other);
    cgai_neural_destroy(loaded);
}

/** @brief Save arbitrary malformed bytes and require load rejection.
 * @param bytes Borrowed readable payload span.
 * @param count Physical payload length. */
static void reject_bytes(const uint8_t *bytes, size_t count) {
    /* Step 1: Save the complete intentionally malformed payload to a separate path. */
    TEST_CHECK(cgai_file_write_all(checkpoint_other, bytes, count) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: A malformed checkpoint cannot publish a partially loaded model. */
    TEST_CHECK(cgai_neural_checkpoint_load(checkpoint_other) == NULL,
               "malformed checkpoint was accepted");
}

/** @brief Replace one trusted checkpoint line and require the resulting state to fail.
 * @param bytes Borrowed trusted terminated checkpoint bytes.
 * @param count Physical byte length excluding sentinel NUL.
 * @param prefix Borrowed unique target line prefix.
 * @param replacement Borrowed replacement line without a line ending. */
static void reject_line(const uint8_t *bytes, size_t count, const char *prefix,
                        const char *replacement) {
    /* Step 1: Find a complete trusted line and size an independent replacement allocation. */
    const char *start = strstr((const char *)bytes, prefix);
    TEST_CHECK(start != NULL, "corruption prefix was absent");
    const char *end = strchr(start, '\n');
    TEST_CHECK(end != NULL, "corruption line was unterminated");
    const size_t before = (size_t)(start - (const char *)bytes);
    const size_t after = count - (size_t)(end - (const char *)bytes);
    const size_t length = strlen(replacement);
    uint8_t *changed = malloc(before + length + after);
    TEST_CHECK(changed != NULL, "could not allocate corrupted checkpoint");
    /* Step 2: Preserve the original LF and suffix, then check rejection and release ownership. */
    memcpy(changed, bytes, before);
    memcpy(changed + before, replacement, length);
    memcpy(changed + before + length, end, after);
    reject_bytes(changed, before + length + after);
    free(changed);
}

/** @brief Reject unsafe counts, inconsistent metadata, malformed indices and scalar values.
 * @param bytes Borrowed valid trained terminated checkpoint payload.
 * @param count Physical payload byte length. */
static void check_bad_fields(const uint8_t *bytes, size_t count) {
    /* Step 1: Reject impossible version, shape, table and optimizer declarations. */
    reject_line(bytes, count, "CGAI-CHECKPOINT ", "CGAI-CHECKPOINT 2");
    reject_line(bytes, count, "embedding_dimensions ", "embedding_dimensions 18446744073709551616");
    reject_line(bytes, count, "vocabulary_size ", "vocabulary_size 8193");
    reject_line(bytes, count, "parameter_count ", "parameter_count 2000001");
    reject_line(bytes, count, "parameter_count ", "parameter_count 1");
    reject_line(bytes, count, "optimizer ", "optimizer 0");
    reject_line(bytes, count, "training_epochs ", "training_epochs 18446744073709551615");
    /* Step 2: Reject nonfinite doubles, negative moments and incorrect scalar ordering. */
    reject_line(bytes, count, "routing_temperature ", "routing_temperature nan");
    reject_line(bytes, count, "parameter 0 ", "parameter 0 nan");
    reject_line(bytes, count, "parameter 0 ", "parameter 0 0x1p+1024");
    reject_line(bytes, count, "parameter 1 ", "parameter 0 0x0p+0");
    reject_line(bytes, count, "adam 0 ", "adam 0 nan 0x0p+0");
    reject_line(bytes, count, "adam 0 ", "adam 0 0x0p+0 -0x1p+0");
}

/** @brief Reject malformed control IDs, spellings, encoded lengths and duplicate vocabulary.
 * @param bytes Borrowed valid terminated checkpoint payload.
 * @param count Physical payload byte length. */
static void check_bad_vocabulary(const uint8_t *bytes, size_t count) {
    /* Step 1: Controls, IDs and declared payload lengths must remain exact. */
    reject_line(bytes, count, "token 0 ", "token 0 5 78626f733e");
    reject_line(bytes, count, "token 3 ", "token 4 5 616c706861");
    reject_line(bytes, count, "token 3 ", "token 3 1048577 61");
    reject_line(bytes, count, "token 3 ", "token 3 5 616c70686g");
    reject_line(bytes, count, "token 3 ", "token 3 5 616c7068");
    /* Step 2: Reject embedded NUL, uppercase, whitespace, multi-token text and duplicates. */
    reject_line(bytes, count, "token 3 ", "token 3 5 006c706861");
    reject_line(bytes, count, "token 3 ", "token 3 5 416c706861");
    reject_line(bytes, count, "token 3 ", "token 3 1 20");
    reject_line(bytes, count, "token 3 ", "token 3 3 612c62");
    reject_line(bytes, count, "token 4 ", "token 4 5 616c706861");
}

/** @brief Reject truncated files and every form of trailing content.
 * @param bytes Borrowed valid terminated checkpoint payload.
 * @param count Physical payload byte length. */
static void check_bad_boundaries(const uint8_t *bytes, size_t count) {
    /* Step 1: Reject missing final LF, incomplete scalar sections and an empty file. */
    reject_bytes(bytes, count - 1U);
    reject_bytes(bytes, count / 2U);
    reject_bytes(bytes, 0U);
    /* Step 2: Reject appended whitespace and additional nonempty content. */
    uint8_t *changed = malloc(count + 6U);
    TEST_CHECK(changed != NULL, "could not allocate trailing-content fixture");
    memcpy(changed, bytes, count);
    changed[count] = '\n';
    reject_bytes(changed, count + 1U);
    memcpy(changed + count, "extra\n", 6U);
    reject_bytes(changed, count + 6U);
    free(changed);
}

/** @brief Load equivalent CRLF content and require identical model/optimizer values.
 * @param model Borrowed original model corresponding to the checkpoint bytes.
 * @param bytes Borrowed valid LF checkpoint payload.
 * @param count Physical LF payload byte length. */
static void check_crlf(const cgai_neural_model *model, const uint8_t *bytes, size_t count) {
    /* Step 1: Convert line endings in an independent allocation, preserving all payload bytes. */
    uint8_t *converted = malloc(count * 2U);
    TEST_CHECK(converted != NULL, "could not allocate CRLF fixture");
    size_t length = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (bytes[i] == '\n')
            converted[length++] = '\r';
        converted[length++] = bytes[i];
    }
    TEST_CHECK(cgai_file_write_all(checkpoint_other, converted, length) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: Compare complete loaded state and release independent ownership. */
    cgai_neural_model *loaded = cgai_neural_checkpoint_load(checkpoint_other);
    TEST_CHECK(loaded != NULL, cgai_last_error());
    check_state(model, loaded);
    cgai_neural_destroy(loaded);
    free(converted);
}

/** @brief Require invalid save preflight to preserve the existing successful checkpoint.
 * @param model Borrowed mutable model, restored after the attempted invalid save.
 * @param scalar Borrowed mutable model scalar to corrupt temporarily.
 * @param invalid Invalid value for the selected scalar. */
static void check_save_preserves(cgai_neural_model *model, double *scalar, double invalid) {
    /* Step 1: Snapshot the successful destination before introducing invalid model state. */
    size_t before_count = 0U;
    uint8_t *before = read_checkpoint(checkpoint_path, &before_count);
    const double original = *scalar;
    *scalar = invalid;
    TEST_CHECK(cgai_neural_checkpoint_save(model, checkpoint_path) == CGAI_STATUS_ERROR,
               "invalid checkpoint save succeeded");
    *scalar = original;
    /* Step 2: Require byte-for-byte preservation after rejected serialization preflight. */
    size_t after_count = 0U;
    uint8_t *after = read_checkpoint(checkpoint_path, &after_count);
    TEST_CHECK(before_count == after_count && memcmp(before, after, before_count) == 0,
               "rejected checkpoint save replaced its destination");
    free(after);
    free(before);
}

/** @brief Populate owned optimizer state directly for serialization checks.
 * @param model Borrowed initialized fixture with absent moments. */
static void optimizer_fixture(cgai_neural_model *model) {
    const size_t count = model->parameter_count;
    TEST_CHECK(count > 0U && count <= CGAI_NEURAL_MAX_PARAMETERS,
               "invalid codec fixture parameter capacity");
    model->adam_first = calloc(count, sizeof(*model->adam_first));
    model->adam_second = calloc(count, sizeof(*model->adam_second));
    TEST_CHECK(model->adam_first != NULL && model->adam_second != NULL,
               "could not allocate owned codec fixture moments");
    for (size_t i = 0U; i < count; ++i) {
        model->adam_first[i] = 0.01 * (double)(i + 1U);
        model->adam_second[i] = 0.02 * (double)(i + 1U);
    }
    model->training_step = 6U;
    model->training_epochs = 3U;
    model->training_shuffle = 7U;
}

/** @brief Check populated checkpoints, corruption rejection and input newline portability.
 * @param model Borrowed mutable initialized model. */
static void check_populated(cgai_neural_model *model) {
    /* Step 1: Explicit fixture moments exercise their exact text round trip. */
    optimizer_fixture(model);
    check_roundtrip(model);
    size_t count = 0U;
    uint8_t *bytes = read_checkpoint(checkpoint_path, &count);
    /* Step 2: Check complete state corruption and accepted CRLF normalization. */
    check_bad_fields(bytes, count);
    check_bad_vocabulary(bytes, count);
    check_bad_boundaries(bytes, count);
    check_crlf(model, bytes, count);
    free(bytes);
    /* Step 3: Invalid finite-state preflight must preserve successful artifacts. */
    check_save_preserves(model, model->parameters, NAN);
    check_save_preserves(model, model->adam_first, INFINITY);
    check_save_preserves(model, model->adam_second, -1.0);
}

/** @brief Check exact extreme doubles and legal partial failed-training counters.
 * @param model Borrowed mutable model with initialized optimizer moments. */
static void check_extremes(cgai_neural_model *model) {
    /* Step 1: Exercise signed zero, subnormals and the largest supported finite double. */
    model->parameters[0] = -0.0;
    model->parameters[1] = DBL_TRUE_MIN;
    model->parameters[2] = DBL_MAX;
    model->adam_first[0] = -DBL_TRUE_MIN;
    model->adam_second[0] = DBL_TRUE_MIN;
    check_roundtrip(model);
    /* Step 2: A partially failed first epoch has moments and steps but no complete epoch. */
    model->training_epochs = 0U;
    check_roundtrip(model);
    /* Step 3: An allocated optimizer with no completed updates is also legal state. */
    model->training_step = 0U;
    check_roundtrip(model);
}

/** @brief Run standalone exact checkpoint and malformed-input checks.
 * @return Zero after successful checks; failed assertions terminate the process. */
int main(void) {
    /* Step 1: Validate public paths and the empty optimizer round trip. */
    cgai_neural_model *model = checkpoint_fixture();
    TEST_CHECK(cgai_neural_checkpoint_load(NULL) == NULL, "null checkpoint load path accepted");
    TEST_CHECK(cgai_neural_checkpoint_save(model, NULL) == CGAI_STATUS_ERROR,
               "null checkpoint save path accepted");
    TEST_CHECK(cgai_neural_checkpoint_save(NULL, checkpoint_path) == CGAI_STATUS_ERROR,
               "null checkpoint model accepted");
    check_roundtrip(model);
    /* Step 2: Check trained moments, malformed inputs, float extremes and partial state. */
    check_populated(model);
    check_extremes(model);
    /* Step 3: Release model and every test-owned file after successful checks. */
    cgai_neural_destroy(model);
    TEST_CHECK(remove(checkpoint_path) == 0, "could not remove checkpoint fixture");
    TEST_CHECK(remove(checkpoint_other) == 0, "could not remove secondary checkpoint fixture");
    puts("Neural text checkpoint checks passed.");
    return 0;
}
