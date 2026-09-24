/** @file node_artifact.h @brief Model Buffer ownership and conversion. */

#ifndef CGAI_NODE_ARTIFACT_H
#define CGAI_NODE_ARTIFACT_H
#include "centroid_gai_abi.h"
#include <node_api.h>

/**
 * @brief Decode a JavaScript Buffer into a newly owned ABI model.
 *
 * Node's buffer memory stays JavaScript-owned and is borrowed only during synchronous decoding.
 * The ABI copies model state into its own allocations, so no pointer into Buffer storage survives.
 * Initialize the caller's handle to NULL before calling; this helper can fail before import starts.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param value Borrowed JavaScript value required to be a Buffer.
 * @param model Non-NULL address of an initially NULL handle; receives owned model state on success.
 * @return One after import, otherwise zero with a type/native exception requested.
 */
int cgai_node_import(napi_env env, napi_value value, cgai_abi_model **model);
/**
 * @brief Copy a model artifact into a JavaScript-owned Buffer.
 *
 * The ABI first returns an allocation that must be freed with its own release function. Buffer
 * creation then copies those bytes into Node-managed storage. Releasing the ABI allocation before
 * checking the Node result ensures both successful and failed Buffer creation use the same cleanup.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param model Non-NULL borrowed ABI model, kept stable during encoding.
 * @return JavaScript Buffer handle owning a copy, or NULL after an ABI/Node error.
 */
napi_value cgai_node_export(napi_env env, const cgai_abi_model *model);

#endif
