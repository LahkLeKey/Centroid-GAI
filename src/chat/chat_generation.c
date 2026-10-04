/** @file chat_generation.c @brief Conversation formatting and neural reply accounting. */
#include "internal/chat_internal.h"
#include "internal/error.h"
#include "neural/neural_generation.h"
#include <math.h>

/** @brief Translate shared numerical stopping into conversation result semantics.
 * @param finish Terminal encoded-generation condition.
 * @return Corresponding conversation finish reason. */
static cgai_chat_finish_reason reply_finish(cgai_neural_encoded_finish finish) {
    if (finish == CGAI_NEURAL_ENCODED_EOS)
        return CGAI_CHAT_FINISH_EOS;
    if (finish == CGAI_NEURAL_ENCODED_REPETITION)
        return CGAI_CHAT_FINISH_REPETITION;
    return CGAI_CHAT_FINISH_LIMIT;
}

/** @brief Format one conversation and generate its bounded reply using shared numerics.
 * @param model Borrowed immutable conversation model.
 * @param messages Borrowed complete dialogue ending in a user question.
 * @param count Number of structured messages.
 * @param requested Borrowed options, or NULL for defaults.
 * @param output Writable destination receiving complete tokens and a final NUL.
 * @param output_size Positive capacity in bytes including NUL.
 * @param result Writable conversation accounting, published only on success.
 * @return OK at EOS, token limit or repetition; ERROR with a diagnostic otherwise. */
cgai_status cgai_chat_reply(const cgai_chat_model *model, const cgai_chat_message *messages,
                            size_t count, const cgai_chat_options *requested, char *output,
                            size_t output_size, cgai_chat_result *result) {
    const cgai_chat_options options = requested ? *requested : cgai_chat_default_options();
    if (model == NULL || output == NULL || output_size == 0U || result == NULL ||
        options.max_tokens > 4096U || !isfinite(options.temperature) || options.temperature < 0.0 ||
        options.temperature > 100.0)
        return cgai_fail("invalid chat generation arguments");
    cgai_chat_prompt_data prompt = {0};
    if (!cgai_chat_format(model, messages, count, &prompt))
        return CGAI_STATUS_ERROR;
    output[0] = '\0';
    cgai_token_id context[CGAI_NEURAL_MAX_CONTEXT];
    cgai_chat_context(model, &prompt, NULL, 0U, context);
    const cgai_neural_encoded_request request = {context,
                                                 model->config.prompt_window,
                                                 options.max_tokens,
                                                 options.temperature,
                                                 options.seed ? options.seed : model->config.seed,
                                                 output,
                                                 output_size};
    cgai_neural_encoded_result generated = {0};
    const cgai_status status = cgai_neural_generate_encoded(model->network, &request, &generated);
    if (status) {
        *result = (cgai_chat_result){
            generated.generated_tokens,     prompt.count,    prompt.dropped,         prompt.unknown,
            reply_finish(generated.finish), prompt.evidence, prompt.dropped_evidence};
    }
    return status;
}
