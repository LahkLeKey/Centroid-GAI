/** @file npc_optimizer.c @brief Private v3 role derivatives and one-event numerical updates. */
#include "npc_optimizer.h"
#include "gameplay/gameplay_internal.h"
#include "internal/error.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Accumulate exact role cross entropy derivatives through outer centroid distances.
 * @param session Successful forward and exclusive derivative scratch.
 * @param role Original full-record observed specialist role.
 * @param gradient Complete combined parameter derivatives, already containing NLL. */
static void npc_v3_optimizer_routing(cgai_gameplay_session *session, uint32_t role,
                                     double *gradient) {
    const cgai_gameplay_model *model = session->model;
    const size_t hidden = model->config.hidden_dimensions;
    const size_t outer = (size_t)(model->outer - model->parameters);
    for (size_t module = 0U; module < 2U; ++module) {
        const double target = module == role ? NPC_ROLE_TARGET : 1.0 - NPC_ROLE_TARGET;
        const double score = NPC_ROUTING_BALANCE * (session->alpha[module] - target);
        for (size_t coordinate = 0U; coordinate < hidden; ++coordinate) {
            const size_t index = module * hidden + coordinate;
            const double derivative = 2.0 * score *
                                      (session->hidden[coordinate] - model->outer[index]) /
                                      model->config.routing_temperature;
            gradient[outer + index] += derivative;
            session->hidden_gradient[coordinate] -= derivative;
        }
    }
}

/** @brief Add routing-only shared affine tanh derivatives to existing NLL derivatives.
 * @param session Exclusive routing derivative scratch.
 * @param gradient Complete already accumulated parameter derivatives. */
static void npc_v3_optimizer_encoder(cgai_gameplay_session *session, double *gradient) {
    const cgai_gameplay_model *model = session->model;
    const size_t encoder = (size_t)(model->encoder - model->parameters);
    const size_t bias = (size_t)(model->bias - model->parameters);
    for (size_t hidden = 0U; hidden < model->config.hidden_dimensions; ++hidden) {
        const double derivative = session->hidden_gradient[hidden] *
                                  (1.0 - session->hidden[hidden] * session->hidden[hidden]);
        gradient[bias + hidden] += derivative;
        for (size_t input = 0U; input < model->input_count; ++input) {
            const size_t index = hidden * model->input_count + input;
            gradient[encoder + index] += derivative * session->input[input];
            session->input_gradient[input] += derivative * model->encoder[index];
        }
    }
}

/** @brief Add routing-only derivatives to task-relevant observed embedding categories.
 * @param session Borrowed completed shared routing derivatives.
 * @param example Matching forward categorical state and task.
 * @param gradient Complete already accumulated parameter derivatives. */
static void npc_v3_optimizer_embeddings(const cgai_gameplay_session *session,
                                        const cgai_gameplay_example *example, double *gradient) {
    const cgai_gameplay_model *model = session->model;
    const size_t dimensions = model->config.embedding_dimensions;
    for (size_t feature = 0U; feature < model->config.feature_count; ++feature) {
        if ((model->config.task_features[example->task] & (UINT64_C(1) << feature)) == 0U)
            continue;
        const size_t category = model->category_offsets[feature] + example->state.values[feature];
        for (size_t dimension = 0U; dimension < dimensions; ++dimension)
            gradient[category * dimensions + dimension] +=
                session->input_gradient[feature * dimensions + dimension];
    }
}

/** @brief Differentiate role-conditional NLL plus the frozen observed-role router loss.
 * @param session Exclusive scratch borrowing the mutable model.
 * @param example Borrowed target matching that forward state and task.
 * @param role Original full-record observed role, preserved across history ablation.
 * @param gradient Writable complete parameter_count scalar array, overwritten.
 * @return OK for finite exact combined derivatives, ERROR otherwise. */
cgai_status npc_v3_optimizer_gradient(cgai_gameplay_session *session,
                                      const cgai_gameplay_example *example, uint32_t role,
                                      double *gradient) {
    if (session == NULL || session->model == NULL || example == NULL || gradient == NULL ||
        session->model->config.module_count != 2U || role > 1U ||
        !cgai_gameplay_validate_example(session->model, example) ||
        session->model->config.task_modules[example->task] != UINT64_C(3))
        return cgai_fail("v3 role objective requires a matching two-module task and role");
    if (!cgai_gameplay_forward(session, &example->state, example->task, UINT64_C(1) << role) ||
        !cgai_gameplay_backward(session, example, gradient) ||
        !cgai_gameplay_forward(session, &example->state, example->task, UINT64_C(3)))
        return CGAI_STATUS_ERROR;
    memset(session->hidden_gradient, 0,
           session->model->config.hidden_dimensions * sizeof(*session->hidden_gradient));
    memset(session->input_gradient, 0,
           session->model->input_count * sizeof(*session->input_gradient));
    npc_v3_optimizer_routing(session, role, gradient);
    npc_v3_optimizer_encoder(session, gradient);
    npc_v3_optimizer_embeddings(session, example, gradient);
    for (size_t index = 0U; index < session->model->parameter_count; ++index)
        if (!isfinite(gradient[index]))
            return cgai_fail("nonfinite v3 conditional action and routing derivative");
    return CGAI_STATUS_OK;
}

/** Per-event scratch; moment buffers stay private until the entire update validates. */
typedef struct npc_v3_optimizer_state {
    cgai_gameplay_model *model;
    npc_v3_update_settings settings;
    size_t parameter_count;
    double *gradient;
    cgai_gameplay_session *session;
    double *first;
    double *second;
    int owns_moments;
} npc_v3_optimizer_state;

typedef struct npc_v3_optimizer_adam {
    double scale;
    double first_correction;
    double second_correction;
} npc_v3_optimizer_adam;

typedef struct npc_v3_optimizer_update {
    double first;
    double second;
    double value;
} npc_v3_optimizer_update;

static cgai_status npc_v3_optimizer_validate_settings(const npc_v3_update_settings *settings) {
    if (settings == NULL || !isfinite(settings->learning_rate) || settings->learning_rate <= 0.0 ||
        settings->learning_rate > 1.0 || !isfinite(settings->gradient_clip) ||
        settings->gradient_clip <= 0.0 || settings->gradient_clip > 1000.0 ||
        !isfinite(settings->weight_decay) || settings->weight_decay < 0.0 ||
        settings->weight_decay > 1.0)
        return cgai_fail("invalid NPC numerical update settings");
    return CGAI_STATUS_OK;
}

static cgai_status npc_v3_optimizer_validate_event(const cgai_gameplay_model *model,
                                                   const cgai_gameplay_example *example,
                                                   uint32_t role) {
    if (model == NULL || example == NULL || role > 1U || model->config.module_count != 2U ||
        !cgai_gameplay_validate_example(model, example) ||
        model->config.task_modules[example->task] != UINT64_C(3))
        return cgai_fail("NPC numerical objective requires a compatible two-module event");
    return CGAI_STATUS_OK;
}

static cgai_status npc_v3_optimizer_validate_continuation(const npc_v3_optimizer_state *work) {
    const cgai_gameplay_model *model = work->model;
    if (model->training_step == UINT64_MAX)
        return cgai_fail("NPC numerical update counter would overflow");
    if ((model->adam_first == NULL) != (model->adam_second == NULL) ||
        (model->adam_first == NULL && (model->training_step != 0U || model->training_epochs != 0U)))
        return cgai_fail("invalid NPC numerical moment ownership");
    return CGAI_STATUS_OK;
}

static cgai_status npc_v3_optimizer_prepare_moments(npc_v3_optimizer_state *work) {
    work->first = work->model->adam_first;
    work->second = work->model->adam_second;
    if (work->first != NULL)
        return CGAI_STATUS_OK;
    work->owns_moments = 1;
    work->first = calloc(work->parameter_count, sizeof(*work->first));
    work->second = calloc(work->parameter_count, sizeof(*work->second));
    if (work->first == NULL || work->second == NULL)
        return cgai_fail("could not allocate NPC numerical moments");
    return CGAI_STATUS_OK;
}

static void npc_v3_optimizer_destroy_optimizer(npc_v3_optimizer_state *work) {
    free(work->gradient);
    cgai_gameplay_session_destroy(work->session);
    if (work->owns_moments) {
        free(work->first);
        free(work->second);
    }
}

static cgai_status npc_v3_optimizer_prepare_optimizer(npc_v3_optimizer_state *work) {
    if (!npc_v3_optimizer_validate_continuation(work))
        return CGAI_STATUS_ERROR;
    work->parameter_count = work->model->parameter_count;
    work->gradient = calloc(work->parameter_count, sizeof(*work->gradient));
    work->session = cgai_gameplay_session_create(work->model, 0U);
    if (work->gradient == NULL || work->session == NULL)
        return cgai_fail("could not allocate NPC numerical scratch");
    return npc_v3_optimizer_prepare_moments(work);
}

/** Propose the existing clipped, bias-corrected AdamW scalar without publication. */
static npc_v3_optimizer_update npc_v3_optimizer_propose_update(const npc_v3_optimizer_state *work,
                                                               const npc_v3_optimizer_adam *adam,
                                                               size_t index) {
    const double gradient = work->gradient[index] * adam->scale;
    npc_v3_optimizer_update update;
    update.first = 0.9 * work->first[index] + 0.1 * gradient;
    update.second = 0.999 * work->second[index] + 0.001 * gradient * gradient;
    update.value = work->model->parameters[index] -
                   work->settings.learning_rate * (update.first / adam->first_correction) /
                       (sqrt(update.second / adam->second_correction) + 1e-8);
    if (work->settings.weight_decay != 0.0)
        update.value -= work->settings.learning_rate * work->settings.weight_decay *
                        work->model->parameters[index];
    return update;
}

static cgai_status npc_v3_optimizer_prepare_adam(const npc_v3_optimizer_state *work,
                                                 npc_v3_optimizer_adam *adam) {
    double squared = 0.0;
    for (size_t i = 0U; i < work->parameter_count; ++i)
        squared += work->gradient[i] * work->gradient[i];
    if (!isfinite(squared))
        return cgai_fail("nonfinite NPC numerical gradient norm");
    const double norm = sqrt(squared);
    adam->scale = norm > work->settings.gradient_clip ? work->settings.gradient_clip / norm : 1.0;
    const double step = (double)(work->model->training_step + 1U);
    adam->first_correction = 1.0 - pow(0.9, step);
    adam->second_correction = 1.0 - pow(0.999, step);
    return CGAI_STATUS_OK;
}

static cgai_status npc_v3_optimizer_validate_updates(const npc_v3_optimizer_state *work,
                                                     const npc_v3_optimizer_adam *adam) {
    for (size_t i = 0U; i < work->parameter_count; ++i) {
        const npc_v3_optimizer_update update = npc_v3_optimizer_propose_update(work, adam, i);
        if (!isfinite(update.first) || !isfinite(update.second) || update.second < 0.0 ||
            !isfinite(update.value))
            return cgai_fail("nonfinite NPC numerical Adam update");
    }
    return CGAI_STATUS_OK;
}

static void npc_v3_optimizer_publish_moments(npc_v3_optimizer_state *work) {
    if (work->owns_moments) {
        work->model->adam_first = work->first;
        work->model->adam_second = work->second;
        work->owns_moments = 0;
    }
}

static cgai_status npc_v3_optimizer_apply_update(npc_v3_optimizer_state *work) {
    npc_v3_optimizer_adam adam;
    if (!npc_v3_optimizer_prepare_adam(work, &adam) ||
        !npc_v3_optimizer_validate_updates(work, &adam))
        return CGAI_STATUS_ERROR;
    npc_v3_optimizer_publish_moments(work);
    for (size_t i = 0U; i < work->parameter_count; ++i) {
        const npc_v3_optimizer_update update = npc_v3_optimizer_propose_update(work, &adam, i);
        work->first[i] = update.first;
        work->second[i] = update.second;
        work->model->parameters[i] = update.value;
    }
    ++work->model->training_step;
    return CGAI_STATUS_OK;
}

static cgai_status npc_v3_optimizer_event_gradient(npc_v3_optimizer_state *work,
                                                   const cgai_gameplay_example *example,
                                                   uint32_t role) {
    if (!cgai_gameplay_forward(work->session, &example->state, example->task, UINT64_C(3)))
        return CGAI_STATUS_ERROR;
    return npc_v3_optimizer_gradient(work->session, example, role, work->gradient);
}

cgai_status npc_v3_optimizer_step(cgai_gameplay_model *model, const cgai_gameplay_example *example,
                                  uint32_t role, const npc_v3_update_settings *settings) {
    if (!npc_v3_optimizer_validate_settings(settings) ||
        !npc_v3_optimizer_validate_event(model, example, role))
        return CGAI_STATUS_ERROR;
    npc_v3_optimizer_state work = {0};
    work.model = model;
    work.settings = *settings;
    cgai_status status = npc_v3_optimizer_prepare_optimizer(&work);
    if (status)
        status = npc_v3_optimizer_event_gradient(&work, example, role);
    if (status)
        status = npc_v3_optimizer_apply_update(&work);
    npc_v3_optimizer_destroy_optimizer(&work);
    return status;
}
