/** @file test_neural_training.c @brief Reproducibility and ordered held-out learning tests. */
#include "test_neural.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Controlled rule: the ordered pair a,b predicts left; b,a predicts right. */
static const char cgai_test_order_train[] =
    "a b left b a right a b left b a right a b left b a right a b left b a right "
    "a b left b a right a b left b a right a b left b a right a b left b a right "
    "a b left b a right a b left b a right a b left b a right a b left b a right "
    "a b left b a right a b left b a right a b left b a right a b left b a right";
/** Separate sequence begins at a different phase and has a different length. */
static const char cgai_test_order_heldout[] =
    "b a right a b left b a right a b left b a right a b left "
    "b a right a b left b a right a b left b a right a b left b a right a b left";

/** @brief Create a small model with enough components for the ordered grammar.
 *
 * Vocabulary construction receives training text only, keeping held-out spellings
 * outside the constructor. The shape is fixed for reproducible test runtime.
 * @return Owned initialized model; assertions terminate on failure. */
static cgai_neural_model *cgai_test_order_model(void) {
    /* Step 1: Configure an inexpensive positional model. */
    cgai_neural_config config = cgai_neural_default_config();
    config.embedding_dimensions = 4U;
    config.hidden_dimensions = 8U;
    config.centroid_count = 8U;
    config.context_window = 2U;
    config.seed = 123U;
    /* Step 2: Construct the owned model from the training split. */
    cgai_neural_model *model = cgai_neural_create(&config, cgai_test_order_train);
    TEST_CHECK(model != NULL, cgai_last_error());
    return model;
}

/** @brief Require each trainable parameter group to receive an update.
 *
 * At least one coordinate must change per group; unused embeddings and masked
 * BOS logits are allowed to retain their initial values.
 * @param model Borrowed trained model.
 * @param before Borrowed parameter_count snapshot captured before training. */
static void cgai_test_neural_updated_groups(const cgai_neural_model *model, const double *before) {
    /* Step 1: Describe the contiguous parameter groups using their slice boundaries. */
    const double *groups[] = {model->embeddings, model->encoder,
                              model->bias,       model->centroids,
                              model->logits,     model->parameters + model->parameter_count};
    /* Step 2: Compare each group against its corresponding initial snapshot. */
    for (size_t group = 0U; group < 5U; ++group) {
        const size_t offset = (size_t)(groups[group] - model->parameters);
        const size_t count = (size_t)(groups[group + 1U] - groups[group]);
        TEST_CHECK(memcmp(groups[group], before + offset, count * sizeof(*before)) != 0,
                   "a neural parameter group did not train");
    }
}

/** @brief Check the learned distinction between reversed two-word contexts.
 *
 * Requiring opposite preferred outputs verifies that the held-out loss decrease
 * was accompanied by the intended order-sensitive conditional behavior.
 * @param model Borrowed trained model configured with two context positions. */
static void cgai_test_neural_learned_order(const cgai_neural_model *model) {
    /* Step 1: Predict left from the a,b context using separate scratch storage. */
    cgai_neural_workspace *workspace = cgai_neural_workspace_create(model);
    TEST_CHECK(workspace != NULL, cgai_last_error());
    cgai_token_id context[] = {cgai_neural_lookup(model, "a"), cgai_neural_lookup(model, "b")};
    const size_t left = cgai_neural_lookup(model, "left").value;
    const size_t right = cgai_neural_lookup(model, "right").value;
    TEST_CHECK(cgai_neural_forward(model, context, workspace) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(workspace->probabilities[left] > 0.8, "a,b did not learn left");
    /* Step 2: Reverse the positions and require the opposite learned target. */
    context[0] = cgai_neural_lookup(model, "b");
    context[1] = cgai_neural_lookup(model, "a");
    TEST_CHECK(cgai_neural_forward(model, context, workspace) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(workspace->probabilities[right] > 0.8, "b,a did not learn right");
    cgai_neural_workspace_destroy(workspace);
}

/** @brief Evaluate independent held-out text before and after reproducible training.
 *
 * Both models start identically and receive the same shuffled epochs. Numerical
 * equality is required within one build and platform, not across architectures.
 * @param first Borrowed mutable model receiving the primary training run.
 * @param second Borrowed mutable identically initialized model for repeatability. */
static void cgai_test_neural_fit(cgai_neural_model *first, cgai_neural_model *second) {
    /* Step 1: Measure initial held-out loss and select deterministic Adam settings. */
    cgai_neural_metrics before;
    cgai_neural_metrics after;
    cgai_neural_training training = cgai_neural_default_training();
    training.epochs = 60U;
    training.learning_rate = 0.015;
    TEST_CHECK(cgai_neural_evaluate(first, cgai_test_order_heldout, &before) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: Train independent copies and require identical resulting parameters. */
    TEST_CHECK(cgai_neural_train(first, cgai_test_order_train, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_train(second, cgai_test_order_train, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(memcmp(first->parameters, second->parameters,
                      first->parameter_count * sizeof(*first->parameters)) == 0,
               "fixed-seed training was not reproducible");
    /* Step 3: Require material held-out improvement and print measured evidence. */
    TEST_CHECK(cgai_neural_evaluate(first, cgai_test_order_heldout, &after) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(after.cross_entropy < before.cross_entropy * 0.5,
               "ordered held-out loss did not improve sufficiently");
    TEST_CHECK(after.unknown_tokens == 0U && after.accuracy > 0.85,
               "controlled held-out predictions were inaccurate");
    printf("Neural controlled held-out CE: %.6f -> %.6f; accuracy %.6f\n", before.cross_entropy,
           after.cross_entropy, after.accuracy);
}

/** @brief Verify unknown training words cannot extend an established vocabulary.
 *
 * Later training calls update the fixed network using UNK for new spellings.
 * This fixture is separate from the held-out learning experiment. */
static void cgai_test_neural_frozen_training(void) {
    /* Step 1: Train one epoch on a spelling absent from the initial vocabulary. */
    cgai_neural_model *model = cgai_test_neural_fixture();
    const size_t before = model->vocabulary_size;
    cgai_neural_training training = cgai_neural_default_training();
    training.epochs = 1U;
    TEST_CHECK(cgai_neural_train(model, "unseen a", &training) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: Confirm the vocabulary remains frozen and lookup still returns UNK. */
    TEST_CHECK(model->vocabulary_size == before, "later training grew vocabulary");
    TEST_CHECK(cgai_neural_lookup(model, "unseen").value == CGAI_TOKEN_UNKNOWN,
               "unknown training spelling was interned");
    cgai_neural_destroy(model);
}

/** @brief Check reproducible training and held-out learning on an ordered task.
 *
 * A saved flat parameter block allows checking every parameter group for learning,
 * while two independent models establish reproducibility of initialization and fit. */
void cgai_test_neural_training(void) {
    /* Step 1: Create identical models and retain their initial parameter values. */
    cgai_neural_model *first = cgai_test_order_model();
    cgai_neural_model *second = cgai_test_order_model();
    const size_t bytes = first->parameter_count * sizeof(*first->parameters);
    double *before = malloc(bytes);
    TEST_CHECK(before != NULL, "parameter snapshot allocation failed");
    memcpy(before, first->parameters, bytes);
    TEST_CHECK(memcmp(first->parameters, second->parameters, bytes) == 0,
               "fixed-seed initialization was not reproducible");
    /* Step 2: Check learning, all parameter groups, and conditional word order. */
    cgai_test_neural_fit(first, second);
    cgai_test_neural_updated_groups(first, before);
    cgai_test_neural_learned_order(first);
    /* Step 3: Release owned storage and check the independent unknown-word case. */
    free(before);
    cgai_neural_destroy(second);
    cgai_neural_destroy(first);
    cgai_test_neural_frozen_training();
}
