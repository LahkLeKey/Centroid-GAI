/** @file abi_metadata.c @brief Fixed-width model metadata. */

#include "centroid_gai_abi.h"
#include "internal/abi_utils.h"
#include "internal/cgai_internal.h"
#include "internal/error.h"

/**
 * @brief Copy model dimensions and learned-state counters into ABI metadata.
 *
 * The result is a scalar snapshot: it contains no pointers into the model and owns no heap storage.
 * Configuration dimensions are narrowed to the fixed widths allowed by the model's limits; counters
 * are widened to uint64_t. Callers must keep the model alive and avoid concurrent mutation while
 * the snapshot is read. This function does not serialize the model or access PostgreSQL.
 *
 * @param model Non-NULL borrowed model handle.
 * @param output Non-NULL writable destination for a complete metadata structure.
 * @return CGAI_ABI_OK after copying the snapshot, or CGAI_ABI_INVALID_ARGUMENT for NULL inputs.
 */
cgai_abi_status cgai_abi_model_get_metadata(const cgai_abi_model *model,
                                            cgai_abi_model_metadata *output) {
    /* Step 1: Check the borrowed handle and destination before accessing either. */
    if (model == NULL || output == NULL) {
        (void)cgai_fail("ABI model and metadata output are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    /* Step 2: View the opaque handle as a read-only core model. */
    const cgai_model *core = cgai_abi_const_core_model(model);
    /* Step 3: Assemble identity fields, bounded configuration values, and learned counters. */
    const cgai_abi_model_metadata result = {(uint32_t)sizeof(cgai_abi_model_metadata),
                                            CGAI_ABI_VERSION,
                                            1U,
                                            (uint32_t)core->config.dimensions,
                                            (uint32_t)core->config.centroid_count,
                                            (uint32_t)core->config.context_window,
                                            (uint64_t)core->vocabulary_size,
                                            (uint64_t)core->examples_seen};
    /* Step 4: Publish a copy whose lifetime is independent of the model. */
    *output = result;
    return CGAI_ABI_OK;
}
