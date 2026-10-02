/** @file gameplay_backward.c @brief Exact log-mixture responsibilities through both centroid
 * layers. */
#include "gameplay_internal.h"
#include "internal/error.h"
#include <math.h>
#include <string.h>

/** @brief Backpropagate one distance score into shared hidden coordinates and its centroid.
 * @param session Exclusive forward/backward scratch.
 * @param centroid Borrowed hidden-dimensional routing centroid.
 * @param centroid_gradient Writable corresponding parameter-gradient slice.
 * @param score_gradient Scalar derivative of the negative distance score. */
static void backward_distance(cgai_gameplay_session *session, const double *centroid,
                              double *centroid_gradient, double score_gradient) {
    /* Step 1: The centroid derivative is the opposite of its shared hidden derivative. */
    const double scale = -2.0 * score_gradient / session->model->config.routing_temperature;
    for (size_t hidden = 0U; hidden < session->model->config.hidden_dimensions; ++hidden) {
        const double derivative = scale * (session->hidden[hidden] - centroid[hidden]);
        session->hidden_gradient[hidden] += derivative;
        centroid_gradient[hidden] -= derivative;
    }
}

/** @brief Accumulate one expert logit's derivative through its module-local conditional readout.
 * @param session Exclusive successful forward/backward scratch.
 * @param gradient Writable complete parameter-gradient array.
 * @param task Requested task-local head.
 * @param module Compatible active specialist module.
 * @param output Task-local categorical output index.
 * @param derivative Scalar derivative of this expert's conditional logit. */
static void backward_decoder(cgai_gameplay_session *session, double *gradient, uint32_t task,
                             size_t module, size_t output, double derivative) {
    /* Step1: Internal experts reuse their module's task readout, conditioned by square-root fan-in.
     */
    const cgai_gameplay_model *model = session->model;
    const size_t hidden = model->config.hidden_dimensions;
    const size_t base = (module * model->config.output_counts[task] + output) * hidden;
    const size_t offset = model->decoder_offsets[task] + base;
    const double scaled = derivative / sqrt((double)hidden);
    for (size_t coordinate = 0U; coordinate < hidden; ++coordinate) {
        gradient[offset + coordinate] += scaled * session->hidden[coordinate];
        session->hidden_gradient[coordinate] += scaled * model->decoders[task][base + coordinate];
    }
}

/** @brief Backpropagate one conditional expert distribution weighted by target responsibility.
 * @param session Exclusive successful forward scratch.
 * @param example Borrowed target matching this forward.
 * @param gradient Writable complete parameter-gradient array.
 * @param expert Dense module-bank expert index.
 * @param responsibility Posterior share of the target assigned to this expert. */
static void backward_head(cgai_gameplay_session *session, const cgai_gameplay_example *example,
                          double *gradient, size_t expert, double responsibility) {
    /* Step 1: Categorical loss uses target posterior mass rather than prior routing alone. */
    const cgai_gameplay_model *model = session->model;
    const size_t outputs = model->config.output_counts[example->task];
    const size_t offset = model->head_offsets[example->task] + expert * outputs;
    for (size_t output = 0U; output < outputs; ++output) {
        const double derivative =
            responsibility *
            (session->head_probabilities[expert * model->maximum_outputs + output] -
             (output == example->target ? 1.0 : 0.0));
        gradient[offset + output] = derivative;
        backward_decoder(session, gradient, example->task,
                         expert / model->config.centroids_per_module, output, derivative);
    }
}

/** @brief Compute one expert's target responsibility using stable forward logarithms.
 * @param session Borrowed successful forward scratch.
 * @param example Borrowed matching target.
 * @param module Compatible module index.
 * @param expert Dense expert index within this module.
 * @return Target posterior probability for this exact expert. */
static double expert_responsibility(const cgai_gameplay_session *session,
                                    const cgai_gameplay_example *example, size_t module,
                                    size_t expert) {
    /* Step 1: Divide joint target mass by the whole composed target likelihood in log space. */
    return exp(session->log_alpha[module] + session->log_beta[expert] +
               session->log_heads[expert * session->model->maximum_outputs + example->target] -
               session->log_outputs[example->target]);
}

/** @brief Backpropagate one module's outer centroid, internal centroids and task heads.
 * @param session Exclusive successful forward scratch.
 * @param example Borrowed matching target.
 * @param gradient Writable complete zeroed parameter-gradient array.
 * @param module Compatible active module index. */
static void backward_module(cgai_gameplay_session *session, const cgai_gameplay_example *example,
                            double *gradient, size_t module) {
    /* Step 1: Outer routing learns the specialist's posterior responsibility for this target. */
    const cgai_gameplay_model *model = session->model;
    const size_t hidden = model->config.hidden_dimensions;
    const double responsibility =
        exp(session->log_alpha[module] +
            session->log_module_outputs[module * model->maximum_outputs + example->target] -
            session->log_outputs[example->target]);
    backward_distance(session, model->outer + module * hidden,
                      gradient + (model->outer - model->parameters) + module * hidden,
                      session->alpha[module] - responsibility);
    /* Step 2: Internal routing and categorical experts split the module's target responsibility. */
    const size_t bank = model->config.centroids_per_module;
    for (size_t expert = module * bank; expert < (module + 1U) * bank; ++expert) {
        const double expert_share = expert_responsibility(session, example, module, expert);
        backward_head(session, example, gradient, expert, expert_share);
        backward_distance(session, model->inner + expert * hidden,
                          gradient + (model->inner - model->parameters) + expert * hidden,
                          responsibility * session->beta[expert] - expert_share);
    }
}

/** @brief Backpropagate the shared affine tanh encoder into ordered input embedding slots.
 * @param session Exclusive scratch with accumulated hidden derivatives.
 * @param gradient Writable complete parameter-gradient array. */
static void backward_encoder(cgai_gameplay_session *session, double *gradient) {
    /* Step 1: Apply tanh's local derivative and accumulate ordered encoder derivatives. */
    const cgai_gameplay_model *model = session->model;
    double *encoder = gradient + (model->encoder - model->parameters);
    double *bias = gradient + (model->bias - model->parameters);
    for (size_t hidden = 0U; hidden < model->config.hidden_dimensions; ++hidden) {
        const double derivative = session->hidden_gradient[hidden] *
                                  (1.0 - session->hidden[hidden] * session->hidden[hidden]);
        bias[hidden] = derivative;
        for (size_t input = 0U; input < model->input_count; ++input) {
            encoder[hidden * model->input_count + input] = derivative * session->input[input];
            session->input_gradient[input] +=
                derivative * model->encoder[hidden * model->input_count + input];
        }
    }
}

/** @brief Accumulate only task-relevant category embedding gradients.
 * @param session Borrowed backward scratch with ordered input gradients.
 * @param example Borrowed matching categorical state and task.
 * @param gradient Writable complete parameter-gradient array. */
static void backward_embeddings(const cgai_gameplay_session *session,
                                const cgai_gameplay_example *example, double *gradient) {
    /* Step 1: Declared irrelevant fields retain zero gradients in the aligned shared coordinates.
     */
    const cgai_gameplay_model *model = session->model;
    const size_t dimensions = model->config.embedding_dimensions;
    for (size_t feature = 0U; feature < model->config.feature_count; ++feature)
        if ((model->config.task_features[example->task] & (UINT64_C(1) << feature)) != 0U) {
            const size_t category =
                model->category_offsets[feature] + example->state.values[feature];
            for (size_t dimension = 0U; dimension < dimensions; ++dimension)
                gradient[category * dimensions + dimension] +=
                    session->input_gradient[feature * dimensions + dimension];
        }
}

/** @brief Reject nonfinite gradient scalars before any optimizer mutation.
 * @param gradient Borrowed complete derivative array.
 * @param count Number of initialized scalars.
 * @return OK for finite derivatives, ERROR otherwise. */
static cgai_status validate_gradient(const double *gradient, size_t count) {
    /* Step 1: Invalid derivatives never enter the optimizer. */
    for (size_t i = 0U; i < count; ++i)
        if (!isfinite(gradient[i]))
            return cgai_fail("nonfinite gameplay gradient");
    return CGAI_STATUS_OK;
}

/** @brief Differentiate one stable composed task-local negative log likelihood.
 * @param session Exclusive successful matching forward scratch.
 * @param example Borrowed target matching the last forward state and task.
 * @param gradient Writable complete scalar gradient array.
 * @return OK on finite exact derivatives, ERROR otherwise. */
cgai_status cgai_gameplay_backward(cgai_gameplay_session *session,
                                   const cgai_gameplay_example *example, double *gradient) {
    /* Step 1: Clear every inactive head and unobserved category gradient. */
    const cgai_gameplay_model *model = session->model;
    memset(gradient, 0, model->parameter_count * sizeof(*gradient));
    memset(session->hidden_gradient, 0,
           model->config.hidden_dimensions * sizeof(*session->hidden_gradient));
    memset(session->input_gradient, 0, model->input_count * sizeof(*session->input_gradient));
    /* Step 2: Differentiate both routing layers and the shared task-aligned representation. */
    for (size_t module = 0U; module < model->config.module_count; ++module)
        if ((session->active_mask & (UINT64_C(1) << module)) != 0U)
            backward_module(session, example, gradient, module);
    backward_encoder(session, gradient);
    backward_embeddings(session, example, gradient);
    /* Step 3: Reject invalid derivatives before the optimizer publishes a parameter update. */
    return validate_gradient(gradient, model->parameter_count);
}
