/** @file test_neural_resume.c @brief Exact continuation, checkpoint and reset behavior tests. */
#include "internal/neural_internal.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Training-only vocabulary and ordered target windows used by every continuation fixture. */
static const char resume_text[] =
    "a b left b a right a b left b a right a b left b a right a b left b a right";

/** @brief Create a compact initialized model with a reproducible local seed.
 * @return Owned model; failed prerequisites terminate the test process. */
static cgai_neural_model *resume_model(void) {
    /* Step 1: Keep exact numerical comparisons inexpensive with a small fixed architecture. */
    cgai_neural_config config = cgai_neural_default_config();
    config.embedding_dimensions = 2U;
    config.hidden_dimensions = 3U;
    config.centroid_count = 3U;
    config.context_window = 2U;
    config.seed = 73U;
    /* Step 2: Establish the vocabulary exclusively from training text. */
    cgai_neural_model *model = cgai_neural_create(&config, resume_text);
    TEST_CHECK(model != NULL, cgai_last_error());
    return model;
}

/** @brief Execute additional complete continuation passes with fixed Adam settings.
 * @param model Borrowed mutable fixture.
 * @param epochs Number of additional full passes. */
static void resume_train(cgai_neural_model *model, size_t epochs) {
    /* Step 1: Change only the number of passes between calls. */
    cgai_neural_training training = cgai_neural_default_training();
    training.epochs = epochs;
    TEST_CHECK(cgai_neural_train_continue(model, resume_text, &training) == CGAI_STATUS_OK,
               cgai_last_error());
}

/** @brief Require exact weights, moments and progress for two successful continuations.
 * @param first Borrowed completed fixture.
 * @param second Borrowed independently owned fixture. */
static void resume_equal(const cgai_neural_model *first, const cgai_neural_model *second) {
    /* Step 1: Compare every numerical scalar, including moments which affect future steps. */
    const size_t bytes = first->parameter_count * sizeof(*first->parameters);
    TEST_CHECK(first->parameter_count == second->parameter_count, "continuation shape changed");
    TEST_CHECK(first->adam_first != NULL && second->adam_first != NULL &&
                   first->adam_second != NULL && second->adam_second != NULL,
               "continuation moments are missing");
    TEST_CHECK(memcmp(first->parameters, second->parameters, bytes) == 0,
               "partitioned continuation changed weights");
    TEST_CHECK(memcmp(first->adam_first, second->adam_first, bytes) == 0,
               "partitioned continuation changed first moments");
    TEST_CHECK(memcmp(first->adam_second, second->adam_second, bytes) == 0,
               "partitioned continuation changed second moments");
    /* Step 2: Compare pass accounting and the stream used by the next epoch. */
    const cgai_neural_progress a = cgai_neural_get_progress(first);
    const cgai_neural_progress b = cgai_neural_get_progress(second);
    TEST_CHECK(a.epochs == b.epochs && a.steps == b.steps, "continuation progress changed");
    TEST_CHECK(first->training_shuffle == second->training_shuffle,
               "continuation shuffle stream changed");
}

/** @brief Check that partitioning complete passes preserves exact training state.
 * @return Owned completed fixture reused by checkpoint tests. */
static cgai_neural_model *resume_partitioned(void) {
    /* Step 1: Train one copy in one call and another in differently sized calls. */
    cgai_neural_model *whole = resume_model();
    cgai_neural_model *parts = resume_model();
    resume_train(whole, 6U);
    resume_train(parts, 2U);
    resume_train(parts, 1U);
    resume_train(parts, 3U);
    resume_equal(whole, parts);
    /* Step 2: Check target accounting includes one EOS update in every complete pass. */
    const cgai_neural_progress progress = cgai_neural_get_progress(parts);
    TEST_CHECK(progress.epochs == 6U && progress.steps == 6U * 25U,
               "continuation counters omit targets or completed passes");
    cgai_neural_destroy(parts);
    return whole;
}

/** @brief Reload a checkpoint and prove that future passes match the original model.
 * @param original Borrowed mutable completed continuation fixture. */
static void resume_checkpoint(cgai_neural_model *original) {
    /* Step 1: Compare all persisted continuation state before applying further updates. */
    const char *path = "centroid_gai_neural_resume.checkpoint";
    TEST_CHECK(cgai_neural_checkpoint_save(original, path) == CGAI_STATUS_OK, cgai_last_error());
    cgai_neural_model *loaded = cgai_neural_checkpoint_load(path);
    TEST_CHECK(loaded != NULL, cgai_last_error());
    resume_equal(original, loaded);
    /* Step 2: Exact state restoration must also preserve the next shuffled passes. */
    resume_train(original, 2U);
    resume_train(loaded, 1U);
    resume_train(loaded, 1U);
    resume_equal(original, loaded);
    cgai_neural_destroy(loaded);
    TEST_CHECK(remove(path) == 0, "could not remove continuation checkpoint");
}

/** @brief Snapshot the weights and both moment arrays before a rejected request.
 * @param model Borrowed fixture with existing continuation moments.
 * @return Owned three-array snapshot, released with free(). */
static double *resume_snapshot(const cgai_neural_model *model) {
    /* Step 1: Retain all numerical state which could affect later deterministic updates. */
    const size_t bytes = model->parameter_count * sizeof(*model->parameters);
    double *before = malloc(3U * bytes);
    TEST_CHECK(before != NULL, "could not allocate rejection snapshot");
    memcpy(before, model->parameters, bytes);
    memcpy(before + model->parameter_count, model->adam_first, bytes);
    memcpy(before + 2U * model->parameter_count, model->adam_second, bytes);
    return before;
}

/** @brief Compare current numerical state with a three-array rejection snapshot.
 * @param model Borrowed fixture with existing continuation moments.
 * @param before Borrowed snapshot containing weights, first moments and second moments. */
static void resume_unchanged(const cgai_neural_model *model, const double *before) {
    /* Step 1: Check optimizer moments as well as externally observable model weights. */
    const size_t bytes = model->parameter_count * sizeof(*model->parameters);
    TEST_CHECK(memcmp(model->parameters, before, bytes) == 0 &&
                   memcmp(model->adam_first, before + model->parameter_count, bytes) == 0 &&
                   memcmp(model->adam_second, before + 2U * model->parameter_count, bytes) == 0,
               "rejected training changed numerical state");
}

/** @brief Require rejected training requests to preserve current continuation state.
 * @param model Borrowed mutable model with existing continuation state. */
static void resume_bad_settings(cgai_neural_model *model) {
    /* Step 1: Snapshot numerical state and pass accounting before invalid requests. */
    double *before = resume_snapshot(model);
    double *const moments = model->adam_first;
    const cgai_neural_progress progress = cgai_neural_get_progress(model);
    const uint64_t shuffle = model->training_shuffle;
    cgai_neural_training training = cgai_neural_default_training();
    training.learning_rate = NAN;
    /* Step 2: Invalid continuation and legacy calls must not clear retained state. */
    TEST_CHECK(cgai_neural_train_continue(model, resume_text, &training) == CGAI_STATUS_ERROR,
               "invalid continuation rate accepted");
    TEST_CHECK(cgai_neural_train(model, resume_text, &training) == CGAI_STATUS_ERROR,
               "invalid fresh rate accepted");
    TEST_CHECK(cgai_neural_train(model, "", NULL) == CGAI_STATUS_ERROR,
               "empty fresh training text accepted");
    TEST_CHECK(cgai_neural_train_continue(model, "", NULL) == CGAI_STATUS_ERROR,
               "empty continuation text accepted");
    TEST_CHECK(model->adam_first == moments, "rejected training changed moment ownership");
    resume_unchanged(model, before);
    TEST_CHECK(model->training_step == progress.steps &&
                   model->training_epochs == progress.epochs && model->training_shuffle == shuffle,
               "rejected training changed continuation progress");
    free(before);
}

/** @brief Check fresh training clears continuation and retains its original repeatability.
 * @param model Borrowed mutable trained model whose continuation is reset on success. */
static void resume_legacy_reset(cgai_neural_model *model) {
    /* Step 1: Ordinary neural artifacts preserve weights while intentionally resetting moments. */
    const char *path = "centroid_gai_neural_resume.cgnn";
    TEST_CHECK(cgai_neural_save(model, path) == CGAI_STATUS_OK, cgai_last_error());
    cgai_neural_model *fresh = cgai_neural_load(path);
    TEST_CHECK(fresh != NULL && fresh->adam_first == NULL && fresh->adam_second == NULL,
               "ordinary artifact retained optimizer state");
    /* Step 2: Fresh optimization produces identical weights with or without prior moments. */
    cgai_neural_training training = cgai_neural_default_training();
    training.epochs = 2U;
    TEST_CHECK(cgai_neural_train(model, resume_text, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_train(fresh, resume_text, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(memcmp(model->parameters, fresh->parameters,
                      model->parameter_count * sizeof(*model->parameters)) == 0,
               "legacy training retained continuation moments");
    TEST_CHECK(model->adam_first == NULL && model->adam_second == NULL &&
                   model->training_step == 0U && model->training_epochs == 0U,
               "legacy training did not reset continuation progress");
    cgai_neural_destroy(fresh);
    TEST_CHECK(remove(path) == 0, "could not remove ordinary continuation fixture");
}

/** @brief Reject progress overflow before changing weights or allocating continuation moments. */
static void resume_overflow(void) {
    /* Step 1: Exhaust the update counter in an otherwise newly initialized fixture. */
    cgai_neural_model *model = resume_model();
    const double first = model->parameters[0];
    cgai_neural_training training = cgai_neural_default_training();
    training.epochs = 1U;
    model->training_step = UINT64_MAX;
    TEST_CHECK(cgai_neural_train_continue(model, resume_text, &training) == CGAI_STATUS_ERROR,
               "continuation step counter overflow accepted");
    /* Step 2: Independently exhaust the complete-pass counter. */
    model->training_step = 0U;
    model->training_epochs = UINT64_MAX;
    TEST_CHECK(cgai_neural_train_continue(model, resume_text, &training) == CGAI_STATUS_ERROR,
               "continuation epoch counter overflow accepted");
    TEST_CHECK(model->parameters[0] == first && model->adam_first == NULL &&
                   model->adam_second == NULL,
               "overflow rejection mutated the model");
    cgai_neural_destroy(model);
}

/** @brief Run continuation tests independently of legacy mathematical learning checks.
 * @return Zero after exact state, durable continuation and validation assertions pass. */
int main(void) {
    /* Step 1: Check null progress and exact partitioning of complete passes. */
    const cgai_neural_progress empty = cgai_neural_get_progress(NULL);
    TEST_CHECK(empty.epochs == 0U && empty.steps == 0U, "null model has nonzero progress");
    cgai_neural_model *model = resume_partitioned();
    /* Step 2: Verify durable continuation, rejection behavior and legacy reset compatibility. */
    resume_checkpoint(model);
    resume_bad_settings(model);
    resume_legacy_reset(model);
    cgai_neural_destroy(model);
    resume_overflow();
    return 0;
}
