/** @file node_chat.c @brief Native conversation artifact conversion and measured results. */
#include "node_chat.h"
#include <stdio.h>
#include <stdlib.h>

cgai_chat_model *cgai_node_chat_import(napi_env env, napi_value value) {
    bool buffer = false;
    void *data = NULL;
    size_t size = 0U;
    if (napi_is_buffer(env, value, &buffer) != napi_ok || !buffer ||
        napi_get_buffer_info(env, value, &data, &size) != napi_ok) {
        cgai_node_chat_error(env, "chat artifact must be a Buffer");
        return NULL;
    }
    cgai_chat_model *model = cgai_chat_decode(data, size);
    if (model == NULL)
        cgai_node_chat_error(env, NULL);
    return model;
}

napi_value cgai_node_chat_export(napi_env env, const cgai_chat_model *model) {
    uint8_t *data = NULL;
    size_t size = 0U;
    napi_value result = NULL;
    if (!cgai_chat_encode(model, &data, &size))
        return cgai_node_chat_error(env, NULL);
    const napi_status status = napi_create_buffer_copy(env, size, data, NULL, &result);
    cgai_chat_buffer_free(data);
    return status == napi_ok ? result : cgai_node_chat_error(env, "could not copy chat artifact");
}

int cgai_node_chat_number(napi_env env, napi_value object, const char *name, double number) {
    napi_value value;
    if (napi_create_double(env, number, &value) == napi_ok &&
        napi_set_named_property(env, object, name, value) == napi_ok)
        return 1;
    cgai_node_chat_error(env, "could not create chat number");
    return 0;
}

int cgai_node_chat_string(napi_env env, napi_value object, const char *name, const char *text) {
    napi_value value;
    if (napi_create_string_utf8(env, text, NAPI_AUTO_LENGTH, &value) == napi_ok &&
        napi_set_named_property(env, object, name, value) == napi_ok)
        return 1;
    cgai_node_chat_error(env, "could not create chat string");
    return 0;
}

napi_value cgai_node_chat_metrics(napi_env env, const cgai_neural_metrics *metrics) {
    napi_value result;
    if (napi_create_object(env, &result) != napi_ok ||
        !cgai_node_chat_number(env, result, "tokens", (double)metrics->tokens) ||
        !cgai_node_chat_number(env, result, "unknownTokens", (double)metrics->unknown_tokens) ||
        !cgai_node_chat_number(env, result, "crossEntropy", metrics->cross_entropy) ||
        !cgai_node_chat_number(env, result, "perplexity", metrics->perplexity) ||
        !cgai_node_chat_number(env, result, "accuracy", metrics->accuracy))
        return NULL;
    return result;
}

/** @brief Create JSON metadata from validated native dimensions.
 * @param env Borrowed runtime.
 * @param model Borrowed validated model.
 * @return Runtime string or NULL with exception. */
static napi_value metadata(napi_env env, const cgai_chat_model *model) {
    cgai_chat_config config;
    size_t vocabulary = 0U, parameters = 0U;
    if (!cgai_chat_metadata(model, &config, &vocabulary, &parameters))
        return cgai_node_chat_error(env, NULL);
    char json[1024];
    const int length = snprintf(
        json, sizeof(json),
        "{\"engineKind\":\"neural-centroid-chat\",\"formatVersion\":%u,\"protocolVersion\":%u,"
        "\"tokenizerVersion\":1,\"vocabularySize\":%zu,\"parameterCount\":%zu,\"config\":{"
        "\"embeddingDimensions\":%zu,\"hiddenDimensions\":%zu,\"centroidCount\":%zu,"
        "\"promptWindow\":%zu,\"responseWindow\":%zu,\"evidenceWindow\":%zu,\"routingTemperature\":"
        "%.17g,\"seed\":\"%"
        "llu\"}}",
        cgai_chat_protocol_version(model), cgai_chat_protocol_version(model), vocabulary,
        parameters, config.embedding_dimensions, config.hidden_dimensions, config.centroid_count,
        config.prompt_window, config.response_window, config.evidence_window,
        config.routing_temperature, (unsigned long long)config.seed);
    napi_value result;
    if (length < 0 || (size_t)length >= sizeof(json) ||
        napi_create_string_utf8(env, json, (size_t)length, &result) != napi_ok)
        return cgai_node_chat_error(env, "could not create chat metadata");
    return result;
}

napi_value cgai_node_chat_inspect(napi_env env, napi_callback_info info) {
    size_t count = 1U;
    napi_value args[1];
    if (napi_get_cb_info(env, info, &count, args, NULL, NULL) != napi_ok || count != 1U)
        return cgai_node_chat_error(env, "expected chat artifact");
    cgai_chat_model *model = cgai_node_chat_import(env, args[0]);
    if (model == NULL)
        return NULL;
    napi_value result = metadata(env, model);
    cgai_chat_destroy(model);
    return result;
}

/** @brief Package a trained artifact with before/after training measurements.
 * @param env Borrowed runtime.
 * @param model Borrowed trained model.
 * @param before Borrowed initial metrics.
 * @param after Borrowed final metrics.
 * @return Runtime object or NULL. */
static napi_value training_result(napi_env env, const cgai_chat_model *model,
                                  const cgai_neural_metrics *before,
                                  const cgai_neural_metrics *after) {
    napi_value result;
    napi_value payload = cgai_node_chat_export(env, model);
    napi_value first = cgai_node_chat_metrics(env, before);
    napi_value last = cgai_node_chat_metrics(env, after);
    if (payload == NULL || first == NULL || last == NULL ||
        napi_create_object(env, &result) != napi_ok ||
        napi_set_named_property(env, result, "payload", payload) != napi_ok ||
        napi_set_named_property(env, result, "before", first) != napi_ok ||
        napi_set_named_property(env, result, "after", last) != napi_ok)
        return cgai_node_chat_error(env, "could not create training result");
    return result;
}

napi_value cgai_node_chat_train(napi_env env, napi_callback_info info) {
    size_t count = 3U;
    napi_value args[3];
    cgai_chat_config config;
    cgai_neural_training training;
    if (napi_get_cb_info(env, info, &count, args, NULL, NULL) != napi_ok || count != 3U ||
        !cgai_node_chat_settings(env, args[1], args[2], &config, &training))
        return cgai_node_chat_error(env, "invalid chat training arguments");
    cgai_node_chat_examples examples = {0};
    const int copied = cgai_node_chat_read_examples(env, args[0], &examples);
    cgai_chat_model *model =
        copied ? cgai_chat_create(&config, examples.items, examples.count) : NULL;
    cgai_neural_metrics before = {0}, after = {0};
    napi_value result = NULL;
    if (model != NULL && cgai_chat_evaluate(model, examples.items, examples.count, &before) &&
        cgai_chat_train(model, examples.items, examples.count, &training) &&
        cgai_chat_evaluate(model, examples.items, examples.count, &after))
        result = training_result(env, model, &before, &after);
    cgai_chat_destroy(model);
    cgai_node_chat_free_examples(&examples);
    return result != NULL ? result : cgai_node_chat_error(env, NULL);
}

napi_value cgai_node_chat_evaluate(napi_env env, napi_callback_info info) {
    size_t count = 2U;
    napi_value args[2];
    if (napi_get_cb_info(env, info, &count, args, NULL, NULL) != napi_ok || count != 2U)
        return cgai_node_chat_error(env, "expected artifact and examples");
    cgai_chat_model *model = cgai_node_chat_import(env, args[0]);
    if (model == NULL)
        return NULL;
    cgai_node_chat_examples examples = {0};
    cgai_neural_metrics metrics = {0};
    napi_value result = NULL;
    if (cgai_node_chat_read_examples(env, args[1], &examples) &&
        cgai_chat_evaluate(model, examples.items, examples.count, &metrics))
        result = cgai_node_chat_metrics(env, &metrics);
    cgai_node_chat_free_examples(&examples);
    cgai_chat_destroy(model);
    return result != NULL ? result : cgai_node_chat_error(env, NULL);
}
