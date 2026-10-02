/** @file test_gameplay_codec.c @brief Malformed composed artifacts and exact continuation decoding.
 */
#include "gameplay/gameplay_internal.h"
#include "internal/file_utils.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Test-local original and mutated artifact names, isolated by the CTest directory. */
static const char artifact_path[] = "gameplay-codec-fixture.cggp";
/** Exact continuation fixture path. */
static const char checkpoint_path[] = "gameplay-codec-fixture.cgcheckpoint";
/** Malformed artifact destination reused after each constructor has closed it. */
static const char mutated_path[] = "gameplay-codec-mutated.dat";
/** Owned complete snapshot from the native writer. */
typedef struct codec_bytes {
    uint8_t *data; /**< Owned payload with one NUL sentinel. */
    size_t count;  /**< Payload length excluding sentinel. */
} codec_bytes;
/** One deliberately malformed metadata or scalar row replacement. */
typedef struct codec_mutation {
    const char *prefix; /**< Exact row key and trailing separator. */
    const char *value;  /**< Replacement scalar token. */
} codec_mutation;

/** @brief Create a complete small network suitable for exhaustive malformed-input checks.
 * @return Owned initialized model. */
static cgai_gameplay_model *codec_fixture(void) {
    /* Step1: Retain every parameter family with only38 trainable scalar values. */
    cgai_gameplay_config config = {0};
    config.seed = 42U;
    config.routing_temperature = 1.0;
    config.feature_count = 1U;
    config.embedding_dimensions = 2U;
    config.hidden_dimensions = 2U;
    config.module_count = 2U;
    config.centroids_per_module = 2U;
    config.task_count = 1U;
    config.cardinalities[0] = 2U;
    config.output_counts[0] = 2U;
    config.task_modules[0] = 3U;
    config.task_features[0] = 1U;
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    return model;
}

/** @brief Read one complete native artifact snapshot into owned storage.
 * @param path Borrowed test-owned artifact path.
 * @return Owned payload with byte length and NUL sentinel. */
static codec_bytes read_bytes(const char *path) {
    /* Step1: The shared file reader adds a sentinel without altering measured payload bytes. */
    codec_bytes bytes = {0};
    TEST_CHECK(cgai_file_read_all(path, &bytes.data, &bytes.count) == CGAI_STATUS_OK,
               cgai_last_error());
    return bytes;
}

/** @brief Require the requested public constructor rejects complete malformed payload bytes.
 * @param bytes Borrowed malformed byte payload.
 * @param count Exact payload length, possibly zero.
 * @param checkpoint Nonzero for the continuation constructor. */
static void reject_bytes(const uint8_t *bytes, size_t count, int checkpoint) {
    /* Step1: Each decoder must close and release its unpublished partial model on failure. */
    TEST_CHECK(cgai_file_write_all(mutated_path, bytes, count) == CGAI_STATUS_OK,
               cgai_last_error());
    cgai_gameplay_model *model =
        checkpoint ? cgai_gameplay_checkpoint_load(mutated_path) : cgai_gameplay_load(mutated_path);
    TEST_CHECK(model == NULL, "malformed gameplay artifact escaped its constructor");
    TEST_CHECK(cgai_last_error()[0] != '\0', "malformed gameplay artifact lacks diagnostic");
}

/** @brief Replace one complete row scalar without changing any other encoded bytes.
 * @param original Borrowed original native byte snapshot.
 * @param mutation Borrowed exact row key and replacement scalar.
 * @return Owned mutated payload with NUL sentinel. */
static codec_bytes replace_row(const codec_bytes *original, const codec_mutation *mutation) {
    /* Step1: Locate the first complete native schema row and its exact line ending. */
    const char *start = strstr((const char *)original->data, mutation->prefix);
    TEST_CHECK(start != NULL, "codec mutation key is absent");
    const char *end = strchr(start, '\n');
    TEST_CHECK(end != NULL, "codec mutation row is unterminated");
    const size_t before = (size_t)(start - (const char *)original->data);
    const size_t removed = (size_t)(end - start);
    const size_t added = strlen(mutation->prefix) + strlen(mutation->value);
    codec_bytes changed = {NULL, original->count - removed + added};
    changed.data = malloc(changed.count + 1U);
    TEST_CHECK(changed.data != NULL, "could not allocate malformed codec payload");
    /* Step2: Keep the original newline and all following schema/scalar rows intact. */
    memcpy(changed.data, original->data, before);
    memcpy(changed.data + before, mutation->prefix, strlen(mutation->prefix));
    memcpy(changed.data + before + strlen(mutation->prefix), mutation->value,
           strlen(mutation->value));
    memcpy(changed.data + before + added, end, original->count - before - removed + 1U);
    return changed;
}

/** @brief Apply every independent malformed row to an otherwise complete valid file.
 * @param original Borrowed original valid native payload.
 * @param mutations Borrowed malformed scalar definitions.
 * @param count Number of mutation definitions.
 * @param checkpoint Nonzero for the continuation constructor. */
static void reject_mutations(const codec_bytes *original, const codec_mutation *mutations,
                             size_t count, int checkpoint) {
    /* Step1: One bad scalar must reject the entire otherwise valid artifact. */
    for (size_t i = 0U; i < count; ++i) {
        codec_bytes changed = replace_row(original, &mutations[i]);
        reject_bytes(changed.data, changed.count, checkpoint);
        free(changed.data);
    }
}

/** @brief Reject shape, integer overflow, scalar syntax and numerical corruption.
 * @param original Borrowed valid inference payload. */
static void malformed_inference(const codec_bytes *original) {
    /* Step1: Every field is mutated in an otherwise complete correctly ordered artifact. */
    static const codec_mutation mutations[] = {{"CGAI-GAMEPLAY-MODEL ", "2"},
                                               {"seed ", "18446744073709551616"},
                                               {"seed ", "-1"},
                                               {"seed ", "+1"},
                                               {"seed ", "1junk"},
                                               {"routing_temperature ", "1.0"},
                                               {"routing_temperature ", "nan"},
                                               {"routing_temperature ", "0x1p1024"},
                                               {"routing_temperature ", "0x1p-99999"},
                                               {"routing_temperature ", "0x0p0"},
                                               {"feature_count ", "0"},
                                               {"feature_count ", "17"},
                                               {"feature_count ", "18446744073709551616"},
                                               {"embedding_dimensions ", "65"},
                                               {"hidden_dimensions ", "0"},
                                               {"module_count ", "9"},
                                               {"centroids_per_module ", "33"},
                                               {"task_count ", "9"},
                                               {"cardinality ", "0"},
                                               {"cardinality ", "4294967296"},
                                               {"output_count ", "1"},
                                               {"output_count ", "65"},
                                               {"task_modules ", "0"},
                                               {"task_modules ", "4"},
                                               {"task_features ", "0"},
                                               {"task_features ", "2"},
                                               {"parameter_count ", "0"},
                                               {"parameter_count ", "31"},
                                               {"parameter_count ", "18446744073709551615"},
                                               {"weight ", "nan"},
                                               {"weight ", "-inf"},
                                               {"weight ", "0x1p1024"},
                                               {"weight ", "0x1p-99999"},
                                               {"weight ", "0.5"},
                                               {"weight ", "0x1p0junk"},
                                               {"weight ", " 0x1p0"},
                                               {"end ", "0"}};
    reject_mutations(original, mutations, sizeof(mutations) / sizeof(*mutations), 0);
}

/** @brief Reject optimizer corruption and incoherent continuation metadata.
 * @param original Borrowed valid populated Adam checkpoint payload. */
static void malformed_checkpoint(const codec_bytes *original) {
    /* Step1: Continuation has stricter counters and nonnegative finite second-moment arrays. */
    static const codec_mutation mutations[] = {{"CGAI-GAMEPLAY-CHECKPOINT ", "2"},
                                               {"optimizer_present ", "2"},
                                               {"optimizer_present ", "0"},
                                               {"training_step ", "0"},
                                               {"training_epochs ", "7"},
                                               {"training_shuffle ", "18446744073709551616"},
                                               {"first ", "nan"},
                                               {"first ", "0x1p1024"},
                                               {"second ", "nan"},
                                               {"second ", "-0x1p0"},
                                               {"second ", "0x1p1024"},
                                               {"second ", "0.25"},
                                               {"end ", "2"}};
    reject_mutations(original, mutations, sizeof(mutations) / sizeof(*mutations), 1);
}

/** @brief Reject incomplete schemas/arrays, unterminated rows and appended hidden payloads.
 * @param original Borrowed complete native payload.
 * @param checkpoint Nonzero for the continuation constructor. */
static void truncation_and_trailing(const codec_bytes *original, int checkpoint) {
    /* Step1: Empty/header-only/mid-array/missing-end/final-newline truncations all fail closed. */
    const char *first_weight = strstr((const char *)original->data, "weight ");
    TEST_CHECK(first_weight != NULL, "fixture lacks parameter rows");
    const size_t positions[] = {0U,
                                1U,
                                20U,
                                (size_t)(first_weight - (const char *)original->data),
                                original->count / 2U,
                                original->count - 6U,
                                original->count - 1U};
    for (size_t i = 0U; i < sizeof(positions) / sizeof(*positions); ++i)
        reject_bytes(original->data, positions[i], checkpoint);
    /* Step2: Even whitespace or NUL after the exact end marker is rejected. */
    uint8_t *trailing = malloc(original->count + 7U);
    TEST_CHECK(trailing != NULL, "could not allocate trailing-data fixture");
    memcpy(trailing, original->data, original->count);
    trailing[original->count] = '\n';
    reject_bytes(trailing, original->count + 1U, checkpoint);
    trailing[original->count] = '\0';
    reject_bytes(trailing, original->count + 1U, checkpoint);
    memcpy(trailing + original->count, "end 1\n", 6U);
    reject_bytes(trailing, original->count + 6U, checkpoint);
    free(trailing);
}

/** @brief Check exact parameters and every continuation scalar survive native decoding.
 * @param original Borrowed trained immutable source model.
 * @param loaded Borrowed complete restored continuation model. */
static void equal_continuation(const cgai_gameplay_model *original,
                               const cgai_gameplay_model *loaded) {
    /* Step1: Shape-derived slices retain the same scalar ordering and metadata. */
    TEST_CHECK(original->parameter_count == loaded->parameter_count &&
                   original->config.seed == loaded->config.seed &&
                   original->config.task_features[0] == loaded->config.task_features[0],
               "checkpoint changed composed shape");
    TEST_CHECK(memcmp(original->parameters, loaded->parameters,
                      original->parameter_count * sizeof(double)) == 0,
               "checkpoint changed exact weights");
    /* Step2: Adam arrays and model-local random stream retain physical double identity. */
    TEST_CHECK(memcmp(original->adam_first, loaded->adam_first,
                      original->parameter_count * sizeof(double)) == 0 &&
                   memcmp(original->adam_second, loaded->adam_second,
                          original->parameter_count * sizeof(double)) == 0,
               "checkpoint changed exact Adam moments");
    TEST_CHECK(original->training_step == loaded->training_step &&
                   original->training_epochs == loaded->training_epochs &&
                   original->training_shuffle == loaded->training_shuffle,
               "checkpoint changed continuation counters or shuffle state");
}

/** @brief Check exact zero, signed zero and smallest positive subnormal scalar decoding.
 * @param model Borrowed mutable complete model. */
static void roundtrip_special_scalars(cgai_gameplay_model *model) {
    /* Step1: These representable scalars must preserve every bit in portable hexadecimal storage.
     */
    model->parameters[0] = 0.0;
    model->parameters[1] = -0.0;
    model->parameters[2] = nextafter(0.0, 1.0);
    TEST_CHECK(cgai_gameplay_save(model, artifact_path) == CGAI_STATUS_OK, cgai_last_error());
    cgai_gameplay_model *loaded = cgai_gameplay_load(artifact_path);
    TEST_CHECK(loaded != NULL, cgai_last_error());
    TEST_CHECK(
        memcmp(model->parameters, loaded->parameters, model->parameter_count * sizeof(double)) == 0,
        "portable hex scalar codec changed signed zero or subnormal representation");
    cgai_gameplay_destroy(loaded);
}

/** @brief Validate fresh zero-moment checkpoints and populated exact continuation decoding.
 * @param model Borrowed mutable fixture. */
static void roundtrip_training(cgai_gameplay_model *model) {
    /* Step1: Untrained checkpoints contain absent moments and unchanged zero progress. */
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, checkpoint_path) == CGAI_STATUS_OK,
               cgai_last_error());
    cgai_gameplay_model *fresh = cgai_gameplay_checkpoint_load(checkpoint_path);
    TEST_CHECK(fresh != NULL && fresh->adam_first == NULL && fresh->adam_second == NULL &&
                   fresh->training_step == 0U && fresh->training_epochs == 0U,
               "fresh checkpoint introduced optimizer state");
    cgai_gameplay_destroy(fresh);
    /* Step2: Complete independent Adam steps make all optimizer/progress fields meaningful. */
    const cgai_gameplay_example examples[] = {{{{0U}}, 0U, 0U}, {{{1U}}, 0U, 1U}};
    const cgai_gameplay_training training = {3U, 0.01, 5.0, 0.05};
    TEST_CHECK(cgai_gameplay_train_continue(model, examples, 2U, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, checkpoint_path) == CGAI_STATUS_OK,
               cgai_last_error());
    cgai_gameplay_model *loaded = cgai_gameplay_checkpoint_load(checkpoint_path);
    TEST_CHECK(loaded != NULL, cgai_last_error());
    equal_continuation(model, loaded);
    cgai_gameplay_destroy(loaded);
}

/** @brief Ensure inference and continuation constructors cannot accept each other's format. */
static void distinct_formats(void) {
    /* Step1: Public artifact kinds retain separate magic despite sharing bounded shape syntax. */
    TEST_CHECK(cgai_gameplay_checkpoint_load(artifact_path) == NULL,
               "continuation constructor accepted inference artifact");
    TEST_CHECK(cgai_gameplay_load(checkpoint_path) == NULL,
               "inference constructor accepted optimizer checkpoint");
    static const uint8_t legacy[] = "CGAI-NEURAL-MODEL 1\n";
    reject_bytes(legacy, sizeof(legacy) - 1U, 0);
    reject_bytes(legacy, sizeof(legacy) - 1U, 1);
}

/** @brief Verify complete valid decoding and malformed-input failure isolation.
 * @return Zero after all exact and malformed codec checks pass. */
int main(void) {
    /* Step1: Generate valid native fixtures and check exact portable scalar/Adam roundtrips. */
    cgai_gameplay_model *model = codec_fixture();
    roundtrip_special_scalars(model);
    roundtrip_training(model);
    cgai_gameplay_destroy(model);
    const codec_bytes artifact = read_bytes(artifact_path);
    const codec_bytes checkpoint = read_bytes(checkpoint_path);
    /* Step2: Reject single-field corruption and every incomplete or appended payload form. */
    malformed_inference(&artifact);
    malformed_checkpoint(&checkpoint);
    truncation_and_trailing(&artifact, 0);
    truncation_and_trailing(&checkpoint, 1);
    distinct_formats();
    free(artifact.data);
    free(checkpoint.data);
    /* Step3: Release only files owned by this test process. */
    TEST_CHECK(remove(artifact_path) == 0, "could not remove valid codec artifact");
    TEST_CHECK(remove(checkpoint_path) == 0, "could not remove valid codec checkpoint");
    TEST_CHECK(remove(mutated_path) == 0, "could not remove malformed codec artifact");
    return 0;
}
