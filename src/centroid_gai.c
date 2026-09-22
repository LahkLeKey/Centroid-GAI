/** @file centroid_gai.c @brief Model lifecycle and configuration operations. */

#include "centroid_gai.h"

#include "internal/cgai_internal.h"
#include "internal/constants.h"
#include "internal/error.h"
#include "internal/vocabulary.h"

#include <stdlib.h>
#include <string.h>

/** Returns the default model configuration. */
cgai_config cgai_default_config(void) {
    const cgai_config config = {CGAI_DEFAULT_DIMENSIONS, CGAI_DEFAULT_CENTROID_COUNT,
                                CGAI_DEFAULT_CONTEXT_WINDOW, CGAI_DEFAULT_SEED};
    return config;
}

/** Validates model dimensions before any allocation occurs. */
static cgai_status validate_config(const cgai_config *config) {
    if (config->dimensions == 0U || config->centroid_count == 0U || config->context_window == 0U ||
        config->dimensions > CGAI_MAX_DIMENSIONS ||
        config->centroid_count > CGAI_MAX_CENTROID_COUNT ||
        config->context_window > CGAI_MAX_CONTEXT_WINDOW) {
        return cgai_fail("invalid model configuration");
    }
    if (config->dimensions > SIZE_MAX / config->centroid_count / sizeof(float)) {
        return cgai_fail("model dimensions are too large");
    }
    return CGAI_STATUS_OK;
}

/** Allocates the model object and its centroid-owned numeric arrays. */
static cgai_model *allocate_model(const cgai_config *config) {
    cgai_model *model = (cgai_model *)calloc(1U, sizeof(*model));
    if (model == NULL) {
        (void)cgai_fail("could not allocate model");
        return NULL;
    }
    model->config = *config;
    model->centroids = (float *)calloc(config->centroid_count * config->dimensions, sizeof(float));
    model->cluster_sizes = (uint64_t *)calloc(config->centroid_count, sizeof(uint64_t));
    return model;
}

/** Installs the reserved vocabulary entries required by every model. */
static cgai_status install_special_tokens(cgai_model *model) {
    const char *tokens[] = {CGAI_TOKEN_BOS_TEXT, CGAI_TOKEN_EOS_TEXT, CGAI_TOKEN_UNKNOWN_TEXT};
    for (size_t i = 0; i < CGAI_SPECIAL_TOKEN_COUNT; ++i) {
        if (!cgai_token_id_is_valid(cgai_vocabulary_add(model, tokens[i]))) {
            return CGAI_STATUS_ERROR;
        }
    }
    return CGAI_STATUS_OK;
}

/** Validates configuration, allocates model storage, and installs special tokens. */
cgai_model *cgai_model_create(const cgai_config *requested) {
    cgai_error_clear();
    const cgai_config config = requested != NULL ? *requested : cgai_default_config();
    if (validate_config(&config) != CGAI_STATUS_OK) {
        return NULL;
    }
    cgai_model *model = allocate_model(&config);
    if (model == NULL || model->centroids == NULL || model->cluster_sizes == NULL ||
        install_special_tokens(model) != CGAI_STATUS_OK) {
        cgai_model_destroy(model);
        if (strcmp(cgai_last_error(), "no error") == 0) {
            (void)cgai_fail("could not allocate model storage");
        }
        return NULL;
    }
    return model;
}

/** Releases all model-owned vocabulary, centroid, and count storage. */
void cgai_model_destroy(cgai_model *model) {
    if (model == NULL) {
        return;
    }
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        free(model->vocabulary[i]);
    }
    free(model->vocabulary);
    free(model->centroids);
    free(model->cluster_sizes);
    free(model->token_counts);
    free(model);
}

/** Returns the current vocabulary cardinality without exposing internal storage. */
size_t cgai_model_vocabulary_size(const cgai_model *model) {
    return model != NULL ? model->vocabulary_size : 0U;
}

/** Returns the number of learned transitions without exposing internal storage. */
size_t cgai_model_examples_seen(const cgai_model *model) {
    return model != NULL ? model->examples_seen : 0U;
}
