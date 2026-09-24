/** @file abi_serialization.c @brief ABI artifact import and export. */

#include "centroid_gai.h"
#include "centroid_gai_abi.h"
#include "internal/abi_utils.h"
#include "internal/error.h"
#include "internal/model_io.h"
#include <stdlib.h>

/**
 * @brief Measure, allocate, and fill a model artifact for the ABI.
 *
 * The encoder is called twice: once without a destination to measure the artifact, then again
 * with exactly that much storage. The caller must provide an empty output descriptor and must
 * prevent model mutation between these passes. If encoding fails after allocation, this helper
 * releases the allocation and restores the empty data pointer.
 *
 * @param model Non-NULL, stable core model borrowed during both encoding passes.
 * @param output Non-NULL descriptor with data == NULL and size == 0 on entry.
 * @return CGAI_ABI_OK on success, OUT_OF_MEMORY if malloc fails, or ERROR if encoding fails.
 */
static cgai_abi_status allocate_export_buffer(const cgai_model *model, cgai_abi_buffer *output) {
    /* Step 1: Measure the full binary artifact before choosing an allocation size. */
    size_t required = 0U;
    if (cgai_model_encode(model, NULL, 0U, &required) != CGAI_STATUS_OK) {
        return CGAI_ABI_ERROR;
    }
    /* Step 2: Allocate the measured number of bytes using the ABI's allocator. */
    output->data = (uint8_t *)malloc(required);
    if (output->data == NULL) {
        return CGAI_ABI_OUT_OF_MEMORY;
    }
    /* Step 3: Fill the allocation; keep ownership here until encoding succeeds. */
    if (cgai_model_encode(model, output->data, required, &required) != CGAI_STATUS_OK) {
        free(output->data);
        output->data = NULL;
        return CGAI_ABI_ERROR;
    }
    /* Step 4: Publish the initialized byte count and leave buffer release to the caller. */
    output->size = required;
    return CGAI_ABI_OK;
}

/**
 * @brief Decode borrowed artifact bytes into a separately owned model handle.
 *
 * The byte range only needs to stay alive for this call: decoding reconstructs owned model arrays
 * and strings. The format uses native numeric representations and is intended for trusted artifacts
 * from compatible builds. Validation rejects many malformed inputs but is not an untrusted-file
 * sandbox. The caller destroys a returned model with cgai_abi_model_destroy().
 *
 * @param data Non-NULL readable byte range containing the entire artifact.
 * @param size Number of readable bytes beginning at data.
 * @param output Non-NULL address of the destination handle; cleared after pointer validation.
 * @return CGAI_ABI_OK, INVALID_ARGUMENT for NULL pointers, or ERROR when decoding fails.
 */
cgai_abi_status cgai_abi_model_import(const uint8_t *data, size_t size, cgai_abi_model **output) {
    /* Step 1: Discard an earlier diagnostic before starting this import. */
    cgai_error_clear();
    /* Step 2: Require readable input and a place to publish the decoded handle. */
    if (data == NULL || output == NULL) {
        (void)cgai_fail("ABI model bytes, size, and output are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    /* Step 3: Initialize failure output, then reconstruct a model from the bytes. */
    *output = NULL;
    cgai_model *model = cgai_model_decode(data, size);
    if (model == NULL) {
        return CGAI_ABI_ERROR;
    }
    /* Step 4: Transfer the newly owned model to the caller after complete decoding. */
    *output = (cgai_abi_model *)model;
    return CGAI_ABI_OK;
}

/**
 * @brief Allocate a complete binary artifact from a borrowed model handle.
 *
 * Pass an empty descriptor; replacing an existing data pointer here would lose that allocation.
 * On success the caller owns the descriptor's lifetime and must release its data through
 * cgai_abi_buffer_free(). The bytes contain model state only; database records and checksums are
 * managed by the TypeScript persistence layer. Concurrent model mutation must be excluded.
 *
 * @param model Non-NULL model to serialize without mutation.
 * @param output Non-NULL writable empty descriptor receiving the artifact allocation.
 * @return CGAI_ABI_OK, INVALID_ARGUMENT for NULL inputs, or the allocation/encoding helper's error status.
 */
cgai_abi_status cgai_abi_model_export(const cgai_abi_model *model, cgai_abi_buffer *output) {
    /* Step 1: Start a fresh error-reporting context for this export. */
    cgai_error_clear();
    /* Step 2: Validate the model and destination before touching the descriptor. */
    if (model == NULL || output == NULL) {
        (void)cgai_fail("ABI model and output buffer are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    /* Step 3: Make failure cleanup predictable by starting with an empty result. */
    output->data = NULL;
    output->size = 0U;
    /* Step 4: Run the size-query and encoding passes using the read-only core view. */
    return allocate_export_buffer(cgai_abi_const_core_model(model), output);
}
