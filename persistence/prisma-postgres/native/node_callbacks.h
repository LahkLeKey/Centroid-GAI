/** @file node_callbacks.h @brief Exported JavaScript callback implementations. */

#ifndef CGAI_NODE_CALLBACKS_H
#define CGAI_NODE_CALLBACKS_H
#include "centroid_gai_abi.h"
#include <node_api.h>

/**
 * @brief Implement the JavaScript trainModel(text, config?) callback.
 *
 * napi_get_cb_info reads at most the capacity supplied in argc and updates it to the available count.
 * The callback checks the required text argument before indexing argv. Configuration defaults,
 * string ownership, and model lifetime are handled by the focused helper chain.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param info Opaque callback metadata supplied by Node for this invocation.
 * @return JavaScript artifact Buffer, or NULL on invalid arguments or failed training/export.
 */
napi_value cgai_node_train(napi_env env, napi_callback_info info);
/**
 * @brief Implement JavaScript inspectModel(Buffer) as a metadata snapshot.
 *
 * The imported model is temporary and always destroyed before converting the scalar snapshot to
 * JavaScript. That is safe because metadata contains only copied integers. The TypeScript wrapper
 * adds libraryVersion separately; this callback returns the six fields supplied by the model snapshot.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param info Opaque callback metadata supplied by Node for this invocation.
 * @return JavaScript metadata object, or NULL when arguments, import, metadata, or conversion fail.
 */
napi_value cgai_node_inspect(napi_env env, napi_callback_info info);
/**
 * @brief Implement JavaScript generateModel(Buffer, prompt, maxTokens, temperature, seed?).
 *
 * The callback reserves five argument handles, checks the four required positions, and separates
 * numeric conversion from model import. All native allocations belong to downstream helpers and
 * are released before this callback returns. Seed zero delegates to the model's configured seed.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param info Opaque callback metadata supplied by Node for this invocation.
 * @return JavaScript continuation String, or NULL with an argument/native/Node error.
 */
napi_value cgai_node_generate(napi_env env, napi_callback_info info);

#endif
