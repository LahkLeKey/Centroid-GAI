/** @file neural_model.c @brief Neural model ownership, bounded shapes and seeded initialization. */
#include "internal/error.h"
#include "internal/model_random.h"
#include "internal/neural_internal.h"
#include <math.h>
#include <stdlib.h>

/** @brief Return a small reproducible network configuration.
 * @return Configuration value needing no cleanup. */
cgai_neural_config cgai_neural_default_config(void) {
    /* Step 1: Keep dimensions and seed explicit and reproducible. */
    const cgai_neural_config config = {8U, 16U, 16U, 4U, UINT64_C(42), 1.0};
    return config;
}

/** @brief Return default Adam settings.
 * @return Settings value needing no cleanup. */
cgai_neural_training cgai_neural_default_training(void) {
    /* Step 1: Choose conservative steps for the small default network. */
    const cgai_neural_training training = {20U, 0.01, 5.0};
    return training;
}

/** @brief Bound every dimension before computing allocation sizes.
 * @param config Borrowed non-NULL configuration.
 * @return OK on supported dimensions and routing scale, ERROR otherwise. */
cgai_status cgai_neural_validate_config(const cgai_neural_config *config) {
    /* Step 1: Limit products to values safely representable even on 32-bit hosts. */
    if (config->embedding_dimensions == 0U || config->embedding_dimensions > 64U ||
        config->hidden_dimensions == 0U || config->hidden_dimensions > 128U ||
        config->centroid_count == 0U || config->centroid_count > 128U ||
        config->context_window == 0U || config->context_window > CGAI_NEURAL_MAX_CONTEXT) {
        return cgai_fail("unsupported neural dimensions");
    }
    /* Step 2: Reject nonfinite or extreme distance scaling. */
    if (!isfinite(config->routing_temperature) || config->routing_temperature < 0.01 ||
        config->routing_temperature > 100.0) {
        return cgai_fail("neural routing temperature must be between 0.01 and 100");
    }
    return CGAI_STATUS_OK;
}

/** @brief Allocate the single parameter block and assign borrowed slices.
 * @param model Mutable shell with valid config and initialized vocabulary.
 * @return OK on allocation, ERROR otherwise; caller owns cleanup. */
cgai_status cgai_neural_allocate_parameters(cgai_neural_model *model) {
    /* Step 1: Compute bounded table sizes in scalar doubles. */
    const size_t embeddings = model->vocabulary_size * model->config.embedding_dimensions;
    const size_t encoder = model->config.hidden_dimensions * model->config.context_window *
                           model->config.embedding_dimensions;
    const size_t centroids = model->config.centroid_count * model->config.hidden_dimensions;
    model->parameter_count = embeddings + encoder + model->config.hidden_dimensions + centroids +
                             model->config.centroid_count * model->vocabulary_size;
    if (model->parameter_count > CGAI_NEURAL_MAX_PARAMETERS) {
        return cgai_fail("neural parameter limit exceeded");
    }
    /* Step 2: Establish one owner, with slices in serialization/gradient order. */
    model->parameters = calloc(model->parameter_count, sizeof(double));
    if (model->parameters == NULL)
        return cgai_fail("could not allocate neural parameters");
    model->output_size = model->vocabulary_size;
    model->embeddings = model->parameters;
    model->encoder = model->embeddings + embeddings;
    model->bias = model->encoder + encoder;
    model->centroids = model->bias + model->config.hidden_dimensions;
    model->logits = model->centroids + centroids;
    return CGAI_STATUS_OK;
}

/** @brief Initialize a contiguous parameter slice with symmetric uniform noise.
 * @param values Borrowed mutable slice.
 * @param count Scalar length.
 * @param scale Positive uniform half-width.
 * @param state Borrowed seeded random state, advanced on each scalar. */
static void initialize_slice(double *values, size_t count, double scale, uint64_t *state) {
    /* Step 1: Use the upper 53 random bits to construct reproducible doubles. */
    for (size_t i = 0U; i < count; ++i) {
        const double unit = (double)(cgai_random_next(state) >> 11U) * 0x1.0p-53;
        values[i] = (2.0 * unit - 1.0) * scale;
    }
}

/** @brief Break centroid and expert symmetry while keeping encoder activations moderate.
 * @param model Borrowed mutable fully allocated model. */
void cgai_neural_initialize_parameters(cgai_neural_model *model) {
    /* Step 1: Keep all randomness local to this initialization. */
    uint64_t state = model->config.seed;
    const size_t input = model->config.context_window * model->config.embedding_dimensions;
    initialize_slice(model->embeddings, model->vocabulary_size * model->config.embedding_dimensions,
                     0.5, &state);
    /* Step 2: Leave biases zero and randomize both routing and expert parameters. */
    initialize_slice(model->encoder, model->config.hidden_dimensions * input,
                     sqrt(3.0 / (double)input), &state);
    initialize_slice(model->centroids,
                     model->config.centroid_count * model->config.hidden_dimensions, 0.5, &state);
    initialize_slice(model->logits, model->config.centroid_count * model->vocabulary_size, 0.1,
                     &state);
}

/** @brief Initialize weights and a frozen vocabulary from training text only.
 * @param requested Borrowed shape, or NULL for defaults.
 * @param vocabulary_text Borrowed nonempty training text; held-out text must not be included.
 * @return Owned model, or NULL with a diagnostic. Limits: 8192 vocabulary entries,
 * two million scalar parameters, and 1 MiB per token spelling. */
cgai_neural_model *cgai_neural_create(const cgai_neural_config *requested,
                                      const char *vocabulary_text) {
    /* Step 1: Check public inputs and shape before creating ownership. */
    cgai_error_clear();
    const cgai_neural_config config = requested ? *requested : cgai_neural_default_config();
    if (vocabulary_text == NULL || !cgai_neural_validate_config(&config)) {
        cgai_fail("invalid neural configuration or vocabulary text");
        return NULL;
    }
    cgai_neural_model *model = calloc(1U, sizeof(*model));
    if (model == NULL) {
        cgai_fail("could not allocate neural model");
        return NULL;
    }
    /* Step 2: Build vocabulary before parameter sizes are calculated. */
    model->config = config;
    if (!cgai_neural_build_vocabulary(model, vocabulary_text) ||
        !cgai_neural_allocate_parameters(model)) {
        cgai_neural_destroy(model);
        return NULL;
    }
    cgai_neural_initialize_parameters(model);
    return model;
}

/** @brief Release a model and every owned allocation.
 * @param model Owned handle, or NULL; invalid after this call. */
void cgai_neural_destroy(cgai_neural_model *model) {
    /* Step 1: Accept the empty result of a failed construction. */
    if (model == NULL)
        return;
    /* Step 2: Destroy independent strings before their pointer array and parameters. */
    if (model->vocabulary != NULL) {
        for (size_t i = 0U; i < model->vocabulary_size; ++i)
            free(model->vocabulary[i]);
    }
    free(model->vocabulary);
    cgai_neural_reset_training(model);
    free(model->parameters);
    free(model);
}

/** @brief Report the frozen vocabulary size, including BOS, EOS and UNK.
 * @param model Borrowed handle, or NULL.
 * @return Vocabulary size, or zero for NULL. */
size_t cgai_neural_vocabulary_size(const cgai_neural_model *model) {
    /* Step 1: Return a value, retaining all ownership in the model. */
    return model ? model->vocabulary_size : 0U;
}

/** @brief Clear continuation moments and counters before fresh optimization.
 * @param model Borrowed mutable initialized model; weights remain unchanged. */
void cgai_neural_reset_training(cgai_neural_model *model) {
    /* Step 1: Release model-owned optimizer state without changing learned parameters. */
    free(model->adam_first);
    free(model->adam_second);
    model->adam_first = NULL;
    model->adam_second = NULL;
    model->training_step = 0U;
    model->training_epochs = 0U;
    model->training_shuffle = 0U;
}

/** @brief Report accumulated continuation passes and target updates without mutation.
 * @param model Borrowed handle, or NULL.
 * @return Current counters, or both zero for NULL or a fresh model. */
cgai_neural_progress cgai_neural_get_progress(const cgai_neural_model *model) {
    /* Step 1: Return counters by value while retaining all ownership in the model. */
    const cgai_neural_progress progress = {model ? model->training_epochs : 0U,
                                           model ? model->training_step : 0U};
    return progress;
}
