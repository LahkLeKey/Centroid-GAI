/** @file abi_utils.h @brief Private conversions for the opaque native ABI handle. */

#ifndef CGAI_ABI_UTILS_H
#define CGAI_ABI_UTILS_H

#include "cgai_internal.h"

#include "centroid_gai_abi.h"

/**
 * @brief View an opaque ABI handle as the same mutable core model pointer.
 *
 * Only handles created/imported by this library may be converted. The cast changes the C pointer's
 * static type; it does not allocate, copy, validate, or transfer the model. The ABI intentionally
 * hides the structure from foreign callers while internal operations use its core representation.
 *
 * @param model Borrowed ABI handle originating from this library, or NULL.
 * @return Aliased core pointer with the same lifetime and ownership, or NULL.
 */
static inline cgai_model *cgai_abi_core_model(
    cgai_abi_model
        *model) { /* Step 1: Restore the internal pointer type without changing the address. */
    return (cgai_model *)model;
}

/**
 * @brief View an opaque ABI handle as a read-only core model pointer.
 *
 * The const-qualified view permits state queries without permitting mutation through this pointer.
 * It does not make concurrent mutation by other callers safe, nor does it validate an arbitrary
 * address. The same underlying model remains owned by the original handle owner.
 *
 * @param model Borrowed ABI handle originating from this library, or NULL.
 * @return Read-only aliased core pointer, or NULL.
 */
static inline const cgai_model *cgai_abi_const_core_model(const cgai_abi_model *model) {
    /* Step 1: Preserve read-only access while restoring the private pointer type. */
    return (const cgai_model *)model;
}

#endif