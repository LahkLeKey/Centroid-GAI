/** @file gameplay_model.c @brief Bounded aligned hierarchical centroid model ownership. */
#include "gameplay_internal.h"
#include "internal/error.h"
#include "internal/model_random.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Construct a safe low-bit domain mask.
 * @param count Supported width.
 * @return All low requested bits. */
uint64_t cgai_gameplay_mask(size_t count) {
    /* Step 1: Avoid the undefined full-width shift. */
    return count == 64U ? UINT64_MAX : (UINT64_C(1) << count) - UINT64_C(1);
}

/** @brief Validate categorical shape and zeroed trailing entries.
 * @param config Borrowed bounded dimensions.
 * @return OK on valid feature entries, ERROR otherwise. */
static cgai_status validate_features(const cgai_gameplay_config *config) {
    /* Step 1: Bound each categorical table independently. */
    for (size_t i = 0U; i < CGAI_GAMEPLAY_MAX_FEATURES; ++i) {
        const uint32_t count = config->cardinalities[i];
        if ((i < config->feature_count && (count == 0U || count > 64U)) ||
            (i >= config->feature_count && count != 0U))
            return cgai_fail("invalid gameplay categorical shape");
    }
    return CGAI_STATUS_OK;
}

/** @brief Validate task-local head widths and routing eligibility.
 * @param config Borrowed bounded dimensions.
 * @return OK on valid task entries, ERROR otherwise. */
static cgai_status validate_tasks(const cgai_gameplay_config *config) {
    /* Step 1: Every active head has an output domain and nonempty compatible specialists. */
    const uint64_t modules = cgai_gameplay_mask(config->module_count);
    const uint64_t features = cgai_gameplay_mask(config->feature_count);
    for (size_t i = 0U; i < CGAI_GAMEPLAY_MAX_TASKS; ++i) {
        if (i < config->task_count) {
            if (config->output_counts[i] < 2U || config->output_counts[i] > 64U ||
                config->task_modules[i] == 0U || (config->task_modules[i] & ~modules) != 0U ||
                config->task_features[i] == 0U || (config->task_features[i] & ~features) != 0U)
                return cgai_fail("invalid gameplay task head");
        } else if (config->output_counts[i] != 0U || config->task_modules[i] != 0U ||
                   config->task_features[i] != 0U)
            return cgai_fail("unused gameplay task entries must be zero");
    }
    return CGAI_STATUS_OK;
}

/** @brief Validate bounded dimensions before computing any allocation sizes.
 * @param config Borrowed requested shape.
 * @return OK for supported architecture, ERROR otherwise. */
cgai_status cgai_gameplay_validate_config(const cgai_gameplay_config *config) {
    /* Step 1: Reject dimensions which exceed complete static scratch bounds. */
    if (config == NULL || config->feature_count == 0U || config->feature_count > 16U ||
        config->embedding_dimensions == 0U || config->embedding_dimensions > 64U ||
        config->hidden_dimensions == 0U || config->hidden_dimensions > 64U ||
        config->module_count == 0U || config->module_count > 8U ||
        config->centroids_per_module == 0U || config->centroids_per_module > 32U ||
        config->task_count == 0U || config->task_count > 8U)
        return cgai_fail("invalid gameplay architecture dimensions");
    /* Step 2: Require a usable finite routing distance scale. */
    if (!isfinite(config->routing_temperature) || config->routing_temperature < 0.01 ||
        config->routing_temperature > 100.0)
        return cgai_fail("invalid gameplay routing temperature");
    return validate_features(config) && validate_tasks(config);
}

/** @brief Count each fixed slice and assign category/head prefix offsets.
 * @param model Borrowed zeroed shell with validated configuration. */
static void count_parameters(cgai_gameplay_model *model) {
    /* Step 1: Account for ordered categorical inputs and the shared representation. */
    const cgai_gameplay_config *config = &model->config;
    for (size_t i = 0U; i < config->feature_count; ++i) {
        model->category_offsets[i] = model->category_count;
        model->category_count += config->cardinalities[i];
    }
    model->input_count = config->feature_count * config->embedding_dimensions;
    model->parameter_count =
        model->category_count * config->embedding_dimensions +
        config->hidden_dimensions * model->input_count +
        config->hidden_dimensions *
            (1U + config->module_count + config->module_count * config->centroids_per_module);
    /* Step 2: Each task owns a separate compatible categorical prediction domain. */
    for (size_t task = 0U; task < config->task_count; ++task) {
        model->decoder_offsets[task] = model->parameter_count;
        model->parameter_count +=
            config->module_count * config->hidden_dimensions * config->output_counts[task];
        model->head_offsets[task] = model->parameter_count;
        model->parameter_count +=
            config->module_count * config->centroids_per_module * config->output_counts[task];
        if (model->maximum_outputs < config->output_counts[task])
            model->maximum_outputs = config->output_counts[task];
    }
}

/** @brief Bind named borrowed parameter pointers to the documented contiguous layout.
 * @param model Borrowed shell with allocated complete parameter block. */
static void bind_parameters(cgai_gameplay_model *model) {
    /* Step 1: Bind shared and hierarchical slices in serialization order. */
    const cgai_gameplay_config *config = &model->config;
    model->embeddings = model->parameters;
    model->encoder = model->embeddings + model->category_count * config->embedding_dimensions;
    model->bias = model->encoder + config->hidden_dimensions * model->input_count;
    model->outer = model->bias + config->hidden_dimensions;
    model->inner = model->outer + config->module_count * config->hidden_dimensions;
    /* Step 2: Task-local output heads retain their own independent widths. */
    for (size_t task = 0U; task < config->task_count; ++task) {
        model->decoders[task] = model->parameters + model->decoder_offsets[task];
        model->heads[task] = model->parameters + model->head_offsets[task];
    }
}

/** @brief Allocate zeroed bounded parameters without random initialization.
 * @param config Borrowed complete shape.
 * @return Owned model or NULL; incomplete allocations are released. */
cgai_gameplay_model *cgai_gameplay_allocate(const cgai_gameplay_config *config) {
    /* Step 1: Validate before allocating or multiplying requested dimensions. */
    if (!cgai_gameplay_validate_config(config))
        return NULL;
    cgai_gameplay_model *model = calloc(1U, sizeof(*model));
    if (model == NULL) {
        (void)cgai_fail("could not allocate gameplay model owner");
        return NULL;
    }
    model->config = *config;
    count_parameters(model);
    /* Step 2: Acquire the complete weight block before binding slices. */
    model->parameters = calloc(model->parameter_count, sizeof(*model->parameters));
    if (model->parameters == NULL || model->parameter_count > CGAI_GAMEPLAY_MAX_PARAMETERS) {
        cgai_gameplay_destroy(model);
        (void)cgai_fail("could not allocate gameplay weights");
        return NULL;
    }
    bind_parameters(model);
    return model;
}

/** @brief Produce a deterministic moderate symmetric scalar.
 * @param state Borrowed initialization random stream.
 * @param scale Positive amplitude.
 * @return Random scalar in the requested symmetric interval. */
static double random_scalar(uint64_t *state, double scale) {
    /* Step 1: Use a defined exact53-bit fraction without a process-global random generator. */
    const double fraction = (double)(cgai_random_next(state) >> 11U) * 0x1p-53;
    return (2.0 * fraction - 1.0) * scale;
}

/** @brief Initialize aligned categorical embeddings, retaining distinct category coordinates.
 * @param model Borrowed allocated model.
 * @param state Borrowed deterministic initialization stream. */
static void initialize_embeddings(cgai_gameplay_model *model, uint64_t *state) {
    /* Step 1: Orthogonal category directions are available when the shape permits them. */
    const size_t dimensions = model->config.embedding_dimensions;
    for (size_t category = 0U; category < model->category_count; ++category)
        for (size_t dimension = 0U; dimension < dimensions; ++dimension) {
            const size_t index = category * dimensions + dimension;
            /* Keep the canonical random stream identical when exact category coordinates fit. */
            model->embeddings[index] = random_scalar(state, 0.04);
            if (dimensions >= model->category_count)
                model->embeddings[index] = category == dimension ? 1.0 : 0.0;
        }
}

/** @brief Seed centroid experts with distinct trainable task-local label preferences.
 * @param model Borrowed allocated model.
 * @param state Borrowed deterministic initialization stream. */
static void initialize_heads(cgai_gameplay_model *model, uint64_t *state) {
    /* Step 1: Preserve all labels while preventing a uniform-expert learning collapse. */
    const size_t bank = model->config.centroids_per_module;
    for (size_t task = 0U; task < model->config.task_count; ++task) {
        const size_t outputs = model->config.output_counts[task];
        for (size_t expert = 0U; expert < model->config.module_count * bank; ++expert)
            for (size_t output = 0U; output < outputs; ++output)
                model->heads[task][expert * outputs + output] =
                    (output == (expert + task) % outputs ? 2.0 : -2.0) + random_scalar(state, 0.02);
    }
}

/** @brief Align initial hidden category coordinates when both bounded dimensions permit them.
 * @param model Borrowed model after its canonical dense encoder initialization. */
static void initialize_category_encoder(cgai_gameplay_model *model) {
    /* Step1: Small configurations retain their seeded dense fallback without changing random draws.
     */
    const cgai_gameplay_config *config = &model->config;
    if (config->embedding_dimensions < model->category_count ||
        config->hidden_dimensions < model->category_count)
        return;
    memset(model->encoder, 0, config->hidden_dimensions * model->input_count * sizeof(double));
    /* Step2: Every feature maps its own unit category coordinate into the same shared hidden axis.
     */
    for (size_t feature = 0U; feature < config->feature_count; ++feature)
        for (size_t value = 0U; value < config->cardinalities[feature]; ++value) {
            const size_t category = model->category_offsets[feature] + value;
            model->encoder[category * model->input_count + feature * config->embedding_dimensions +
                           category] = 1.0;
        }
}

/** @brief Initialize the seeded dense encoder stream and apply the shape-only category prior.
 * @param model Borrowed allocated model.
 * @param state Borrowed canonical initialization random stream. */
static void initialize_encoder(cgai_gameplay_model *model, uint64_t *state) {
    /* Step1: Every encoder weight consumes its normal draw, including structured configurations. */
    const size_t count = model->config.hidden_dimensions * model->input_count;
    for (size_t i = 0U; i < count; ++i)
        model->encoder[i] = random_scalar(state, 1.0 / sqrt((double)model->input_count));
    /* Step2: Aligned dimensions preserve independent initial category axes without semantic rules.
     */
    initialize_category_encoder(model);
}

/** @brief Initialize a shared coordinate system and all trainable centroid experts.
 * @param model Borrowed allocated model. */
static void initialize_parameters(cgai_gameplay_model *model) {
    /* Step 1: Keep encoder and centroids moderate while breaking routing symmetry. */
    uint64_t state = model->config.seed;
    initialize_embeddings(model, &state);
    initialize_encoder(model, &state);
    const size_t routing_count = model->config.module_count * model->config.hidden_dimensions;
    for (size_t i = 0U; i < routing_count; ++i)
        model->outer[i] = random_scalar(&state, 0.3);
    for (size_t i = 0U; i < routing_count * model->config.centroids_per_module; ++i)
        model->inner[i] = random_scalar(&state, 0.3);
    /* Step 2: Initialize task heads in the same deterministic stream. */
    initialize_heads(model, &state);
}

/** @brief Create a complete initialized composed network.
 * @param config Borrowed complete shape.
 * @return Owned model, or NULL. */
cgai_gameplay_model *cgai_gameplay_create(const cgai_gameplay_config *config) {
    /* Step 1: Acquire all model ownership, then initialize reproducibly. */
    cgai_gameplay_model *model = cgai_gameplay_allocate(config);
    if (model != NULL)
        initialize_parameters(model);
    return model;
}

/** @brief Release owned parameters and optional Adam continuation state.
 * @param model Owned initialized or partially initialized handle, or NULL. */
void cgai_gameplay_destroy(cgai_gameplay_model *model) {
    /* Step 1: Named slices borrow the parameter allocation and require no separate release. */
    if (model == NULL)
        return;
    free(model->parameters);
    free(model->adam_first);
    free(model->adam_second);
    free(model);
}

/** @brief Copy validated immutable architecture without allocation.
 * @param model Borrowed initialized model.
 * @param config Writable output, unchanged on error.
 * @return OK on publication, ERROR otherwise. */
cgai_status cgai_gameplay_get_config(const cgai_gameplay_model *model,
                                     cgai_gameplay_config *config) {
    /* Step 1: Publish only after both arguments are validated. */
    if (model == NULL || config == NULL)
        return cgai_fail("missing gameplay configuration argument");
    *config = model->config;
    return CGAI_STATUS_OK;
}

/** @brief Inspect completed training counters without allocation.
 * @param model Borrowed initialized model, or NULL.
 * @return Completed progress, zero for NULL. */
cgai_gameplay_progress cgai_gameplay_get_progress(const cgai_gameplay_model *model) {
    /* Step 1: A NULL handle has no completed work. */
    cgai_gameplay_progress progress = {0U, 0U};
    if (model != NULL) {
        progress.epochs = model->training_epochs;
        progress.steps = model->training_step;
    }
    return progress;
}
