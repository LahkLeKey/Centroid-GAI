#include "node_artifact.h"
#include "node_callbacks.h"
#include "node_error.h"
#include <math.h>

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

static napi_value json_string(napi_env env, cgai_abi_buffer *json) {
    napi_value result;
    const napi_status status =
        napi_create_string_utf8(env, (const char *)json->data, json->size, &result);
    cgai_abi_buffer_free(json);
    return cgai_node_check(env, status) ? result : NULL;
}

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

typedef struct merge_models {
    cgai_abi_model *owned[32];
    const cgai_abi_model *sources[32];
} merge_models;

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

static napi_value export_merge(napi_env env, const merge_models *models, uint32_t count,
                               uint32_t target) {
    cgai_abi_model *merged = NULL;
    if (cgai_abi_model_merge(models->sources, count, target, &merged) != CGAI_ABI_OK)
        return cgai_node_native_error(env);
    napi_value result = cgai_node_export(env, merged);
    cgai_abi_model_destroy(merged);
    return result;
}

static napi_value merge_payloads(napi_env env, napi_value array, uint32_t count, uint32_t target) {
    merge_models models = {0};
    napi_value result = NULL;
    if (import_sources(env, array, count, &models))
        result = export_merge(env, &models, count, target);
    for (uint32_t i = 0; i < count; ++i)
        cgai_abi_model_destroy(models.owned[i]);
    return result;
}

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
