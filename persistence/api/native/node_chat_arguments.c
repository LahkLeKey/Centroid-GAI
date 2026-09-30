/** @file node_chat_arguments.c @brief Bounded copies of structured JavaScript dialogue inputs. */
#include "node_chat.h"
#include "node_error.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Request a JavaScript exception without replacing an already pending exception.
 * @param env Borrowed runtime.
 * @param message Borrowed diagnostic, or NULL for cgai_last_error().
 * @return NULL, the Node-API error sentinel. */
napi_value cgai_node_chat_error(napi_env env, const char *message) {
    /* Step 1: Preserve failures from property access or earlier Node operations. */
    bool pending = false;
    (void)napi_is_exception_pending(env, &pending);
    if (!pending)
        (void)napi_throw_error(env, NULL, message ? message : cgai_last_error());
    return NULL;
}

/** @brief Validate and measure a bounded nonempty JavaScript array.
 * @param env Borrowed runtime.
 * @param value Borrowed candidate array.
 * @param maximum Maximum permitted element count.
 * @param count Writable native count.
 * @return One on success, zero with exception otherwise. */
static int chat_array(napi_env env, napi_value value, size_t maximum, size_t *count) {
    /* Step 1: Check array identity before extracting a bounded length. */
    bool array = false;
    uint32_t length = 0U;
    if (napi_is_array(env, value, &array) != napi_ok || !array ||
        napi_get_array_length(env, value, &length) != napi_ok || length == 0U || length > maximum) {
        cgai_node_chat_error(env, "expected a nonempty bounded chat array");
        return 0;
    }
    *count = (size_t)length;
    return 1;
}

/** @brief Copy a bounded exact UTF-8 string while rejecting embedded terminators.
 * @param env Borrowed runtime.
 * @param value Borrowed string value.
 * @param budget Writable remaining aggregate allocation budget.
 * @return Owned terminated text, or NULL with an exception; caller frees it. */
static char *chat_text(napi_env env, napi_value value, size_t *budget) {
    /* Step 1: Bound both individual content and aggregate memory before allocating. */
    size_t bytes = 0U;
    if (napi_get_value_string_utf8(env, value, NULL, 0U, &bytes) != napi_ok || bytes == 0U ||
        bytes > 65536U || bytes + 1U > *budget) {
        cgai_node_chat_error(env,
                             "chat text must contain 1..65536 UTF-8 bytes within dataset limit");
        return NULL;
    }
    char *text = malloc(bytes + 1U);
    if (text == NULL) {
        cgai_node_chat_error(env, "could not allocate chat text");
        return NULL;
    }
    /* Step 2: Preserve exact byte boundaries, including embedded-NUL rejection. */
    if (napi_get_value_string_utf8(env, value, text, bytes + 1U, &bytes) != napi_ok ||
        memchr(text, '\0', bytes) != NULL) {
        free(text);
        cgai_node_chat_error(env, "invalid chat text or embedded NUL");
        return NULL;
    }
    *budget -= bytes + 1U;
    return text;
}

/** @brief Read one role/content object into an already zeroed entry.
 * @param env Borrowed runtime.
 * @param value Borrowed message object.
 * @param message Mutable destination owning any copied content.
 * @param budget Writable remaining dataset allocation budget.
 * @return One on success, zero with exception otherwise. */
static int chat_message(napi_env env, napi_value value, cgai_chat_message *message,
                        size_t *budget) {
    /* Step 1: Validate the exact numeric role before copying any text. */
    napi_value role, content;
    double number = 0.0;
    if (napi_get_named_property(env, value, "role", &role) != napi_ok ||
        napi_get_value_double(env, role, &number) != napi_ok || !isfinite(number) || number < 0.0 ||
        number > 2.0 || floor(number) != number ||
        napi_get_named_property(env, value, "content", &content) != napi_ok) {
        cgai_node_chat_error(env, "invalid structured chat message");
        return 0;
    }
    /* Step 2: Publish the role and owned content for unconditional caller cleanup. */
    message->role = (cgai_chat_role)(unsigned)number;
    message->content = chat_text(env, content, budget);
    return message->content != NULL;
}

/** @brief Allocate the bounded message table and debit its shared budget.
 * @param env Borrowed runtime.
 * @param messages Mutable table owner with count populated.
 * @param budget Writable remaining byte budget.
 * @return Nonzero on successful allocation. */
static int allocate_messages(napi_env env, cgai_node_chat_messages *messages, size_t *budget) {
    if (messages->count * sizeof(*messages->items) > *budget) {
        cgai_node_chat_error(env, "chat dataset exceeds allocation budget");
        return 0;
    }
    messages->items = calloc(messages->count, sizeof(*messages->items));
    if (messages->items == NULL) {
        cgai_node_chat_error(env, "could not allocate chat messages");
        return 0;
    }
    *budget -= messages->count * sizeof(*messages->items);
    return 1;
}

/** @brief Copy a JavaScript structured message array within a shared memory budget.
 * @param env Borrowed runtime.
 * @param value Borrowed array of numeric role/content objects.
 * @param messages Zero-initialized mutable owner, including partial results on failure.
 * @param budget Writable remaining allocation bytes.
 * @return One on success, zero with an exception otherwise; caller always destroys owner. */
int cgai_node_chat_read_messages(napi_env env, napi_value value, cgai_node_chat_messages *messages,
                                 size_t *budget) {
    /* Step 1: Prove the message table fits the shared input budget. */
    if (!chat_array(env, value, 1024U, &messages->count))
        return 0;
    if (!allocate_messages(env, messages, budget))
        return 0;
    /* Step 2: Retain partial ownership so the caller can release failed conversions. */
    for (size_t i = 0U; i < messages->count; ++i) {
        napi_value entry;
        if (napi_get_element(env, value, (uint32_t)i, &entry) != napi_ok ||
            !chat_message(env, entry, &messages->items[i], budget))
            return 0;
    }
    return 1;
}

/** @brief Release complete or partial copied messages.
 * @param messages Borrowed initialized owner, reset after release. */
void cgai_node_chat_free_messages(cgai_node_chat_messages *messages) {
    /* Step 1: Strings are independent allocations; release them before their table. */
    if (messages->items != NULL)
        for (size_t i = 0U; i < messages->count; ++i)
            free((void *)messages->items[i].content);
    free(messages->items);
    memset(messages, 0, sizeof(*messages));
}

/** @brief Copy one independent example without joining it to adjacent records.
 * @param env Borrowed runtime.
 * @param value Borrowed example object.
 * @param example Mutable zeroed destination retaining partial ownership.
 * @param budget Writable shared allocation budget.
 * @return One on success, zero with exception otherwise. */
static int chat_example(napi_env env, napi_value value, cgai_chat_example *example,
                        size_t *budget) {
    /* Step 1: Obtain the structured prefix and separate assistant target. */
    napi_value messages, answer;
    if (napi_get_named_property(env, value, "messages", &messages) != napi_ok ||
        napi_get_named_property(env, value, "answer", &answer) != napi_ok) {
        cgai_node_chat_error(env, "chat examples need messages and answer");
        return 0;
    }
    cgai_node_chat_messages copied = {0};
    const int ok = cgai_node_chat_read_messages(env, messages, &copied, budget);
    /* Step 2: Transfer even partial message ownership into the outer example. */
    example->messages = copied.items;
    example->message_count = copied.count;
    if (!ok)
        return 0;
    example->answer = chat_text(env, answer, budget);
    return example->answer != NULL;
}

/** @brief Copy independent training examples with a 16 MiB aggregate allocation budget.
 * @param env Borrowed runtime.
 * @param value Borrowed example array.
 * @param examples Zero-initialized owner, including partial results on failure.
 * @return One on success, zero with an exception otherwise; caller always destroys owner. */
int cgai_node_chat_read_examples(napi_env env, napi_value value,
                                 cgai_node_chat_examples *examples) {
    /* Step 1: Bound the table and reserve the remaining budget for nested strings/arrays. */
    if (!chat_array(env, value, 10000U, &examples->count))
        return 0;
    examples->items = calloc(examples->count, sizeof(*examples->items));
    if (examples->items == NULL) {
        cgai_node_chat_error(env, "could not allocate chat examples");
        return 0;
    }
    size_t budget = 16777216U - examples->count * sizeof(*examples->items);
    /* Step 2: Copy records independently; no caller text is retained after the callback. */
    for (size_t i = 0U; i < examples->count; ++i) {
        napi_value entry;
        if (napi_get_element(env, value, (uint32_t)i, &entry) != napi_ok ||
            !chat_example(env, entry, &examples->items[i], &budget))
            return 0;
    }
    return 1;
}

/** @brief Release complete or partial training examples.
 * @param examples Borrowed initialized owner, reset after release. */
void cgai_node_chat_free_examples(cgai_node_chat_examples *examples) {
    /* Step 1: Destroy each nested prefix and independently copied answer. */
    if (examples->items != NULL) {
        for (size_t i = 0U; i < examples->count; ++i) {
            cgai_node_chat_messages messages = {(cgai_chat_message *)examples->items[i].messages,
                                                examples->items[i].message_count};
            cgai_node_chat_free_messages(&messages);
            free((void *)examples->items[i].answer);
        }
    }
    /* Step 2: Clear outer ownership after its contents are released. */
    free(examples->items);
    memset(examples, 0, sizeof(*examples));
}
