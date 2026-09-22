/** @file addon.c @brief Node-API bridge from TypeScript Buffers to the stable C ABI. */

#include <node_api.h>

#include "centroid_gai_abi.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NAPI_CALL(env, call)                                                                       \
    do {                                                                                           \
        if ((call) != napi_ok) {                                                                   \
            const napi_extended_error_info *error_details = NULL;                                  \
            (void)napi_get_last_error_info((env), &error_details);                                 \
            napi_throw_error((env), NULL,                                                          \
                             error_details != NULL && error_details->error_message != NULL         \
                                 ? error_details->error_message                                    \
                                 : "Node-API failure");                                            \
            return NULL;                                                                           \
        }                                                                                          \
    } while (0)

static napi_value throw_native_error(napi_env env) {
    napi_throw_error(env, NULL, cgai_abi_last_error());
    return NULL;
}

static char *utf8_argument(napi_env env, napi_value value) {
    size_t length = 0U;
    if (napi_get_value_string_utf8(env, value, NULL, 0U, &length) != napi_ok) {
        napi_throw_type_error(env, NULL, "expected a UTF-8 string");
        return NULL;
    }
    char *text = (char *)malloc(length + 1U);
    if (text == NULL) {
        napi_throw_error(env, NULL, "could not allocate string argument");
        return NULL;
    }
    if (napi_get_value_string_utf8(env, value, text, length + 1U, &length) != napi_ok) {
        free(text);
        napi_throw_type_error(env, NULL, "could not read UTF-8 string");
        return NULL;
    }
    return text;
}

static int optional_uint32(napi_env env, napi_value object, const char *name, uint32_t *output) {
    bool present = false;
    if (napi_has_named_property(env, object, name, &present) != napi_ok || !present) {
        return 1;
    }
    napi_value value;
    return napi_get_named_property(env, object, name, &value) == napi_ok &&
           napi_get_value_uint32(env, value, output) == napi_ok;
}

static int optional_seed(napi_env env, napi_value object, uint64_t *output) {
    bool present = false;
    if (napi_has_named_property(env, object, "seed", &present) != napi_ok || !present) {
        return 1;
    }
    napi_value value;
    bool lossless = false;
    return napi_get_named_property(env, object, "seed", &value) == napi_ok &&
           napi_get_value_bigint_uint64(env, value, output, &lossless) == napi_ok && lossless;
}

static napi_value abi_version(napi_env env, napi_callback_info info) {
    (void)info;
    napi_value result;
    NAPI_CALL(env, napi_create_uint32(env, cgai_abi_version(), &result));
    return result;
}

static napi_value library_version(napi_env env, napi_callback_info info) {
    (void)info;
    napi_value result;
    NAPI_CALL(env,
              napi_create_string_utf8(env, cgai_abi_library_version(), NAPI_AUTO_LENGTH, &result));
    return result;
}

static napi_value persistence_schema(napi_env env, napi_callback_info info) {
    (void)info;
    napi_value result;
    NAPI_CALL(env, napi_create_string_utf8(env, cgai_abi_persistence_schema_json(),
                                           NAPI_AUTO_LENGTH, &result));
    return result;
}

static napi_value export_model(napi_env env, cgai_abi_model *model) {
    cgai_abi_buffer buffer = {0};
    if (cgai_abi_model_export(model, &buffer) != CGAI_ABI_OK) {
        return throw_native_error(env);
    }
    void *data = NULL;
    napi_value result;
    const napi_status status = napi_create_buffer(env, buffer.size, &data, &result);
    if (status != napi_ok) {
        cgai_abi_buffer_free(&buffer);
        NAPI_CALL(env, status);
    }
    memcpy(data, buffer.data, buffer.size);
    cgai_abi_buffer_free(&buffer);
    if (data == NULL) {
        return throw_native_error(env);
    }
    return result;
}

static napi_value train_model(napi_env env, napi_callback_info info) {
    size_t argc = 2U;
    napi_value argv[2];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
    if (argc < 1U) {
        napi_throw_type_error(env, NULL, "trainModel requires training text");
        return NULL;
    }
    char *text = utf8_argument(env, argv[0]);
    if (text == NULL) {
        return NULL;
    }
    cgai_abi_config config;
    if (cgai_abi_default_config(&config) != CGAI_ABI_OK) {
        free(text);
        return throw_native_error(env);
    }
    if (argc >= 2U && (!optional_uint32(env, argv[1], "dimensions", &config.dimensions) ||
                       !optional_uint32(env, argv[1], "centroidCount", &config.centroid_count) ||
                       !optional_uint32(env, argv[1], "contextWindow", &config.context_window) ||
                       !optional_seed(env, argv[1], &config.seed))) {
        free(text);
        napi_throw_type_error(env, NULL, "invalid native model configuration");
        return NULL;
    }
    cgai_abi_model *model = NULL;
    if (cgai_abi_model_create(&config, &model) != CGAI_ABI_OK ||
        cgai_abi_model_train(model, text) != CGAI_ABI_OK) {
        free(text);
        cgai_abi_model_destroy(model);
        return throw_native_error(env);
    }
    free(text);
    napi_value result = export_model(env, model);
    cgai_abi_model_destroy(model);
    return result;
}

static int import_buffer(napi_env env, napi_value value, cgai_abi_model **model) {
    bool is_buffer = false;
    void *data = NULL;
    size_t size = 0U;
    if (napi_is_buffer(env, value, &is_buffer) != napi_ok || !is_buffer ||
        napi_get_buffer_info(env, value, &data, &size) != napi_ok) {
        napi_throw_type_error(env, NULL, "expected a model Buffer");
        return 0;
    }
    if (cgai_abi_model_import((const uint8_t *)data, size, model) != CGAI_ABI_OK) {
        (void)throw_native_error(env);
        return 0;
    }
    return 1;
}

static void set_uint32(napi_env env, napi_value object, const char *name, uint32_t value) {
    napi_value item;
    (void)napi_create_uint32(env, value, &item);
    (void)napi_set_named_property(env, object, name, item);
}

static void set_bigint(napi_env env, napi_value object, const char *name, uint64_t value) {
    napi_value item;
    (void)napi_create_bigint_uint64(env, value, &item);
    (void)napi_set_named_property(env, object, name, item);
}

static napi_value inspect_model(napi_env env, napi_callback_info info) {
    size_t argc = 1U;
    napi_value argv[1];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
    if (argc != 1U) {
        napi_throw_type_error(env, NULL, "inspectModel requires a model Buffer");
        return NULL;
    }
    cgai_abi_model *model = NULL;
    if (!import_buffer(env, argv[0], &model)) {
        return NULL;
    }
    cgai_abi_model_metadata metadata;
    if (cgai_abi_model_get_metadata(model, &metadata) != CGAI_ABI_OK) {
        cgai_abi_model_destroy(model);
        return throw_native_error(env);
    }
    cgai_abi_model_destroy(model);
    napi_value result;
    NAPI_CALL(env, napi_create_object(env, &result));
    set_uint32(env, result, "formatVersion", metadata.format_version);
    set_uint32(env, result, "dimensions", metadata.dimensions);
    set_uint32(env, result, "centroidCount", metadata.centroid_count);
    set_uint32(env, result, "contextWindow", metadata.context_window);
    set_bigint(env, result, "vocabularySize", metadata.vocabulary_size);
    set_bigint(env, result, "examplesSeen", metadata.examples_seen);
    return result;
}

static napi_value generate_model(napi_env env, napi_callback_info info) {
    size_t argc = 5U;
    napi_value argv[5];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
    if (argc < 4U) {
        napi_throw_type_error(env, NULL,
                              "generateModel requires Buffer, prompt, maxTokens, and temperature");
        return NULL;
    }
    cgai_abi_model *model = NULL;
    if (!import_buffer(env, argv[0], &model)) {
        return NULL;
    }
    char *prompt = utf8_argument(env, argv[1]);
    uint32_t max_tokens = 0U;
    double temperature = 0.0;
    uint64_t seed = 0U;
    bool lossless = true;
    const int arguments_ok =
        prompt != NULL && napi_get_value_uint32(env, argv[2], &max_tokens) == napi_ok &&
        napi_get_value_double(env, argv[3], &temperature) == napi_ok &&
        (argc < 5U ||
         (napi_get_value_bigint_uint64(env, argv[4], &seed, &lossless) == napi_ok && lossless));
    if (!arguments_ok || max_tokens > 1000000U) {
        free(prompt);
        cgai_abi_model_destroy(model);
        if (prompt != NULL) {
            napi_throw_type_error(env, NULL, "invalid native generation arguments");
        }
        return NULL;
    }
    const size_t capacity = (size_t)max_tokens * 128U + 1U;
    char *output = (char *)malloc(capacity);
    cgai_abi_buffer generated = {0};
    if (output == NULL || cgai_abi_model_generate(model, prompt, max_tokens, temperature, seed,
                                                  &generated) != CGAI_ABI_OK) {
        free(output);
        free(prompt);
        cgai_abi_model_destroy(model);
        return output == NULL ? (napi_throw_error(env, NULL, "could not allocate native output"),
                                 (napi_value)NULL)
                              : throw_native_error(env);
    }
    memcpy(output, generated.data, generated.size + 1U);
    cgai_abi_buffer_free(&generated);
    napi_value result;
    const napi_status status = napi_create_string_utf8(env, output, strlen(output), &result);
    free(output);
    free(prompt);
    cgai_abi_model_destroy(model);
    if (status != napi_ok) {
        napi_throw_error(env, NULL, "could not create generated JavaScript string");
        return NULL;
    }
    return result;
}

NAPI_MODULE_INIT() {
    const napi_property_descriptor properties[] = {
        {"abiVersion", NULL, abi_version, NULL, NULL, NULL, napi_default, NULL},
        {"libraryVersion", NULL, library_version, NULL, NULL, NULL, napi_default, NULL},
        {"persistenceSchema", NULL, persistence_schema, NULL, NULL, NULL, napi_default, NULL},
        {"trainModel", NULL, train_model, NULL, NULL, NULL, napi_default, NULL},
        {"inspectModel", NULL, inspect_model, NULL, NULL, NULL, napi_default, NULL},
        {"generateModel", NULL, generate_model, NULL, NULL, NULL, napi_default, NULL},
    };
    NAPI_CALL(env, napi_define_properties(env, exports, sizeof(properties) / sizeof(properties[0]),
                                          properties));
    return exports;
}
