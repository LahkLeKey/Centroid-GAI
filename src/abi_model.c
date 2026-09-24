/** @file abi_model.c @brief ABI model lifecycle and training. */

#include "centroid_gai.h"
#include "centroid_gai_abi.h"
#include "internal/abi_utils.h"
#include "internal/error.h"

/**
 * @brief Validate the ABI configuration and create an opaque, owned model handle.
 *
 * A pointer-to-pointer lets this function place a newly allocated handle in the caller's variable.
 * After both input pointers are accepted, that variable is cleared before any validation can fail.
 * The ABI handle is the core model pointer viewed through an opaque type; no second model is copied.
 * The caller must eventually destroy a successful handle with cgai_abi_model_destroy().
 *
 * @param config Non-NULL configuration initialized for this ABI's size and version.
 * @param output Non-NULL address of the caller's handle variable; receives NULL on subsequent failure.
 * @return CGAI_ABI_OK, INVALID_ARGUMENT for missing pointers or reserved bits, VERSION_MISMATCH for
 * incompatible layout/version, or ERROR when core validation or allocation fails.
 */
cgai_abi_status cgai_abi_model_create(const cgai_abi_config *config, cgai_abi_model **output) {
    /* Step 1: Validate the two pointer arguments before using either. */
    if (config == NULL || output == NULL) {
        (void)cgai_fail("ABI config and model output are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    /* Step 2: Clear the result, then verify layout identity and unsupported reserved bits. */
    *output = NULL;
    if (config->struct_size != sizeof(cgai_abi_config) || config->abi_version != CGAI_ABI_VERSION) {
        (void)cgai_fail("ABI config version or size does not match");
        return CGAI_ABI_VERSION_MISMATCH;
    }
    if (config->reserved != 0U) {
        (void)cgai_fail("ABI config reserved field must be zero");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    /* Step 3: Translate fixed-width configuration fields to native array-size fields. */
    const cgai_config core_config = {(size_t)config->dimensions, (size_t)config->centroid_count,
                                     (size_t)config->context_window, config->seed};
    /* Step 4: Let the core validate numeric limits and allocate all model-owned storage. */
    cgai_model *model = cgai_model_create(&core_config);
    if (model == NULL) {
        return CGAI_ABI_ERROR;
    }
    /* Step 5: Transfer the successfully created handle to the caller without copying the model. */
    *output = (cgai_abi_model *)model;
    return CGAI_ABI_OK;
}

/**
 * @brief Destroy a model that was created or imported through the ABI.
 *
 * Converting the opaque handle restores its internal pointer type without allocating or copying.
 * The core destructor frees every model-owned array and the model itself. The caller's handle
 * variable is passed by value, so it is not reset; callers should set their own variable to NULL
 * if it might otherwise be reused. Destroying the same non-NULL handle twice is invalid.
 *
 * @param model Owned model handle to release, or NULL for a no-op.
 */
void cgai_abi_model_destroy(cgai_abi_model *model) {
    /* Step 1: Convert the handle and delegate the complete ownership cleanup to the core. */
    cgai_model_destroy(cgai_abi_core_model(model));
}

/**
 * @brief Apply another training corpus to an existing ABI model.
 *
 * The text is borrowed and is not retained after the call. Training changes vocabulary, centroid
 * means, and transition counts in place. A failure is not a transaction rollback: some learning
 * or vocabulary growth may already have occurred. Callers must serialize mutation of a model.
 * Core success is CGAI_STATUS_OK, while ABI success is CGAI_ABI_OK; compare the named constants.
 *
 * @param model Non-NULL mutable handle owned by the caller.
 * @param utf8_text Non-NULL NUL-terminated corpus; empty or whitespace-only text fails in the core.
 * @return CGAI_ABI_OK on success, INVALID_ARGUMENT for missing pointers, or ERROR for a core failure.
 */
cgai_abi_status cgai_abi_model_train(cgai_abi_model *model, const char *utf8_text) {
    /* Step 1: Reject missing input pointers at the ABI boundary. */
    if (model == NULL || utf8_text == NULL) {
        (void)cgai_fail("ABI model and training text are required");
        return CGAI_ABI_INVALID_ARGUMENT;
    }
    /* Step 2: Borrow the core model for training and translate its status into the ABI status domain. */
    return cgai_model_train_text(cgai_abi_core_model(model), utf8_text) == CGAI_STATUS_OK
               ? CGAI_ABI_OK
               : CGAI_ABI_ERROR;
}
