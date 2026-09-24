/** @file node_generation.c @brief JavaScript generation arguments and buffer lifetime. */

#include "node_arguments.h"
#include "node_artifact.h"
#include "node_callbacks.h"
#include "node_error.h"
#include <stdlib.h>

/**
 * @brief Native scalar settings copied from JavaScript generation arguments.
 *
 * This value owns no prompt, Buffer, or model storage. Zero initialization supplies
 * the omitted seed default; conversion fills the required numeric settings before
 * artifact import and prompt allocation begin.
 */
typedef struct generation_options {
    uint32_t max_tokens; /**< Converted token count, bounded by the Node argument parser. */
    double temperature;  /**< Parsed double; final finite/nonnegative validation belongs to core. */
    uint64_t seed;       /**< Losslessly converted BigInt, or zero when omitted. */
} generation_options;

/**
 * @brief Read numeric generation options from positional JavaScript arguments.
 *
 * The caller has already established at least four arguments. A fifth seed must be a BigInt that
 * fits uint64_t without loss. max_tokens is extracted with Node's uint32 conversion and then
 * capped. The core later rejects negative/nonfinite temperature. Options can be partially filled on
 * failure.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param argc Argument count, at least four and at most the callback's five-slot capacity.
 * @param argv Borrowed vector containing numeric values at indices two, three, and optionally four.
 * @param options Non-NULL zero-initialized destination for numeric settings.
 * @return One for accepted conversions/cap, otherwise zero after requesting a TypeError.
 */
static int generation_arguments(napi_env env, size_t argc, const napi_value *argv,
                                generation_options *options) {
    /* Step 1: Prepare the seed-conversion flag before evaluating optional BigInt input. */
    bool lossless = true;
    /* Step 2: Read the token limit and temperature, validate any seed, and enforce the binding's
     * token cap. */
    if (napi_get_value_uint32(env, argv[2], &options->max_tokens) != napi_ok ||
        napi_get_value_double(env, argv[3], &options->temperature) != napi_ok ||
        (argc >= 5U &&
         (napi_get_value_bigint_uint64(env, argv[4], &options->seed, &lossless) != napi_ok ||
          !lossless)) ||
        options->max_tokens > 1000000U) {
        (void)napi_throw_type_error(env, NULL, "invalid native generation arguments");
        return 0;
    }
    /* Step 3: Allow execution only after the full numeric conversion condition succeeds. */
    return 1;
}

/**
 * @brief Generate ABI-owned text, copy it to JavaScript, then release native bytes.
 *
 * Unlike artifact bytes, generated text is passed to Node's UTF-8 string constructor. The explicit
 * size excludes the trailing NUL, avoiding a second strlen scan. The ABI buffer is released even
 * if Node cannot allocate the JavaScript string. The model and prompt remain borrowed.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param model Non-NULL borrowed trained ABI model.
 * @param prompt Borrowed NUL-terminated native prompt.
 * @param options Non-NULL borrowed numeric settings accepted by the argument parser.
 * @return JavaScript continuation String, or NULL after an ABI/Node error.
 */
static napi_value generate_text(napi_env env, const cgai_abi_model *model, const char *prompt,
                                const generation_options *options) {
    /* Step 1: Start with an empty descriptor and request ABI-owned generated text. */
    cgai_abi_buffer generated = {0};
    if (cgai_abi_model_generate(model, prompt, options->max_tokens, options->temperature,
                                options->seed, &generated) != CGAI_ABI_OK) {
        return cgai_node_native_error(env);
    }
    /* Step 2: Copy exactly the generated text bytes into a Node-managed string. */
    napi_value result = NULL;
    const napi_status status =
        napi_create_string_utf8(env, (const char *)generated.data, generated.size, &result);
    /* Step 3: Release native text before translating the string-construction status. */
    cgai_abi_buffer_free(&generated);
    return cgai_node_check(env, status) ? result : NULL;
}

/**
 * @brief Own the imported model and copied prompt for one generation request.
 *
 * Both resources are local to this helper. If prompt conversion fails after import, the model is
 * still destroyed on the shared cleanup path. JavaScript receives a copied string, so freeing the
 * prompt and model after generate_text returns does not invalidate a successful result.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param argv Borrowed arguments with artifact Buffer at zero and prompt at one.
 * @param options Borrowed settings already accepted by generation_arguments().
 * @return JavaScript continuation String, or NULL when import, conversion, or generation fails.
 */
static napi_value generate_artifact(napi_env env, const napi_value *argv,
                                    const generation_options *options) {
    /* Step 1: Initialize model ownership and decode the supplied Buffer. */
    cgai_abi_model *model = NULL;
    if (!cgai_node_import(env, argv[0], &model)) {
        return NULL;
    }
    /* Step 2: Copy the prompt and generate only if the copy succeeded. */
    char *prompt = cgai_node_utf8_argument(env, argv[1]);
    napi_value result = prompt != NULL ? generate_text(env, model, prompt, options) : NULL;
    /* Step 3: Release the prompt and imported model before returning the independently managed
     * result. */
    free(prompt);
    cgai_abi_model_destroy(model);
    return result;
}

/**
 * @brief Implement JavaScript generateModel(Buffer, prompt, maxTokens, temperature, seed?).
 *
 * The callback reserves five argument handles, checks the four required positions, and separates
 * numeric conversion from model import. All native allocations belong to downstream helpers and
 * are released before this callback returns. Seed zero delegates to the model's configured seed.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param info Opaque callback metadata supplied by Node for this invocation.
 * @return JavaScript continuation String, or NULL with an argument/native/Node error.
 */
napi_value cgai_node_generate(napi_env env, napi_callback_info info) {
    /* Step 1: Read up to five borrowed JavaScript arguments into local handles. */
    size_t argc = 5U;
    napi_value argv[5];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
    /* Step 2: Reject a request missing any required positional argument. */
    if (argc < 4U) {
        (void)napi_throw_type_error(
            env, NULL, "generateModel requires Buffer, prompt, maxTokens, and temperature");
        return NULL;
    }
    /* Step 3: Zero-initialize default seed and execute only after numeric conversion succeeds. */
    generation_options options = {0};
    return generation_arguments(env, argc, argv, &options) ? generate_artifact(env, argv, &options)
                                                           : NULL;
}
