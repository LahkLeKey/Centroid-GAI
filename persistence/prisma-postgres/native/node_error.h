/** @file node_error.h @brief Node-API exception translation. */

#ifndef CGAI_NODE_ERROR_H
#define CGAI_NODE_ERROR_H
#include "centroid_gai_abi.h"
#include <node_api.h>

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
napi_value cgai_node_native_error(napi_env env);
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
int cgai_node_check(napi_env env, napi_status status);
/**
 * @brief Return immediately from a callback if one Node-API operation fails.
 *
 * The do/while wrapper makes the macro behave as one statement in surrounding
 * control flow. The operation expression is evaluated once; its status is passed
 * to cgai_node_check() for exception translation. The return exits the calling
 * function, not a helper, so use this only before acquiring native resources that
 * require cleanup. Resource-owning paths store status, clean up, then translate it.
 * @param env Borrowed Node environment; use a plain variable without side effects.
 * @param call Node-API expression yielding a napi_status.
 */
#define NAPI_CALL(env, call)                                                                       \
    do {                                                                                           \
        if (!cgai_node_check((env), (call))) {                                                     \
            return NULL;                                                                           \
        }                                                                                          \
    } while (0)

#endif
