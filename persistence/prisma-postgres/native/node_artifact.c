/** @file node_artifact.c @brief Model artifact import and export. */

#include "node_artifact.h"
#include "node_error.h"

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
int cgai_node_import(napi_env env, napi_value value, cgai_abi_model **model) {
    /* Step 1: Prepare borrowed buffer metadata and confirm the value is a Buffer. */
    bool is_buffer = false;
    void *data = NULL;
    size_t size = 0U;
    if (napi_is_buffer(env, value, &is_buffer) != napi_ok || !is_buffer ||
        napi_get_buffer_info(env, value, &data, &size) != napi_ok) {
        napi_throw_type_error(env, NULL, "expected a model Buffer");
        return 0;
    }
    /* Step 2: Decode the readable byte span into independent model storage and translate native failure. */
    if (cgai_abi_model_import((const uint8_t *)data, size, model) != CGAI_ABI_OK) {
        (void)cgai_node_native_error(env);
        return 0;
    }
    /* Step 3: Transfer model ownership to the callback, which must destroy it. */
    return 1;
}

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
napi_value cgai_node_export(napi_env env, const cgai_abi_model *model) {
    /* Step 1: Start an empty ABI descriptor and request a complete serialized artifact. */
    cgai_abi_buffer buffer = {0};
    if (cgai_abi_model_export(model, &buffer) != CGAI_ABI_OK) {
        return cgai_node_native_error(env);
    }
    /* Step 2: Copy artifact bytes into a Buffer managed by Node. */
    napi_value result = NULL;
    const napi_status status =
        napi_create_buffer_copy(env, buffer.size, buffer.data, NULL, &result);
    /* Step 3: Release the ABI allocation before translating the Node status. */
    cgai_abi_buffer_free(&buffer);
    return cgai_node_check(env, status) ? result : NULL;
}
