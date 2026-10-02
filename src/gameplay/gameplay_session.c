/** @file gameplay_session.c @brief Reusable capped typed composed-network queries. */
#include "gameplay_internal.h"
#include "internal/error.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Count complete reusable numerical scratch scalars.
 * @param model Borrowed initialized model.
 * @return Complete bounded scratch scalar count. */
static size_t session_scalars(const cgai_gameplay_model *model) {
    /* Step 1: Every dense expert retains log probabilities and ordinary probabilities. */
    const size_t modules = model->config.module_count;
    const size_t experts = modules * model->config.centroids_per_module;
    return 2U * (model->input_count + model->config.hidden_dimensions + modules + experts +
                 experts * model->maximum_outputs + model->maximum_outputs) +
           2U * modules * model->maximum_outputs;
}

/** @brief Count owned session shell and all numerical scratch.
 * @param model Borrowed initialized model, or NULL.
 * @return Complete requested heap payload, or zero. */
size_t cgai_gameplay_session_bytes(const cgai_gameplay_model *model) {
    /* Step 1: Validated dimensions keep scalar and byte products representable. */
    return model == NULL ? 0U
                         : sizeof(cgai_gameplay_session) + session_scalars(model) * sizeof(double);
}

/** @brief Inspect full dense work and owned allocations without allocation.
 * @param model Borrowed initialized model.
 * @param resources Writable result, unchanged on error.
 * @return OK on publication, ERROR otherwise. */
cgai_status cgai_gameplay_get_resources(const cgai_gameplay_model *model,
                                        cgai_gameplay_resources *resources) {
    /* Step 1: Build a complete local result before publishing caller state. */
    if (model == NULL || resources == NULL)
        return cgai_fail("missing gameplay resource argument");
    const cgai_gameplay_config *config = &model->config;
    cgai_gameplay_resources result = {0};
    result.parameter_count = model->parameter_count;
    result.parameter_bytes = model->parameter_count * sizeof(double);
    result.optimizer_bytes = model->adam_first == NULL ? 0U : 2U * result.parameter_bytes;
    result.model_bytes = sizeof(*model) + result.parameter_bytes + result.optimizer_bytes;
    result.session_bytes = cgai_gameplay_session_bytes(model);
    result.encoder_multiply_adds = config->hidden_dimensions * model->input_count;
    result.outer_coordinates = config->module_count * config->hidden_dimensions;
    result.inner_coordinates = result.outer_coordinates * config->centroids_per_module;
    result.maximum_head_logits =
        config->module_count * config->centroids_per_module * model->maximum_outputs;
    result.maximum_head_multiply_adds =
        config->module_count * config->hidden_dimensions * model->maximum_outputs;
    *resources = result;
    return CGAI_STATUS_OK;
}

/** @brief Consume one contiguous scratch slice.
 * @param cursor Writable remaining storage pointer.
 * @param count Scalar count in the requested slice.
 * @return Borrowed start of this slice. */
static double *take_slice(double **cursor, size_t count) {
    /* Step 1: Every caller uses the already counted complete block. */
    double *result = *cursor;
    *cursor += count;
    return result;
}

/** @brief Bind all numerical workspace pointers to one owned allocation.
 * @param session Borrowed owner with complete allocation. */
static void bind_session(cgai_gameplay_session *session) {
    /* Step 1: Bind shared encoder and both hierarchical routing levels. */
    const cgai_gameplay_model *model = session->model;
    const size_t modules = model->config.module_count;
    const size_t experts = modules * model->config.centroids_per_module;
    double *cursor = session->storage;
    session->input = take_slice(&cursor, model->input_count);
    session->hidden = take_slice(&cursor, model->config.hidden_dimensions);
    session->log_alpha = take_slice(&cursor, modules);
    session->alpha = take_slice(&cursor, modules);
    session->log_beta = take_slice(&cursor, experts);
    session->beta = take_slice(&cursor, experts);
    session->decoder_logits = take_slice(&cursor, modules * model->maximum_outputs);
    session->log_heads = take_slice(&cursor, experts * model->maximum_outputs);
    session->head_probabilities = take_slice(&cursor, experts * model->maximum_outputs);
    session->log_outputs = take_slice(&cursor, model->maximum_outputs);
    session->outputs = take_slice(&cursor, model->maximum_outputs);
    session->log_module_outputs = take_slice(&cursor, modules * model->maximum_outputs);
    session->hidden_gradient = take_slice(&cursor, model->config.hidden_dimensions);
    session->input_gradient = take_slice(&cursor, model->input_count);
}

/** @brief Allocate capped owned reusable scratch.
 * @param model Borrowed immutable model, outliving this session.
 * @param max_session_bytes Requested complete cap, zero disables.
 * @return Owned initialized session or NULL. */
cgai_gameplay_session *cgai_gameplay_session_create(const cgai_gameplay_model *model,
                                                    size_t max_session_bytes) {
    /* Step 1: Check complete requested ownership before acquiring memory. */
    const size_t bytes = cgai_gameplay_session_bytes(model);
    if (bytes == 0U || (max_session_bytes != 0U && bytes > max_session_bytes)) {
        (void)cgai_fail("gameplay session exceeds requested memory budget");
        return NULL;
    }
    cgai_gameplay_session *session = calloc(1U, sizeof(*session));
    if (session == NULL) {
        (void)cgai_fail("could not allocate gameplay session owner");
        return NULL;
    }
    /* Step 2: Publish slices only after the complete numerical block exists. */
    session->model = model;
    session->storage_count = session_scalars(model);
    session->storage = calloc(session->storage_count, sizeof(*session->storage));
    if (session->storage == NULL) {
        cgai_gameplay_session_destroy(session);
        (void)cgai_fail("could not allocate gameplay numerical scratch");
        return NULL;
    }
    bind_session(session);
    return session;
}

/** @brief Release complete session ownership while retaining its borrowed model.
 * @param session Owned initialized or partially allocated session, or NULL. */
void cgai_gameplay_session_destroy(cgai_gameplay_session *session) {
    /* Step 1: The model pointer and all named slices are borrowed. */
    if (session == NULL)
        return;
    free(session->storage);
    free(session);
}

/** @brief Validate each configured category and zeroed trailing observation.
 * @param model Borrowed initialized model.
 * @param state Borrowed complete observation.
 * @return OK on a complete contract state, ERROR otherwise. */
cgai_status cgai_gameplay_validate_state(const cgai_gameplay_model *model,
                                         const cgai_gameplay_state *state) {
    /* Step 1: Validate before any numerical workspace mutation. */
    if (model == NULL || state == NULL)
        return cgai_fail("missing gameplay state argument");
    for (size_t i = 0U; i < CGAI_GAMEPLAY_MAX_FEATURES; ++i)
        if ((i < model->config.feature_count &&
             state->values[i] >= model->config.cardinalities[i]) ||
            (i >= model->config.feature_count && state->values[i] != 0U))
            return cgai_fail("invalid gameplay category value");
    return CGAI_STATUS_OK;
}

/** @brief Validate a complete independent target.
 * @param model Borrowed initialized model.
 * @param example Borrowed complete observation and task-local target.
 * @return OK on valid task/state/target, ERROR otherwise. */
cgai_status cgai_gameplay_validate_example(const cgai_gameplay_model *model,
                                           const cgai_gameplay_example *example) {
    /* Step 1: Bound the task before reading task-local dimensions. */
    if (model == NULL || example == NULL || example->task >= model->config.task_count ||
        example->target >= model->config.output_counts[example->task])
        return cgai_fail("invalid gameplay example target");
    return cgai_gameplay_validate_state(model, &example->state);
}

/** @brief Construct an unrestricted typed request without changing invalid caller output.
 * @param model Borrowed initialized model.
 * @param task Requested valid head.
 * @param state Borrowed complete observation.
 * @param request Writable result.
 * @return OK on publication, ERROR otherwise. */
cgai_status cgai_gameplay_default_request(const cgai_gameplay_model *model, uint32_t task,
                                          const cgai_gameplay_state *state,
                                          cgai_gameplay_request *request) {
    /* Step 1: Validate the complete request before publishing any field. */
    if (model == NULL || request == NULL || task >= model->config.task_count ||
        !cgai_gameplay_validate_state(model, state))
        return cgai_fail("invalid default gameplay request");
    cgai_gameplay_request result = {0};
    result.state = *state;
    result.task = task;
    result.contract_version = CGAI_GAMEPLAY_CONTRACT_VERSION;
    result.allowed_outputs = cgai_gameplay_mask(model->config.output_counts[task]);
    result.allowed_modules = model->config.task_modules[task];
    *request = result;
    return CGAI_STATUS_OK;
}

/** @brief Validate host masks and version before numerical mutation.
 * @param model Borrowed initialized model.
 * @param request Borrowed complete query.
 * @return OK for a valid query, ERROR otherwise. */
static cgai_status validate_request(const cgai_gameplay_model *model,
                                    const cgai_gameplay_request *request) {
    /* Step 1: Bound task, version and configured output/module domains. */
    if (request == NULL || request->contract_version != CGAI_GAMEPLAY_CONTRACT_VERSION ||
        request->task >= model->config.task_count)
        return cgai_fail("invalid gameplay request contract");
    const size_t outputs = model->config.output_counts[request->task];
    if (request->recent_output >= outputs ||
        (request->allowed_outputs & ~cgai_gameplay_mask(outputs)) != 0U ||
        (request->allowed_modules & ~model->config.task_modules[request->task]) != 0U)
        return cgai_fail("invalid gameplay request permissions");
    return cgai_gameplay_validate_state(model, &request->state);
}

/** @brief Suppress the recent nonzero ID and always retain fallback zero.
 * @param request Borrowed already validated query.
 * @return Complete effective legal output mask. */
static uint64_t legal_outputs(const cgai_gameplay_request *request) {
    /* Step 1: Suppression never removes fallback zero. */
    uint64_t outputs = request->allowed_outputs | UINT64_C(1);
    if (request->recent_output != 0U)
        outputs &= ~(UINT64_C(1) << request->recent_output);
    return outputs;
}

/** @brief Select the largest admitted unconditional task likelihood with stable lower-ID ties.
 * @param session Borrowed successful numerical forward.
 * @param allowed Complete effective output domain.
 * @return Stable legal output ID. */
static uint32_t select_output(const cgai_gameplay_session *session, uint64_t allowed) {
    /* Step 1: Compare log probabilities to preserve distinctions below ordinary underflow. */
    uint32_t best = 0U;
    for (uint32_t output = 1U; output < session->model->config.output_counts[session->task];
         ++output)
        if ((allowed & (UINT64_C(1) << output)) != 0U &&
            session->log_outputs[output] > session->log_outputs[best])
            best = output;
    return best;
}

/** @brief Publish selected output and both levels of routing diagnostics locally.
 * @param session Borrowed successful numerical forward.
 * @param allowed Complete effective output domain.
 * @param result Writable zeroed local result. */
static void describe_selection(const cgai_gameplay_session *session, uint64_t allowed,
                               cgai_gameplay_result *result) {
    /* Step 1: Resolve the admitted task-local ID and its unconditional likelihood. */
    result->output = select_output(session, allowed);
    result->probability = session->outputs[result->output];
    result->abstained = result->output == 0U;
    result->forward_passes = 1U;
    /* Step 2: Explain each active specialist's posterior share for the selected ID. */
    for (size_t module = 0U; module < session->model->config.module_count; ++module)
        if ((session->active_mask & (UINT64_C(1) << module)) != 0U) {
            ++result->active_modules;
            result->module_weights[module] = session->alpha[module];
            result->module_contributions[module] =
                fmin(1.0, exp(session->log_alpha[module] +
                              session->log_module_outputs[module * session->model->maximum_outputs +
                                                          result->output] -
                              session->log_outputs[result->output]));
        }
    result->active_centroids = result->active_modules * session->model->config.centroids_per_module;
}

/** @brief Return one legal bounded composed-network proposal without allocation or I/O.
 * @param session Exclusive reusable scratch.
 * @param request Borrowed complete typed query.
 * @param result Writable output, unchanged on any error.
 * @return OK after publication, ERROR otherwise. */
cgai_status cgai_gameplay_select(cgai_gameplay_session *session,
                                 const cgai_gameplay_request *request,
                                 cgai_gameplay_result *result) {
    /* Step 1: Validate before publishing or performing a forward. */
    if (session == NULL || result == NULL || !validate_request(session->model, request))
        return cgai_fail("invalid gameplay selection argument");
    cgai_gameplay_result selected = {0};
    const uint64_t outputs = legal_outputs(request);
    /* Step 2: Empty legal work domains produce deterministic fallback without inference. */
    if (request->allowed_modules == 0U || outputs == UINT64_C(1)) {
        selected.probability = 1.0;
        selected.abstained = 1;
    } else {
        if (!cgai_gameplay_forward(session, &request->state, request->task,
                                   request->allowed_modules))
            return CGAI_STATUS_ERROR;
        describe_selection(session, outputs, &selected);
    }
    *result = selected;
    return CGAI_STATUS_OK;
}

/** @brief Evaluate stable unrestricted target loss for a selected specialist subset.
 * @param session Exclusive reusable numerical scratch.
 * @param example Borrowed independent target.
 * @param allowed_modules Nonempty configured eligible specialist subset.
 * @param loss Writable result, unchanged on error.
 * @return OK after finite scoring, ERROR otherwise. */
cgai_status cgai_gameplay_evaluate_modules(cgai_gameplay_session *session,
                                           const cgai_gameplay_example *example,
                                           uint64_t allowed_modules, double *loss) {
    /* Step 1: Validate the target and routing subset before accessing task-local dimensions. */
    if (session == NULL || loss == NULL || !cgai_gameplay_validate_example(session->model, example))
        return cgai_fail("invalid gameplay evaluation argument");
    if (allowed_modules == 0U ||
        (allowed_modules & ~session->model->config.task_modules[example->task]) != 0U)
        return cgai_fail("invalid gameplay evaluation specialist mask");
    /* Step 2: Use the stable log mixture rather than an underflow-prone ordinary probability. */
    if (!cgai_gameplay_forward(session, &example->state, example->task, allowed_modules))
        return CGAI_STATUS_ERROR;
    *loss = -session->log_outputs[example->target];
    return CGAI_STATUS_OK;
}

/** @brief Evaluate stable unrestricted target loss using every eligible module.
 * @param session Exclusive numerical scratch.
 * @param example Borrowed independent target.
 * @param loss Writable result, unchanged on error.
 * @return OK after finite scoring, ERROR otherwise. */
cgai_status cgai_gameplay_evaluate(cgai_gameplay_session *session,
                                   const cgai_gameplay_example *example, double *loss) {
    /* Step 1: Bound the task before reading its module eligibility. */
    if (session == NULL || example == NULL || example->task >= session->model->config.task_count)
        return cgai_fail("invalid gameplay evaluation task");
    return cgai_gameplay_evaluate_modules(session, example,
                                          session->model->config.task_modules[example->task], loss);
}
