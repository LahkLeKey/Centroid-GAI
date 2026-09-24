/** @file abi_config.c @brief Fixed-width ABI configuration conversion. */

#include "centroid_gai_abi.h"

#include "centroid_gai.h"
#include "internal/error.h"

/**
 * @brief Translate the core defaults into the versioned ABI configuration layout.
 *
 * The core configuration uses size_t for native array sizes, while the ABI configuration uses
 * explicit integer widths for its configuration fields. The default limits fit those fields.
 * A local result is populated completely before it is copied to the caller. The reserved field
 * is set to zero because nonzero values are rejected by model creation.
 *
 * @param output Non-NULL pointer to writable cgai_abi_config storage; no allocation is returned.
 * @return CGAI_ABI_OK when the structure is filled, or CGAI_ABI_INVALID_ARGUMENT with a native
 * diagnostic.
 */
cgai_abi_status cgai_abi_default_config(cgai_abi_config *output) {
    /* Step 1: Reject a missing destination before dereferencing it. */
    if (output == NULL) {
        (void)cgai_fail("ABI config output is required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    /* Step 2: Ask the core for defaults so the ABI does not maintain a second set of values. */
    const cgai_config config = cgai_default_config();
    /* Step 3: Add structure size and ABI version, narrow bounded defaults, and clear reserved bits.
     */
    const cgai_abi_config result = {(uint32_t)sizeof(cgai_abi_config),
                                    CGAI_ABI_VERSION,
                                    (uint32_t)config.dimensions,
                                    (uint32_t)config.centroid_count,
                                    (uint32_t)config.context_window,
                                    0U,
                                    config.seed};
    /* Step 4: Copy the complete structure into caller-owned storage. */
    *output = result;
    return CGAI_ABI_OK;
}