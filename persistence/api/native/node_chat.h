/** @file node_chat.h @brief Owned Node-API conversion and callbacks for native conversations. */
#ifndef CGAI_NODE_CHAT_H
#define CGAI_NODE_CHAT_H
#include "centroid_gai_chat.h"
#include <node_api.h>
/** Owned native copies of a JavaScript message array. */
typedef struct cgai_node_chat_messages {
    cgai_chat_message *items; /**< Owned messages and their independently owned strings. */
    size_t count;             /**< Number of allocated zero-initialized entries. */
} cgai_node_chat_messages;
/** Owned native copies of independent training examples. */
typedef struct cgai_node_chat_examples {
    cgai_chat_example *items; /**< Owned examples, messages and answers. */
    size_t count;             /**< Number of allocated zero-initialized entries. */
} cgai_node_chat_examples;
/** @brief Request a JavaScript exception without replacing an already pending exception.
 * @param env Borrowed runtime.
 * @param message Borrowed diagnostic, or NULL for cgai_last_error().
 * @return NULL, the Node-API error sentinel. */
napi_value cgai_node_chat_error(napi_env env, const char *message);
/** @brief Read exactly count finite doubles from a Float64Array.
 * @param env Borrowed runtime.
 * @param value Borrowed typed array.
 * @param output Writable count-element array.
 * @param count Required array length.
 * @return One on success, zero with an exception otherwise. */
int cgai_node_chat_numbers(napi_env env, napi_value value, double *output, size_t count);
/** @brief Convert a bounded lossless unsigned BigInt seed.
 * @param env Borrowed runtime.
 * @param value Borrowed BigInt.
 * @param seed Writable seed.
 * @return One on success, zero with an exception otherwise. */
int cgai_node_chat_seed(napi_env env, napi_value value, uint64_t *seed);
/** @brief Copy a JavaScript structured message array within a shared memory budget.
 * @param env Borrowed runtime.
 * @param value Borrowed array of numeric role/content objects.
 * @param messages Zero-initialized mutable owner, including partial results on failure.
 * @param budget Writable remaining allocation bytes.
 * @return One on success, zero with an exception otherwise; caller always destroys owner. */
int cgai_node_chat_read_messages(napi_env env, napi_value value, cgai_node_chat_messages *messages,
                                 size_t *budget);
/** @brief Release complete or partial copied messages.
 * @param messages Borrowed initialized owner, reset after release. */
void cgai_node_chat_free_messages(cgai_node_chat_messages *messages);
/** @brief Copy independent training examples with a 16 MiB aggregate allocation budget.
 * @param env Borrowed runtime.
 * @param value Borrowed example array.
 * @param examples Zero-initialized owner, including partial results on failure.
 * @return One on success, zero with an exception otherwise; caller always destroys owner. */
int cgai_node_chat_read_examples(napi_env env, napi_value value, cgai_node_chat_examples *examples);
/** @brief Release complete or partial training examples.
 * @param examples Borrowed initialized owner, reset after release. */
void cgai_node_chat_free_examples(cgai_node_chat_examples *examples);
/** @brief Read validated numeric configuration and optimizer settings.
 * @param env Borrowed runtime.
 * @param value Float64Array of D,H,K,prompt,response,routing,epochs,rate,clip,evidence.
 * @param seed Borrowed BigInt initialization seed.
 * @param config Writable model shape.
 * @param training Writable optimizer settings.
 * @return One on success, zero with exception otherwise. */
int cgai_node_chat_settings(napi_env env, napi_value value, napi_value seed,
                            cgai_chat_config *config, cgai_neural_training *training);
/** @brief Decode a borrowed JavaScript Buffer into an owned native model.
 * @param env Borrowed runtime.
 * @param value Borrowed model Buffer, bounded at 64 MiB.
 * @return Owned model or NULL with an exception; destroy the successful result. */
cgai_chat_model *cgai_node_chat_import(napi_env env, napi_value value);
/** @brief Copy model bytes into a JavaScript Buffer and release temporary native bytes.
 * @param env Borrowed runtime.
 * @param model Borrowed immutable model.
 * @return JavaScript-owned Buffer or NULL with an exception. */
napi_value cgai_node_chat_export(napi_env env, const cgai_chat_model *model);
/** @brief Convert measured metrics into a JavaScript object.
 * @param env Borrowed runtime.
 * @param metrics Borrowed scalar measurements.
 * @return Runtime-owned object or NULL with an exception. */
napi_value cgai_node_chat_metrics(napi_env env, const cgai_neural_metrics *metrics);
/** @brief Assign a JavaScript numeric property.
 * @param env Borrowed runtime.
 * @param object Borrowed result object.
 * @param name Borrowed property name.
 * @param number Scalar value to copy.
 * @return One on success, zero with exception otherwise. */
int cgai_node_chat_number(napi_env env, napi_value object, const char *name, double number);
/** @brief Assign a JavaScript string property by copying native text.
 * @param env Borrowed runtime.
 * @param object Borrowed result object.
 * @param name Borrowed property name.
 * @param text Borrowed terminated string.
 * @return One on success, zero with exception otherwise. */
int cgai_node_chat_string(napi_env env, napi_value object, const char *name, const char *text);
/** @brief Train structured dialogues and return artifact plus measured training losses.
 * @param env Borrowed runtime.
 * @param info Borrowed callback arguments: examples, numeric settings, seed.
 * @return Runtime-owned result or NULL with exception. */
napi_value cgai_node_chat_train(napi_env env, napi_callback_info info);
/** @brief Inspect a versioned chat artifact through the native decoder.
 * @param env Borrowed runtime.
 * @param info Borrowed callback argument: artifact Buffer.
 * @return JSON metadata string or NULL with exception. */
napi_value cgai_node_chat_inspect(napi_env env, napi_callback_info info);
/** @brief Score independent dialogue answers without changing the model.
 * @param env Borrowed runtime.
 * @param info Borrowed callback arguments: artifact, examples.
 * @return Metrics object or NULL with exception. */
napi_value cgai_node_chat_evaluate(napi_env env, napi_callback_info info);
/** @brief Generate from structured messages with persistent question conditioning.
 * @param env Borrowed runtime.
 * @param info Borrowed callback arguments: artifact, messages, numeric options, seed.
 * @return Reply object or NULL with exception. */
napi_value cgai_node_chat_reply(napi_env env, napi_callback_info info);
#endif
