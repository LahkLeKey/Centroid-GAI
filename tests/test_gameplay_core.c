/** @file test_gameplay_core.c @brief Typed composition budgets, failure isolation and exact Adam
 * replay. */
#include "gameplay/gameplay_internal.h"
#include "internal/file_utils.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Independent examples cover two task domains at each complete categorical state. */
static const cgai_gameplay_example records[] = {{{{0U}}, 0U, 0U}, {{{1U}}, 0U, 1U},
                                                {{{2U}}, 0U, 2U}, {{{0U}}, 1U, 0U},
                                                {{{1U}}, 1U, 1U}, {{{2U}}, 1U, 0U}};
/** Test-local full-state checkpoint paths, isolated by the CTest working directory. */
static const char before_path[] = "gameplay-core-before.cgcheckpoint";
/** Secondary exact checkpoint path for validation and continuation comparisons. */
static const char after_path[] = "gameplay-core-after.cgcheckpoint";
/** Test-local compact inference artifact path. */
static const char inference_path[] = "gameplay-core-model.cggp";

/** @brief Return a bounded aligned two-task architecture.
 * @return Complete valid compact shape. */
static cgai_gameplay_config core_config(void) {
    /* Step 1: Keep the ownership/continuation fixture small while retaining all network layers. */
    cgai_gameplay_config config = {0};
    config.seed = 42U;
    config.routing_temperature = 1.0;
    config.feature_count = 1U;
    config.embedding_dimensions = 3U;
    config.hidden_dimensions = 4U;
    config.module_count = 2U;
    config.centroids_per_module = 6U;
    config.task_count = 2U;
    config.cardinalities[0] = 3U;
    config.output_counts[0] = 3U;
    config.output_counts[1] = 2U;
    config.task_modules[0] = 3U;
    config.task_modules[1] = 3U;
    config.task_features[0] = 1U;
    config.task_features[1] = 1U;
    return config;
}

/** @brief Create a checked initialized compact network.
 * @return Owned initialized model. */
static cgai_gameplay_model *fixture(void) {
    /* Step 1: Use public initialization for every independent fixture. */
    const cgai_gameplay_config config = core_config();
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    return model;
}

/** @brief Require exact identity of checkpoint weights, moments, counters and shuffling state. */
static void equal_checkpoints(void) {
    /* Step 1: Read two complete owned byte snapshots. */
    uint8_t *before = NULL;
    uint8_t *after = NULL;
    size_t before_count = 0U;
    size_t after_count = 0U;
    TEST_CHECK(cgai_file_read_all(before_path, &before, &before_count) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_file_read_all(after_path, &after, &after_count) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: The complete published checkpoint must be physically identical. */
    TEST_CHECK(before_count == after_count && memcmp(before, after, before_count) == 0,
               "composed model continuation state changed unexpectedly");
    free(before);
    free(after);
}

/** @brief Check a valid first target does not execute before a malformed later record fails.
 * @param model Borrowed mutable fixture.
 * @param invalid Borrowed deliberately malformed second target. */
static void reject_record(cgai_gameplay_model *model, const cgai_gameplay_example *invalid) {
    /* Step 1: Complete preflight must precede Adam allocation, shuffling and all updates. */
    const cgai_gameplay_example examples[] = {records[0], *invalid};
    const cgai_gameplay_training training = {1U, 0.01, 5.0, 0.0};
    TEST_CHECK(cgai_gameplay_train_continue(model, examples, 2U, &training) == CGAI_STATUS_ERROR,
               "invalid later gameplay record was accepted");
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, after_path) == CGAI_STATUS_OK,
               cgai_last_error());
    equal_checkpoints();
}

/** @brief Reject unsupported parameter shrinkage without changing exact continuation state.
 * @param model Borrowed mutable initialized fixture.
 * @param decay Deliberately invalid decoupled shrinkage. */
static void invalid_decay(cgai_gameplay_model *model, double decay) {
    /* Step1: Decay validation precedes moment allocation, shuffling and all parameter updates. */
    const cgai_gameplay_training training = {1U, 0.01, 5.0, decay};
    TEST_CHECK(cgai_gameplay_train_continue(model, records, 6U, &training) == CGAI_STATUS_ERROR,
               "unsupported gameplay parameter shrinkage was accepted");
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, after_path) == CGAI_STATUS_OK,
               cgai_last_error());
    equal_checkpoints();
}

/** @brief Verify invalid targets/settings preserve fresh and populated optimizer state.
 * @param model Borrowed initialized mutable model. */
static void invalid_training(cgai_gameplay_model *model) {
    /* Step 1: Snapshot full state before checking each malformed complete target. */
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, before_path) == CGAI_STATUS_OK,
               cgai_last_error());
    const cgai_gameplay_example invalid[] = {
        {{{3U}}, 0U, 0U}, {{{0U, 1U}}, 0U, 0U}, {{{0U}}, 2U, 0U}, {{{0U}}, 0U, 3U}};
    for (size_t i = 0U; i < 4U; ++i)
        reject_record(model, &invalid[i]);
    /* Step 2: Nonfinite optimizer controls also fail before any continuation mutation. */
    const cgai_gameplay_training training = {1U, NAN, 5.0, 0.0};
    TEST_CHECK(cgai_gameplay_train_continue(model, records, 6U, &training) == CGAI_STATUS_ERROR,
               "nonfinite gameplay training rate was accepted");
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, after_path) == CGAI_STATUS_OK,
               cgai_last_error());
    equal_checkpoints();
    invalid_decay(model, NAN);
    invalid_decay(model, INFINITY);
    invalid_decay(model, -0.01);
    invalid_decay(model, 1.01);
}

/** @brief Check exact session caps and complete model resource accounting.
 * @param model Borrowed initialized model. */
static void resource_caps(cgai_gameplay_model *model) {
    /* Step 1: Requested scratch cap includes both session ownership and all numerical arrays. */
    cgai_gameplay_resources resources;
    TEST_CHECK(cgai_gameplay_get_resources(model, &resources) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(resources.parameter_bytes == model->parameter_count * sizeof(double),
               "gameplay parameter bytes are incomplete");
    TEST_CHECK(resources.model_bytes ==
                   sizeof(*model) + resources.parameter_bytes + resources.optimizer_bytes,
               "gameplay model heap accounting is incomplete");
    TEST_CHECK(cgai_gameplay_session_create(model, resources.session_bytes - 1U) == NULL,
               "gameplay session ignored exact cap");
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, resources.session_bytes);
    TEST_CHECK(session != NULL, cgai_last_error());
    TEST_CHECK(resources.session_bytes ==
                   sizeof(*session) + session->storage_count * sizeof(double),
               "gameplay session heap accounting is incomplete");
    TEST_CHECK(resources.maximum_head_logits == 36U && resources.inner_coordinates == 48U &&
                   resources.maximum_head_multiply_adds == 24U,
               "dense hierarchical work accounting changed");
    cgai_gameplay_session_destroy(session);
}

/** @brief Require all complete independent task targets to learn correctly.
 * @param model Borrowed mutable fixture. */
static void independent_learning(cgai_gameplay_model *model) {
    /* Step 1: Train balanced complete observations with separate task-local targets. */
    const cgai_gameplay_training training = {300U, 0.02, 5.0, 0.0};
    TEST_CHECK(cgai_gameplay_train_continue(model, records, 6U, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    const cgai_gameplay_progress progress = cgai_gameplay_get_progress(model);
    TEST_CHECK(progress.epochs == 300U && progress.steps == 1800U,
               "joint gameplay training counted unrelated targets");
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    /* Step 2: Each task's independent state chooses its own learned output domain. */
    for (size_t i = 0U; i < 6U; ++i) {
        cgai_gameplay_request request;
        cgai_gameplay_result result;
        TEST_CHECK(cgai_gameplay_default_request(model, records[i].task, &records[i].state,
                                                 &request) == CGAI_STATUS_OK,
                   cgai_last_error());
        TEST_CHECK(cgai_gameplay_select(session, &request, &result) == CGAI_STATUS_OK,
                   cgai_last_error());
        TEST_CHECK(result.output == records[i].target && result.probability > 0.9,
                   "composed independent task did not learn its own target");
    }
    cgai_gameplay_session_destroy(session);
}

/** @brief Require one training call and two checkpoint-separated calls to be exactly equivalent.
 * @param decay Recorded decoupled shrinkage, zero for ordinary Adam. */
static void exact_continuation(double decay) {
    /* Step 1: Train identical networks for the same canonical complete-pass recipe. */
    cgai_gameplay_model *combined = fixture();
    cgai_gameplay_model *split = fixture();
    cgai_gameplay_training training = {4U, 0.01, 5.0, decay};
    TEST_CHECK(cgai_gameplay_train_continue(combined, records, 6U, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    training.epochs = 2U;
    TEST_CHECK(cgai_gameplay_train_continue(split, records, 6U, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_gameplay_checkpoint_save(split, after_path) == CGAI_STATUS_OK,
               cgai_last_error());
    cgai_gameplay_destroy(split);
    /* Step 2: Restore moments/counters/shuffle stream and compare the whole resulting checkpoint.
     */
    split = cgai_gameplay_checkpoint_load(after_path);
    TEST_CHECK(split != NULL, cgai_last_error());
    TEST_CHECK(cgai_gameplay_train_continue(split, records, 6U, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_gameplay_checkpoint_save(combined, before_path) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_gameplay_checkpoint_save(split, after_path) == CGAI_STATUS_OK,
               cgai_last_error());
    equal_checkpoints();
    cgai_gameplay_destroy(split);
    cgai_gameplay_destroy(combined);
}

/** @brief Require inference export retains exact weights and removes optimizer allocations.
 * @param model Borrowed trained immutable fixture. */
static void inference_export(cgai_gameplay_model *model) {
    /* Step 1: Export/load weights using the distinct composed inference format. */
    TEST_CHECK(cgai_gameplay_save(model, inference_path) == CGAI_STATUS_OK, cgai_last_error());
    cgai_gameplay_model *loaded = cgai_gameplay_load(inference_path);
    TEST_CHECK(loaded != NULL, cgai_last_error());
    TEST_CHECK(loaded->parameter_count == model->parameter_count &&
                   memcmp(loaded->parameters, model->parameters,
                          model->parameter_count * sizeof(double)) == 0,
               "composed inference export changed exact trained weights");
    TEST_CHECK(loaded->adam_first == NULL && loaded->adam_second == NULL &&
                   loaded->training_step == 0U && loaded->training_epochs == 0U,
               "inference export retained continuation ownership");
    resource_caps(loaded);
    cgai_gameplay_destroy(loaded);
}

/** @brief Verify one constrained query's output, repetition and work contracts.
 * @param session Exclusive initialized scratch.
 * @param request Borrowed valid unrestricted query.
 * @param outputs Requested output bit mask.
 * @param modules Requested module bit mask.
 * @param recent Nonzero output ID to suppress, or zero. */
static void permission_query(cgai_gameplay_session *session, const cgai_gameplay_request *request,
                             uint64_t outputs, uint64_t modules, uint32_t recent) {
    /* Step 1: Resolve one legal bounded query and inspect its host constraints. */
    cgai_gameplay_request limited = *request;
    limited.allowed_outputs = outputs;
    limited.allowed_modules = modules;
    limited.recent_output = recent;
    cgai_gameplay_result result;
    TEST_CHECK(cgai_gameplay_select(session, &limited, &result) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(result.output == 0U || (outputs & (UINT64_C(1) << result.output)) != 0U,
               "gameplay selector violated output permissions");
    TEST_CHECK(result.output == 0U || result.output != recent,
               "gameplay selector repeated suppressed output");
    TEST_CHECK(result.abstained == (result.output == 0U) && result.forward_passes <= 1U,
               "gameplay selector violated bounded fallback contract");
}

/** @brief Verify successful constrained queries obey all mask/recent/module combinations.
 * @param session Exclusive initialized scratch.
 * @param request Borrowed complete valid unrestricted query. */
static void exhaustive_permissions(cgai_gameplay_session *session,
                                   const cgai_gameplay_request *request) {
    /* Step 1: Every output mask includes fallback zero, even when the host omits it. */
    for (uint64_t outputs = 0U; outputs < 8U; ++outputs)
        for (uint64_t modules = 0U; modules < 4U; ++modules)
            for (uint32_t recent = 0U; recent < 3U; ++recent)
                permission_query(session, request, outputs, modules, recent);
}

/** @brief Require malformed or nonfinite queries preserve the complete caller-owned result.
 * @param session Exclusive initialized numerical scratch.
 * @param invalid Borrowed deliberately invalid complete query. */
static void reject_query(cgai_gameplay_session *session, const cgai_gameplay_request *invalid) {
    /* Step 1: Byte preservation covers output IDs and every routing diagnostic. */
    cgai_gameplay_result result;
    memset(&result, 0x5A, sizeof(result));
    const cgai_gameplay_result before = result;
    TEST_CHECK(cgai_gameplay_select(session, invalid, &result) == CGAI_STATUS_ERROR,
               "invalid gameplay request was accepted");
    TEST_CHECK(memcmp(&result, &before, sizeof(result)) == 0,
               "failed gameplay query changed caller result");
}

/** @brief Check a rejected nonfinite active centroid and a successful excluded-centroid query.
 * @param session Exclusive scratch borrowing a mutable fixture.
 * @param request Borrowed complete valid unrestricted query. */
static void numerical_failure(cgai_gameplay_session *session,
                              const cgai_gameplay_request *request) {
    /* Step 1: An excluded nonfinite centroid contributes no work; admitting it fails safely. */
    cgai_gameplay_model *model = (cgai_gameplay_model *)session->model;
    const double original = model->outer[4];
    model->outer[4] = NAN;
    reject_query(session, request);
    cgai_gameplay_request limited = *request;
    limited.allowed_modules = 1U;
    cgai_gameplay_result result;
    TEST_CHECK(cgai_gameplay_select(session, &limited, &result) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(result.module_weights[1] == 0.0 && result.module_contributions[1] == 0.0,
               "excluded nonfinite module retained contribution");
    model->outer[4] = original;
}

/** @brief Exercise malformed masks/state and recover from a finite-model numerical error.
 * @param session Exclusive scratch borrowing a mutable test fixture.
 * @param request Borrowed complete valid query. */
static void request_failures(cgai_gameplay_session *session, const cgai_gameplay_request *request) {
    /* Step 1: Version, category and permissions are validated before a forward. */
    cgai_gameplay_request invalid = *request;
    invalid.contract_version = 2U;
    reject_query(session, &invalid);
    invalid = *request;
    invalid.allowed_outputs = 8U;
    reject_query(session, &invalid);
    invalid = *request;
    invalid.allowed_modules = 4U;
    reject_query(session, &invalid);
    invalid = *request;
    invalid.state.values[0] = 3U;
    reject_query(session, &invalid);
    /* Step 2: Numeric failure retains caller state and scratch remains reusable. */
    numerical_failure(session, request);
}

/** @brief Verify an excluded specialist readout is neither evaluated nor admitted into diagnostics.
 * @param session Exclusive scratch borrowing a mutable initialized fixture.
 * @param request Borrowed valid complete unrestricted query. */
static void excluded_readout(cgai_gameplay_session *session, const cgai_gameplay_request *request) {
    /* Step1: A nonfinite conditional readout fails only when its own module is admitted. */
    cgai_gameplay_model *model = (cgai_gameplay_model *)session->model;
    const size_t second = model->config.hidden_dimensions * model->config.output_counts[0];
    const double original = model->decoders[0][second];
    model->decoders[0][second] = NAN;
    reject_query(session, request);
    cgai_gameplay_request limited = *request;
    limited.allowed_modules = 1U;
    cgai_gameplay_result result;
    TEST_CHECK(cgai_gameplay_select(session, &limited, &result) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(result.active_modules == 1U && result.module_contributions[1] == 0.0,
               "excluded specialist readout retained work or probability");
    model->decoders[0][second] = original;
}

/** @brief Run independent runtime mask/fallback and numerical isolation checks. */
static void runtime_contract(void) {
    /* Step 1: Validate all constrained work domains on an initialized typed state. */
    cgai_gameplay_model *model = fixture();
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    cgai_gameplay_request request;
    TEST_CHECK(cgai_gameplay_default_request(model, 0U, &records[1].state, &request) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    exhaustive_permissions(session, &request);
    request_failures(session, &request);
    excluded_readout(session, &request);
    /* Step 2: Stable ties choose the always-legal lowest ID without mutating any authored domain.
     */
    memset(model->heads[0], 0, 36U * sizeof(double));
    cgai_gameplay_result result;
    TEST_CHECK(cgai_gameplay_select(session, &request, &result) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(result.output == 0U, "composed decoder changed stable lower-ID tie breaking");
    cgai_gameplay_session_destroy(session);
    cgai_gameplay_destroy(model);
}

/** @brief Reject malformed architecture and check a full-width64-bit task domain. */
static void shape_contract(void) {
    /* Step 1: Unused fields and task masks are part of the canonical typed schema. */
    cgai_gameplay_config config = core_config();
    config.cardinalities[1] = 1U;
    TEST_CHECK(cgai_gameplay_create(&config) == NULL, "unused feature shape was accepted");
    config = core_config();
    config.task_modules[0] = 4U;
    TEST_CHECK(cgai_gameplay_create(&config) == NULL, "out-of-domain module mask was accepted");
    config = core_config();
    config.task_features[0] = 2U;
    TEST_CHECK(cgai_gameplay_create(&config) == NULL, "out-of-domain feature mask was accepted");
    /* Step 2: The maximum output domain avoids an undefined full-width shift. */
    config = core_config();
    config.output_counts[0] = 64U;
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    cgai_gameplay_request request;
    TEST_CHECK(cgai_gameplay_default_request(model, 0U, &records[0].state, &request) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(request.allowed_outputs == UINT64_MAX,
               "full-width output permissions were truncated");
    cgai_gameplay_destroy(model);
}

/** @brief Verify decoupled shrinkage changes an unobserved category without entering Adam moments.
 */
static void decoupled_shrinkage(void) {
    /* Step1: An unobserved category has exactly zero loss gradient and zero Adam moments. */
    cgai_gameplay_model *model = fixture();
    const size_t unobserved = model->config.embedding_dimensions + 1U;
    const double original = model->parameters[unobserved];
    const cgai_gameplay_training training = {1U, 0.01, 5.0, 0.1};
    TEST_CHECK(cgai_gameplay_train_continue(model, records, 1U, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step2: Shrinkage applies outside gradient clipping and both moment accumulators. */
    TEST_CHECK(model->parameters[unobserved] == original - 0.01 * 0.1 * original &&
                   model->adam_first[unobserved] == 0.0 && model->adam_second[unobserved] == 0.0,
               "parameter shrinkage was coupled to Adam gradient moments");
    cgai_gameplay_destroy(model);
}

/** @brief Create three distinct task domains with different feature/module eligibility.
 * @return Complete bounded aligned three-task shape. */
static cgai_gameplay_config third_config(void) {
    /* Step1: Extend the generic contract without changing the first two task IDs. */
    cgai_gameplay_config config = core_config();
    config.feature_count = 2U;
    config.embedding_dimensions = 5U;
    config.task_count = 3U;
    config.cardinalities[1] = 2U;
    config.output_counts[2] = 4U;
    config.task_modules[1] = 1U;
    config.task_modules[2] = 2U;
    config.task_features[1] = 2U;
    config.task_features[2] = 3U;
    return config;
}

/** @brief Populate complete independent targets across every typed three-task state.
 * @param examples Writable18-entry canonical record array. */
static void third_examples(cgai_gameplay_example *examples) {
    /* Step1: Each complete two-field observation has one distinct target per requested task. */
    size_t index = 0U;
    for (uint32_t first = 0U; first < 3U; ++first)
        for (uint32_t second = 0U; second < 2U; ++second)
            for (uint32_t task = 0U; task < 3U; ++task) {
                const cgai_gameplay_state state = {{first, second}};
                examples[index].state = state;
                examples[index].task = task;
                examples[index].target = task == 0U   ? first
                                         : task == 1U ? second
                                                      : (first == 0U ? 0U : 2U) + second;
                ++index;
            }
}

/** @brief Require a complete learned target and its declared specialist exclusions.
 * @param session Exclusive initialized trained numerical scratch.
 * @param example Borrowed independent target across one of three task domains. */
static void third_prediction(cgai_gameplay_session *session, const cgai_gameplay_example *example) {
    /* Step1: Every task resolves its own numbering, including third-head-only IDthree. */
    cgai_gameplay_request request;
    cgai_gameplay_result result;
    TEST_CHECK(cgai_gameplay_default_request(session->model, example->task, &example->state,
                                             &request) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_gameplay_select(session, &request, &result) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(result.output == example->target && result.probability > 0.8,
               "generic third-head bundle did not learn task-local independent targets");
    /* Step2: Single-specialist tasks leave the incompatible module exactly excluded. */
    TEST_CHECK(result.active_modules == (example->task == 0U ? 2U : 1U),
               "three-head runtime evaluated an incompatible specialist");
    if (example->task != 0U) {
        const size_t excluded = example->task == 1U ? 1U : 0U;
        TEST_CHECK(result.module_weights[excluded] == 0.0 &&
                       result.module_contributions[excluded] == 0.0,
                   "third-head routing retained excluded module probability");
    }
}

/** @brief Check third-head domain masks and zeroed unused schema entries after decoding.
 * @param session Exclusive restored three-task scratch.
 * @param example Borrowed third-task target selecting IDthree. */
static void third_permissions(cgai_gameplay_session *session,
                              const cgai_gameplay_example *example) {
    /* Step1: Distinct output/module domains persist through the portable checkpoint schema. */
    cgai_gameplay_request request;
    TEST_CHECK(cgai_gameplay_default_request(session->model, 2U, &example->state, &request) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(request.allowed_outputs == 15U && request.allowed_modules == 2U,
               "third-head permissions changed after checkpoint decoding");
    request.allowed_modules = 3U;
    reject_query(session, &request);
    request.allowed_modules = 2U;
    request.allowed_outputs = 16U;
    reject_query(session, &request);
    /* Step2: Nonzero unused shape entries cannot appear when another head is scaffolded. */
    for (size_t task = 3U; task < CGAI_GAMEPLAY_MAX_TASKS; ++task)
        TEST_CHECK(session->model->config.output_counts[task] == 0U &&
                       session->model->config.task_modules[task] == 0U &&
                       session->model->config.task_features[task] == 0U,
                   "third-head extension changed unused task schema entries");
}

/** @brief Require different task feature masks survive third-head extension and checkpoint
 * decoding.
 * @param session Exclusive restored three-task numerical scratch.
 * @param examples Borrowed canonical18-entry independent target array. */
static void third_independence(cgai_gameplay_session *session,
                               const cgai_gameplay_example *examples) {
    /* Step1: Headzero excludes fieldone while headone excludes fieldzero. */
    double first = 0.0;
    double second = 0.0;
    TEST_CHECK(cgai_gameplay_evaluate(session, &examples[0], &first) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_gameplay_evaluate(session, &examples[3], &second) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(first == second, "third-head extension changed first-head feature exclusion");
    TEST_CHECK(cgai_gameplay_evaluate(session, &examples[1], &first) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_gameplay_evaluate(session, &examples[7], &second) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(first == second, "third-head extension changed second-head feature exclusion");
}

/** @brief Replace a trained three-task model with an exactly decoded continuation snapshot.
 * @param model Owned trained model, released after writing its complete checkpoint.
 * @return Owned restored model with exact parameters, moments, counters and shuffle state. */
static cgai_gameplay_model *third_roundtrip(cgai_gameplay_model *model) {
    /* Step1: Native checkpoint ownership retains every dynamic task slice and optimizer scalar. */
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, before_path) == CGAI_STATUS_OK,
               cgai_last_error());
    cgai_gameplay_destroy(model);
    model = cgai_gameplay_checkpoint_load(before_path);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, after_path) == CGAI_STATUS_OK,
               cgai_last_error());
    equal_checkpoints();
    const cgai_gameplay_progress progress = cgai_gameplay_get_progress(model);
    TEST_CHECK(progress.epochs == 300U && progress.steps == 5400U,
               "third-task checkpoint changed exact training progress");
    return model;
}

/** @brief Exercise a real third task through native training, queries and complete checkpoint
 * restore. */
static void third_head_runtime(void) {
    /* Step1: Train all three domains through the same generic native AdamW/session implementation.
     */
    const cgai_gameplay_config config = third_config();
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    cgai_gameplay_example examples[18] = {0};
    third_examples(examples);
    const cgai_gameplay_training training = {300U, 0.02, 5.0, 0.0};
    TEST_CHECK(cgai_gameplay_train_continue(model, examples, 18U, &training) == CGAI_STATUS_OK,
               cgai_last_error());
    /* Step2: Exact full-state restore retains every decoder/bank/domain and query capability. */
    model = third_roundtrip(model);
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    for (size_t index = 0U; index < 18U; ++index)
        third_prediction(session, &examples[index]);
    third_permissions(session, &examples[11]);
    third_independence(session, examples);
    cgai_gameplay_session_destroy(session);
    cgai_gameplay_destroy(model);
}

/** @brief Run complete typed runtime, model ownership and deterministic continuation checks.
 * @return Zero after all invariants pass. */
int main(void) {
    /* Step 1: Fresh invalid preparation must retain absent persistent optimizer allocations. */
    shape_contract();
    runtime_contract();
    cgai_gameplay_model *model = fixture();
    resource_caps(model);
    invalid_training(model);
    TEST_CHECK(model->adam_first == NULL && model->adam_second == NULL,
               "invalid gameplay training allocated persistent Adam state");
    /* Step 2: Learn independent tasks, preserve trained state on invalid records, export/replay. */
    independent_learning(model);
    invalid_training(model);
    resource_caps(model);
    inference_export(model);
    cgai_gameplay_destroy(model);
    exact_continuation(0.0);
    exact_continuation(0.05);
    decoupled_shrinkage();
    third_head_runtime();
    /* Step 3: Release only the complete test-owned artifacts. */
    TEST_CHECK(remove(before_path) == 0, "could not remove first composed checkpoint");
    TEST_CHECK(remove(after_path) == 0, "could not remove second composed checkpoint");
    TEST_CHECK(remove(inference_path) == 0, "could not remove composed inference fixture");
    return 0;
}
