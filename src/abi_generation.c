/** @file abi_generation.c @brief ABI generation and output allocation. */

#include "centroid_gai.h"
#include "centroid_gai_abi.h"
#include "internal/abi_utils.h"
#include "internal/error.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Generate a continuation and return text allocated by the ABI.
 *
 * The handle is borrowed: generation reads the model without transferring or changing its
 * ownership. The output structure belongs to the caller, but its data allocation must be released
 * with cgai_abi_buffer_free(). Pass an empty buffer; this function does not free an earlier
 * allocation. A C string ends with a zero byte (NUL); output->size counts the text bytes before
 * that terminator. The 128-byte allowance per requested token is a capacity estimate, not a
 * tokenizer limit. Long tokens can exhaust that capacity and make the core generation call fail.
 *
 * @param model Non-NULL trained model handle, kept alive throughout this call.
 * @param utf8_prompt Non-NULL NUL-terminated prompt; borrowed for this call.
 * @param max_tokens Maximum number of continuation tokens; zero requests an empty continuation.
 * @param temperature Sampling temperature; the core rejects negative or nonfinite values.
 * @param seed Random seed; zero tells the core to use the model's configured seed.
 * @param output Non-NULL writable buffer descriptor receiving the allocation and text length.
 * @return CGAI_ABI_OK on success, INVALID_ARGUMENT for NULL inputs, OUT_OF_MEMORY on allocation
 * failure, or ERROR if core generation fails. After valid pointers are accepted, failure leaves an
 * empty buffer.
 */
cgai_abi_status cgai_abi_model_generate(const cgai_abi_model *model, const char *utf8_prompt,
                                        uint32_t max_tokens, double temperature, uint64_t seed,
                                        cgai_abi_buffer *output) {
    /* Step 1: Check pointers before reading the handle or writing through the output pointer. */
    if (model == NULL || utf8_prompt == NULL || output == NULL) {
        (void)cgai_fail("ABI generation arguments are invalid");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    /* Step 2: Initialize an empty result so later failure paths have a consistent state. */
    output->data = NULL;
    output->size = 0U;
    /* Step 3: Reserve estimated text space plus one byte for the terminating NUL. */
    const size_t capacity = (size_t)max_tokens * 128U + 1U;
    output->data = (uint8_t *)malloc(capacity);
    if (output->data == NULL) {
        return CGAI_ABI_OUT_OF_MEMORY;
    }
    /* Step 4: Generate directly into the allocation; release it if the core reports failure. */
    if (cgai_model_generate(cgai_abi_const_core_model(model), utf8_prompt, (size_t)max_tokens,
                            temperature, seed, (char *)output->data, capacity) != CGAI_STATUS_OK) {
        free(output->data);
        output->data = NULL;
        return CGAI_ABI_ERROR;
    }
    /* Step 5: Publish the text length after generation has produced a valid C string. */
    output->size = strlen((char *)output->data);
    return CGAI_ABI_OK;
}
