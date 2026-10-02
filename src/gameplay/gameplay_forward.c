/** @file gameplay_forward.c @brief Stable dense hierarchical centroid and task-head mixtures. */
#include "gameplay_internal.h"
#include "internal/error.h"
#include <math.h>
#include <string.h>

/** @brief Add two nonnegative quantities represented as logarithms.
 * @param left Logarithm, permitting negative infinity.
 * @param right Logarithm, permitting negative infinity.
 * @return Stable logarithm of their sum. */
double cgai_gameplay_log_add(double left, double right) {
    /* Step 1: Empty accumulators contribute no probability mass. */
    if (left == -INFINITY)
        return right;
    if (right == -INFINITY)
        return left;
    /* Step 2: Keep the exponential argument nonpositive. */
    const double maximum = fmax(left, right);
    return maximum + log1p(exp(fmin(left, right) - maximum));
}

/** @brief Find the largest active finite score and reject malformed active logits.
 * @param values Borrowed logit scores.
 * @param count Supported width.
 * @param mask Complete nonempty active domain.
 * @param maximum Writable largest active score.
 * @return OK on finite scores, ERROR otherwise. */
static cgai_status maximum_score(const double *values, size_t count, uint64_t mask,
                                 double *maximum) {
    /* Step 1: Only compatible active modules participate in numerical validation. */
    *maximum = -INFINITY;
    for (size_t i = 0U; i < count; ++i)
        if ((mask & (UINT64_C(1) << i)) != 0U) {
            if (!isfinite(values[i]))
                return cgai_fail("nonfinite gameplay routing or expert score");
            *maximum = fmax(*maximum, values[i]);
        }
    return CGAI_STATUS_OK;
}

/** @brief Sum active shifted exponential scores.
 * @param values Borrowed finite active scores.
 * @param count Supported width.
 * @param active Nonempty eligible position mask.
 * @param maximum Largest active score.
 * @return Positive finite shifted normalizer. */
static double exponential_sum(const double *values, size_t count, uint64_t active, double maximum) {
    /* Step 1: Shifting ensures every exponential is at most one. */
    double total = 0.0;
    for (size_t i = 0U; i < count; ++i)
        if ((active & (UINT64_C(1) << i)) != 0U)
            total += exp(values[i] - maximum);
    return total;
}

/** @brief Normalize bounded scores stably, retaining exact log probabilities through underflow.
 * @param values Writable scores, overwritten by log probabilities.
 * @param probabilities Writable normalized probabilities.
 * @param count Supported width, 1..64.
 * @param mask Eligible positions, zero means all.
 * @return OK on finite active scores, ERROR otherwise. */
cgai_status cgai_gameplay_log_softmax(double *values, double *probabilities, size_t count,
                                      uint64_t mask) {
    /* Step 1: Find a finite active maximum before taking exponentials. */
    const uint64_t active = mask == 0U ? cgai_gameplay_mask(count) : mask;
    double maximum = 0.0;
    if (!maximum_score(values, count, active, &maximum))
        return CGAI_STATUS_ERROR;
    /* Step 2: Normalize in log space and explicitly erase excluded probabilities. */
    const double logarithm = log(exponential_sum(values, count, active, maximum));
    for (size_t i = 0U; i < count; ++i) {
        values[i] =
            (active & (UINT64_C(1) << i)) != 0U ? (values[i] - maximum) - logarithm : -INFINITY;
        probabilities[i] = exp(values[i]);
    }
    return CGAI_STATUS_OK;
}

/** @brief Copy ordered task-relevant category embeddings into shared encoder scratch.
 * @param session Exclusive scratch.
 * @param state Borrowed validated complete observation.
 * @param task Requested valid task. */
static void prepare_input(cgai_gameplay_session *session, const cgai_gameplay_state *state,
                          uint32_t task) {
    /* Step 1: Contract-declared irrelevant fields contribute zero without moving slot positions. */
    const cgai_gameplay_model *model = session->model;
    const size_t dimensions = model->config.embedding_dimensions;
    memset(session->input, 0, model->input_count * sizeof(*session->input));
    for (size_t feature = 0U; feature < model->config.feature_count; ++feature)
        if ((model->config.task_features[task] & (UINT64_C(1) << feature)) != 0U) {
            const size_t category = model->category_offsets[feature] + state->values[feature];
            memcpy(session->input + feature * dimensions, model->embeddings + category * dimensions,
                   dimensions * sizeof(*session->input));
        }
}

/** @brief Apply the shared ordered affine encoder and tanh nonlinearity.
 * @param session Exclusive scratch with prepared ordered inputs.
 * @return OK on finite activations, ERROR otherwise. */
static cgai_status encode_hidden(cgai_gameplay_session *session) {
    /* Step 1: Every task uses the same aligned encoder coordinate system. */
    const cgai_gameplay_model *model = session->model;
    for (size_t hidden = 0U; hidden < model->config.hidden_dimensions; ++hidden) {
        double value = model->bias[hidden];
        for (size_t input = 0U; input < model->input_count; ++input)
            value += model->encoder[hidden * model->input_count + input] * session->input[input];
        if (!isfinite(value))
            return cgai_fail("nonfinite gameplay encoder activation");
        session->hidden[hidden] = tanh(value);
    }
    return CGAI_STATUS_OK;
}

/** @brief Compute negative squared centroid distance at the shared routing scale.
 * @param session Borrowed scratch with finite encoded hidden representation.
 * @param centroid Borrowed hidden-dimensional centroid coordinates.
 * @return Negative distance score; callers reject nonfinite results. */
static double centroid_score(const cgai_gameplay_session *session, const double *centroid) {
    /* Step 1: Learned module and expert centroids share exactly the same hidden coordinates. */
    double distance = 0.0;
    for (size_t hidden = 0U; hidden < session->model->config.hidden_dimensions; ++hidden) {
        const double difference = session->hidden[hidden] - centroid[hidden];
        distance += difference * difference;
    }
    return -distance / session->model->config.routing_temperature;
}

/** @brief Route across compatible specialist module centroids.
 * @param session Exclusive scratch with hidden representation.
 * @param modules Nonempty eligible module mask.
 * @return OK on finite routing, ERROR otherwise. */
static cgai_status route_modules(cgai_gameplay_session *session, uint64_t modules) {
    /* Step 1: Excluded centroids are never evaluated or admitted into the mixture. */
    const cgai_gameplay_model *model = session->model;
    for (size_t module = 0U; module < model->config.module_count; ++module)
        session->log_alpha[module] =
            (modules & (UINT64_C(1) << module)) != 0U
                ? centroid_score(session, model->outer + module * model->config.hidden_dimensions)
                : -INFINITY;
    return cgai_gameplay_log_softmax(session->log_alpha, session->alpha, model->config.module_count,
                                     modules);
}

/** @brief Route through one module's complete internal centroid bank.
 * @param session Exclusive scratch with hidden representation.
 * @param module Compatible active module index.
 * @return OK on finite expert routing, ERROR otherwise. */
static cgai_status route_experts(cgai_gameplay_session *session, size_t module) {
    /* Step 1: Dense routing evaluates every expert in this compatible bank. */
    const cgai_gameplay_model *model = session->model;
    const size_t bank = model->config.centroids_per_module;
    const size_t start = module * bank;
    for (size_t expert = 0U; expert < bank; ++expert)
        session->log_beta[start + expert] = centroid_score(
            session, model->inner + (start + expert) * model->config.hidden_dimensions);
    return cgai_gameplay_log_softmax(session->log_beta + start, session->beta + start, bank, 0U);
}

/** @brief Precompute one module's conditional task readout shared across its internal experts.
 * @param session Exclusive scratch with the shared hidden representation.
 * @param task Requested compatible task-local head.
 * @param module Compatible active specialist module.
 * @return OK on finite conditional logits, ERROR otherwise. */
static cgai_status decode_hidden(cgai_gameplay_session *session, uint32_t task, size_t module) {
    /* Step1: Only this admitted module's task-local readout is evaluated before its expert
     * softmaxes. */
    const cgai_gameplay_model *model = session->model;
    const size_t hidden = model->config.hidden_dimensions;
    const size_t base = module * model->config.output_counts[task] * hidden;
    const double scale = 1.0 / sqrt((double)hidden);
    for (size_t output = 0U; output < model->config.output_counts[task]; ++output) {
        double value = 0.0;
        for (size_t coordinate = 0U; coordinate < hidden; ++coordinate)
            value += model->decoders[task][base + output * hidden + coordinate] *
                     session->hidden[coordinate];
        if (!isfinite(value))
            return cgai_fail("nonfinite gameplay conditional task readout");
        session->decoder_logits[module * model->maximum_outputs + output] = value * scale;
    }
    return CGAI_STATUS_OK;
}

/** @brief Normalize one task-local centroid expert's state-conditional categorical head.
 * @param session Exclusive scratch.
 * @param task Requested compatible head.
 * @param expert Dense module-bank expert index.
 * @return OK on finite categorical logits, ERROR otherwise. */
static cgai_status predict_expert(cgai_gameplay_session *session, uint32_t task, size_t expert) {
    /* Step 1: Distinct heads share hidden routing while retaining independent output IDs. */
    const cgai_gameplay_model *model = session->model;
    const size_t outputs = model->config.output_counts[task];
    const size_t offset = expert * model->maximum_outputs;
    const size_t module = expert / model->config.centroids_per_module;
    for (size_t output = 0U; output < outputs; ++output)
        session->log_heads[offset + output] =
            model->heads[task][expert * outputs + output] +
            session->decoder_logits[module * model->maximum_outputs + output];
    return cgai_gameplay_log_softmax(session->log_heads + offset,
                                     session->head_probabilities + offset, outputs, 0U);
}

/** @brief Combine internal bank predictions into one specialist's task distribution.
 * @param session Exclusive scratch after this module's routing and head normalization.
 * @param module Active module index.
 * @param task Requested valid head. */
static void mix_experts(cgai_gameplay_session *session, size_t module, uint32_t task) {
    /* Step 1: Sum internal centroid probabilities entirely in log space. */
    const size_t bank = session->model->config.centroids_per_module;
    const size_t maximum_outputs = session->model->maximum_outputs;
    for (size_t output = 0U; output < session->model->config.output_counts[task]; ++output) {
        double mixed = -INFINITY;
        for (size_t expert = module * bank; expert < (module + 1U) * bank; ++expert)
            mixed = cgai_gameplay_log_add(
                mixed,
                session->log_beta[expert] + session->log_heads[expert * maximum_outputs + output]);
        session->log_module_outputs[module * maximum_outputs + output] = fmin(mixed, 0.0);
    }
}

/** @brief Build task distributions for every admitted specialist.
 * @param session Exclusive scratch with successful outer routing.
 * @param task Requested valid head.
 * @param modules Nonempty compatible module mask.
 * @return OK on finite internal routing and task heads, ERROR otherwise. */
static cgai_status predict_modules(cgai_gameplay_session *session, uint32_t task,
                                   uint64_t modules) {
    /* Step 1: Ineligible modules contribute exactly zero probability and consume no bank work. */
    for (size_t module = 0U; module < session->model->config.module_count; ++module)
        if ((modules & (UINT64_C(1) << module)) != 0U) {
            if (!decode_hidden(session, task, module) || !route_experts(session, module))
                return CGAI_STATUS_ERROR;
            for (size_t expert = module * session->model->config.centroids_per_module;
                 expert < (module + 1U) * session->model->config.centroids_per_module; ++expert)
                if (!predict_expert(session, task, expert))
                    return CGAI_STATUS_ERROR;
            mix_experts(session, module, task);
        }
    return CGAI_STATUS_OK;
}

/** @brief Combine compatible specialist predictions into the final task-local distribution.
 * @param session Exclusive scratch with every active specialist prediction.
 * @param task Requested valid head.
 * @param modules Nonempty eligible module mask.
 * @return OK on finite likelihoods, ERROR otherwise. */
static cgai_status mix_modules(cgai_gameplay_session *session, uint32_t task, uint64_t modules) {
    /* Step 1: Neural composition sums outer and inner posterior mass before host constraints. */
    for (size_t output = 0U; output < session->model->config.output_counts[task]; ++output) {
        double mixed = -INFINITY;
        for (size_t module = 0U; module < session->model->config.module_count; ++module)
            if ((modules & (UINT64_C(1) << module)) != 0U)
                mixed = cgai_gameplay_log_add(
                    mixed,
                    session->log_alpha[module] +
                        session->log_module_outputs[module * session->model->maximum_outputs +
                                                    output]);
        if (!isfinite(mixed))
            return cgai_fail("nonfinite gameplay composed likelihood");
        session->log_outputs[output] = fmin(mixed, 0.0);
        session->outputs[output] = exp(session->log_outputs[output]);
    }
    return CGAI_STATUS_OK;
}

/** @brief Run one stable hierarchical task forward without allocation or host side effects.
 * @param session Exclusive reusable numerical scratch.
 * @param state Borrowed validated observation.
 * @param task Requested valid head.
 * @param modules Nonempty compatible module mask.
 * @return OK on successful finite computation, ERROR otherwise. */
cgai_status cgai_gameplay_forward(cgai_gameplay_session *session, const cgai_gameplay_state *state,
                                  uint32_t task, uint64_t modules) {
    /* Step 1: Encode ordered declared inputs and route through both centroid layers. */
    prepare_input(session, state, task);
    if (!encode_hidden(session) || !route_modules(session, modules) ||
        !predict_modules(session, task, modules) || !mix_modules(session, task, modules))
        return CGAI_STATUS_ERROR;
    /* Step 2: Publish scratch metadata only for a complete successful forward. */
    session->task = task;
    session->active_mask = modules;
    return CGAI_STATUS_OK;
}
