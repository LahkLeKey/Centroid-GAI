#include "node_arguments.h"
#include "node_artifact.h"
#include "node_callbacks.h"
#include "node_error.h"
#include <math.h>
#include <stdlib.h>

/**
 * @brief Validate an exact unsigned 32-bit JavaScript numeric argument.
 * @param env Borrowed Node-API environment for this invocation.
 * @param value Borrowed input value to validate or serialize.
 * @param output Writable destination; receives the result or owned output allocation.
 * @return JavaScript result, or NULL with a pending exception.
 */
static int uint_argument(napi_env env, napi_value value, uint32_t *output) {
    napi_valuetype type;
    double number;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_number ||
        napi_get_value_double(env, value, &number) != napi_ok || !isfinite(number) ||
        number < 0.0 || number > UINT32_MAX || floor(number) != number) {
        (void)napi_throw_type_error(env, NULL,
                                    "inspection and merge arguments must be unsigned integers");
        return 0;
    }
    *output = (uint32_t)number;
    return 1;
}

/**
 * @brief Copy ABI JSON into a JavaScript string and release the ABI allocation.
 * @param env Borrowed Node-API environment for this invocation.
 * @param json Owned ABI JSON buffer; released after conversion.
 * @return JavaScript result, or NULL with a pending exception.
 */
static napi_value json_string(napi_env env, cgai_abi_buffer *json) {
    napi_value result;
    const napi_status status =
        napi_create_string_utf8(env, (const char *)json->data, json->size, &result);
    cgai_abi_buffer_free(json);
    return cgai_node_check(env, status) ? result : NULL;
}

/**
 * @brief Import temporary model ownership and inspect a bounded artifact section.
 * @param env Borrowed Node-API environment for this invocation.
 * @param payload Borrowed JavaScript artifact Buffer.
 * @param options Validated inspection section, offset, limit, and centroid values.
 * @return JavaScript result, or NULL with a pending exception.
 */
static napi_value inspect_payload(napi_env env, napi_value payload, const uint32_t *options) {
    cgai_abi_model *model = NULL;
    if (!cgai_node_import(env, payload, &model))
        return NULL;
    cgai_abi_buffer json = {0};
    const cgai_abi_status status =
        cgai_abi_model_inspect_json(model, options[0], options[1], options[2], options[3], &json);
    cgai_abi_model_destroy(model);
    return status == CGAI_ABI_OK ? json_string(env, &json) : cgai_node_native_error(env);
}

/**
 * @brief Validate JavaScript inspection arguments and return bounded native JSON.
 * @param env Borrowed Node-API environment for this invocation.
 * @param info Borrowed JavaScript callback metadata.
 * @return JavaScript result, or NULL with a pending exception.
 */
napi_value cgai_node_contents(napi_env env, napi_callback_info info) {
    size_t argc = 5U;
    napi_value argv[5];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
    if (argc != 5U) {
        (void)napi_throw_type_error(
            env, NULL, "inspectContents requires payload, section, offset, limit, and centroid");
        return NULL;
    }
    uint32_t options[4];
    for (size_t i = 0U; i < 4U; ++i)
        if (!uint_argument(env, argv[i + 1U], &options[i]))
            return NULL;
    return inspect_payload(env, argv[0], options);
}

/**
 * @brief Copy text, import a model, and release both after native matching.
 * @param env Borrowed Node-API environment for this invocation.
 * @param payload Borrowed JavaScript artifact Buffer.
 * @param input Borrowed JavaScript context string.
 * @param limit Maximum number of result entries.
 * @return JavaScript result, or NULL with a pending exception.
 */
static napi_value match_payload(napi_env env, napi_value payload, napi_value input,
                                uint32_t limit) {
    char *text = cgai_node_utf8_argument(env, input);
    if (!text)
        return NULL;
    cgai_abi_model *model = NULL;
    if (!cgai_node_import(env, payload, &model)) {
        free(text);
        return NULL;
    }
    cgai_abi_buffer json = {0};
    const cgai_abi_status status = cgai_abi_model_match_json(model, text, limit, &json);
    cgai_abi_model_destroy(model);
    free(text);
    return status == CGAI_ABI_OK ? json_string(env, &json) : cgai_node_native_error(env);
}

/**
 * @brief Validate JavaScript matching arguments and return nearest learned patterns.
 * @param env Borrowed Node-API environment for this invocation.
 * @param info Borrowed JavaScript callback metadata.
 * @return JavaScript result, or NULL with a pending exception.
 */
napi_value cgai_node_match(napi_env env, napi_callback_info info) {
    size_t argc = 3U;
    napi_value argv[3];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
    if (argc != 3U) {
        (void)napi_throw_type_error(env, NULL, "matchPatterns requires payload, text, and limit");
        return NULL;
    }
    uint32_t limit;
    if (!uint_argument(env, argv[2], &limit))
        return NULL;
    return match_payload(env, argv[0], argv[1], limit);
}

/** @brief Temporary ownership for imported Node merge sources. */
typedef struct merge_models {
    cgai_abi_model *owned[32];         /**< Owned imported handles. */
    const cgai_abi_model *sources[32]; /**< Borrowed view of the imported handles. */
} merge_models;

/**
 * @brief Import source buffers into independently owned temporary handles.
 * @param env Borrowed Node-API environment for this invocation.
 * @param array Borrowed JavaScript array of source buffers.
 * @param count Number of entries supplied to this operation.
 * @param models Temporary imported model handles owned by the merge operation.
 * @return JavaScript result, or NULL with a pending exception.
 */
static int import_sources(napi_env env, napi_value array, uint32_t count, merge_models *models) {
    for (uint32_t i = 0; i < count; ++i) {
        napi_value item;
        if (!cgai_node_check(env, napi_get_element(env, array, i, &item)) ||
            !cgai_node_import(env, item, &models->owned[i]))
            return 0;
        models->sources[i] = models->owned[i];
    }
    return 1;
}

/**
 * @brief Merge borrowed handles and export the result to a JavaScript buffer.
 * @param env Borrowed Node-API environment for this invocation.
 * @param models Temporary imported model handles owned by the merge operation.
 * @param count Number of entries supplied to this operation.
 * @param target Requested compacted capacity; zero preserves active rows.
 * @return JavaScript result, or NULL with a pending exception.
 */
static napi_value export_merge(napi_env env, const merge_models *models, uint32_t count,
                               uint32_t target) {
    cgai_abi_model *merged = NULL;
    if (cgai_abi_model_merge(models->sources, count, target, &merged) != CGAI_ABI_OK)
        return cgai_node_native_error(env);
    napi_value result = cgai_node_export(env, merged);
    cgai_abi_model_destroy(merged);
    return result;
}

/**
 * @brief Own and clean up all temporary handles used to merge artifact buffers.
 * @param env Borrowed Node-API environment for this invocation.
 * @param array Borrowed JavaScript array of source buffers.
 * @param count Number of entries supplied to this operation.
 * @param target Requested compacted capacity; zero preserves active rows.
 * @return JavaScript result, or NULL with a pending exception.
 */
static napi_value merge_payloads(napi_env env, napi_value array, uint32_t count, uint32_t target) {
    merge_models models = {0};
    napi_value result = NULL;
    if (import_sources(env, array, count, &models))
        result = export_merge(env, &models, count, target);
    for (uint32_t i = 0; i < count; ++i)
        cgai_abi_model_destroy(models.owned[i]);
    return result;
}

/**
 * @brief Validate JavaScript merge arguments and return a combined artifact.
 * @param env Borrowed Node-API environment for this invocation.
 * @param info Borrowed JavaScript callback metadata.
 * @return JavaScript result, or NULL with a pending exception.
 */
napi_value cgai_node_merge(napi_env env, napi_callback_info info) {
    size_t argc = 2U;
    napi_value argv[2];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
    bool is_array = false;
    uint32_t count, target;
    if (argc != 2U || napi_is_array(env, argv[0], &is_array) != napi_ok || !is_array ||
        napi_get_array_length(env, argv[0], &count) != napi_ok || count == 0U || count > 32U) {
        (void)napi_throw_type_error(
            env, NULL, "mergeModels requires an array of 1 to 32 model Buffers and a target count");
        return NULL;
    }
    if (!uint_argument(env, argv[1], &target))
        return NULL;
    return merge_payloads(env, argv[0], count, target);
}
