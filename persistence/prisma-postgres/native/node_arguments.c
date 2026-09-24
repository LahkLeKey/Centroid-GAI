/** @file node_arguments.c @brief Owned UTF-8 strings and optional model configuration. */

#include "node_arguments.h"
#include <stdlib.h>

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
char *cgai_node_utf8_argument(napi_env env, napi_value value) {
    /* Step 1: Ask Node for the UTF-8 byte length and reject non-string values. */
    size_t length = 0U;
    if (napi_get_value_string_utf8(env, value, NULL, 0U, &length) != napi_ok) {
        napi_throw_type_error(env, NULL, "expected a UTF-8 string");
        return NULL;
    }
    /* Step 2: Allocate measured bytes plus the C string terminator. */
    char *text = (char *)malloc(length + 1U);
    if (text == NULL) {
        napi_throw_error(env, NULL, "could not allocate string argument");
        return NULL;
    }
    /* Step 3: Copy into owned memory; release it if the second conversion call fails. */
    if (napi_get_value_string_utf8(env, value, text, length + 1U, &length) != napi_ok) {
        free(text);
        napi_throw_type_error(env, NULL, "could not read UTF-8 string");
        return NULL;
    }
    /* Step 4: Transfer native string ownership to the callback that requested the copy. */
    return text;
}

/**
 * @brief Read an optional numeric property into an existing configuration field.
 *
 * Absence is successful and preserves the caller's default. A present property is converted with
 * Node-API's uint32 extraction rules; this helper does not itself prove the input was a positive
 * integer within model limits. Model creation performs the later capacity validation.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param object Borrowed JavaScript object from which to read a property.
 * @param name Borrowed NUL-terminated property name.
 * @param output Non-NULL destination initially holding the default uint32_t value.
 * @return One if absent or converted, otherwise zero; the caller supplies the configuration error.
 */
static int optional_uint32(napi_env env, napi_value object, const char *name, uint32_t *output) {
    /* Step 1: Ask whether the property exists, treating a failed query as failure. */
    bool present = false;
    if (napi_has_named_property(env, object, name, &present) != napi_ok) {
        return 0;
    }
    /* Step 2: Retain the existing default when no property was supplied. */
    if (!present) {
        return 1;
    }
    /* Step 3: Fetch the property and extract its uint32 representation only if lookup succeeds. */
    napi_value value;
    return napi_get_named_property(env, object, name, &value) == napi_ok &&
           napi_get_value_uint32(env, value, output) == napi_ok;
}

/**
 * @brief Read an optional BigInt seed without accepting a lossy uint64 conversion.
 *
 * Seeds can exceed JavaScript Number's exact integer range, so the binding uses BigInt. Node-API
 * reports both the converted word and whether information was lost. The caller accepts the value
 * only when that lossless flag is true; an absent property keeps the configured default.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param object Borrowed JavaScript configuration object.
 * @param output Non-NULL mutable uint64_t seed initialized with a default.
 * @return One for absence or a lossless conversion, otherwise zero.
 */
static int optional_seed(napi_env env, napi_value object, uint64_t *output) {
    /* Step 1: Check whether the seed property is available. */
    bool present = false;
    if (napi_has_named_property(env, object, "seed", &present) != napi_ok) {
        return 0;
    }
    /* Step 2: Preserve the existing default when the property is absent. */
    if (!present) {
        return 1;
    }
    /* Step 3: Read the BigInt and require its value to fit uint64_t exactly. */
    napi_value value;
    bool lossless = false;
    return napi_get_named_property(env, object, "seed", &value) == napi_ok &&
           napi_get_value_bigint_uint64(env, value, output, &lossless) == napi_ok && lossless;
}

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
int cgai_node_config(napi_env env, napi_value value, cgai_abi_config *config) {
    /* Step 1: Apply dimensions, centroid count, context window, and optional lossless seed in
     * order. */
    if (optional_uint32(env, value, "dimensions", &config->dimensions) &&
        optional_uint32(env, value, "centroidCount", &config->centroid_count) &&
        optional_uint32(env, value, "contextWindow", &config->context_window) &&
        optional_seed(env, value, &config->seed)) {
        return 1;
    }
    /* Step 2: Translate any failed property conversion into the binding's configuration error. */
    (void)napi_throw_type_error(env, NULL, "invalid native model configuration");
    return 0;
}
