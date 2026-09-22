/** @file abi_utils.h @brief Private conversions for the opaque native ABI handle. */

#ifndef CGAI_ABI_UTILS_H
#define CGAI_ABI_UTILS_H

#include "cgai_internal.h"

#include "centroid_gai_abi.h"

/** Converts an ABI handle to the private model representation. */
static inline cgai_model *cgai_abi_core_model(cgai_abi_model *model) { return (cgai_model *)model; }

/** Converts a const ABI handle to the private model representation. */
static inline const cgai_model *cgai_abi_const_core_model(const cgai_abi_model *model) {
    return (const cgai_model *)model;
}

#endif