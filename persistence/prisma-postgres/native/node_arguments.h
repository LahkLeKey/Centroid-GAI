/** @file node_arguments.h @brief JavaScript string and configuration conversion. */

#ifndef CGAI_NODE_ARGUMENTS_H
#define CGAI_NODE_ARGUMENTS_H
#include "centroid_gai_abi.h"
#include <node_api.h>

/**
 * @brief Copy a JavaScript string into caller-owned, NUL-terminated native memory.
 *
 * The first Node-API call measures UTF-8 bytes without copying. The second fills an allocation with
 * one extra byte for NUL. JavaScript retains ownership of the original value. Native consumers use
 * C strings, so an embedded NUL in the JavaScript string terminates what those consumers observe.
 * On success the caller must free the returned pointer.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param value Borrowed JavaScript value expected to be a string.
 * @return Owned UTF-8 allocation, or NULL after requesting a type/allocation exception.
 */
char *cgai_node_utf8_argument(napi_env env, napi_value value);
/**
 * @brief Apply optional JavaScript configuration properties to initialized ABI defaults.
 *
 * Each helper updates one native field only when its property is present. Short-circuit AND stops
 * at the first failed lookup/conversion. Earlier fields can already be changed on failure, so the
 * caller must discard the configuration after a zero result. No model is allocated here.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param value Borrowed JavaScript configuration value.
 * @param config Non-NULL ABI configuration already initialized by cgai_abi_default_config().
 * @return One if all supplied properties convert, otherwise zero after requesting a TypeError.
 */
int cgai_node_config(napi_env env, napi_value value, cgai_abi_config *config);

#endif
