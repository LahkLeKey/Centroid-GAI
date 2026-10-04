/** @file test_npc_v2_optimizer.c @brief Exact balance derivatives and Adam continuation. */
#include "gameplay/gameplay_internal.h"
#include "npc_optimizer.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Complete scalar count of the compact independent derivative fixture. */
#define OPTIMIZER_SCALARS 142U

/** Small numerical owner exercising every parameter family without gameplay outcomes. */
typedef struct optimizer_fixture {
    cgai_gameplay_model *model;         /**< Exclusive compact test model. */
    cgai_gameplay_session *session;     /**< Matching reusable derivative scratch. */
    double gradient[OPTIMIZER_SCALARS]; /**< Combined objective derivatives. */
    double ordinary[OPTIMIZER_SCALARS]; /**< Independent ordinary NLL derivatives. */
} optimizer_fixture;

/** @brief Construct a compact two-module network with one task-irrelevant field.
 * @return Complete finite derivative-test configuration. */
static cgai_gameplay_config optimizer_config(void) {
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
    config.task_modules[0] = config.task_modules[1] = 3U;
    config.task_features[0] = 3U;
    config.task_features[1] = 1U;
    return config;
}

/** @brief Initialize nonzero readouts to exercise NLL and routing encoder derivatives.
 * @param model Exclusive compact model. */
static void optimizer_readouts(cgai_gameplay_model *model) {
    for (size_t task = 0U; task < model->config.task_count; ++task) {
        const size_t count =
            2U * model->config.output_counts[task] * model->config.hidden_dimensions;
        for (size_t index = 0U; index < count; ++index)
            model->decoders[task][index] = 0.01 * ((double)(index % 7U) - 3.0);
    }
}

/** @brief Acquire a complete compact model and reusable numerical ownership.
 * @return Valid initialized test owner. */
static optimizer_fixture optimizer_create(void) {
    const cgai_gameplay_config config = optimizer_config();
    optimizer_fixture fixture = {0};
    fixture.model = cgai_gameplay_create(&config);
    TEST_CHECK(fixture.model != NULL, cgai_last_error());
    TEST_CHECK(fixture.model->parameter_count == OPTIMIZER_SCALARS,
               "compact fixture parameter accounting changed");
    optimizer_readouts(fixture.model);
    fixture.session = cgai_gameplay_session_create(fixture.model, 0U);
    TEST_CHECK(fixture.session != NULL, "could not acquire optimizer derivative fixture");
    return fixture;
}

/** @brief Release the complete compact derivative owner.
 * @param fixture Exclusive owner, accepting successful complete allocation. */
static void optimizer_destroy(optimizer_fixture *fixture) {
    cgai_gameplay_session_destroy(fixture->session);
    cgai_gameplay_destroy(fixture->model);
}

/** @brief Independently score NLL plus reverse-uniform KL using stable forward logarithms.
 * @param fixture Exclusive reusable forward scratch.
 * @param example Complete categorical input and independent target.
 * @return Finite combined objective. */
static double optimizer_loss(optimizer_fixture *fixture, const cgai_gameplay_example *example) {
    double nll = 0.0;
    TEST_CHECK(cgai_gameplay_evaluate(fixture->session, example, &nll), cgai_last_error());
    const double loss =
        nll -
        NPC_ROUTING_BALANCE *
            (0.5 * (fixture->session->log_alpha[0] + fixture->session->log_alpha[1]) + log(2.0));
    TEST_CHECK(isfinite(loss), "combined objective is not finite");
    return loss;
}

/** @brief Compare every combined parameter derivative with an independent symmetric difference.
 * @param fixture Exclusive mutable numerical fixture.
 * @param example Complete task-specific categorical input and target. */
static void optimizer_differences(optimizer_fixture *fixture,
                                  const cgai_gameplay_example *example) {
    (void)optimizer_loss(fixture, example);
    TEST_CHECK(npc_v2_optimizer_gradient(fixture->session, example, fixture->gradient),
               cgai_last_error());
    const double epsilon = 1e-5;
    for (size_t index = 0U; index < fixture->model->parameter_count; ++index) {
        const double original = fixture->model->parameters[index];
        fixture->model->parameters[index] = original + epsilon;
        const double positive = optimizer_loss(fixture, example);
        fixture->model->parameters[index] = original - epsilon;
        const double negative = optimizer_loss(fixture, example);
        fixture->model->parameters[index] = original;
        const double numerical = (positive - negative) / (2.0 * epsilon);
        TEST_CHECK(fabs(numerical - fixture->gradient[index]) < 2e-7 * (1.0 + fabs(numerical)),
                   "combined parameter derivative disagrees with finite difference");
    }
}

/** @brief Require exact zero derivatives over one inactive contiguous parameter interval.
 * @param gradient Borrowed combined derivatives.
 * @param begin Inclusive inactive scalar offset.
 * @param end Exclusive inactive scalar offset. */
static void optimizer_zeros(const double *gradient, size_t begin, size_t end) {
    for (size_t index = begin; index < end; ++index)
        TEST_CHECK(gradient[index] == 0.0, "inactive parameter received an auxiliary derivative");
}

/** @brief Verify routing auxiliary work leaves internal experts and task readouts unchanged.
 * @param fixture Exclusive compact numerical owner.
 * @param example Matching complete task input and target. */
static void optimizer_parameter_support(optimizer_fixture *fixture,
                                        const cgai_gameplay_example *example) {
    (void)optimizer_loss(fixture, example);
    TEST_CHECK(cgai_gameplay_backward(fixture->session, example, fixture->ordinary),
               cgai_last_error());
    TEST_CHECK(npc_v2_optimizer_gradient(fixture->session, example, fixture->gradient),
               cgai_last_error());
    const size_t inner = (size_t)(fixture->model->inner - fixture->model->parameters);
    for (size_t index = inner; index < fixture->model->parameter_count; ++index)
        TEST_CHECK(fixture->gradient[index] == fixture->ordinary[index],
                   "routing auxiliary loss changed inner routing, heads or decoders");
    optimizer_zeros(fixture->gradient, 0U, 5U);
    optimizer_zeros(fixture->gradient, 10U, 25U);
    const size_t heads = fixture->model->head_offsets[0];
    optimizer_zeros(fixture->gradient, heads, heads + 18U);
    const size_t decoder = fixture->model->decoder_offsets[0];
    optimizer_zeros(fixture->gradient, decoder, decoder + 18U);
}

/** @brief Check both task feature masks and every shared routing derivative family. */
static void optimizer_math(void) {
    optimizer_fixture fixture = optimizer_create();
    cgai_gameplay_example example = {{{1U, 2U}}, 0U, 2U};
    optimizer_differences(&fixture, &example);
    example.task = 1U;
    example.target = 1U;
    optimizer_differences(&fixture, &example);
    optimizer_parameter_support(&fixture, &example);
    optimizer_destroy(&fixture);
}

/** @brief Separate outer centroids enough to underflow one prior while retaining finite logs.
 * @param fixture Exclusive compact model with a successful current forward. */
static void optimizer_collapse(optimizer_fixture *fixture) {
    for (size_t hidden = 0U; hidden < fixture->model->config.hidden_dimensions; ++hidden) {
        fixture->model->outer[hidden] = fixture->session->hidden[hidden];
        fixture->model->outer[3U + hidden] = fixture->session->hidden[hidden];
    }
    fixture->model->outer[3] += 32.0;
}

/** @brief Verify the stable reverse KL retains a restorative derivative for an underflowed prior.
 */
static void optimizer_dead_route(void) {
    optimizer_fixture fixture = optimizer_create();
    const cgai_gameplay_example example = {{{1U, 2U}}, 0U, 2U};
    (void)optimizer_loss(&fixture, &example);
    optimizer_collapse(&fixture);
    const double before = optimizer_loss(&fixture, &example);
    const double route = fixture.session->log_alpha[1];
    TEST_CHECK(fixture.session->alpha[1] == 0.0 && isfinite(route),
               "collapse fixture did not exercise stable underflowed routing");
    TEST_CHECK(cgai_gameplay_backward(fixture.session, &example, fixture.ordinary),
               cgai_last_error());
    TEST_CHECK(npc_v2_optimizer_gradient(fixture.session, &example, fixture.gradient),
               cgai_last_error());
    const size_t outer = (size_t)(fixture.model->outer - fixture.model->parameters);
    for (size_t index = outer; index < outer + 6U; ++index)
        fixture.model->parameters[index] -=
            1e-4 * (fixture.gradient[index] - fixture.ordinary[index]);
    TEST_CHECK(optimizer_loss(&fixture, &example) < before && fixture.session->log_alpha[1] > route,
               "reverse KL did not restore a dead routing log probability");
    optimizer_destroy(&fixture);
}

/** @brief Require invalid one-module eligibility to preserve parameters and optimizer ownership. */
static void optimizer_rejection(void) {
    optimizer_fixture fixture = optimizer_create();
    const cgai_gameplay_example example = {{{1U, 2U}}, 0U, 2U};
    const npc_v2_update_settings settings = {0.001, 5.0, 0.05};
    memcpy(fixture.gradient, fixture.model->parameters,
           fixture.model->parameter_count * sizeof(*fixture.gradient));
    fixture.model->config.task_modules[0] = 1U;
    TEST_CHECK(!npc_v2_optimizer_step(fixture.model, &example, &settings),
               "routing objective admitted one-module training");
    TEST_CHECK(fixture.model->adam_first == NULL && fixture.model->adam_second == NULL &&
                   fixture.model->training_epochs == 0U && fixture.model->training_step == 0U &&
                   fixture.model->training_shuffle == 0U &&
                   memcmp(fixture.gradient, fixture.model->parameters,
                          fixture.model->parameter_count * sizeof(*fixture.gradient)) == 0,
               "rejected routing eligibility mutated continuation ownership");
    optimizer_destroy(&fixture);
}

/** @brief Require identical complete parameters, moments, shuffle state and update counters.
 * @param first First complete continuation result.
 * @param second Independently segmented continuation result. */
static void optimizer_same(const cgai_gameplay_model *first, const cgai_gameplay_model *second) {
    const size_t bytes = first->parameter_count * sizeof(*first->parameters);
    TEST_CHECK(first->parameter_count == second->parameter_count &&
                   memcmp(first->parameters, second->parameters, bytes) == 0 &&
                   memcmp(first->adam_first, second->adam_first, bytes) == 0 &&
                   memcmp(first->adam_second, second->adam_second, bytes) == 0 &&
                   first->training_step == second->training_step &&
                   first->training_epochs == second->training_epochs &&
                   first->training_shuffle == second->training_shuffle,
               "segmented routing-balanced continuation changed exact optimizer state");
}

/** Apply a caller-ordered event sequence; production admission belongs to the Life trainer. */
static void optimizer_events(optimizer_fixture *fixture, unsigned int begin, unsigned int count) {
    const cgai_gameplay_example examples[] = {{{{1U, 2U}}, 0U, 2U}, {{{0U, 1U}}, 1U, 1U}};
    const npc_v2_update_settings settings = {0.001, 5.0, 0.05};
    for (unsigned int i = begin; i < begin + count; ++i)
        TEST_CHECK(npc_v2_optimizer_step(fixture->model, &examples[i % 2U], &settings),
                   cgai_last_error());
}

/** Identical admitted events preserve exact moments independently of call grouping. */
static void optimizer_continuation(void) {
    optimizer_fixture first = optimizer_create();
    optimizer_fixture second = optimizer_create();
    optimizer_events(&first, 0U, 4U);
    optimizer_events(&second, 0U, 2U);
    optimizer_events(&second, 2U, 2U);
    optimizer_same(first.model, second.model);
    TEST_CHECK(first.model->training_step == 4U && first.model->training_epochs == 0U &&
                   first.model->training_shuffle == 0U,
               "one-event primitive changed epoch or scheduler-owned shuffle state");
    optimizer_destroy(&second);
    optimizer_destroy(&first);
}

/** A failed derivative leaves even initial moment ownership unpublished. */
static void optimizer_nonfinite_rejection(void) {
    optimizer_fixture fixture = optimizer_create();
    const cgai_gameplay_example example = {{{1U, 2U}}, 0U, 2U};
    const npc_v2_update_settings settings = {0.001, 5.0, 0.05};
    fixture.model->encoder[0] = INFINITY;
    memcpy(fixture.gradient, fixture.model->parameters, sizeof(fixture.gradient));
    TEST_CHECK(!npc_v2_optimizer_step(fixture.model, &example, &settings),
               "nonfinite forward was admitted as a numerical update");
    TEST_CHECK(fixture.model->adam_first == NULL && fixture.model->adam_second == NULL &&
                   fixture.model->training_step == 0U &&
                   memcmp(fixture.gradient, fixture.model->parameters, sizeof(fixture.gradient)) ==
                       0,
               "failed derivative published initial moments or weights");
    optimizer_destroy(&fixture);
}

/** A bad final moment rejects all earlier scalar proposals without partial publication. */
static void optimizer_late_rejection(void) {
    optimizer_fixture fixture = optimizer_create();
    const cgai_gameplay_example example = {{{1U, 2U}}, 0U, 2U};
    const npc_v2_update_settings settings = {0.001, 5.0, 0.05};
    double second[OPTIMIZER_SCALARS];
    TEST_CHECK(npc_v2_optimizer_step(fixture.model, &example, &settings), cgai_last_error());
    fixture.model->adam_second[OPTIMIZER_SCALARS - 1U] = -1.0;
    memcpy(fixture.gradient, fixture.model->parameters, sizeof(fixture.gradient));
    memcpy(fixture.ordinary, fixture.model->adam_first, sizeof(fixture.ordinary));
    memcpy(second, fixture.model->adam_second, sizeof(second));
    TEST_CHECK(!npc_v2_optimizer_step(fixture.model, &example, &settings),
               "negative final variance was accepted");
    TEST_CHECK(
        fixture.model->training_step == 1U &&
            memcmp(fixture.gradient, fixture.model->parameters, sizeof(fixture.gradient)) == 0 &&
            memcmp(fixture.ordinary, fixture.model->adam_first, sizeof(fixture.ordinary)) == 0 &&
            memcmp(second, fixture.model->adam_second, sizeof(second)) == 0,
        "late rejected scalar partially changed parameters or moments");
    optimizer_destroy(&fixture);
}
/** @brief Execute independent mathematical and exact continuation verification.
 * @return Zero after every check passes. */
int main(void) {
    optimizer_math();
    optimizer_dead_route();
    optimizer_rejection();
    optimizer_continuation();
    optimizer_nonfinite_rejection();
    optimizer_late_rejection();
    puts("NPC v2 optimizer: exact combined gradients, dead routing and event numerics pass");
    return EXIT_SUCCESS;
}
