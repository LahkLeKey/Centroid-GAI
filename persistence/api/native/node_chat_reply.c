/** @file node_chat_reply.c @brief Bounded conversation generation callback. */
#include "node_chat.h"
#include <math.h>
#include <stdlib.h>

/** @brief Convert generation options before narrowing integers.
 * @param env Borrowed runtime.
 * @param value Borrowed Float64Array.
 * @param seed Borrowed BigInt.
 * @param options Writable options.
 * @return Nonzero on valid settings. */
static int reply_options(napi_env env, napi_value value, napi_value seed,
                         cgai_chat_options *options) {
    double numbers[2];
    if (!cgai_node_chat_numbers(env, value, numbers, 2U) || numbers[0] < 0.0 ||
        numbers[0] > 4096.0 || floor(numbers[0]) != numbers[0] || numbers[1] < 0.0 ||
        numbers[1] > 100.0) {
        cgai_node_chat_error(env, "invalid chat reply settings");
        return 0;
    }
    options->max_tokens = (size_t)numbers[0];
    options->temperature = numbers[1];
    return cgai_node_chat_seed(env, seed, &options->seed);
}

/** @brief Publish successful reply text and accounting.
 * @param env Borrowed runtime.
 * @param content Borrowed terminated output.
 * @param usage Borrowed successful result.
 * @return Runtime object or NULL. */
static napi_value reply_result(napi_env env, const char *content, const cgai_chat_result *usage) {
    napi_value result;
    const char *finish = usage->finish_reason == CGAI_CHAT_FINISH_EOS          ? "eos"
                         : usage->finish_reason == CGAI_CHAT_FINISH_REPETITION ? "repetition"
                                                                               : "length";
    if (napi_create_object(env, &result) != napi_ok ||
        !cgai_node_chat_string(env, result, "content", content) ||
        !cgai_node_chat_string(env, result, "finishReason", finish) ||
        !cgai_node_chat_number(env, result, "generatedTokens", (double)usage->generated_tokens) ||
        !cgai_node_chat_number(env, result, "promptTokens", (double)usage->prompt_tokens) ||
        !cgai_node_chat_number(env, result, "droppedMessages", (double)usage->dropped_messages) ||
        !cgai_node_chat_number(env, result, "unknownTokens", (double)usage->unknown_tokens) ||
        !cgai_node_chat_number(env, result, "evidenceTokens", (double)usage->evidence_tokens) ||
        !cgai_node_chat_number(env, result, "droppedEvidence", (double)usage->dropped_evidence))
        return NULL;
    return result;
}

/** @brief Copy a prompt and execute bounded generation on a validated model.
 * @param env Borrowed runtime.
 * @param model Borrowed decoded model.
 * @param value Borrowed JavaScript messages.
 * @param options Borrowed validated generation settings.
 * @return Runtime reply or NULL with exception. */
static napi_value run_reply(napi_env env, const cgai_chat_model *model, napi_value value,
                            const cgai_chat_options *options) {
    cgai_node_chat_messages messages = {0};
    size_t budget = 16777216U;
    char *output = malloc(4194304U);
    cgai_chat_result usage = {0};
    napi_value result = NULL;
    if (output != NULL && cgai_node_chat_read_messages(env, value, &messages, &budget) &&
        cgai_chat_reply(model, messages.items, messages.count, options, output, 4194304U, &usage))
        result = reply_result(env, output, &usage);
    if (result == NULL)
        cgai_node_chat_error(env, NULL);
    free(output);
    cgai_node_chat_free_messages(&messages);
    return result;
}

napi_value cgai_node_chat_reply(napi_env env, napi_callback_info info) {
    size_t count = 4U;
    napi_value args[4];
    cgai_chat_options options;
    if (napi_get_cb_info(env, info, &count, args, NULL, NULL) != napi_ok || count != 4U ||
        !reply_options(env, args[2], args[3], &options))
        return cgai_node_chat_error(env, "invalid chat reply arguments");
    cgai_chat_model *model = cgai_node_chat_import(env, args[0]);
    if (model == NULL)
        return NULL;
    napi_value result = run_reply(env, model, args[1], &options);
    cgai_chat_destroy(model);
    return result;
}
