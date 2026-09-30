/** @file neural_math.c @brief Stable mixture likelihoods and analytical neural centroid gradients.
 */

#include "internal/neural_math.h"
#include "internal/error.h"
#include "internal/size_utils.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Borrowed views into a caller-owned flat parameter gradient. */
typedef struct cgai_neural_derivatives {
    double *embeddings; /**< Vocabulary-by-embedding-dimensions gradient rows. */
    double *encoder;    /**< Hidden-by-concatenated-input affine weight gradients. */
    double *bias;       /**< Hidden-dimensions affine bias gradients. */
    double *centroids;  /**< Centroid-count-by-hidden-dimensions center gradients. */
    double *logits;     /**< Centroid-count-by-vocabulary-size expert logit gradients. */
} cgai_neural_derivatives;

/**
 * @brief Count scratch elements with checked allocation arithmetic.
 *
 * The slice lengths describe two input arrays, two hidden arrays, three routing arrays, one
 * expert table, and one prediction array. A successful count also fits as a byte allocation.
 *
 * @param model Borrowed initialized model supplying positive array dimensions.
 * @param count Writable number of doubles required; unchanged until products are checked.
 * @return One when the allocation is representable, or zero on size overflow.
 */
static int cgai_neural_workspace_size(const cgai_neural_model *model, size_t *count) {
    /* Step 1: Prove the flattened input and expert products fit before using them. */
    size_t input = 0U;
    size_t experts = 0U;
    if (!cgai_size_mul(model->config.context_window, model->config.embedding_dimensions, &input) ||
        !cgai_size_mul(model->config.centroid_count, model->vocabulary_size, &experts)) {
        return 0;
    }
    /* Step 2: Sum every disjoint slice, checking each addition for overflow. */
    const size_t slices[] = {input,
                             input,
                             model->config.hidden_dimensions,
                             model->config.hidden_dimensions,
                             model->config.centroid_count,
                             model->config.centroid_count,
                             model->config.centroid_count,
                             experts,
                             model->vocabulary_size};
    *count = 0U;
    for (size_t index = 0U; index < sizeof(slices) / sizeof(slices[0]); ++index) {
        if (!cgai_size_add(*count, slices[index], count)) {
            return 0;
        }
    }
    /* Step 3: calloc still needs the element count to fit after conversion to bytes. */
    return *count <= SIZE_MAX / sizeof(double);
}

/**
 * @brief Divide one allocated scratch buffer into nonoverlapping shape-specific slices.
 *
 * Pointer increments are measured in doubles, not bytes. The size helper has already checked every
 * product and total, so the final input-gradient slice ends exactly at the allocation boundary.
 *
 * @param model Borrowed initialized model defining all slice widths.
 * @param workspace Mutable workspace owning storage of the checked total length.
 */
static void cgai_neural_workspace_bind(const cgai_neural_model *model,
                                       cgai_neural_workspace *workspace) {
    /* Step 1: Retain identity and assign forward arrays in their contiguous storage order. */
    const size_t input = model->config.context_window * model->config.embedding_dimensions;
    workspace->model = model;
    workspace->input = workspace->storage;
    workspace->hidden = workspace->input + input;
    workspace->log_gates = workspace->hidden + model->config.hidden_dimensions;
    workspace->gates = workspace->log_gates + model->config.centroid_count;
    workspace->log_experts = workspace->gates + model->config.centroid_count;
    workspace->probabilities =
        workspace->log_experts + model->config.centroid_count * model->vocabulary_size;
    /* Step 2: Place backward scratch after the probability vector. */
    workspace->responsibilities = workspace->probabilities + model->vocabulary_size;
    workspace->hidden_gradient = workspace->responsibilities + model->config.centroid_count;
    workspace->input_gradient = workspace->hidden_gradient + model->config.hidden_dimensions;
}

/**
 * @brief Allocate all scratch storage required by one immutable model shape.
 *
 * The model remains caller-owned. Zero initialization makes a new workspace unreadable as a
 * forward result until a successful calculation explicitly sets its ready flag.
 *
 * @param model Borrowed initialized model, or NULL to receive a diagnostic.
 * @return New caller-owned workspace, or NULL after cleaning up any partial allocation.
 */
cgai_neural_workspace *cgai_neural_workspace_create(const cgai_neural_model *model) {
    /* Step 1: Establish representable capacities before allocating either ownership layer. */
    size_t count = 0U;
    if (model == NULL || !cgai_neural_workspace_size(model, &count)) {
        cgai_fail("Invalid neural workspace dimensions.");
        return NULL;
    }
    cgai_neural_workspace *workspace = calloc(1U, sizeof(*workspace));
    if (workspace == NULL) {
        cgai_fail("Cannot allocate neural workspace.");
        return NULL;
    }
    /* Step 2: Allocate the numeric buffer and release its owner if this second allocation fails. */
    workspace->storage = calloc(count, sizeof(double));
    if (workspace->storage == NULL) {
        cgai_neural_workspace_destroy(workspace);
        cgai_fail("Cannot allocate neural workspace arrays.");
        return NULL;
    }
    /* Step 3: Publish slice pointers only after storage exists. */
    cgai_neural_workspace_bind(model, workspace);
    return workspace;
}

/**
 * @brief Free an owned workspace without releasing or changing its borrowed model.
 *
 * Only storage owns the numeric allocation; individual slice pointers must never be freed. NULL
 * is accepted during error cleanup, and all borrowed views expire when this function returns.
 *
 * @param workspace Owned workspace to release, or NULL.
 */
void cgai_neural_workspace_destroy(cgai_neural_workspace *workspace) {
    /* Step 1: Make cleanup safe for a constructor that has not allocated its owner yet. */
    if (workspace == NULL) {
        return;
    }
    /* Step 2: Release the shared numeric allocation and then its pointer container. */
    free(workspace->storage);
    free(workspace);
}

/**
 * @brief Obtain a stable maximum and shifted log normalizer for a score suffix.
 *
 * Subtracting the largest finite score makes every exponent at most one. The suffix is nonempty;
 * its begin index lets expert distributions exclude BOS without adding a trainable mask value.
 *
 * @param values Borrowed count-element score vector.
 * @param count Number of readable scores.
 * @param begin First included score, strictly below count.
 * @param maximum Writable largest included score.
 * @param log_sum Writable logarithm of the sum of shifted exponentials.
 * @return CGAI_STATUS_OK for finite scores, or an error with a diagnostic.
 */
static cgai_status cgai_neural_normalizer(const double *values, size_t count, size_t begin,
                                          double *maximum, double *log_sum) {
    /* Step 1: Reject nonfinite parameters and find a shift that bounds every exponential. */
    *maximum = -HUGE_VAL;
    for (size_t index = begin; index < count; ++index) {
        if (!isfinite(values[index])) {
            return cgai_fail("Neural computation produced a nonfinite score.");
        }
        *maximum = fmax(*maximum, values[index]);
    }
    /* Step 2: Sum only shifted exponentials, including at least one exact unit term. */
    double sum = 0.0;
    for (size_t index = begin; index < count; ++index) {
        sum += exp(values[index] - *maximum);
    }
    *log_sum = log(sum);
    return CGAI_STATUS_OK;
}

/**
 * @brief Normalize finite scores in log space, optionally overwriting the input.
 *
 * The maximum and sum are completely computed before any score is overwritten. Extremely wide
 * score ranges whose subtraction overflows are rejected instead of silently producing infinities.
 *
 * @param values Borrowed count-element input scores, possibly identical to output.
 * @param output Writable count-element log probabilities; prefix before begin is untouched.
 * @param count Number of scores and output slots.
 * @param begin First included score, strictly below count.
 * @return CGAI_STATUS_OK with finite included log probabilities, otherwise an error diagnostic.
 */
static cgai_status cgai_neural_log_softmax(const double *values, double *output, size_t count,
                                           size_t begin) {
    /* Step 1: Calculate the stable scalar normalization before mutating any input slot. */
    double maximum = 0.0;
    double log_sum = 0.0;
    if (!cgai_neural_normalizer(values, count, begin, &maximum, &log_sum)) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 2: Store normalized log scores and reject arithmetic overflow. */
    for (size_t index = begin; index < count; ++index) {
        output[index] = (values[index] - maximum) - log_sum;
        if (!isfinite(output[index])) {
            return cgai_fail("Neural log probability exceeds numeric range.");
        }
    }
    return CGAI_STATUS_OK;
}

/**
 * @brief Copy token embedding rows into their distinct context positions.
 *
 * Earlier and later tokens occupy different input columns, preserving order. Repeated tokens read
 * the same embedding parameters but populate separate input slots for encoder differentiation.
 *
 * @param model Borrowed initialized model containing embedding rows.
 * @param context Borrowed context_window token IDs, already checked for a non-NULL pointer.
 * @param workspace Mutable workspace whose input slice receives concatenated embeddings.
 * @return CGAI_STATUS_OK, or an error if any context ID is outside the vocabulary.
 */
static cgai_status cgai_neural_embed(const cgai_neural_model *model, const cgai_token_id *context,
                                     cgai_neural_workspace *workspace) {
    /* Step 1: Validate each token before using its index to select an embedding row. */
    const size_t dimensions = model->config.embedding_dimensions;
    for (size_t position = 0U; position < model->config.context_window; ++position) {
        if (context[position].value >= model->vocabulary_size) {
            return cgai_fail("Neural context contains an invalid token ID.");
        }
        /* Step 2: Copy one full row into this position's nonoverlapping input slot. */
        memcpy(workspace->input + position * dimensions,
               model->embeddings + context[position].value * dimensions,
               dimensions * sizeof(double));
    }
    return CGAI_STATUS_OK;
}

/**
 * @brief Apply the trainable affine map and tanh activation to the ordered input.
 *
 * Encoder rows each contain context_window times embedding_dimensions weights. Nonfinite affine
 * sums are rejected before tanh could hide an overflow by saturating to plus or minus one.
 *
 * @param model Borrowed initialized neural parameters.
 * @param workspace Mutable scratch containing input and receiving hidden activations.
 * @return CGAI_STATUS_OK, or an error diagnostic for nonfinite computation.
 */
static cgai_status cgai_neural_encode(const cgai_neural_model *model,
                                      cgai_neural_workspace *workspace) {
    /* Step 1: Each hidden row sees every position-specific input component. */
    const size_t input = model->config.context_window * model->config.embedding_dimensions;
    for (size_t hidden = 0U; hidden < model->config.hidden_dimensions; ++hidden) {
        double value = model->bias[hidden];
        for (size_t column = 0U; column < input; ++column) {
            value += model->encoder[hidden * input + column] * workspace->input[column];
        }
        /* Step 2: Validate the preactivation and retain its bounded nonlinear output. */
        if (!isfinite(value)) {
            return cgai_fail("Neural encoder produced a nonfinite activation.");
        }
        workspace->hidden[hidden] = tanh(value);
    }
    return CGAI_STATUS_OK;
}

/**
 * @brief Calculate one negative squared-distance routing score.
 *
 * The positive routing temperature controls distance sharpness. The caller validates the score
 * during normalization, so an overflow or nonfinite centroid propagates to a reported failure.
 *
 * @param model Borrowed model containing centroids and routing temperature.
 * @param workspace Borrowed scratch with current hidden activations.
 * @param centroid Valid centroid row ID.
 * @return Negative squared distance divided by routing temperature, possibly nonfinite on failure.
 */
static double cgai_neural_gate_score(const cgai_neural_model *model,
                                     const cgai_neural_workspace *workspace,
                                     cgai_centroid_id centroid) {
    /* Step 1: Accumulate squared coordinate differences within this centroid row. */
    double distance = 0.0;
    for (size_t hidden = 0U; hidden < model->config.hidden_dimensions; ++hidden) {
        const double difference =
            workspace->hidden[hidden] -
            model->centroids[centroid.value * model->config.hidden_dimensions + hidden];
        distance += difference * difference;
    }
    /* Step 2: Nearby centroids receive the largest, least-negative routing scores. */
    return -distance / model->config.routing_temperature;
}

/**
 * @brief Normalize centroid distances and every expert's trainable vocabulary logits.
 *
 * Gate normalization includes all centroids. Expert normalization starts at EOS, thereby assigning
 * BOS zero probability regardless of its unused parameter slot.
 *
 * @param model Borrowed initialized model with positive centroid count and vocabulary size.
 * @param workspace Mutable scratch with hidden activations and output log-distribution arrays.
 * @return CGAI_STATUS_OK, or an error if any required score cannot be normalized finitely.
 */
static cgai_status cgai_neural_distributions(const cgai_neural_model *model,
                                             cgai_neural_workspace *workspace) {
    /* Step 1: Turn all centroid distances into a normalized routing distribution. */
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        workspace->log_gates[row] =
            cgai_neural_gate_score(model, workspace, cgai_centroid_id_from_size(row));
    }
    if (!cgai_neural_log_softmax(workspace->log_gates, workspace->log_gates,
                                 model->config.centroid_count, 0U)) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 2: Retain linear routing weights and each expert's non-BOS log probabilities. */
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        workspace->gates[row] = exp(workspace->log_gates[row]);
        double *expert = workspace->log_experts + row * model->vocabulary_size;
        expert[CGAI_TOKEN_BOS] = -HUGE_VAL;
        if (!cgai_neural_log_softmax(model->logits + row * model->vocabulary_size, expert,
                                     model->output_size, CGAI_TOKEN_EOS)) {
            return CGAI_STATUS_ERROR;
        }
    }
    return CGAI_STATUS_OK;
}

/**
 * @brief Combine expert probabilities using nonnegative normalized routing weights.
 *
 * This linear-space vector is used for generation. Loss evaluation separately uses log space so
 * extremely unlikely target tokens remain measurable even when this vector underflows at a slot.
 *
 * @param model Borrowed model defining expert and token counts.
 * @param workspace Mutable scratch with normalized distributions and writable predictions.
 */
static void cgai_neural_mix(const cgai_neural_model *model, cgai_neural_workspace *workspace) {
    /* Step 1: Reset the complete vocabulary vector, preserving a zero BOS entry. */
    memset(workspace->probabilities, 0, model->vocabulary_size * sizeof(double));
    /* Step 2: Add each expert's probability vector scaled by its context-dependent gate. */
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        for (size_t token = CGAI_TOKEN_EOS; token < model->output_size; ++token) {
            workspace->probabilities[token] +=
                workspace->gates[row] *
                exp(workspace->log_experts[row * model->vocabulary_size + token]);
        }
    }
}

/**
 * @brief Run ordered embedding lookup, tanh encoding, and differentiable centroid mixing.
 *
 * Only scratch storage changes. Readiness is cleared before any operation that may fail, preventing
 * callers from reading an old loss after an invalid context or numeric overflow.
 *
 * @param model Borrowed initialized model whose parameters remain unchanged during this call.
 * @param context Borrowed context_window valid token IDs, including left padding when needed.
 * @param workspace Mutable scratch allocated for exactly this model.
 * @return CGAI_STATUS_OK with ready predictions, otherwise CGAI_STATUS_ERROR and a diagnostic.
 */
cgai_status cgai_neural_forward(const cgai_neural_model *model, const cgai_token_id *context,
                                cgai_neural_workspace *workspace) {
    /* Step 1: Invalidate previous outputs and verify the borrowed object relationship. */
    if (workspace != NULL) {
        workspace->ready = 0;
    }
    if (model == NULL || context == NULL || workspace == NULL || workspace->model != model) {
        return cgai_fail("Neural forward requires a model, context, and matching workspace.");
    }
    /* Step 2: Stop at the first failed layer; no partial result becomes ready. */
    if (!cgai_neural_embed(model, context, workspace) || !cgai_neural_encode(model, workspace) ||
        !cgai_neural_distributions(model, workspace)) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 3: Publish a completed prediction vector after every distribution is valid. */
    cgai_neural_mix(model, workspace);
    workspace->ready = 1;
    return CGAI_STATUS_OK;
}

/**
 * @brief Combine target-specific log contributions without linear-probability underflow.
 *
 * Each contribution is log gate plus log expert probability. A second log-sum-exp combines them;
 * unlike taking log(probabilities[target]), this remains finite for very unlikely targets.
 *
 * @param model Borrowed model defining centroid and vocabulary counts.
 * @param workspace Borrowed successful forward result.
 * @param target Valid non-BOS output token ID.
 * @return Target log probability; the public loss function diagnoses nonfinite arithmetic.
 */
static double cgai_neural_log_probability(const cgai_neural_model *model,
                                          const cgai_neural_workspace *workspace,
                                          cgai_token_id target) {
    /* Step 1: Find the largest log contribution across all centroid experts. */
    double maximum = -HUGE_VAL;
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        maximum =
            fmax(maximum, workspace->log_gates[row] +
                              workspace->log_experts[row * model->vocabulary_size + target.value]);
    }
    /* Step 2: Sum shifted contributions and restore their common logarithmic scale. */
    double sum = 0.0;
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        sum += exp((workspace->log_gates[row] +
                    workspace->log_experts[row * model->vocabulary_size + target.value]) -
                   maximum);
    }
    return maximum + log(sum);
}

/**
 * @brief Evaluate next-token negative log likelihood from a current forward result.
 *
 * The caller must not mutate parameters between forward and loss. Tiny positive log probabilities
 * caused by rounding are clamped to zero so a valid loss remains nonnegative.
 *
 * @param model Borrowed model used to produce workspace's current prediction.
 * @param workspace Borrowed successful forward workspace for this model.
 * @param target Valid non-BOS token to score.
 * @return Finite nonnegative loss, or HUGE_VAL with a diagnostic on invalid state or overflow.
 */
double cgai_neural_loss(const cgai_neural_model *model, const cgai_neural_workspace *workspace,
                        cgai_token_id target) {
    /* Step 1: Validate state and target before dereferencing any probability rows. */
    if (model == NULL || workspace == NULL || workspace->model != model || !workspace->ready ||
        target.value == CGAI_TOKEN_BOS || target.value >= model->output_size) {
        cgai_fail("Neural loss requires a ready workspace and a valid non-BOS target.");
        return HUGE_VAL;
    }
    /* Step 2: Use log-space mixing so representable losses survive probability underflow. */
    const double log_probability = cgai_neural_log_probability(model, workspace, target);
    if (!isfinite(log_probability)) {
        cgai_fail("Neural target loss exceeds numeric range.");
        return HUGE_VAL;
    }
    return fmax(0.0, -log_probability);
}

/**
 * @brief Map flat gradient storage into the same parameter groups as the model.
 *
 * Model parameter pointers all refer to slices of its single parameter allocation. Subtracting
 * its base obtains element offsets, which are then reused within caller-owned gradient storage.
 *
 * @param model Borrowed model with consistent parameter slice pointers.
 * @param gradient Borrowed writable parameter_count doubles.
 * @return Nonowning gradient views whose lifetime is bounded by the caller's allocation.
 */
static cgai_neural_derivatives cgai_neural_gradient_views(const cgai_neural_model *model,
                                                          double *gradient) {
    /* Step 1: Preserve the parameter layout exactly without duplicating offset formulas. */
    const cgai_neural_derivatives views = {gradient + (model->embeddings - model->parameters),
                                           gradient + (model->encoder - model->parameters),
                                           gradient + (model->bias - model->parameters),
                                           gradient + (model->centroids - model->parameters),
                                           gradient + (model->logits - model->parameters)};
    return views;
}

/** @brief Normalize target responsibilities without cancelling large negative log likelihoods.
 *
 * Removing the common expert scale before adding gates preserves routing differences
 * even when every expert assigns the target a log probability near minus 1e308.
 * @param model Borrowed shape and vocabulary.
 * @param workspace Mutable forward scratch receiving normalized target responsibilities.
 * @param target Valid non-BOS supervised token.
 * @return OK for finite normalized responsibilities, ERROR otherwise. */
static cgai_status cgai_neural_target_responsibilities(const cgai_neural_model *model,
                                                       cgai_neural_workspace *workspace,
                                                       cgai_token_id target) {
    /* Step 1: Remove only the common expert scale, retaining relative routing weights. */
    double maximum = -HUGE_VAL;
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        maximum =
            fmax(maximum, workspace->log_experts[row * model->vocabulary_size + target.value]);
    }
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        workspace->responsibilities[row] =
            (workspace->log_experts[row * model->vocabulary_size + target.value] - maximum) +
            workspace->log_gates[row];
    }
    /* Step 2: Normalize before exponentiation, so responsibilities sum to one. */
    if (!cgai_neural_log_softmax(workspace->responsibilities, workspace->responsibilities,
                                 model->config.centroid_count, 0U))
        return CGAI_STATUS_ERROR;
    for (size_t row = 0U; row < model->config.centroid_count; ++row)
        workspace->responsibilities[row] = exp(workspace->responsibilities[row]);
    return CGAI_STATUS_OK;
}

/**
 * @brief Differentiate each expert softmax using its target posterior responsibility.
 *
 * Responsibility is the fraction of the target probability supplied by an expert, computed from
 * log probabilities. Multiplying its softmax-minus-target derivative yields the mixture gradient.
 *
 * @param model Borrowed parameter shape and vocabulary.
 * @param workspace Borrowed scratch containing normalized target responsibilities.
 * @param target Valid non-BOS supervised token ID.
 * @param derivatives Borrowed gradient views, already zeroed including unused BOS slots.
 */
static void cgai_neural_expert_backward(const cgai_neural_model *model,
                                        cgai_neural_workspace *workspace, cgai_token_id target,
                                        const cgai_neural_derivatives *derivatives) {
    /* Step 1: Read each expert's previously normalized target responsibility. */
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        const size_t offset = row * model->vocabulary_size;
        const double responsibility = workspace->responsibilities[row];
        /* Step 2: Weight the expert cross-entropy derivative; BOS stays exactly zero. */
        for (size_t token = CGAI_TOKEN_EOS; token < model->output_size; ++token) {
            derivatives->logits[offset + token] =
                responsibility *
                (exp(workspace->log_experts[offset + token]) - (token == target.value ? 1.0 : 0.0));
        }
    }
}

/**
 * @brief Differentiate squared-distance routing into centers and hidden activations.
 *
 * The score derivative is prior routing weight minus target posterior responsibility. Hidden
 * and centroid derivatives have opposite signs because they enter the distance as a difference.
 *
 * @param model Borrowed model with finite centers and positive routing temperature.
 * @param workspace Mutable scratch with routing posteriors and zeroed hidden derivatives.
 * @param derivatives Borrowed writable centroid gradient view.
 */
static void cgai_neural_routing_backward(const cgai_neural_model *model,
                                         cgai_neural_workspace *workspace,
                                         const cgai_neural_derivatives *derivatives) {
    /* Step 1: Differentiate the routing softmax using each expert's posterior responsibility. */
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        const double score_gradient = workspace->gates[row] - workspace->responsibilities[row];
        /* Step 2: Accumulate hidden gradients and write this center's opposite derivative. */
        for (size_t hidden = 0U; hidden < model->config.hidden_dimensions; ++hidden) {
            const size_t index = row * model->config.hidden_dimensions + hidden;
            const double derivative = 2.0 * score_gradient *
                                      (workspace->hidden[hidden] - model->centroids[index]) /
                                      model->config.routing_temperature;
            derivatives->centroids[index] = derivative;
            workspace->hidden_gradient[hidden] -= derivative;
        }
    }
}

/**
 * @brief Differentiate tanh and its affine map into weights, biases, and ordered inputs.
 *
 * The stored tanh activation supplies its derivative as one minus activation squared. Input
 * gradients accumulate contributions from all hidden rows before embedding rows are updated.
 *
 * @param model Borrowed initialized affine weight parameters.
 * @param workspace Mutable scratch holding hidden gradients and zeroed input gradients.
 * @param derivatives Borrowed writable encoder and bias gradient views.
 */
static void cgai_neural_encoder_backward(const cgai_neural_model *model,
                                         cgai_neural_workspace *workspace,
                                         const cgai_neural_derivatives *derivatives) {
    /* Step 1: Apply tanh's derivative separately to each hidden preactivation. */
    const size_t input = model->config.context_window * model->config.embedding_dimensions;
    for (size_t hidden = 0U; hidden < model->config.hidden_dimensions; ++hidden) {
        const double activation = workspace->hidden[hidden];
        const double derivative =
            workspace->hidden_gradient[hidden] * (1.0 - activation * activation);
        derivatives->bias[hidden] = derivative;
        /* Step 2: Form the affine outer product and propagate through the unchanged weights. */
        for (size_t column = 0U; column < input; ++column) {
            derivatives->encoder[hidden * input + column] = derivative * workspace->input[column];
            workspace->input_gradient[column] +=
                derivative * model->encoder[hidden * input + column];
        }
    }
}

/**
 * @brief Accumulate positional input derivatives into their shared token embedding rows.
 *
 * A token may appear at multiple positions, including repeated BOS padding. Addition is necessary
 * because those positions all depend on the same learned embedding row.
 *
 * @param model Borrowed model defining context and embedding widths.
 * @param context Borrowed context_window token IDs validated by forward.
 * @param workspace Borrowed scratch with complete positional input gradients.
 * @param derivatives Borrowed writable embedding gradient view, initially zero.
 */
static void cgai_neural_embedding_backward(const cgai_neural_model *model,
                                           const cgai_token_id *context,
                                           const cgai_neural_workspace *workspace,
                                           const cgai_neural_derivatives *derivatives) {
    /* Step 1: Visit every context position and its embedding components. */
    const size_t dimensions = model->config.embedding_dimensions;
    for (size_t position = 0U; position < model->config.context_window; ++position) {
        for (size_t component = 0U; component < dimensions; ++component) {
            /* Step 2: Sum positional contributions into the token's shared parameter row. */
            derivatives->embeddings[context[position].value * dimensions + component] +=
                workspace->input_gradient[position * dimensions + component];
        }
    }
}

/**
 * @brief Clear backward scratch and propagate a target's loss through all trainable layers.
 *
 * Every derivative uses the same unchanged parameter state. The gradient views borrow caller
 * storage, while activation derivatives occupy reusable workspace slices.
 *
 * @param model Borrowed unchanged model that produced the current forward result.
 * @param context Borrowed valid context_window IDs.
 * @param target Valid non-BOS supervised output token.
 * @param workspace Mutable ready forward workspace.
 * @param gradient Writable parameter_count doubles, distinct from model and scratch storage.
 */
static void cgai_neural_backward(const cgai_neural_model *model, const cgai_token_id *context,
                                 cgai_token_id target, cgai_neural_workspace *workspace,
                                 double *gradient) {
    /* Step 1: Reset every accumulation buffer before following the chain rule. */
    const cgai_neural_derivatives derivatives = cgai_neural_gradient_views(model, gradient);
    memset(gradient, 0, model->parameter_count * sizeof(double));
    memset(workspace->hidden_gradient, 0, model->config.hidden_dimensions * sizeof(double));
    memset(workspace->input_gradient, 0,
           model->config.context_window * model->config.embedding_dimensions * sizeof(double));
    /* Step 2: Propagate from expert likelihoods through routing, encoder, and token lookup. */
    cgai_neural_expert_backward(model, workspace, target, &derivatives);
    cgai_neural_routing_backward(model, workspace, &derivatives);
    cgai_neural_encoder_backward(model, workspace, &derivatives);
    cgai_neural_embedding_backward(model, context, workspace, &derivatives);
}

/**
 * @brief Calculate and validate one example's complete analytical loss gradient.
 *
 * Forward runs inside this call so activations and derivatives correspond to exactly the same
 * parameter state. On success loss remains available from workspace for reporting or checking.
 *
 * @param model Borrowed initialized model whose parameters are not mutated here.
 * @param context Borrowed context_window valid token IDs.
 * @param target Valid non-BOS next-token ID.
 * @param workspace Mutable scratch created for model.
 * @param gradient Writable parameter_count doubles separate from model and scratch arrays.
 * @return CGAI_STATUS_OK for a finite full gradient, otherwise an error with a diagnostic.
 */
cgai_status cgai_neural_gradient(const cgai_neural_model *model, const cgai_token_id *context,
                                 cgai_token_id target, cgai_neural_workspace *workspace,
                                 double *gradient) {
    /* Step 1: Reject absent output storage and targets outside the output vocabulary. */
    if (model == NULL || gradient == NULL || target.value == CGAI_TOKEN_BOS ||
        target.value >= model->output_size) {
        return cgai_fail("Neural gradient requires writable storage and a valid non-BOS target.");
    }
    if (!cgai_neural_forward(model, context, workspace) ||
        !isfinite(cgai_neural_loss(model, workspace, target)) ||
        !cgai_neural_target_responsibilities(model, workspace, target)) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 2: Compute every derivative before the training caller can update any parameter. */
    cgai_neural_backward(model, context, target, workspace, gradient);
    /* Step 3: Detect backward overflow so training never applies an invalid gradient. */
    for (size_t index = 0U; index < model->parameter_count; ++index) {
        if (!isfinite(gradient[index])) {
            return cgai_fail("Neural gradient exceeds numeric range.");
        }
    }
    return CGAI_STATUS_OK;
}
