/** @file node_training.c @brief JavaScript model training callback. */

#include "node_arguments.h"
#include "node_artifact.h"
#include "node_callbacks.h"
#include "node_error.h"
#include <stdlib.h>

/**
 * @brief Create a temporary model, train it, and return a serialized JavaScript Buffer.
 *
 * Both text and config are borrowed. The temporary model belongs to this function and is destroyed
 * whether training or export fails. JavaScript receives copied artifact bytes, not the model
 * pointer, so no native model lifetime extends past this synchronous callback.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param text Borrowed NUL-terminated training text.
 * @param config Borrowed valid-layout ABI configuration.
 * @return JavaScript artifact Buffer, or NULL after a native/Node error.
 */
static napi_value train_text(napi_env env, const char *text, const cgai_abi_config *config) {
    /* Step 1: Initialize ownership before constructing and training the model. */
    cgai_abi_model *model = NULL;
    if (cgai_abi_model_create(config, &model) != CGAI_ABI_OK ||
        cgai_abi_model_train(model, text) != CGAI_ABI_OK) {
        cgai_abi_model_destroy(model);
        return cgai_node_native_error(env);
    }
    /* Step 2: Export learned state into a JavaScript-owned Buffer. */
    napi_value result = cgai_node_export(env, model);
    /* Step 3: Release the temporary model after export, regardless of the returned Buffer result.
     */
    cgai_abi_model_destroy(model);
    return result;
}

/**
 * @brief Prepare defaults and copied text before delegating training.
 *
 * Configuration parsing precedes text allocation so an invalid option needs no string cleanup.
 * A successful text conversion transfers a malloc allocation to this helper, which frees it after
 * training regardless of the result. Earlier JavaScript values remain borrowed from the callback.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param argc Available argument count; at least one text argument is required by the caller.
 * @param argv Borrowed arguments: text at zero, optional configuration at one.
 * @return JavaScript artifact Buffer, or NULL with an exception requested by the failed phase.
 */
static napi_value train_arguments(napi_env env, size_t argc, const napi_value *argv) {
    /* Step 1: Obtain ABI defaults, including its structure-size and version fields. */
    cgai_abi_config config;
    if (cgai_abi_default_config(&config) != CGAI_ABI_OK) {
        return cgai_node_native_error(env);
    }
    /* Step 2: Apply optional configuration overrides before allocating text. */
    if (argc >= 2U && !cgai_node_config(env, argv[1], &config)) {
        return NULL;
    }
    /* Step 3: Copy the JavaScript string into owned C-string storage. */
    char *text = cgai_node_utf8_argument(env, argv[0]);
    if (text == NULL) {
        return NULL;
    }
    /* Step 4: Train/export synchronously, then release the copied text before returning. */
    napi_value result = train_text(env, text, &config);
    free(text);
    return result;
}

/**
 * @brief Implement the JavaScript trainModel(text, config?) callback.
 *
 * napi_get_cb_info reads at most the capacity supplied in argc and updates it to the available
 * count. The callback checks the required text argument before indexing argv. Configuration
 * defaults, string ownership, and model lifetime are handled by the focused helper chain.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param info Opaque callback metadata supplied by Node for this invocation.
 * @return JavaScript artifact Buffer, or NULL on invalid arguments or failed training/export.
 */
napi_value cgai_node_train(napi_env env, napi_callback_info info) {
    /* Step 1: Reserve slots for text and optional configuration, then read callback arguments. */
    size_t argc = 2U;
    napi_value argv[2];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
    /* Step 2: Reject a missing required text value before reading argv[0]. */
    if (argc < 1U) {
        (void)napi_throw_type_error(env, NULL, "trainModel requires training text");
        return NULL;
    }
    /* Step 3: Delegate conversion, model lifetime, and artifact export to the argument helper. */
    return train_arguments(env, argc, argv);
}
