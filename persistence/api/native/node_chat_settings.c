/** @file node_chat_settings.c @brief Exact bounded numeric options for conversation callbacks. */
#include "node_chat.h"
#include <math.h>
#include <string.h>

/** @brief Read exactly count finite doubles from a Float64Array.
 * @param env Borrowed runtime.
 * @param value Borrowed typed array.
 * @param output Writable count-element array.
 * @param count Required array length.
 * @return One on success, zero with an exception otherwise. */
int cgai_node_chat_numbers(napi_env env, napi_value value, double *output, size_t count) {
    /* Step 1: Require the correct typed-array kind and exact expected length. */
    napi_typedarray_type kind;
    size_t length = 0U, offset = 0U;
    void *data = NULL;
    napi_value owner;
    if (napi_get_typedarray_info(env, value, &kind, &length, &data, &owner, &offset) != napi_ok ||
        kind != napi_float64_array || length != count || data == NULL) {
        cgai_node_chat_error(env, "invalid chat numeric options array");
        return 0;
    }
    /* Step 2: Copy first, then validate every scalar without narrowing conversions. */
    memcpy(output, data, count * sizeof(double));
    for (size_t i = 0U; i < count; ++i) {
        if (!isfinite(output[i])) {
            cgai_node_chat_error(env, "chat settings must be finite");
            return 0;
        }
    }
    return 1;
}

/** @brief Convert a bounded lossless unsigned BigInt seed.
 * @param env Borrowed runtime.
 * @param value Borrowed BigInt.
 * @param seed Writable seed.
 * @return One on success, zero with an exception otherwise. */
int cgai_node_chat_seed(napi_env env, napi_value value, uint64_t *seed) {
    /* Step 1: Prevent negative or truncated seeds from crossing the native boundary. */
    bool lossless = false;
    if (napi_get_value_bigint_uint64(env, value, seed, &lossless) != napi_ok || !lossless) {
        cgai_node_chat_error(env, "chat seed must be an unsigned 64-bit BigInt");
        return 0;
    }
    return 1;
}

/** @brief Check integer shape and epoch entries before narrowing to size_t.
 * @param values Borrowed nine finite settings.
 * @return Nonzero when every shape and epoch integer is within a safe preliminary bound. */
static int bounded_settings(const double *values) {
    /* Step 1: Reject negative, fractional or excessive shape values. */
    for (size_t i = 0U; i < 5U; ++i)
        if (values[i] < 1.0 || values[i] > 256.0 || floor(values[i]) != values[i])
            return 0;
    /* Step 2: Full shape relationships and optimizer domains are checked again by C. */
    return values[6] >= 1.0 && values[6] <= 10000.0 && floor(values[6]) == values[6];
}

/** @brief Read validated numeric configuration and optimizer settings.
 * @param env Borrowed runtime.
 * @param value Float64Array of D,H,K,prompt,response,routing,epochs,rate,clip.
 * @param seed Borrowed BigInt initialization seed.
 * @param config Writable model shape.
 * @param training Writable optimizer settings.
 * @return One on success, zero with exception otherwise. */
int cgai_node_chat_settings(napi_env env, napi_value value, napi_value seed,
                            cgai_chat_config *config, cgai_neural_training *training) {
    /* Step 1: Validate representation and integer conversion domains. */
    double values[9];
    if (!cgai_node_chat_numbers(env, value, values, 9U) || !bounded_settings(values)) {
        cgai_node_chat_error(env, "invalid chat shape or epoch count");
        return 0;
    }
    /* Step 2: Publish exact narrowed settings, retaining separate routing/sampling scales. */
    *config = (cgai_chat_config){(size_t)values[0], (size_t)values[1], (size_t)values[2],
                                 (size_t)values[3], (size_t)values[4], 0U,
                                 values[5]};
    *training = (cgai_neural_training){(size_t)values[6], values[7], values[8]};
    return cgai_node_chat_seed(env, seed, &config->seed);
}
