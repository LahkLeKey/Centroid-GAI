/** @file life_optimizer.c @brief Shared owned-slice Adam and explicit routing mass. */
#include "life_optimizer.h"
#include "gameplay/gameplay_internal.h"
#include "internal/error.h"
#include <math.h>
#include <string.h>

typedef struct life_update {
    double value;
    double first;
    double second;
} life_update;

typedef struct life_adam {
    double scale;
    double first[CGAI_GAMEPLAY_MAX_MODULES];
    double second[CGAI_GAMEPLAY_MAX_MODULES];
} life_adam;

static void mark_slice(const cgai_gameplay_model *model, unsigned char *owners, const double *start,
                       size_t count, unsigned char owner) {
    const size_t offset = (size_t)(start - model->parameters);
    memset(owners + offset, owner, count);
}

static void bind_module(const cgai_gameplay_model *model, unsigned char *owners, size_t module) {
    const size_t hidden = model->config.hidden_dimensions;
    const size_t experts = model->config.centroids_per_module;
    const size_t outputs = model->config.output_counts[0];
    const unsigned char owner = (unsigned char)(module + 1U);
    mark_slice(model, owners, model->outer + module * hidden, hidden, owner);
    mark_slice(model, owners, model->inner + module * experts * hidden, experts * hidden, owner);
    mark_slice(model, owners, model->decoders[0] + module * outputs * hidden, outputs * hidden,
               owner);
    mark_slice(model, owners, model->heads[0] + module * experts * outputs, experts * outputs,
               owner);
}

cgai_status life_optimizer_bind(const cgai_gameplay_model *model, unsigned char *owners) {
    if (model == NULL || owners == NULL || model->config.task_count != 1U ||
        !cgai_gameplay_validate_config(&model->config))
        return cgai_fail("invalid Life optimizer ownership map");
    memset(owners, 0, model->parameter_count);
    for (size_t module = 0U; module < model->config.module_count; ++module)
        bind_module(model, owners, module);
    return CGAI_STATUS_OK;
}

static cgai_status validate_view(const life_optimizer_view *view, uint32_t participants) {
    if (view == NULL || view->model == NULL || view->session == NULL ||
        view->session->model != view->model || view->mass == NULL ||
        view->model->config.task_count != 1U || participants == 0U ||
        (participants & ~cgai_gameplay_mask(view->model->config.module_count)) != 0U)
        return cgai_fail("invalid Life optimizer participant view");
    for (size_t module = 0U; module < view->model->config.module_count; ++module)
        if ((participants & (1U << module)) != 0U &&
            (!isfinite(view->mass[module]) || view->mass[module] <= 0.0))
            return cgai_fail("invalid Life optimizer routing mass");
    return CGAI_STATUS_OK;
}

static cgai_status mix_outputs(const life_optimizer_view *view, uint32_t participants) {
    cgai_gameplay_session *session = view->session;
    const size_t outputs = view->model->config.output_counts[0];
    for (size_t output = 0U; output < outputs; ++output) {
        double mixed = -INFINITY;
        for (size_t module = 0U; module < view->model->config.module_count; ++module)
            if ((participants & (1U << module)) != 0U)
                mixed = cgai_gameplay_log_add(
                    mixed, session->log_alpha[module] +
                               session->log_module_outputs[module * outputs + output]);
        if (!isfinite(mixed))
            return cgai_fail("nonfinite mass-aware Life mixture");
        session->log_outputs[output] = fmin(mixed, 0.0);
        session->outputs[output] = exp(session->log_outputs[output]);
    }
    return CGAI_STATUS_OK;
}

cgai_status life_optimizer_forward(const life_optimizer_view *view,
                                   const cgai_gameplay_state *state, uint32_t participants) {
    if (!validate_view(view, participants) || !cgai_gameplay_validate_state(view->model, state) ||
        !cgai_gameplay_forward(view->session, state, 0U, participants))
        return CGAI_STATUS_ERROR;
    /* Backward differentiates these adjusted priors using the composed model's math. */
    for (size_t module = 0U; module < view->model->config.module_count; ++module)
        if ((participants & (1U << module)) != 0U)
            view->session->log_alpha[module] += log(view->mass[module]);
    if (!cgai_gameplay_log_softmax(view->session->log_alpha, view->session->alpha,
                                   view->model->config.module_count, participants))
        return CGAI_STATUS_ERROR;
    return mix_outputs(view, participants);
}

static int scalar_admitted(const life_optimizer_view *view, size_t index, uint32_t participants) {
    return view->owners[index] != 0U &&
           (participants & (1U << ((unsigned int)view->owners[index] - 1U))) != 0U;
}

static cgai_status restricted_clip(const life_optimizer_view *view, uint32_t participants,
                                   double *scale) {
    double squared = 0.0;
    for (size_t i = 0U; i < view->model->parameter_count; ++i)
        if (scalar_admitted(view, i, participants))
            squared += view->gradient[i] * view->gradient[i];
    if (!isfinite(squared))
        return cgai_fail("nonfinite restricted Life gradient norm");
    const double norm = sqrt(squared);
    *scale = norm > 5.0 ? 5.0 / norm : 1.0;
    return CGAI_STATUS_OK;
}

static cgai_status prepare_adam(const life_optimizer_view *view, uint32_t participants,
                                life_adam *adam) {
    if (!restricted_clip(view, participants, &adam->scale))
        return CGAI_STATUS_ERROR;
    for (size_t module = 0U; module < view->model->config.module_count; ++module)
        if ((participants & (1U << module)) != 0U) {
            const double step = (double)(view->steps[module] + 1U);
            adam->first[module] = 1.0 - pow(0.9, step);
            adam->second[module] = 1.0 - pow(0.999, step);
        }
    return CGAI_STATUS_OK;
}

static life_update propose_update(const life_optimizer_view *view, size_t index,
                                  const life_adam *adam) {
    const size_t module = (size_t)view->owners[index] - 1U;
    const double gradient = view->gradient[index] * adam->scale;
    life_update update;
    update.first = 0.9 * view->model->adam_first[index] + 0.1 * gradient;
    update.second = 0.999 * view->model->adam_second[index] + 0.001 * gradient * gradient;
    update.value = view->model->parameters[index] -
                   0.01 * (update.first / adam->first[module]) /
                       (sqrt(update.second / adam->second[module]) + 1e-8) -
                   0.01 * 0.0001 * view->model->parameters[index];
    return update;
}

static cgai_status validate_updates(const life_optimizer_view *view, uint32_t participants,
                                    const life_adam *adam) {
    for (size_t i = 0U; i < view->model->parameter_count; ++i)
        if (scalar_admitted(view, i, participants)) {
            const life_update update = propose_update(view, i, adam);
            if (!isfinite(update.value) || !isfinite(update.first) || !isfinite(update.second) ||
                update.second < 0.0)
                return cgai_fail("nonfinite restricted Life Adam update");
        }
    return CGAI_STATUS_OK;
}

static void apply_updates(const life_optimizer_view *view, uint32_t participants,
                          const life_adam *adam) {
    for (size_t i = 0U; i < view->model->parameter_count; ++i)
        if (scalar_admitted(view, i, participants)) {
            const life_update update = propose_update(view, i, adam);
            view->model->parameters[i] = update.value;
            view->model->adam_first[i] = update.first;
            view->model->adam_second[i] = update.second;
        }
    for (size_t module = 0U; module < view->model->config.module_count; ++module)
        if ((participants & (1U << module)) != 0U)
            ++view->steps[module];
    ++view->model->training_step;
}

static cgai_status validate_training(const life_optimizer_view *view, uint32_t participants) {
    if (!validate_view(view, participants) || view->gradient == NULL || view->owners == NULL ||
        view->steps == NULL || view->model->adam_first == NULL ||
        view->model->adam_second == NULL || view->model->training_step == UINT64_MAX)
        return cgai_fail("invalid Life optimizer training state");
    for (size_t module = 0U; module < view->model->config.module_count; ++module)
        if ((participants & (1U << module)) != 0U && view->steps[module] == UINT64_MAX)
            return cgai_fail("Life optimizer participant clock would overflow");
    for (size_t i = 0U; i < view->model->parameter_count; ++i)
        if (view->owners[i] > view->model->config.module_count)
            return cgai_fail("invalid Life optimizer scalar owner");
    return CGAI_STATUS_OK;
}

cgai_status life_optimizer_step(const life_optimizer_view *view, const cgai_gameplay_state *state,
                                uint32_t target, uint32_t participants) {
    if (!validate_training(view, participants) || state == NULL ||
        target >= view->model->config.output_counts[0])
        return cgai_fail("invalid Life optimizer training target");
    const cgai_gameplay_example example = {*state, 0U, target};
    life_adam adam = {0};
    if (!life_optimizer_forward(view, state, participants) ||
        !cgai_gameplay_backward(view->session, &example, view->gradient) ||
        !prepare_adam(view, participants, &adam) || !validate_updates(view, participants, &adam))
        return CGAI_STATUS_ERROR;
    apply_updates(view, participants, &adam);
    return CGAI_STATUS_OK;
}
