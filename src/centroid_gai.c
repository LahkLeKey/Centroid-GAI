/** @file centroid_gai.c @brief Model lifecycle and configuration operations. */

#include "centroid_gai.h"

#include "internal/cgai_internal.h"
#include "internal/constants.h"
#include "internal/error.h"
#include "internal/vocabulary.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Return a complete configuration with the library's default capacities.
 *
 * The configuration is a small value structure, not a heap allocation. Returning it copies the
 * fields to the caller, who may adjust them before creating a model. The named constants keep
 * defaults in one place for the CLI, core API, and ABI adapter.
 *
 * @return A configuration value requiring no cleanup.
 */
cgai_config cgai_default_config(void) {
    /* Step 1: Collect dimensions, centroid capacity, context length, and deterministic seed. */
    const cgai_config config = {CGAI_DEFAULT_DIMENSIONS, CGAI_DEFAULT_CENTROID_COUNT,
                                CGAI_DEFAULT_CONTEXT_WINDOW, CGAI_DEFAULT_SEED};
    /* Step 2: Return a value copy; no pointer to this local variable escapes. */
    return config;
}

/**
 * @brief Check that a requested model shape fits supported limits and allocation arithmetic.
 *
 * Dimensions are components per vector, centroid_count is the number of vector rows, and
 * context_window is the number of recent tokens to consider. All must be positive. The division
 * check avoids multiplying first, because an overflowing allocation size could reserve too few bytes.
 *
 * @param config Non-NULL borrowed configuration to inspect.
 * @return CGAI_STATUS_OK for a usable shape, otherwise CGAI_STATUS_ERROR with a thread-local diagnostic.
 */
static cgai_status validate_config(const cgai_config *config) {
    /* Step 1: Reject zero capacities and values above the implementation's explicit limits. */
    if (config->dimensions == 0U || config->centroid_count == 0U || config->context_window == 0U ||
        config->dimensions > CGAI_MAX_DIMENSIONS ||
        config->centroid_count > CGAI_MAX_CENTROID_COUNT ||
        config->context_window > CGAI_MAX_CONTEXT_WINDOW) {
        return cgai_fail("invalid model configuration");
    }
    /* Step 2: Check the centroid-array byte product without overflowing an intermediate multiplication. */
    if (config->dimensions > SIZE_MAX / config->centroid_count / sizeof(float)) {
        return cgai_fail("model dimensions are too large");
    }
    /* Step 3: Confirm that allocation may proceed; no model has been created yet. */
    return CGAI_STATUS_OK;
}

/**
 * @brief Allocate a zero-initialized model shell and its centroid arrays.
 *
 * calloc initializes every byte to zero, so unused pointers and counters start empty. The config
 * is copied rather than retained by pointer. A non-NULL result may still have NULL numeric arrays:
 * the higher-level constructor checks those allocations and destroys partial state on failure.
 *
 * @param config Non-NULL configuration that already passed validate_config().
 * @return Owned model shell, possibly partially allocated, or NULL if the shell allocation fails.
 */
static cgai_model *allocate_model(const cgai_config *config) {
    /* Step 1: Allocate the model structure and initialize its ownership fields to zero. */
    cgai_model *model = (cgai_model *)calloc(1U, sizeof(*model));
    if (model == NULL) {
        (void)cgai_fail("could not allocate model");
        return NULL;
    }
    /* Step 2: Copy configuration so its caller-owned storage need not outlive this call. */
    model->config = *config;
    /* Step 3: Allocate a flat centroid matrix and per-centroid observation counters. */
    model->centroids = (float *)calloc(config->centroid_count * config->dimensions, sizeof(float));
    model->cluster_sizes = (uint64_t *)calloc(config->centroid_count, sizeof(uint64_t));
    /* Step 4: Return partial ownership to the constructor, which validates every allocation. */
    return model;
}

/**
 * @brief Insert the three reserved control tokens in their required identifier order.
 *
 * Vocabulary identifiers are insertion indices, so BOS, EOS, and UNKNOWN must be inserted in this
 * order before ordinary words. BOS supplies missing left context, EOS teaches when generation
 * should stop, and UNKNOWN represents prompt words not present in the learned vocabulary.
 *
 * @param model Non-NULL model with empty vocabulary; mutated by each successful insertion.
 * @return CGAI_STATUS_OK after all reserved entries exist, otherwise CGAI_STATUS_ERROR without rollback.
 */
static cgai_status install_special_tokens(cgai_model *model) {
    /* Step 1: List reserved spellings in the same order as their numeric identifiers. */
    const char *tokens[] = {CGAI_TOKEN_BOS_TEXT, CGAI_TOKEN_EOS_TEXT, CGAI_TOKEN_UNKNOWN_TEXT};
    /* Step 2: Insert each spelling and let vocabulary insertion allocate matching count columns. */
    for (size_t i = 0; i < CGAI_SPECIAL_TOKEN_COUNT; ++i) {
        if (!cgai_token_id_is_valid(cgai_vocabulary_add(model, tokens[i]))) {
            return CGAI_STATUS_ERROR;
        }
    }
    /* Step 3: Report completion only after every required control token has an identifier. */
    return CGAI_STATUS_OK;
}

/**
 * @brief Create an empty model with owned storage and reserved vocabulary.
 *
 * Construction copies the configuration, validates it, then allocates arrays and installs control
 * tokens. Any partial allocation is destroyed on failure. A successful model has no learned
 * examples yet; train it before generation. The caller must eventually call cgai_model_destroy().
 *
 * @param requested Borrowed configuration to copy, or NULL to choose defaults.
 * @return New caller-owned model, or NULL with a thread-local diagnostic on failure.
 */
cgai_model *cgai_model_create(const cgai_config *requested) {
    /* Step 1: Begin with a fresh diagnostic and choose a value copy of the requested/default configuration. */
    cgai_error_clear();
    const cgai_config config = requested != NULL ? *requested : cgai_default_config();
    /* Step 2: Reject unsupported shapes before allocating model storage. */
    if (validate_config(&config) != CGAI_STATUS_OK) {
        return NULL;
    }
    /* Step 3: Allocate the shell and arrays, then install the required vocabulary entries. */
    cgai_model *model = allocate_model(&config);
    if (model == NULL || model->centroids == NULL || model->cluster_sizes == NULL ||
        install_special_tokens(model) != CGAI_STATUS_OK) {
        /* Step 4: On any construction failure, release partial storage and retain a useful diagnostic. */
        cgai_model_destroy(model);
        if (strcmp(cgai_last_error(), "no error") == 0) {
            (void)cgai_fail("could not allocate model storage");
        }
        return NULL;
    }
    /* Step 5: Transfer ownership of the completely initialized, untrained model to the caller. */
    return model;
}

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
void cgai_model_destroy(cgai_model *model) {
    /* Step 1: Allow cleanup code to pass an empty handle. */
    if (model == NULL) {
        return;
    }
    /* Step 2: Free each vocabulary spelling before freeing the array that stores its address. */
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        free(model->vocabulary[i]);
    }
    /* Step 3: Release the remaining model-owned arrays. */
    free(model->vocabulary);
    free(model->centroids);
    free(model->cluster_sizes);
    free(model->token_counts);
    /* Step 4: Free the outer structure last, after its fields are no longer needed. */
    free(model);
}

/**
 * @brief Read the number of assigned vocabulary identifiers.
 *
 * This includes BOS, EOS, and UNKNOWN as well as ordinary learned tokens. The count is copied
 * from the model and exposes no internal array. Keep a non-NULL model alive and exclude concurrent
 * mutation while reading; returning zero for NULL is a convenience, not a validation of other pointers.
 *
 * @param model Borrowed model pointer, or NULL.
 * @return Vocabulary entry count, or zero for NULL.
 */
size_t cgai_model_vocabulary_size(const cgai_model *model) {
    /* Step 1: Read the stored count only when a model pointer was supplied. */
    return model != NULL ? model->vocabulary_size : 0U;
}

/**
 * @brief Read the number of learned context-to-token transitions.
 *
 * Training counts every observed target and its final EOS target, rather than counting calls to
 * the training API. Repeated training adds to this total. This accessor returns a value copy and
 * does not lock the model against concurrent mutation.
 *
 * @param model Borrowed live model pointer, or NULL.
 * @return Learned transition count, or zero for NULL.
 */
size_t cgai_model_examples_seen(const cgai_model *model) {
    /* Step 1: Return the counter without dereferencing a NULL pointer. */
    return model != NULL ? model->examples_seen : 0U;
}
