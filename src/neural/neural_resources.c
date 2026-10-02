/** @file neural_resources.c @brief Owned neural memory and per-token work inspection. */
#include "internal/error.h"
#include "internal/neural_math.h"
#include "internal/size_utils.h"
#include <string.h>

/** @brief Check initialized ownership and bounded shape before inspecting allocations.
 * @param model Borrowed non-NULL neural handle.
 * @return One for a supported initialized model, or zero without mutation. */
static int resources_model_valid(const cgai_neural_model *model) {
    /* Step 1: Require complete ownership and the fixed bounded vocabulary layout. */
    if (model->vocabulary == NULL || model->parameters == NULL || model->vocabulary_size < 3U ||
        model->vocabulary_size > CGAI_NEURAL_MAX_VOCABULARY || model->output_size < 2U ||
        model->output_size > model->vocabulary_size || model->parameter_count == 0U ||
        model->parameter_count > CGAI_NEURAL_MAX_PARAMETERS)
        return 0;
    /* Step 2: Reuse shape validation before computing operation counts. */
    return cgai_neural_validate_config(&model->config) == CGAI_STATUS_OK;
}

/** @brief Add each initialized spelling and its terminator to an owned byte total.
 * @param model Borrowed initialized model with bounded vocabulary.
 * @param total Writable private byte total; partial updates remain on failure.
 * @return One for complete representable strings, or zero otherwise. */
static int resources_spelling_bytes(const cgai_neural_model *model, size_t *total) {
    /* Step 1: Require complete strings before measuring each allocation. */
    for (size_t index = 0U; index < model->vocabulary_size; ++index) {
        if (model->vocabulary[index] == NULL)
            return 0;
        size_t bytes = strlen(model->vocabulary[index]);
        /* Step 2: Include the terminator without wrapping either byte sum. */
        if (!cgai_size_add(bytes, 1U, &bytes) || !cgai_size_add(*total, bytes, total))
            return 0;
    }
    return 1;
}

/** @brief Sum parameter, optimizer and string allocations with checked byte arithmetic.
 * @param model Borrowed initialized model with bounded dimensions and vocabulary.
 * @param resources Mutable private report receiving owned allocation byte counts.
 * @return One for representable allocations, or zero without publishing the report. */
static int resources_owned_bytes(const cgai_neural_model *model, cgai_neural_resources *resources) {
    /* Step 1: Count the reserved pointer array and each actually present moment block. */
    size_t pointers = 0U;
    const size_t moments =
        (model->adam_first != NULL ? 1U : 0U) + (model->adam_second != NULL ? 1U : 0U);
    if (!cgai_size_mul(CGAI_NEURAL_MAX_VOCABULARY, sizeof(char *), &pointers) ||
        !cgai_size_mul(model->parameter_count, sizeof(double), &resources->parameter_bytes) ||
        !cgai_size_mul(resources->parameter_bytes, moments, &resources->optimizer_bytes))
        return 0;
    resources->model_bytes = sizeof(*model);
    if (!cgai_size_add(resources->model_bytes, pointers, &resources->model_bytes) ||
        !cgai_size_add(resources->model_bytes, resources->parameter_bytes,
                       &resources->model_bytes) ||
        !cgai_size_add(resources->model_bytes, resources->optimizer_bytes, &resources->model_bytes))
        return 0;
    /* Step 2: Include the exact requested bytes of every owned vocabulary spelling. */
    return resources_spelling_bytes(model, &resources->model_bytes);
}

/** @brief Inspect requested owned heap bytes and dense per-token work without allocation.
 * @param model Borrowed immutable initialized model.
 * @param resources Borrowed writable report; unchanged on every error.
 * @return OK after publishing a complete report, or ERROR with a diagnostic. */
cgai_status cgai_neural_get_resources(const cgai_neural_model *model,
                                      cgai_neural_resources *resources) {
    /* Step 1: Reject invalid handles before reading shape or touching caller output. */
    cgai_error_clear();
    if (model == NULL || resources == NULL)
        return cgai_fail("invalid neural resource inspection arguments");
    if (!resources_model_valid(model))
        return cgai_fail("invalid neural resource inspection model");
    cgai_neural_resources result = {.config = model->config,
                                    .vocabulary_size = model->vocabulary_size,
                                    .output_size = model->output_size,
                                    .parameter_count = model->parameter_count};
    /* Step 2: Measure complete owners, including each reusable generation session. */
    result.workspace_bytes = cgai_neural_workspace_bytes(model);
    result.session_bytes = cgai_neural_session_bytes(model);
    if (result.workspace_bytes == 0U || result.session_bytes == 0U ||
        !resources_owned_bytes(model, &result))
        return cgai_fail("neural resource inspection sizes overflow");
    /* Step 3: Dense forward work visits every centroid and excludes the masked BOS logit. */
    result.encoder_multiply_adds = (uint64_t)model->config.hidden_dimensions *
                                   (uint64_t)model->config.context_window *
                                   (uint64_t)model->config.embedding_dimensions;
    result.routing_coordinates =
        (uint64_t)model->config.centroid_count * (uint64_t)model->config.hidden_dimensions;
    result.expert_logits =
        (uint64_t)model->config.centroid_count * (uint64_t)(model->output_size - 1U);
    *resources = result;
    return CGAI_STATUS_OK;
}
