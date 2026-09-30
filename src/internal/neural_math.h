/** @file neural_math.h @brief Differentiable centroid routing and next-token gradients. */

#ifndef CGAI_NEURAL_MATH_H
#define CGAI_NEURAL_MATH_H

#include "neural_internal.h"

/**
 * @brief Owned scratch storage for one model's forward and backward calculations.
 *
 * All numeric pointers borrow disjoint slices of storage. The associated model must remain alive
 * with unchanged dimensions and vocabulary. Each concurrent reader needs its own workspace.
 */
typedef struct cgai_neural_workspace {
    const cgai_neural_model
        *model;               /**< Borrowed model whose immutable shape determines capacity. */
    double *storage;          /**< Owned contiguous allocation released by destroy. */
    double *input;            /**< Concatenated context_window-by-embedding_dimensions input. */
    double *hidden;           /**< hidden_dimensions tanh encoder activations. */
    double *log_gates;        /**< centroid_count normalized log routing weights. */
    double *gates;            /**< centroid_count routing probabilities. */
    double *log_experts;      /**< centroid_count-by-vocabulary_size log token probabilities. */
    double *probabilities;    /**< vocabulary_size next-token probabilities; BOS is zero. */
    double *responsibilities; /**< centroid_count posterior weights for the current target. */
    double *hidden_gradient;  /**< hidden_dimensions derivatives before encoder backprop. */
    double *input_gradient;   /**< context_window-by-embedding_dimensions input derivatives. */
    int ready;                /**< Nonzero after a successful forward pass on this model. */
} cgai_neural_workspace;

/**
 * @brief Allocate all scratch storage required by one immutable model shape.
 *
 * The model remains caller-owned. Zero initialization makes a new workspace unreadable as a
 * forward result until a successful calculation explicitly sets its ready flag.
 *
 * @param model Borrowed initialized model, or NULL to receive a diagnostic.
 * @return New caller-owned workspace, or NULL after cleaning up any partial allocation.
 */
cgai_neural_workspace *cgai_neural_workspace_create(const cgai_neural_model *model);

/**
 * @brief Free an owned workspace without releasing or changing its borrowed model.
 *
 * Only storage owns the numeric allocation; individual slice pointers must never be freed. NULL
 * is accepted during error cleanup, and all borrowed views expire when this function returns.
 *
 * @param workspace Owned workspace to release, or NULL.
 */
void cgai_neural_workspace_destroy(cgai_neural_workspace *workspace);

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
                                cgai_neural_workspace *workspace);

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
                        cgai_token_id target);

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
                                 double *gradient);

#endif
