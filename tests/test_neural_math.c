/** @file test_neural_math.c @brief Finite-difference and forward invariants for neural routing. */
#include "test_neural.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Prepare nonsymmetric parameters with moderate finite values.
 *
 * Every slice receives the same deterministic scalar recipe so numerical checks
 * exercise embeddings, encoder weights, bias, centroids, and output logits.
 * @param model Borrowed mutable model whose parameters are overwritten. */
static void cgai_test_neural_parameters(cgai_neural_model *model) {
    /* Step 1: Fill the flat parameter block without changing its slice layout. */
    for (size_t index = 0U; index < model->parameter_count; ++index) {
        model->parameters[index] = 0.4 * sin((double)index * 1.7 + 0.3);
    }
}

/** @brief Evaluate one loss after the caller has perturbed a parameter.
 *
 * The repeated input token ensures backward propagation must add two positional
 * contributions into a single embedding row.
 * @param model Borrowed model whose parameters are read without mutation.
 * @param workspace Borrowed scratch storage associated with model.
 * @return Finite scalar loss for target b, or terminate on forward failure. */
static double cgai_test_neural_repeated_loss(const cgai_neural_model *model,
                                             cgai_neural_workspace *workspace) {
    /* Step 1: Construct repeated input IDs and compute the current prediction. */
    const cgai_token_id token = cgai_neural_lookup(model, "a");
    const cgai_token_id context[] = {token, token};
    TEST_CHECK(cgai_neural_forward(model, context, workspace) == CGAI_STATUS_OK, cgai_last_error());
    /* Step 2: Return the selected target's exact log-space loss. */
    return cgai_neural_loss(model, workspace, cgai_neural_lookup(model, "b"));
}

/** @brief Compare every stored derivative against an independent central difference.
 *
 * Each parameter is restored before moving on. Masked BOS logits and unused
 * embeddings are included, so their numerical derivatives must also match zero.
 * @param model Borrowed mutable fixture restored to its original values on success.
 * @param workspace Borrowed scratch storage associated with model.
 * @param gradient Borrowed parameter_count analytical derivatives. */
static void cgai_test_neural_differences(cgai_neural_model *model, cgai_neural_workspace *workspace,
                                         const double *gradient) {
    /* Step 1: Perturb one scalar in each direction while holding all others fixed. */
    const double epsilon = 1e-5;
    for (size_t index = 0U; index < model->parameter_count; ++index) {
        const double saved = model->parameters[index];
        model->parameters[index] = saved + epsilon;
        const double upper = cgai_test_neural_repeated_loss(model, workspace);
        model->parameters[index] = saved - epsilon;
        const double lower = cgai_test_neural_repeated_loss(model, workspace);
        model->parameters[index] = saved;
        /* Step 2: Match derivatives with a combined absolute and relative tolerance. */
        const double numerical = (upper - lower) / (2.0 * epsilon);
        TEST_CHECK(isfinite(gradient[index]), "nonfinite analytical derivative");
        TEST_CHECK(fabs(numerical - gradient[index]) < 2e-7 * (1.0 + fabs(numerical)),
                   "neural gradient disagrees with central difference");
    }
}

/** @brief Compute and validate all parameter gradients for a repeated-token input.
 *
 * The output array has one scalar per trainable parameter and is independent of
 * both model storage and workspace storage, as required by the gradient contract.
 * @param model Borrowed mutable fixture whose parameters remain unchanged.
 * @param workspace Borrowed scratch storage associated with model. */
static void cgai_test_neural_gradient_check(cgai_neural_model *model,
                                            cgai_neural_workspace *workspace) {
    /* Step 1: Allocate analytical derivatives and form the repeated-token context. */
    double *gradient = calloc(model->parameter_count, sizeof(*gradient));
    TEST_CHECK(gradient != NULL, "gradient allocation failed");
    const cgai_token_id token = cgai_neural_lookup(model, "a");
    const cgai_token_id context[] = {token, token};
    /* Step 2: Differentiate once, then compare with separately recomputed losses. */
    TEST_CHECK(cgai_neural_gradient(model, context, cgai_neural_lookup(model, "b"), workspace,
                                    gradient) == CGAI_STATUS_OK,
               cgai_last_error());
    cgai_test_neural_differences(model, workspace, gradient);
    /* Step 3: Release the independent derivative storage. */
    free(gradient);
}

/** @brief Check probability normalization and hard exclusion of BOS output.
 *
 * The mixture must sum to one across vocabulary outputs, and routing must sum to
 * one across centroids. EOS and unknown remain ordinary allowed output targets.
 * @param model Borrowed model whose parameters remain unchanged.
 * @param workspace Borrowed scratch storage associated with model. */
static void cgai_test_neural_normalization(const cgai_neural_model *model,
                                           cgai_neural_workspace *workspace) {
    /* Step 1: Refresh probabilities after prior numerical perturbations. */
    (void)cgai_test_neural_repeated_loss(model, workspace);
    double total = 0.0;
    double gates = 0.0;
    for (size_t index = 0U; index < model->vocabulary_size; ++index) {
        TEST_CHECK(workspace->probabilities[index] >= 0.0, "negative probability");
        total += workspace->probabilities[index];
    }
    /* Step 2: Check both normalization levels and the excluded output token. */
    for (size_t index = 0U; index < model->config.centroid_count; ++index) {
        gates += workspace->gates[index];
    }
    TEST_CHECK(fabs(total - 1.0) < 1e-12 && fabs(gates - 1.0) < 1e-12,
               "mixture or routing is not normalized");
    TEST_CHECK(workspace->probabilities[CGAI_TOKEN_BOS] == 0.0, "BOS was predictable");
}

/** @brief Distinguish contexts with identical words in reversed positions.
 *
 * Concatenated embeddings must preserve position through the encoder and output
 * mixture; an averaged bag of words would make these predictions identical.
 * @param model Borrowed model with two context positions.
 * @param workspace Borrowed scratch storage associated with model. */
static void cgai_test_neural_order(const cgai_neural_model *model,
                                   cgai_neural_workspace *workspace) {
    /* Step 1: Record one target probability for the ordered context a,b. */
    cgai_token_id context[] = {cgai_neural_lookup(model, "a"), cgai_neural_lookup(model, "b")};
    TEST_CHECK(cgai_neural_forward(model, context, workspace) == CGAI_STATUS_OK, cgai_last_error());
    const double first = workspace->probabilities[cgai_neural_lookup(model, "left").value];
    /* Step 2: Reverse the positions and require a measurably different prediction. */
    context[0] = cgai_neural_lookup(model, "b");
    context[1] = cgai_neural_lookup(model, "a");
    TEST_CHECK(cgai_neural_forward(model, context, workspace) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(fabs(first - workspace->probabilities[cgai_neural_lookup(model, "left").value]) >
                   1e-8,
               "encoder ignored token order");
}

/** @brief Verify context construction reads only the available causal prefix.
 *
 * The target and future values are sentinel IDs, making any accidental inclusion
 * visible without relying on numerical prediction differences.
 * @param model Borrowed fixture with exactly two context positions. */
static void cgai_test_neural_causal_context(const cgai_neural_model *model) {
    /* Step 1: Check the first target receives only BOS padding. */
    const cgai_token_id sequence[] = {{3U}, {4U}, {SIZE_MAX}, {SIZE_MAX}};
    cgai_token_id context[2];
    cgai_neural_context(model, sequence, 0U, context);
    TEST_CHECK(context[0].value == CGAI_TOKEN_BOS && context[1].value == CGAI_TOKEN_BOS,
               "initial context leaked a target");
    /* Step 2: Check partial and full windows preserve chronological order. */
    cgai_neural_context(model, sequence, 1U, context);
    TEST_CHECK(context[0].value == CGAI_TOKEN_BOS && context[1].value == 3U,
               "partial context is not left padded");
    cgai_neural_context(model, sequence, 2U, context);
    TEST_CHECK(context[0].value == 3U && context[1].value == 4U,
               "context read the target or reversed positions");
}

/** @brief Verify finite gradients and zero derivatives for every masked output logit.
 *
 * Traversing the allocated flat block directly keeps all assertions within its bounds.
 * @param model Borrowed model defining parameter layout.
 * @param gradient Borrowed parameter_count derivatives. */
static void cgai_test_neural_masked_gradient(const cgai_neural_model *model,
                                             const double *gradient) {
    /* Step 1: Identify the start of the expert-logit slice. */
    const size_t offset = (size_t)(model->logits - model->parameters);
    /* Step 2: Check every derivative, including the first (BOS) column of each expert. */
    for (size_t index = 0U; index < model->parameter_count; ++index) {
        TEST_CHECK(isfinite(gradient[index]), "rare-target gradient was not finite");
        if (index >= offset && (index - offset) % model->vocabulary_size == 0U) {
            TEST_CHECK(gradient[index] == 0.0, "masked BOS logit received a gradient");
        }
    }
}

/** @brief Require target posterior mass to stay normalized at extreme likelihood scales.
 * @param model Borrowed model defining the centroid count.
 * @param workspace Borrowed completed gradient workspace. */
static void cgai_test_neural_responsibility_mass(const cgai_neural_model *model,
                                                 const cgai_neural_workspace *workspace) {
    /* Step 1: Sum target posteriors independently of the forward prediction. */
    double total = 0.0;
    for (size_t row = 0U; row < model->config.centroid_count; ++row)
        total += workspace->responsibilities[row];
    /* Step 2: A normalized mixture must distribute one unit of responsibility. */
    TEST_CHECK(fabs(total - 1.0) < 1e-12, "rare-target responsibilities lost normalization");
}

/** @brief Check finite exact loss and gradients after an output probability underflows.
 *
 * Each component assigns a very negative target logit, keeping the
 * mathematical target probability positive while its double representation is zero.
 * @param model Borrowed mutable fixture; output logits are overwritten for this last scenario.
 * @param workspace Borrowed scratch storage associated with model.
 * @param logit Finite target score small enough to underflow its linear probability. */
static void cgai_test_neural_extreme_loss(cgai_neural_model *model,
                                          cgai_neural_workspace *workspace, double logit) {
    /* Step 1: Make the b target extremely unlikely in every component. */
    const cgai_token_id target = cgai_neural_lookup(model, "b");
    const cgai_token_id context[] = {{3U}, {3U}};
    for (size_t centroid = 0U; centroid < model->config.centroid_count; ++centroid) {
        model->logits[centroid * model->vocabulary_size + target.value] = logit;
    }
    double *gradient = calloc(model->parameter_count, sizeof(*gradient));
    TEST_CHECK(gradient != NULL, "extreme gradient allocation failed");
    /* Step 2: Differentiate in log space despite the underflowed linear probability. */
    TEST_CHECK(cgai_neural_gradient(model, context, target, workspace, gradient) == CGAI_STATUS_OK,
               cgai_last_error());
    const double loss = cgai_neural_loss(model, workspace, target);
    TEST_CHECK(isfinite(loss) && loss > 999.0, "rare-target loss lost numerical stability");
    TEST_CHECK(workspace->probabilities[target.value] == 0.0,
               "extreme test failed to exercise probability underflow");
    /* Step 3: Require finite gradients and exact zero derivatives for masked BOS logits. */
    cgai_test_neural_masked_gradient(model, gradient);
    cgai_test_neural_responsibility_mass(model, workspace);
    free(gradient);
}

/** @brief Keep the mean finite when adding individual finite losses would overflow.
 *
 * The true average remains representable even though the sum of two rare-word
 * losses is not. Perplexity may legitimately overflow as documented. */
static void cgai_test_neural_extreme_mean(void) {
    /* Step 1: Give a repeated evaluation word an extremely small probability. */
    cgai_neural_model *model = cgai_test_neural_fixture();
    const cgai_token_id target = cgai_neural_lookup(model, "a");
    for (size_t centroid = 0U; centroid < model->config.centroid_count; ++centroid) {
        model->logits[centroid * model->vocabulary_size + target.value] = -1e308;
    }
    /* Step 2: Score two rare tokens and EOS; verify the finite two-thirds mean. */
    cgai_neural_metrics metrics;
    TEST_CHECK(cgai_neural_evaluate(model, "a a", &metrics) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(isfinite(metrics.cross_entropy), "finite mean overflowed during evaluation");
    TEST_CHECK(fabs(metrics.cross_entropy / 1e308 - 2.0 / 3.0) < 1e-12,
               "extreme evaluation mean is inaccurate");
    cgai_neural_destroy(model);
}

/** @brief Check gradients, normalized mixtures, masking, and positional encoding.
 *
 * One compact fixture and one independent workspace are shared across numerical
 * checks, then released. Assertions terminate before unsafe follow-up operations. */
void cgai_test_neural_math(void) {
    /* Step 1: Construct a nonsymmetric fixture and associated scratch storage. */
    cgai_neural_model *model = cgai_test_neural_fixture();
    cgai_test_neural_parameters(model);
    cgai_neural_workspace *workspace = cgai_neural_workspace_create(model);
    TEST_CHECK(workspace != NULL, cgai_last_error());
    /* Step 2: Verify calculus and the distribution and context invariants. */
    cgai_test_neural_gradient_check(model, workspace);
    cgai_test_neural_normalization(model, workspace);
    cgai_test_neural_order(model, workspace);
    cgai_test_neural_causal_context(model);
    cgai_test_neural_extreme_loss(model, workspace, -1000.0);
    cgai_test_neural_extreme_loss(model, workspace, -1e308);
    /* Step 3: Release dependent scratch storage before its borrowed model. */
    cgai_neural_workspace_destroy(workspace);
    cgai_neural_destroy(model);
    cgai_test_neural_extreme_mean();
}
