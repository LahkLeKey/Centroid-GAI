/**
 * @file centroid_gai.h
 * @brief Public API for training and running centroid-based text models.
 *
 * The API owns no global model state. A caller creates or loads a model, uses
 * it, and releases it with cgai_model_destroy(). Unless stated otherwise,
 * strings are UTF-8 and remain owned by the caller.
 *
 * Models do not internally synchronize mutation. Training and destruction
 * require exclusive access. Generation and saving may run concurrently only
 * while no caller mutates the same model.
 */

#ifndef CENTROID_GAI_H
#define CENTROID_GAI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @name Library version */
/** @{ */
#define CGAI_VERSION_MAJOR 0 /**< SemVer major version. */
#define CGAI_VERSION_MINOR 2 /**< SemVer minor version. */
#define CGAI_VERSION_PATCH 0 /**< SemVer patch version. */
/** @} */

/**
 * @brief Opaque centroid model owned by the caller.
 *
 * The representation is private. Callers must use the functions in this
 * header rather than relying on allocation layout or internal fields.
 */
typedef struct cgai_model cgai_model;

/** Result of an operation that can fail. */
typedef enum cgai_status {
    CGAI_STATUS_ERROR = 0, /**< The operation failed; inspect cgai_last_error(). */
    CGAI_STATUS_OK = 1     /**< The operation completed successfully. */
} cgai_status;

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
 * @brief Returns a small configuration suitable for experiments.
 * @return A complete configuration that can be passed to cgai_model_create().
 */
cgai_config cgai_default_config(void);

/**
 * @brief Creates an empty model.
 * @param config Configuration to copy, or NULL to use cgai_default_config().
 * @return A caller-owned model, or NULL on failure.
 * @error cgai_last_error() describes invalid limits and allocation failures.
 * @ownership The returned model must be released with cgai_model_destroy().
 */
cgai_model *cgai_model_create(const cgai_config *config);

/**
 * @brief Releases a model and all of its storage.
 * @param model Model to release; NULL is accepted.
 */
void cgai_model_destroy(cgai_model *model);

/**
 * @brief Adds transition examples parsed from UTF-8 text.
 * @param model Model to update.
 * @param text NUL-terminated training text.
 * @return CGAI_STATUS_OK on success, otherwise CGAI_STATUS_ERROR.
 * @note Training mutates the model and may expand its vocabulary.
 * @pre @p model and @p text are non-NULL.
 * @post Successful training increments cgai_model_examples_seen().
 * @error Empty or whitespace-only text is rejected.
 */
cgai_status cgai_model_train_text(cgai_model *model, const char *text);

/**
 * @brief Generates a text continuation from a prompt.
 * @param model Trained model to query.
 * @param prompt NUL-terminated prompt; an empty prompt starts at beginning-of-sequence.
 * @param max_tokens Maximum number of tokens to append.
 * @param temperature Sampling temperature. Zero performs deterministic greedy selection.
 * @param seed Sampling seed. Zero selects the model's configured seed.
 * @param output Destination for the NUL-terminated continuation only.
 * @param output_size Capacity of @p output in bytes, including its terminator.
 * @return CGAI_STATUS_OK on success, otherwise CGAI_STATUS_ERROR.
 * @pre @p model has been trained and @p output_size is nonzero.
 * @post On success, @p output always contains a NUL-terminated string.
 * @error A too-small output buffer leaves the model unchanged and reports an error.
 */
cgai_status cgai_model_generate(const cgai_model *model, const char *prompt, size_t max_tokens,
                                double temperature, uint64_t seed, char *output,
                                size_t output_size);

/**
 * @brief Saves a model to a binary file.
 * @param model Model to serialize.
 * @param path Destination filesystem path.
 * @return CGAI_STATUS_OK on success, otherwise CGAI_STATUS_ERROR.
 * @warning The current format is intended only for trusted files and matching architectures.
 * @note Saving does not transfer ownership of @p model or @p path.
 */
cgai_status cgai_model_save(const cgai_model *model, const char *path);

/**
 * @brief Loads a model from a binary file.
 * @param path Source filesystem path.
 * @return A caller-owned model, or NULL on failure.
 * @warning Only load trusted files created by a compatible build.
 * @error Truncated, malformed, or incompatible files are rejected.
 */
cgai_model *cgai_model_load(const char *path);

/** @return The number of tokens in @p model, or zero when @p model is NULL. */
size_t cgai_model_vocabulary_size(const cgai_model *model);

/** @return The number of training transitions observed, or zero for NULL. */
size_t cgai_model_examples_seen(const cgai_model *model);

/**
 * @brief Returns the calling thread's most recent library error description.
 * @return A library-owned string valid until the next API call on this thread.
 */
const char *cgai_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
