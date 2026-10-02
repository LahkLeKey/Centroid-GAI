/** @file test_neural_examples.c @brief Independent single-target learning and exact resume. */
#include "internal/file_utils.h"
#include "internal/neural_internal.h"
#include "internal/neural_math.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Stable ordered records distinguish both complete and BOS-padded prompts. */
static const cgai_neural_example examples[] = {
    {"a b", "left"}, {"b a", "right"}, {"a", "left"}, {"b", "right"}};
/** Test-owned canonical checkpoint paths, isolated by the CTest working directory. */
static const char before_path[] = "neural-examples-before.cgcheckpoint";
/** Secondary snapshot path for state comparisons and exact resume. */
static const char after_path[] = "neural-examples-after.cgcheckpoint";

/** @brief Create an inexpensive positional task model with training-only vocabulary.
 * @return Owned initialized model; prerequisites terminate on failure. */
static cgai_neural_model *example_fixture(void) {
    /* Step 1: Select a bounded deterministic shape supporting two input positions. */
    cgai_neural_config config = cgai_neural_default_config();
    config.embedding_dimensions = 4U;
    config.hidden_dimensions = 8U;
    config.centroid_count = 8U;
    config.context_window = 2U;
    config.seed = 123U;
    /* Step 2: Construct vocabulary exclusively from task training spellings. */
    cgai_neural_model *model = cgai_neural_create(&config, "a b left right");
    TEST_CHECK(model != NULL, cgai_last_error());
    return model;
}

/** @brief Require exact physical identity between the two test checkpoint snapshots. */
static void compare_checkpoints(void) {
    /* Step 1: Read two separately owned bounded file copies. */
    uint8_t *before = NULL;
    uint8_t *after = NULL;
    size_t before_count = 0U;
    size_t after_count = 0U;
    TEST_CHECK(cgai_file_read_all(before_path, &before, &before_count) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_file_read_all(after_path, &after, &after_count) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: Compare vocabulary, weights, moments and progress through their exact codec. */
    TEST_CHECK(before_count == after_count && memcmp(before, after, before_count) == 0,
               "independent example state changed unexpectedly");
    free(after);
    free(before);
}

/** @brief Reject an invalid later record and require complete state preservation.
 * @param model Borrowed mutable initialized model.
 * @param invalid Borrowed deliberately invalid second record. */
static void reject_record(cgai_neural_model *model, const cgai_neural_example *invalid) {
    /* Step 1: A valid first record must not be learned before the later invalid record fails. */
    const cgai_neural_example records[] = {examples[0], *invalid};
    TEST_CHECK(cgai_neural_train_examples_continue(model, records, 2U, NULL) == CGAI_STATUS_ERROR,
               "invalid independent example was accepted");
    /* Step 2: Exact checkpoint equality includes optimizer allocation and continuation state. */
    TEST_CHECK(cgai_neural_checkpoint_save(model, after_path) == CGAI_STATUS_OK, cgai_last_error());
    compare_checkpoints();
}

/** @brief Reject malformed prompts and targets without changing frozen vocabulary or state.
 * @param model Borrowed mutable initialized model. */
static void check_invalid_records(cgai_neural_model *model) {
    /* Step 1: Snapshot full state before checking independent malformed later records. */
    const cgai_neural_example invalid[] = {
        {NULL, "left"}, {"", "left"},   {"a b a", "left"}, {"unseen", "left"},
        {"a", NULL},    {"a", ""},      {"a", "unseen"},   {"a", "left right"},
        {"a", "<bos>"}, {"a", "<eos>"}, {"a", "<unk>"}};
    const size_t vocabulary = cgai_neural_vocabulary_size(model);
    TEST_CHECK(cgai_neural_checkpoint_save(model, before_path) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: Every malformed record must fail before any model or Adam mutation. */
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(*invalid); ++i)
        reject_record(model, &invalid[i]);
    TEST_CHECK(cgai_neural_vocabulary_size(model) == vocabulary,
               "invalid held-out spellings entered task vocabulary");
}

/** @brief Reject invalid public bounds and settings while retaining initialized state.
 * @param model Borrowed mutable fixture. */
static void check_invalid_arguments(cgai_neural_model *model) {
    /* Step 1: Reject handles, arrays and counts before attempting record access. */
    TEST_CHECK(cgai_neural_train_examples_continue(NULL, examples, 4U, NULL) == CGAI_STATUS_ERROR,
               "null task model was accepted");
    TEST_CHECK(cgai_neural_train_examples_continue(model, NULL, 4U, NULL) == CGAI_STATUS_ERROR,
               "null task array was accepted");
    TEST_CHECK(cgai_neural_train_examples_continue(model, examples, 0U, NULL) == CGAI_STATUS_ERROR,
               "empty task array was accepted");
    TEST_CHECK(cgai_neural_train_examples_continue(model, examples, 10001U, NULL) ==
                   CGAI_STATUS_ERROR,
               "excessive task count was accepted");
    /* Step 2: Reject nonfinite controls before changing optimizer ownership. */
    cgai_neural_training training = cgai_neural_default_training();
    training.learning_rate = NAN;
    TEST_CHECK(cgai_neural_train_examples_continue(model, examples, 4U, &training) ==
                   CGAI_STATUS_ERROR,
               "nonfinite task learning rate was accepted");
    TEST_CHECK(cgai_neural_checkpoint_save(model, after_path) == CGAI_STATUS_OK, cgai_last_error());
    compare_checkpoints();
}

/** @brief Score a complete independent prompt and require confident correct lexical output.
 * @param model Borrowed trained immutable fixture.
 * @param example Borrowed independent test record. */
static void check_prediction(const cgai_neural_model *model, const cgai_neural_example *example) {
    /* Step 1: Construct precisely the BOS-padded prompt used by independent training. */
    size_t count = 0U;
    size_t unknown = 0U;
    cgai_token_id *tokens = cgai_neural_sequence(model, example->prompt, &count, &unknown);
    TEST_CHECK(tokens != NULL && unknown == 0U, cgai_last_error());
    cgai_token_id context[CGAI_NEURAL_MAX_CONTEXT];
    cgai_neural_context(model, tokens, count - 1U, context);
    free(tokens);
    /* Step 2: Score only the target, with neither cross-record history nor an EOS label. */
    cgai_neural_workspace *workspace = cgai_neural_workspace_create(model);
    TEST_CHECK(workspace != NULL, cgai_last_error());
    TEST_CHECK(cgai_neural_forward(model, context, workspace) == CGAI_STATUS_OK, cgai_last_error());
    const cgai_token_id target = cgai_neural_lookup(model, example->target);
    printf("Independent target %s -> %s probability %.6f\n", example->prompt, example->target,
           workspace->probabilities[target.value]);
    TEST_CHECK(workspace->probabilities[target.value] > 0.8,
               "independent prompt did not learn its target");
    TEST_CHECK(cgai_neural_argmax(workspace->probabilities, model->output_size).value ==
                   target.value,
               "independent prompt preferred an unrelated token");
    cgai_neural_workspace_destroy(workspace);
}

/** @brief Learn ordered and short independent prompts with exactly one update per record.
 * @param model Borrowed mutable fixture. */
static void check_learning(cgai_neural_model *model) {
    /* Step 1: Train a controlled task without adding prompt losses or EOS targets. */
    cgai_neural_training training = cgai_neural_default_training();
    training.epochs = 1000U;
    training.learning_rate = 0.015;
    TEST_CHECK(cgai_neural_train_examples_continue(model, examples, 4U, &training) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    const cgai_neural_progress progress = cgai_neural_get_progress(model);
    TEST_CHECK(progress.epochs == 1000U && progress.steps == 4000U,
               "task training included prompt or EOS updates");
    /* Step 2: Require all contexts to retain their independent targets, including BOS padding. */
    for (size_t i = 0U; i < 4U; ++i)
        check_prediction(model, &examples[i]);
    const size_t vocabulary = cgai_neural_vocabulary_size(model);
    cgai_neural_metrics metrics;
    TEST_CHECK(cgai_neural_evaluate(model, "a heldout", &metrics) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(metrics.unknown_tokens == 1U && cgai_neural_vocabulary_size(model) == vocabulary,
               "held-out evaluation grew the task vocabulary");
}

/** @brief Require one combined pass group to equal checkpoint-resumed groups exactly. */
static void check_resume(void) {
    /* Step 1: Learn four complete passes in one model and two in an identical model. */
    cgai_neural_model *combined = example_fixture();
    cgai_neural_model *split = example_fixture();
    cgai_neural_training training = cgai_neural_default_training();
    training.epochs = 4U;
    TEST_CHECK(cgai_neural_train_examples_continue(combined, examples, 4U, &training) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    training.epochs = 2U;
    TEST_CHECK(cgai_neural_train_examples_continue(split, examples, 4U, &training) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_checkpoint_save(split, after_path) == CGAI_STATUS_OK, cgai_last_error());
    cgai_neural_destroy(split);
    /* Step 2: Resume the saved optimizer and require canonical full-state equality. */
    split = cgai_neural_checkpoint_load(after_path);
    TEST_CHECK(split != NULL, cgai_last_error());
    TEST_CHECK(cgai_neural_train_examples_continue(split, examples, 4U, &training) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_checkpoint_save(combined, before_path) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_checkpoint_save(split, after_path) == CGAI_STATUS_OK, cgai_last_error());
    compare_checkpoints();
    cgai_neural_destroy(split);
    cgai_neural_destroy(combined);
}

/** @brief Run independent target learning, invalid-record isolation and exact-resume checks.
 * @return Zero after every prerequisite and behavior check passes. */
int main(void) {
    /* Step 1: Invalid input must preserve a fresh model with no allocated Adam state. */
    cgai_neural_model *model = example_fixture();
    check_invalid_records(model);
    check_invalid_arguments(model);
    TEST_CHECK(model->adam_first == NULL && model->adam_second == NULL,
               "invalid task preparation allocated persistent moments");
    /* Step 2: Train only independent targets, then preserve populated Adam on invalid input. */
    check_learning(model);
    check_invalid_records(model);
    check_invalid_arguments(model);
    cgai_neural_destroy(model);
    check_resume();
    /* Step 3: Release all test-owned artifact files after successful exact-state checks. */
    TEST_CHECK(remove(before_path) == 0, "could not remove first task checkpoint");
    TEST_CHECK(remove(after_path) == 0, "could not remove second task checkpoint");
    puts("Independent neural example checks passed.");
    return 0;
}
