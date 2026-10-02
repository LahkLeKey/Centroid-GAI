/** @file gameplay_internal.h @brief Private hierarchical centroid ownership and numerical work. */
#ifndef CGAI_GAMEPLAY_INTERNAL_H
#define CGAI_GAMEPLAY_INTERNAL_H
#include "centroid_gai_gameplay.h"

/** Maximum trainable scalar count, bounding all parameter allocations. */
#define CGAI_GAMEPLAY_MAX_PARAMETERS 2000000U
/** Owned contiguous parameters; named pointers borrow their corresponding slices. */
struct cgai_gameplay_model {
    cgai_gameplay_config config; /**< Validated immutable dimensions and eligibility. */
    size_t category_count;       /**< Sum of active feature cardinalities. */
    size_t input_count;          /**< Ordered feature count times embedding dimensions. */
    size_t maximum_outputs;      /**< Largest configured output head. */
    size_t parameter_count;      /**< Complete contiguous trainable scalar count. */
    size_t category_offsets[CGAI_GAMEPLAY_MAX_FEATURES]; /**< Feature category prefix sums. */
    size_t head_offsets[CGAI_GAMEPLAY_MAX_TASKS]; /**< Absolute parameter offsets for task heads. */
    size_t decoder_offsets[CGAI_GAMEPLAY_MAX_TASKS]; /**< Absolute task-local linear readout
                                                        offsets. */
    double *parameters; /**< Owned weights: embeddings, encoder, bias, outer, inner, task
                           decoders/heads. */
    double *embeddings; /**< category_count by embedding_dimensions. */
    double *encoder;    /**< hidden_dimensions by input_count ordered encoder. */
    double *bias;       /**< hidden_dimensions shared encoder bias. */
    double *outer;      /**< module_count by hidden_dimensions routing centroids. */
    double *inner;      /**< module_count by centroids_per_module by hidden_dimensions. */
    double *heads[CGAI_GAMEPLAY_MAX_TASKS]; /**< Each module_count by bank by output_count head. */
    double *decoders[CGAI_GAMEPLAY_MAX_TASKS]; /**< Each module by output by hidden readout. */
    double *adam_first;                        /**< Owned first moments, or NULL before training. */
    double *adam_second;       /**< Owned second moments, or NULL before training. */
    uint64_t training_step;    /**< Completed Adam updates. */
    uint64_t training_epochs;  /**< Completed entire dataset passes. */
    uint64_t training_shuffle; /**< Persisted canonical shuffling stream. */
};
/** Complete reusable forward/backward scratch, borrowing an immutable live network. */
struct cgai_gameplay_session {
    const cgai_gameplay_model *model; /**< Borrowed network, outliving this owner. */
    double *storage;                  /**< Owned contiguous numerical workspace. */
    size_t storage_count;             /**< Complete allocated scalar count. */
    uint64_t active_mask;             /**< Last successful forward's eligible module mask. */
    uint32_t task;                    /**< Last successful forward's selected task. */
    double *input;                    /**< Ordered selected categorical embedding vectors. */
    double *hidden;                   /**< Shared tanh encoder output. */
    double *log_alpha;                /**< Module log routing probabilities. */
    double *alpha;                    /**< Module routing probabilities. */
    double *log_beta;                 /**< Module-bank log routing probabilities. */
    double *beta;                     /**< Module-bank routing probabilities. */
    double *decoder_logits;     /**< Module-local conditional logits precomputed once per admitted
                                   module. */
    double *log_heads;          /**< Dense module-bank maximum-output log probabilities. */
    double *head_probabilities; /**< Dense module-bank maximum-output probabilities. */
    double *log_outputs;        /**< Requested task's mixed log probabilities. */
    double *outputs;            /**< Requested task's mixed probabilities. */
    double *log_module_outputs; /**< Requested task's per-module internal mixtures. */
    double *hidden_gradient;    /**< Shared hidden backward scratch. */
    double *input_gradient;     /**< Ordered embedding backward scratch. */
};

/** @brief Validate every bound and unused contract field before allocation.
 * @param config Borrowed complete architecture.
 * @return OK for supported shapes and eligibility, ERROR otherwise. */
cgai_status cgai_gameplay_validate_config(const cgai_gameplay_config *config);
/** @brief Allocate zeroed weights and assign all parameter slices without initialization.
 * @param config Borrowed complete architecture.
 * @return Owned zeroed model, or NULL; used by bounded artifact decoders. */
cgai_gameplay_model *cgai_gameplay_allocate(const cgai_gameplay_config *config);
/** @brief Count complete session heap payload for a validated model.
 * @param model Borrowed complete model.
 * @return Complete owned session and storage bytes, or zero for NULL. */
size_t cgai_gameplay_session_bytes(const cgai_gameplay_model *model);
/** @brief Validate ordered categorical values including unused entries.
 * @param model Borrowed complete model.
 * @param state Borrowed complete observation.
 * @return OK on supported categories, ERROR otherwise. */
cgai_status cgai_gameplay_validate_state(const cgai_gameplay_model *model,
                                         const cgai_gameplay_state *state);
/** @brief Validate one independent task target without parameter mutation.
 * @param model Borrowed complete model.
 * @param example Borrowed complete target.
 * @return OK for valid state, task and task-local output, ERROR otherwise. */
cgai_status cgai_gameplay_validate_example(const cgai_gameplay_model *model,
                                           const cgai_gameplay_example *example);
/** @brief Build stable shared and hierarchical predictions without allocation.
 * @param session Exclusive scratch.
 * @param state Borrowed already validated observation.
 * @param task Already validated task head.
 * @param modules Nonempty eligible module bits.
 * @return OK on finite forward computation, ERROR otherwise. */
cgai_status cgai_gameplay_forward(cgai_gameplay_session *session, const cgai_gameplay_state *state,
                                  uint32_t task, uint64_t modules);
/** @brief Fill complete exact negative-log-likelihood derivatives after a successful forward.
 * @param session Exclusive scratch from the same example's forward.
 * @param example Borrowed target matching the last state and task.
 * @param gradient Writable parameter_count scalar array, fully overwritten.
 * @return OK on finite derivatives, ERROR otherwise. */
cgai_status cgai_gameplay_backward(cgai_gameplay_session *session,
                                   const cgai_gameplay_example *example, double *gradient);
/** @brief Stable normalized exponentials, optionally excluding module mask positions.
 * @param values Writable finite active logits, overwritten with log probabilities.
 * @param probabilities Writable normalized probabilities.
 * @param count Active width, bounded by64.
 * @param mask Eligible positions, zero means all positions active.
 * @return OK on finite active logits, ERROR otherwise. */
cgai_status cgai_gameplay_log_softmax(double *values, double *probabilities, size_t count,
                                      uint64_t mask);
/** @brief Add two logarithms representing nonnegative scalars stably.
 * @param left Logarithm, permitting negative infinity.
 * @param right Logarithm, permitting negative infinity.
 * @return Logarithm of the sum. */
double cgai_gameplay_log_add(double left, double right);
/** @brief Construct a bounded low-bit mask without shifting by64.
 * @param count Supported bit count, 1..64.
 * @return Mask containing the requested low bits. */
uint64_t cgai_gameplay_mask(size_t count);
#endif
