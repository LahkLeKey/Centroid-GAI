/** @file node_error.c @brief Native and Node-API error translation. */

#include "node_error.h"

/**
 * @brief Turn the current ABI diagnostic into a JavaScript Error result.
 *
 * Node copies the diagnostic string into the error object, so the borrowed thread-local ABI string
 * need not outlive this call. Returning NULL is the Node-API callback failure convention; it is not
 * a JavaScript null value. Call only when a preceding native operation has failed.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @return NULL after requesting a JavaScript Error with the native diagnostic.
 */
napi_value cgai_node_native_error(napi_env env) {
    /* Step 1: Request an Error using the current native message. */
    (void)napi_throw_error(env, NULL, cgai_abi_last_error());
    /* Step 2: Return the native callback failure sentinel to the caller. */
    return NULL;
}

/**
 * @brief Translate a failed Node-API status into the common exception path.
 *
 * A successful status is a fast path with no further Node calls. On failure, Node's extended error
 * information may provide a message; otherwise a fixed fallback is used. Error-info pointers are
 * borrowed from Node and are not freed. Call promptly after the operation whose status is checked.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param status Status returned by the preceding Node-API operation.
 * @return One for napi_ok, otherwise zero after attempting to throw an Error.
 */
int cgai_node_check(napi_env env, napi_status status) {
    /* Step 1: Return immediately when the operation succeeded. */
    if (status == napi_ok) {
        return 1;
    }
    /* Step 2: Fetch Node's borrowed explanation for the most recent failure. */
    const napi_extended_error_info *details = NULL;
    (void)napi_get_last_error_info(env, &details);
    /* Step 3: Use the available message or a fallback, then report failure to the native caller. */
    (void)napi_throw_error(env, NULL,
                           details != NULL && details->error_message != NULL
                               ? details->error_message
                               : "Node-API failure");
    return 0;
}
