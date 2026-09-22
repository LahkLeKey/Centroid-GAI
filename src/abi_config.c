/** @file abi_config.c @brief Fixed-width ABI configuration conversion. */

#include "centroid_gai_abi.h"

#include "centroid_gai.h"
#include "internal/error.h"

cgai_abi_status cgai_abi_default_config(cgai_abi_config *output) {
    if (output == NULL) {
        (void)cgai_fail("ABI config output is required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    const cgai_config config = cgai_default_config();
    const cgai_abi_config result = {(uint32_t)sizeof(cgai_abi_config),
                                    CGAI_ABI_VERSION,
                                    (uint32_t)config.dimensions,
                                    (uint32_t)config.centroid_count,
                                    (uint32_t)config.context_window,
                                    0U,
                                    config.seed};
    *output = result;
    return CGAI_ABI_OK;
}