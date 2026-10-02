/** @file test_gameplay_math.c @brief Hierarchical composition and exact gradient verification. */
#include "gameplay/gameplay_internal.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Create a complete compact architecture spanning every parameter family.
 * @return Initialized shape with two categorical tasks and two specialists. */
static cgai_gameplay_config math_config(void) {
    /* Step 1: Small complete dimensions make every scalar finite-difference check inexpensive. */
    cgai_gameplay_config config = {0};
    config.seed = 123U;
    config.routing_temperature = 0.8;
    config.feature_count = 2U;
    config.embedding_dimensions = 5U;
    config.hidden_dimensions = 3U;
    config.module_count = 2U;
    config.centroids_per_module = 3U;
    config.task_count = 2U;
    config.cardinalities[0] = 2U;
    config.cardinalities[1] = 3U;
    config.output_counts[0] = 3U;
    config.output_counts[1] = 2U;
    config.task_modules[0] = 3U;
    config.task_modules[1] = 3U;
    config.task_features[0] = 3U;
    config.task_features[1] = 1U;
    return config;
}

/** @brief Score a fixed independent target without changing network parameters.
 * @param session Borrowed numerical scratch.
 * @param example Borrowed target.
 * @return Finite unrestricted composed negative log likelihood. */
static double score(cgai_gameplay_session *session, const cgai_gameplay_example *example) {
    /* Step 1: Public scoring exercises the complete aligned forward path. */
    double loss = 0.0;
    TEST_CHECK(cgai_gameplay_evaluate(session, example, &loss) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(isfinite(loss) && loss >= 0.0, "invalid composed loss");
    return loss;
}

/** @brief Compare every scalar exact derivative with a symmetric finite difference.
 * @param session Exclusive scratch borrowing the mutable test model.
 * @param example Borrowed complete independent target. */
static void finite_differences(cgai_gameplay_session *session,
                               const cgai_gameplay_example *example) {
    /* Step 1: Obtain exact log-mixture responsibility derivatives for every parameter family. */
    cgai_gameplay_model *model = (cgai_gameplay_model *)session->model;
    double *gradient = calloc(model->parameter_count, sizeof(*gradient));
    TEST_CHECK(gradient != NULL, "could not allocate derivative check");
    (void)score(session, example);
    TEST_CHECK(cgai_gameplay_backward(session, example, gradient) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: Check embeddings, ordered encoder, bias, outer/inner centroids and both task heads.
     */
    const double epsilon = 1e-5;
    for (size_t i = 0U; i < model->parameter_count; ++i) {
        const double original = model->parameters[i];
        model->parameters[i] = original + epsilon;
        const double positive = score(session, example);
        model->parameters[i] = original - epsilon;
        const double negative = score(session, example);
        model->parameters[i] = original;
        const double numerical = (positive - negative) / (2.0 * epsilon);
        TEST_CHECK(fabs(numerical - gradient[i]) < 2e-7 * (1.0 + fabs(numerical)),
                   "hierarchical parameter gradient disagrees with finite difference");
    }
    free(gradient);
}

/** @brief Verify the public prediction is exactly the two-layer neural weighted mixture.
 * @param session Exclusive initialized scratch.
 * @param example Borrowed complete observation and target. */
static void direct_composition(cgai_gameplay_session *session,
                               const cgai_gameplay_example *example) {
    /* Step 1: Compare dense ordinary mixtures against the stable logarithmic implementation. */
    (void)score(session, example);
    const cgai_gameplay_model *model = session->model;
    for (size_t output = 0U; output < model->config.output_counts[example->task]; ++output) {
        double probability = 0.0;
        for (size_t module = 0U; module < model->config.module_count; ++module)
            for (size_t inner = 0U; inner < model->config.centroids_per_module; ++inner) {
                const size_t expert = module * model->config.centroids_per_module + inner;
                probability +=
                    session->alpha[module] * session->beta[expert] *
                    session->head_probabilities[expert * model->maximum_outputs + output];
            }
        TEST_CHECK(fabs(probability - session->outputs[output]) < 1e-14,
                   "composed probability differs from weighted specialist banks");
    }
}

/** @brief Assert a complete half-open derivative range is exactly zero.
 * @param gradient Borrowed complete derivative array.
 * @param first First scalar index in the range.
 * @param last First scalar index after the range. */
static void zero_gradient(const double *gradient, size_t first, size_t last) {
    /* Step 1: Excluded parameters have no numerical path to the requested target. */
    for (size_t i = first; i < last; ++i)
        TEST_CHECK(gradient[i] == 0.0, "excluded or unrequested parameter has a derivative");
}

/** @brief Verify excluded modules and unrequested task heads receive exactly zero derivatives.
 * @param session Exclusive initialized numerical scratch.
 * @param example Borrowed complete target. */
static void excluded_gradients(cgai_gameplay_session *session,
                               const cgai_gameplay_example *example) {
    /* Step 1: Request only the first compatible specialist and differentiate its full head. */
    const cgai_gameplay_model *model = session->model;
    double loss = 0.0;
    double *gradient = calloc(model->parameter_count, sizeof(*gradient));
    TEST_CHECK(gradient != NULL, "could not allocate exclusion gradient");
    TEST_CHECK(cgai_gameplay_evaluate_modules(session, example, 1U, &loss) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(session->alpha[0] == 1.0 && session->alpha[1] == 0.0,
               "excluded outer centroid retained probability mass");
    TEST_CHECK(cgai_gameplay_backward(session, example, gradient) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: Excluded routing and unrequested output parameters have no derivative. */
    const size_t outer = (size_t)(model->outer - model->parameters);
    const size_t inner = (size_t)(model->inner - model->parameters);
    zero_gradient(gradient, outer, outer + 2U * model->config.hidden_dimensions);
    zero_gradient(gradient, inner + 3U * model->config.hidden_dimensions,
                  inner + 6U * model->config.hidden_dimensions);
    zero_gradient(gradient, model->decoder_offsets[0] + 9U, model->decoder_offsets[0] + 18U);
    zero_gradient(gradient, model->head_offsets[0] + 9U, model->parameter_count);
    free(gradient);
}

/** @brief Check target loss and derivatives remain stable below ordinary probability underflow.
 * @param session Exclusive numerical scratch borrowing a mutable test model.
 * @param example Borrowed target with target IDone. */
static void logarithmic_underflow(cgai_gameplay_session *session,
                                  const cgai_gameplay_example *example) {
    /* Step 1: Make each expert's requested target probability too small for a double exponential.
     */
    cgai_gameplay_model *model = (cgai_gameplay_model *)session->model;
    memset(model->decoders[0], 0,
           model->config.module_count * model->config.hidden_dimensions * 3U * sizeof(double));
    for (size_t expert = 0U; expert < 6U; ++expert) {
        model->heads[0][expert * 3U] = 0.0;
        model->heads[0][expert * 3U + 1U] = -2000.0;
        model->heads[0][expert * 3U + 2U] = -3000.0;
    }
    TEST_CHECK(fabs(score(session, example) - 2000.0) < 1e-9,
               "underflow corrupted stable target log likelihood");
    TEST_CHECK(session->outputs[1] == 0.0,
               "test target unexpectedly avoided probability underflow");
    /* Step 2: Log posterior responsibilities still produce finite categorical derivatives. */
    double *gradient = calloc(model->parameter_count, sizeof(*gradient));
    TEST_CHECK(gradient != NULL, "could not allocate underflow derivative check");
    TEST_CHECK(cgai_gameplay_backward(session, example, gradient) == CGAI_STATUS_OK,
               cgai_last_error());
    free(gradient);
}

/** @brief Require public certainty queries retain bounded likelihoods and posterior diagnostics.
 * @param session Exclusive initialized nearly certain numerical scratch.
 * @param state Borrowed complete categorical observation. */
static void certainty_query(cgai_gameplay_session *session, const cgai_gameplay_state *state) {
    /* Step1: The public selection and posterior diagnostics remain bounded probabilities. */
    cgai_gameplay_request request;
    cgai_gameplay_result result;
    TEST_CHECK(cgai_gameplay_default_request(session->model, 0U, state, &request) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_gameplay_select(session, &request, &result) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(result.output == 1U && result.probability == 1.0,
               "roundoff produced likelihood outside the probability domain");
    TEST_CHECK(result.module_contributions[0] <= 1.0 && result.module_contributions[1] <= 1.0,
               "roundoff produced invalid specialist posterior probability");
}

/** @brief Verify nearly certain centroid mixtures cannot publish likelihood aboveone or negative
 * loss. */
static void bounded_roundoff(void) {
    /* Step1: Uniform eight-expert banks expose log-sum normalization rounding at certainty. */
    cgai_gameplay_config config = math_config();
    config.centroids_per_module = 8U;
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    memset(model->parameters, 0, model->parameter_count * sizeof(double));
    for (size_t expert = 0U; expert < 16U; ++expert)
        model->heads[0][expert * 3U + 1U] = 1000.0;
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    const cgai_gameplay_example example = {{{1U, 2U}}, 0U, 1U};
    TEST_CHECK(score(session, &example) == 0.0, "roundoff produced negative task loss");
    /* Step2: The bounded public query resolves the same nearly certain target. */
    certainty_query(session, &example.state);
    cgai_gameplay_session_destroy(session);
    cgai_gameplay_destroy(model);
}

/** @brief Require exact independent unit category coordinates whenever the shape supports them.
 * @param model Borrowed initialized model with enough category embedding coordinates. */
static void orthogonal_categories(const cgai_gameplay_model *model) {
    /* Step1: Exact unit directions avoid arbitrary category aliasing before learned updates. */
    const size_t dimensions = model->config.embedding_dimensions;
    TEST_CHECK(dimensions >= model->category_count, "orthogonal fixture lacks coordinates");
    for (size_t category = 0U; category < model->category_count; ++category)
        for (size_t coordinate = 0U; coordinate < dimensions; ++coordinate)
            TEST_CHECK(model->embeddings[category * dimensions + coordinate] ==
                           (category == coordinate ? 1.0 : 0.0),
                       "category initialization is not an exact orthogonal unit basis");
}

/** @brief Set nonzero module-local readouts so finite differences exercise every hidden path.
 * @param model Borrowed mutable initialized fixture. */
static void condition_readouts(cgai_gameplay_model *model) {
    /* Step1: Initialization retains neutral zero readouts without changing the seeded stream. */
    for (size_t task = 0U; task < model->config.task_count; ++task)
        for (size_t i = 0U; i < model->config.module_count * model->config.output_counts[task] *
                                    model->config.hidden_dimensions;
             ++i) {
            TEST_CHECK(model->decoders[task][i] == 0.0,
                       "conditional task readout was not initialized to zero");
            model->decoders[task][i] = 0.05 * (double)(i % 5U) - 0.1;
        }
}

/** @brief Verify the second task's complete derivatives and declared irrelevant observation.
 * @param session Exclusive initialized numerical scratch.
 * @param example Borrowed complete target, copied before selecting the second head. */
static void feature_relevance(cgai_gameplay_session *session,
                              const cgai_gameplay_example *example) {
    /* Step1: Exact derivatives include the independent second head and zero irrelevant inputs. */
    cgai_gameplay_example isolated = *example;
    isolated.task = 1U;
    finite_differences(session, &isolated);
    /* Step2: Changing the excluded category cannot change this task's numerical prediction. */
    const double before = score(session, &isolated);
    isolated.state.values[1] = 0U;
    TEST_CHECK(score(session, &isolated) == before, "irrelevant feature changed task prediction");
}

/** @brief Require every encoder weight is exactly the declared feature/category coordinate map.
 * @param model Borrowed initialized model with sufficient embedding and hidden dimensions. */
static void category_encoder(const cgai_gameplay_model *model) {
    /* Step1: Initial rows preserve categorical axes rather than mixing feature coordinates. */
    for (size_t hidden = 0U; hidden < model->config.hidden_dimensions; ++hidden)
        for (size_t input = 0U; input < model->input_count; ++input) {
            const size_t feature = hidden < model->config.cardinalities[0] ? 0U : 1U;
            const size_t source = feature * model->config.embedding_dimensions + hidden;
            const double expected = hidden < model->category_count && input == source ? 1.0 : 0.0;
            TEST_CHECK(model->encoder[hidden * model->input_count + input] == expected,
                       "structured encoder changed initial feature/category axis identity");
        }
}

/** @brief Require task masks preserve exact independent category activations through the structured
 * encoder.
 * @param session Exclusive structured initialized-model scratch. */
static void category_activations(cgai_gameplay_session *session) {
    /* Step1: Two distinct fields select global category axes one and four. */
    cgai_gameplay_example example = {{{1U, 2U}}, 0U, 1U};
    (void)score(session, &example);
    for (size_t hidden = 0U; hidden < 6U; ++hidden)
        TEST_CHECK(session->hidden[hidden] == (hidden == 1U || hidden == 4U ? tanh(1.0) : 0.0),
                   "structured encoder mixed independent feature categories");
    /* Step2: The second task masks fieldone, preserving only the first field's active category. */
    example.task = 1U;
    (void)score(session, &example);
    for (size_t hidden = 0U; hidden < 6U; ++hidden)
        TEST_CHECK(session->hidden[hidden] == (hidden == 1U ? tanh(1.0) : 0.0),
                   "structured category encoder violated task feature eligibility");
}

/** @brief Verify shape-only category alignment and complete repeated-seed parameter identity. */
static void structured_initialization(void) {
    /* Step1: Five input categories fit independent axes inside six hidden coordinates. */
    cgai_gameplay_config config = math_config();
    config.hidden_dimensions = 6U;
    cgai_gameplay_model *first = cgai_gameplay_create(&config);
    cgai_gameplay_model *second = cgai_gameplay_create(&config);
    TEST_CHECK(first != NULL && second != NULL, cgai_last_error());
    TEST_CHECK(
        memcmp(first->parameters, second->parameters, first->parameter_count * sizeof(double)) == 0,
        "structured category initialization changed repeated-seed model identity");
    category_encoder(first);
    /* Step2: Exact aligned activations and task masks hold before any target training. */
    cgai_gameplay_session *session = cgai_gameplay_session_create(first, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    category_activations(session);
    cgai_gameplay_session_destroy(session);
    cgai_gameplay_destroy(second);
    cgai_gameplay_destroy(first);
}

/** @brief Keep a finite dense encoder fallback when category axes do not fit embedding dimensions.
 */
static void dense_fallback(void) {
    /* Step1: Four embedding coordinates cannot represent five independent category axes. */
    cgai_gameplay_config config = math_config();
    config.embedding_dimensions = 4U;
    config.hidden_dimensions = 6U;
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(isfinite(model->encoder[0]) && model->encoder[0] != 0.0 && model->encoder[0] != 1.0,
               "small embedding shape lost its seeded finite dense fallback");
    condition_readouts(model);
    /* Step2: Full conditional-mixture gradients remain exact under the ordinary dense fallback. */
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    const cgai_gameplay_example example = {{{1U, 2U}}, 0U, 1U};
    finite_differences(session, &example);
    cgai_gameplay_session_destroy(session);
    cgai_gameplay_destroy(model);
}

/** @brief Verify both complete task losses and specialized feature masks.
 * @return Zero after all mathematical invariants pass. */
int main(void) {
    /* Step 1: Verify exact derivatives and dense composition for each distinct task head. */
    const cgai_gameplay_config config = math_config();
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    orthogonal_categories(model);
    condition_readouts(model);
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    cgai_gameplay_example example = {{{1U, 2U}}, 0U, 1U};
    finite_differences(session, &example);
    direct_composition(session, &example);
    excluded_gradients(session, &example);
    feature_relevance(session, &example);
    /* Step 2: Exercise stable mixtures at categorical likelihood underflow. */
    logarithmic_underflow(session, &example);
    bounded_roundoff();
    structured_initialization();
    dense_fallback();
    cgai_gameplay_session_destroy(session);
    cgai_gameplay_destroy(model);
    return 0;
}
