/**
 * @file model_contract.h
 * @brief Private retained count-model creation, inference and artifact contracts.
 *
 * The API owns no global model state. A caller creates or loads a model, uses
 * it, and releases it with cgai_model_destroy(). Unless stated otherwise,
 * strings are UTF-8 and remain owned by the caller.
 *
 * Models do not internally synchronize mutation. Mutation and destruction
 * require exclusive access. Generation and saving may run concurrently only
 * while no caller mutates the same model.
 */

#ifndef CGAI_MODEL_CONTRACT_H
#define CGAI_MODEL_CONTRACT_H

#include "internal/core_contract.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Opaque centroid model owned by the caller.
 *
 * The complete numerical representation lives in internal/cgai_internal.h.
 */
typedef struct cgai_model cgai_model;

/**
 * @brief Parameters controlling model capacity and context representation.
 *
 * All fields are copied by cgai_model_create(). Changing a caller-owned
 * configuration after creation does not change the model.
 */
typedef struct cgai_config {
    size_t dimensions;     /**< Number of components in each token embedding. */
    size_t centroid_count; /**< Maximum number of learned context centroids. */
    size_t context_window; /**< Maximum recent tokens used for context. */
    uint64_t seed;         /**< Seed used for deterministic token embeddings. */
} cgai_config;

/**
 * @brief Return a complete configuration with the count-model default capacities.
 *
 * The configuration is a small value structure, not a heap allocation. Returning it copies the
 * fields to the caller, who may adjust them before creating a model. Defaults are defined in
 * internal/constants.h.
 *
 * @return A configuration value requiring no cleanup.
 */
cgai_config cgai_default_config(void);

/**
 * @brief Create an empty model with owned storage and reserved vocabulary.
 *
 * Construction copies the configuration, validates it, then allocates arrays and installs control
 * tokens. Any partial allocation is destroyed on failure. A successful model has no learned
 * observations yet; generation requires populated counts from a retained artifact or numerical
 * adapter. The caller must eventually call cgai_model_destroy().
 *
 * @param requested Borrowed configuration to copy, or NULL to choose defaults.
 * @return New caller-owned model, or NULL with a thread-local diagnostic on failure.
 */
cgai_model *cgai_model_create(const cgai_config *requested);

/**
 * @brief Release every allocation owned by a model.
 *
 * The model owns individual vocabulary strings, the pointer array containing those strings, and
 * three numeric arrays. They are separate allocations and must each be released. NULL is accepted
 * for cleanup after failed construction. A non-NULL pointer becomes invalid after this call; the
 * caller must not read, destroy, or generate from it again.
 *
 * @param model Owned model pointer to release, or NULL.
 */
void cgai_model_destroy(cgai_model *model);

/**
 * Create an independently owned union of 1..32 trained models in source order.
 * Sources are borrowed and never mutated. Dimensions, context window, and seed
 * must match. Zero target preserves every active centroid; a positive target
 * (at most the active-row sum) performs approximate, order-dependent weighted
 * nearest-centroid consolidation. Vocabulary and target counts are unioned by
 * spelling. Counts are additive; overlapping training data is not deduplicated.
 * Resource limits and counter overflow fail with NULL and cgai_last_error().
 * Destroy a successful result with cgai_model_destroy().
 * @param sources Borrowed ordered source models.
 * @param count Number of sources, from 1 through 32.
 * @param target_centroids Zero preserves all active rows; otherwise compacted capacity.
 * @return Independently owned model, or NULL with a diagnostic.
 */
cgai_model *cgai_model_merge(const cgai_model *const *sources, size_t count,
                             size_t target_centroids);

/**
 * @brief Generate a continuation into caller-owned storage without changing the model.
 *
 * Only the continuation is written; callers display the original prompt separately if desired.
 * The operation borrows prompt/model/output, allocates a temporary workspace, and frees it on every
 * exit after preparation. Zero tokens yields an empty string for a valid trained model. Callers may
 * read one model concurrently only while no thread mutates or destroys it.
 *
 * @param model Borrowed trained model; NULL or an untrained model is rejected.
 * @param prompt Borrowed NUL-terminated prompt; empty text uses BOS context.
 * @param max_tokens Upper limit on emitted tokens and on reserved generated-history slots.
 * @param temperature Finite nonnegative temperature; zero uses greedy selection.
 * @param seed Sampling seed; zero selects the model's configured seed.
 * @param output Writable caller-owned buffer for continuation bytes and NUL.
 * @param output_size Total capacity of output in bytes.
 * @return CGAI_STATUS_OK on completion, otherwise CGAI_STATUS_ERROR with cgai_last_error()
 * describing failure.
 */
cgai_status cgai_model_generate(const cgai_model *model, const char *prompt, size_t max_tokens,
                                double temperature, uint64_t seed, char *output,
                                size_t output_size);

/**
 * @brief Encode a model into temporary bytes and write those bytes to a file.
 *
 * Serialization allocates an intermediate byte buffer because the file helper accepts a complete
 * artifact. That buffer is released after writing regardless of success. The destination is opened
 * for replacement; writing is not an atomic rename transaction, so a failure may leave a truncated
 * file. The model and path remain caller-owned.
 *
 * @param model Non-NULL stable model to serialize.
 * @param path Non-NULL borrowed NUL-terminated destination path.
 * @return CGAI_STATUS_OK if encoding, writing, and file close succeed; otherwise CGAI_STATUS_ERROR.
 */
cgai_status cgai_model_save(const cgai_model *model, const char *path);

/**
 * @brief Read a complete trusted artifact and return an independently owned model.
 *
 * The file helper appends a convenience NUL byte, but the decoder receives only the actual file
 * length. Decoding copies spellings and numeric arrays, so the temporary file bytes can be freed
 * before return. The format assumes compatible native numeric representations and trusted input.
 *
 * @param path Borrowed NUL-terminated source path; NULL is rejected.
 * @return New caller-owned model to destroy with cgai_model_destroy(), or NULL with a diagnostic.
 */
cgai_model *cgai_model_load(const char *path);

/**
 * @brief Read the number of assigned vocabulary identifiers.
 *
 * This includes BOS, EOS, and UNKNOWN as well as ordinary learned tokens. The count is copied
 * from the model and exposes no internal array. Keep a non-NULL model alive and exclude concurrent
 * mutation while reading; returning zero for NULL is a convenience, not a validation of other
 * pointers.
 *
 * @param model Borrowed model pointer, or NULL.
 * @return Vocabulary entry count, or zero for NULL.
 */
size_t cgai_model_vocabulary_size(const cgai_model *model);

/**
 * @brief Read the number of learned context-to-token transitions.
 *
 * Retained artifacts count every observed target and its final EOS target. This accessor returns a
 * value copy and does not lock the model against concurrent mutation.
 *
 * @param model Borrowed live model pointer, or NULL.
 * @return Learned transition count, or zero for NULL.
 */
size_t cgai_model_examples_seen(const cgai_model *model);

#endif
