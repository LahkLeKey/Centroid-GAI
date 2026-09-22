/** @file abi_model.c @brief Fixed-width ABI model operations. */

#include "centroid_gai_abi.h"

#include "centroid_gai.h"
#include "internal/abi_utils.h"
#include "internal/cgai_internal.h"
#include "internal/error.h"
#include "internal/model_io.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/** Allocates and fills one serialized ABI buffer. */
static cgai_abi_status allocate_export_buffer(const cgai_model *model, cgai_abi_buffer *output) {
    size_t required = 0U;
    if (cgai_model_encode(model, NULL, 0U, &required) != CGAI_STATUS_OK) {
        return CGAI_ABI_ERROR;
    }
    output->data = (uint8_t *)malloc(required);
    if (output->data == NULL) {
        return CGAI_ABI_OUT_OF_MEMORY;
    }
    if (cgai_model_encode(model, output->data, required, &required) != CGAI_STATUS_OK) {
        free(output->data);
        output->data = NULL;
        return CGAI_ABI_ERROR;
    }
    output->size = required;
    return CGAI_ABI_OK;
}

/** Validates an ABI configuration and creates its opaque native model. */
cgai_abi_status cgai_abi_model_create(const cgai_abi_config *config, cgai_abi_model **output) {
    if (config == NULL || output == NULL) {
        (void)cgai_fail("ABI config and model output are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    *output = NULL;
    if (config->struct_size != sizeof(cgai_abi_config) || config->abi_version != CGAI_ABI_VERSION) {
        (void)cgai_fail("ABI config version or size does not match");
        return CGAI_ABI_VERSION_MISMATCH;
    }
    if (config->reserved != 0U) {
        (void)cgai_fail("ABI config reserved field must be zero");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    const cgai_config core_config = {(size_t)config->dimensions, (size_t)config->centroid_count,
                                     (size_t)config->context_window, config->seed};
    cgai_model *model = cgai_model_create(&core_config);
    if (model == NULL) {
        return CGAI_ABI_ERROR;
    }
    *output = (cgai_abi_model *)model;
    return CGAI_ABI_OK;
}

/** Imports a serialized artifact into an opaque ABI model handle. */
cgai_abi_status cgai_abi_model_import(const uint8_t *data, size_t size, cgai_abi_model **output) {
    cgai_error_clear();
    if (data == NULL || output == NULL || size > SIZE_MAX) {
        (void)cgai_fail("ABI model bytes, size, and output are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    *output = NULL;
    cgai_model *model = cgai_model_decode(data, (size_t)size);
    if (model == NULL) {
        return CGAI_ABI_ERROR;
    }
    *output = (cgai_abi_model *)model;
    return CGAI_ABI_OK;
}

/** Releases an opaque ABI model handle. */
void cgai_abi_model_destroy(cgai_abi_model *model) {
    cgai_model_destroy(cgai_abi_core_model(model));
}

/** Adds UTF-8 training text through the fixed-width ABI. */
cgai_abi_status cgai_abi_model_train(cgai_abi_model *model, const char *utf8_text) {
    if (model == NULL || utf8_text == NULL) {
        (void)cgai_fail("ABI model and training text are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    return cgai_model_train_text(cgai_abi_core_model(model), utf8_text) == CGAI_STATUS_OK
               ? CGAI_ABI_OK
               : CGAI_ABI_ERROR;
}

/** Converts native model state into fixed-width ABI metadata. */
cgai_abi_status cgai_abi_model_get_metadata(const cgai_abi_model *model,
                                            cgai_abi_model_metadata *output) {
    if (model == NULL || output == NULL) {
        (void)cgai_fail("ABI model and metadata output are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    const cgai_model *core = cgai_abi_const_core_model(model);
    const cgai_abi_model_metadata result = {(uint32_t)sizeof(cgai_abi_model_metadata),
                                            CGAI_ABI_VERSION,
                                            1U,
                                            (uint32_t)core->config.dimensions,
                                            (uint32_t)core->config.centroid_count,
                                            (uint32_t)core->config.context_window,
                                            (uint64_t)core->vocabulary_size,
                                            (uint64_t)core->examples_seen};
    *output = result;
    return CGAI_ABI_OK;
}

/** Exports an ABI model and reports its required or written byte count. */
cgai_abi_status cgai_abi_model_export(const cgai_abi_model *model, cgai_abi_buffer *output) {
    cgai_error_clear();
    if (model == NULL || output == NULL) {
        (void)cgai_fail("ABI model and output buffer are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    output->data = NULL;
    output->size = 0U;
    return allocate_export_buffer(cgai_abi_const_core_model(model), output);
}

void cgai_abi_buffer_free(cgai_abi_buffer *buffer) {
    if (buffer != NULL) {
        free(buffer->data);
        buffer->data = NULL;
        buffer->size = 0U;
    }
}

/** Generates text through the fixed-width ABI into caller-owned storage. */
cgai_abi_status cgai_abi_model_generate(const cgai_abi_model *model, const char *utf8_prompt,
                                        uint32_t max_tokens, double temperature, uint64_t seed,
                                        cgai_abi_buffer *output) {
    if (model == NULL || utf8_prompt == NULL || output == NULL) {
        (void)cgai_fail("ABI generation arguments are invalid");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    output->data = NULL;
    output->size = 0U;
    const size_t capacity = (size_t)max_tokens * 128U + 1U;
    output->data = (uint8_t *)malloc(capacity);
    if (output->data == NULL) {
        return CGAI_ABI_OUT_OF_MEMORY;
    }
    if (cgai_model_generate(cgai_abi_const_core_model(model), utf8_prompt, (size_t)max_tokens,
                            temperature, seed, (char *)output->data, capacity) != CGAI_STATUS_OK) {
        free(output->data);
        output->data = NULL;
        return CGAI_ABI_ERROR;
    }
    output->size = strlen((char *)output->data);
    return CGAI_ABI_OK;
}