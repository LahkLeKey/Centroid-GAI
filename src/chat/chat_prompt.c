/** @file chat_prompt.c @brief Shared structural formatting with complete-turn truncation. */
#include "internal/chat_internal.h"
#include "internal/error.h"
#include <stdlib.h>
#include <string.h>

/** @brief Bound aggregate prompt text before inspecting dialogue roles.
 * @param messages Borrowed message table.
 * @param count Valid table length.
 * @return OK or ERROR for absent or excessive content. */
static cgai_status validate_text(const cgai_chat_message *messages, size_t count) {
    size_t bytes = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (messages[i].content == NULL)
            return cgai_fail("missing chat message content");
        bytes += strlen(messages[i].content);
        if (bytes > CGAI_CHAT_MAX_TEXT_BYTES)
            return cgai_fail("chat prompt exceeds text budget");
    }
    return CGAI_STATUS_OK;
}

cgai_status cgai_chat_validate_messages(const cgai_chat_message *messages, size_t count) {
    if (messages == NULL || count == 0U || count > CGAI_CHAT_MAX_MESSAGES)
        return cgai_fail("chat requires 1..1024 messages");
    if (!validate_text(messages, count))
        return CGAI_STATUS_ERROR;
    cgai_chat_role expected = CGAI_CHAT_USER;
    for (size_t i = 0U; i < count; ++i) {
        if (messages[i].role == CGAI_CHAT_EVIDENCE && expected == CGAI_CHAT_USER)
            continue;
        if (messages[i].role != expected)
            return cgai_fail("chat history must contain complete alternating turns");
        expected = expected == CGAI_CHAT_USER ? CGAI_CHAT_ASSISTANT : CGAI_CHAT_USER;
    }
    return messages[count - 1U].role == CGAI_CHAT_USER
               ? CGAI_STATUS_OK
               : cgai_fail("chat history must end with the current user question");
}

/** @brief Encode one message as role, lexical data and boundary.
 * @param model Borrowed network.
 * @param message Borrowed structured content.
 * @param count Writable encoded size.
 * @param unknown Writable lexical unknown count.
 * @return Owned IDs or NULL. */
static cgai_token_id *encode_message(const cgai_chat_model *model, const cgai_chat_message *message,
                                     size_t *count, size_t *unknown) {
    cgai_token_id *words = cgai_neural_sequence(model->network, message->content, count, unknown);
    if (words == NULL)
        return NULL;
    cgai_token_id *encoded = realloc(words, (*count + 1U) * sizeof(*words));
    if (encoded == NULL) {
        free(words);
        cgai_fail("could not allocate chat message");
        return NULL;
    }
    memmove(encoded + 1U, encoded, (*count - 1U) * sizeof(*encoded));
    encoded[0] = cgai_token_id_from_size(model->network->output_size + (size_t)message->role);
    encoded[*count] = cgai_token_id_from_size(model->network->output_size + 3U);
    ++*count;
    return encoded;
}

/** @brief Copy an encoded group into proven available prompt capacity.
 * @param prompt Mutable bounded prompt.
 * @param ids Borrowed two-element ID pointer table.
 * @param sizes Borrowed two-element lengths.
 * @param unknown Borrowed two-element unknown counts. */
static void insert_group(cgai_chat_prompt_data *prompt, cgai_token_id *const *ids,
                         const size_t *sizes, const size_t *unknown) {
    const size_t total = sizes[0] + sizes[1];
    memmove(prompt->tokens + total, prompt->tokens, prompt->count * sizeof(*prompt->tokens));
    memcpy(prompt->tokens, ids[0], sizes[0] * sizeof(*ids[0]));
    if (ids[1] != NULL)
        memcpy(prompt->tokens + sizes[0], ids[1], sizes[1] * sizeof(*ids[1]));
    prompt->count += total;
    prompt->unknown += unknown[0] + unknown[1];
}

/** @brief Prepend a whole message or whole user/assistant pair when it fits.
 * @param model Borrowed model.
 * @param messages Borrowed one or two messages.
 * @param count Message count.
 * @param prompt Mutable local prompt.
 * @return OK or ERROR; oversized history is counted as dropped. */
static cgai_status prepend_group(const cgai_chat_model *model, const cgai_chat_message *messages,
                                 size_t count, cgai_chat_prompt_data *prompt) {
    cgai_token_id *ids[2] = {NULL, NULL};
    size_t sizes[2] = {0U, 0U}, unknown[2] = {0U, 0U};
    int ok = 1;
    for (size_t i = 0U; i < count; ++i) {
        ids[i] = encode_message(model, &messages[i], &sizes[i], &unknown[i]);
        ok = ok && ids[i] != NULL;
    }
    const size_t total = sizes[0] + sizes[1];
    if (ok && total <= model->config.prompt_window - prompt->count) {
        insert_group(prompt, ids, sizes, unknown);
    } else if (ok)
        prompt->dropped += count;
    free(ids[0]);
    free(ids[1]);
    return ok ? CGAI_STATUS_OK : CGAI_STATUS_ERROR;
}

/** @brief Apply a separate evidence budget to one complete history group.
 * @param model Borrowed model.
 * @param messages Borrowed one or two messages.
 * @param count Group size.
 * @param prompt Mutable prompt.
 * @param evidence Running retained evidence slots.
 * @return OK or ERROR on encoding failure. */
static cgai_status admit_history(const cgai_chat_model *model, const cgai_chat_message *messages,
                                 size_t count, cgai_chat_prompt_data *prompt, size_t *evidence) {
    cgai_chat_prompt_data candidate = *prompt;
    if (!prepend_group(model, messages, count, &candidate))
        return CGAI_STATUS_ERROR;
    const size_t added = candidate.count - prompt->count;
    if (count == 1U && *evidence + added > model->config.prompt_window / 4U)
        ++prompt->dropped;
    else {
        *prompt = candidate;
        if (count == 1U)
            *evidence += added;
    }
    return CGAI_STATUS_OK;
}

/** @brief Admit history newest-first in indivisible groups.
 * @param model Borrowed shape.
 * @param messages Borrowed history and current question.
 * @param count Message count.
 * @param prompt Mutable prompt already containing the question.
 * @return OK or ERROR. */
static cgai_status format_history(const cgai_chat_model *model, const cgai_chat_message *messages,
                                  size_t count, cgai_chat_prompt_data *prompt) {
    size_t evidence = 0U;
    for (size_t end = count - 1U; end > 0U;) {
        const size_t group = messages[end - 1U].role == CGAI_CHAT_ASSISTANT ? 2U : 1U;
        end -= group;
        if (!admit_history(model, &messages[end], group, prompt, &evidence))
            return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

cgai_status cgai_chat_format(const cgai_chat_model *model, const cgai_chat_message *messages,
                             size_t count, cgai_chat_prompt_data *prompt) {
    if (model == NULL || prompt == NULL || !cgai_chat_validate_messages(messages, count))
        return cgai_fail("invalid chat prompt");
    cgai_chat_prompt_data formatted = {0};
    /* Step 1: Reserve the assistant start and retain the complete current question. */
    formatted.tokens[0] =
        cgai_token_id_from_size(model->network->output_size + CGAI_CHAT_ASSISTANT);
    formatted.count = 1U;
    if (!prepend_group(model, &messages[count - 1U], 1U, &formatted))
        return CGAI_STATUS_ERROR;
    if (formatted.dropped != 0U)
        return cgai_fail("current question exceeds prompt window");
    /* Step 2: Add bounded older groups and publish only after complete formatting. */
    if (!format_history(model, messages, count, &formatted))
        return CGAI_STATUS_ERROR;
    *prompt = formatted;
    return CGAI_STATUS_OK;
}

void cgai_chat_context(const cgai_chat_model *model, const cgai_chat_prompt_data *prompt,
                       const cgai_token_id *answer, size_t position, cgai_token_id *context) {
    /* Step 1: The prompt never shifts when the answer grows. */
    const size_t padding = model->config.prompt_window - prompt->count;
    for (size_t i = 0U; i < padding; ++i)
        context[i] = cgai_token_id_from_size(CGAI_TOKEN_BOS);
    memcpy(context + padding, prompt->tokens, prompt->count * sizeof(*context));
    /* Step 2: Only the answer suffix rolls, excluding the current prediction target. */
    const size_t window = model->config.response_window;
    for (size_t i = 0U; i < window; ++i)
        context[model->config.prompt_window + i] = position + i < window
                                                       ? cgai_token_id_from_size(CGAI_TOKEN_BOS)
                                                       : answer[position + i - window];
}
