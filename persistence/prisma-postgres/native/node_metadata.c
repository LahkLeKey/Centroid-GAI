/** @file node_metadata.c @brief JavaScript model metadata conversion. */

#include "node_artifact.h"
#include "node_callbacks.h"
#include "node_error.h"

/**
 * @brief Create a JavaScript Number and assign it as an object's named property.
 *
 * The two Node-API calls are ordered by short-circuit AND: a failed value creation prevents use
 * of an uninitialized item handle. Node owns the Number and property storage. This helper borrows
 * the object and name and reports failures through the common exception translator.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param object Borrowed JavaScript object to mutate.
 * @param name Borrowed NUL-terminated property name.
 * @param value Unsigned 32-bit scalar, exactly representable as a JavaScript Number.
 * @return One if creation and assignment succeed, otherwise zero with a Node error.
 */
static int set_uint32(napi_env env, napi_value object, const char *name, uint32_t value) {
    /* Step 1: Reserve a local handle for the Node-owned Number. */
    napi_value item;
    /* Step 2: Create the Number first, then assign it only if creation succeeded. */
    return cgai_node_check(env, napi_create_uint32(env, value, &item)) &&
           cgai_node_check(env, napi_set_named_property(env, object, name, item));
}

/**
 * @brief Assign an unsigned 64-bit metadata counter as a JavaScript BigInt.
 *
 * A Number cannot exactly represent all uint64_t counters, so the binding preserves them with
 * BigInt. The created value belongs to Node. Short-circuit evaluation prevents attempting property
 * assignment after value creation fails.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param object Borrowed JavaScript object to mutate.
 * @param name Borrowed NUL-terminated property name.
 * @param value Unsigned 64-bit counter to preserve exactly.
 * @return One after creation and assignment, otherwise zero with a Node error.
 */
static int set_bigint(napi_env env, napi_value object, const char *name, uint64_t value) {
    /* Step 1: Reserve a handle for the runtime-managed BigInt. */
    napi_value item;
    /* Step 2: Create the exact BigInt and assign the named property if creation succeeds. */
    return cgai_node_check(env, napi_create_bigint_uint64(env, value, &item)) &&
           cgai_node_check(env, napi_set_named_property(env, object, name, item));
}

/**
 * @brief Convert borrowed ABI metadata into a JavaScript-owned object.
 *
 * Dimensions and format version use Number because their uint32_t values are exact there; learned
 * counters use BigInt to retain all 64 bits. A property failure stops construction and returns
 * NULL. Any partially built object is managed by Node's handle/garbage-collection system, not
 * free().
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param metadata Non-NULL borrowed scalar snapshot from the ABI.
 * @return JavaScript object with six native metadata fields, or NULL on a Node-API failure.
 */
static napi_value metadata_object(napi_env env, const cgai_abi_model_metadata *metadata) {
    /* Step 1: Create the destination object before assigning any properties. */
    napi_value result;
    NAPI_CALL(env, napi_create_object(env, &result));
    /* Step 2: Fill fixed-width dimensions/version and lossless counters, stopping at the first
     * failed assignment. */
    if (!set_uint32(env, result, "formatVersion", metadata->format_version) ||
        !set_uint32(env, result, "dimensions", metadata->dimensions) ||
        !set_uint32(env, result, "centroidCount", metadata->centroid_count) ||
        !set_uint32(env, result, "contextWindow", metadata->context_window) ||
        !set_bigint(env, result, "vocabularySize", metadata->vocabulary_size) ||
        !set_bigint(env, result, "examplesSeen", metadata->examples_seen)) {
        return NULL;
    }
    /* Step 3: Return the object whose scalar values no longer depend on native snapshot storage. */
    return result;
}

/**
 * @brief Implement JavaScript inspectModel(Buffer) as a metadata snapshot.
 *
 * The imported model is temporary and always destroyed before converting the scalar snapshot to
 * JavaScript. That is safe because metadata contains only copied integers. The TypeScript wrapper
 * adds libraryVersion separately; this callback returns the six fields supplied by the model
 * snapshot.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param info Opaque callback metadata supplied by Node for this invocation.
 * @return JavaScript metadata object, or NULL when arguments, import, metadata, or conversion fail.
 */
napi_value cgai_node_inspect(napi_env env, napi_callback_info info) {
    /* Step 1: Read the required Buffer argument into a local borrowed handle. */
    size_t argc = 1U;
    napi_value argv[1];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
    /* Step 2: Reject a missing argument before examining argv[0]. */
    if (argc != 1U) {
        (void)napi_throw_type_error(env, NULL, "inspectModel requires a model Buffer");
        return NULL;
    }
    /* Step 3: Import owned model state from the Buffer. */
    cgai_abi_model *model = NULL;
    if (!cgai_node_import(env, argv[0], &model)) {
        return NULL;
    }
    /* Step 4: Copy metadata and destroy the imported model before constructing the JavaScript
     * result. */
    cgai_abi_model_metadata metadata;
    const cgai_abi_status status = cgai_abi_model_get_metadata(model, &metadata);
    cgai_abi_model_destroy(model);
    return status == CGAI_ABI_OK ? metadata_object(env, &metadata) : cgai_node_native_error(env);
}
