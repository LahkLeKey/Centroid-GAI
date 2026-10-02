/** @file chat_internal.h @brief Conversation ownership and shared dataset formatting. */
#ifndef CGAI_CHAT_INTERNAL_H
#define CGAI_CHAT_INTERNAL_H
#include "centroid_gai_chat.h"
#include "neural_math.h"
/** Maximum training records per operation. */
#define CGAI_CHAT_MAX_EXAMPLES 10000U
/** Maximum structured messages per prompt. */
#define CGAI_CHAT_MAX_MESSAGES 1024U
/** Maximum aggregate text bytes per dataset or prompt. */
#define CGAI_CHAT_MAX_TEXT_BYTES 16777216U
/** Number of input-only role and turn controls appended after the lexical vocabulary. */
#define CGAI_CHAT_CONTROL_COUNT 4U
/** Owned wrapper; the network retains the existing differentiable centroid parameter layout. */
struct cgai_chat_model {
    cgai_chat_config config;       /**< Copied immutable conversation dimensions. */
    cgai_neural_model *network;    /**< Owned neural parameters and frozen vocabulary. */
    unsigned int protocol_version; /**< Artifact and formatter version, one or two. */
};
/** Bounded complete prompt, left padded independently from answer context. */
typedef struct cgai_chat_prompt_data {
    cgai_token_id tokens[CGAI_NEURAL_MAX_CONTEXT]; /**< Initialized persistent prompt slots. */
    size_t count;            /**< Retained nonpadding IDs including role/turn controls. */
    size_t dropped;          /**< Complete omitted message count. */
    size_t unknown;          /**< Retained lexical UNK occurrences. */
    size_t evidence;         /**< Retained evidence IDs including role/turn controls. */
    size_t dropped_evidence; /**< Complete omitted evidence messages. */
} cgai_chat_prompt_data;
/** Independent prepared dialogue; answer includes one supervised EOS. */
typedef struct cgai_chat_record {
    cgai_chat_prompt_data prompt; /**< Persistent conditioning, excluding target answer. */
    cgai_token_id *answer;        /**< Owned target sequence. */
    size_t count;                 /**< Answer words plus EOS. */
    size_t unknown;               /**< Unknown answer words. */
} cgai_chat_record;
/** Dataset scratch retained for a complete train or evaluate operation. */
typedef struct cgai_chat_dataset {
    cgai_chat_record *records; /**< Owned independent records. */
    size_t count;              /**< Allocated record count, including partial initialization. */
} cgai_chat_dataset;
/** @brief Validate chat dimensions and map to the shared network shape.
 * @param config Borrowed conversation configuration.
 * @param network Optional writable shared configuration.
 * @return OK on bounded shape, ERROR otherwise. */
cgai_status cgai_chat_validate_config(const cgai_chat_config *config, cgai_neural_config *network);
/** @brief Validate dialogue order and aggregate content size.
 * @param messages Borrowed history ending in a user question.
 * @param count Message count.
 * @return OK for complete prior turns and bounded input, ERROR otherwise. */
cgai_status cgai_chat_validate_messages(const cgai_chat_message *messages, size_t count);
/** @brief Format one prompt identically for training, evaluation and inference.
 * @param model Borrowed immutable model.
 * @param messages Borrowed structured history.
 * @param count Message count.
 * @param prompt Borrowed writable fixed-size result, changed only on success.
 * @return OK or ERROR; an oversized current question fails without truncation. */
cgai_status cgai_chat_format(const cgai_chat_model *model, const cgai_chat_message *messages,
                             size_t count, cgai_chat_prompt_data *prompt);
/** @brief Combine immutable prompt positions with a causal answer suffix.
 * @param model Borrowed shape.
 * @param prompt Borrowed prepared prompt.
 * @param answer Borrowed initialized prefix, NULL allowed at position zero.
 * @param position Count of preceding answer IDs; the prediction target is excluded.
 * @param context Writable combined-window array. */
void cgai_chat_context(const cgai_chat_model *model, const cgai_chat_prompt_data *prompt,
                       const cgai_token_id *answer, size_t position, cgai_token_id *context);
/** @brief Prepare bounded independent targets before training can mutate weights.
 * @param model Borrowed immutable vocabulary and shape.
 * @param examples Borrowed independent supervised records.
 * @param count Record count.
 * @param dataset Borrowed zeroed descriptor receiving ownership, even on failure.
 * @return OK on complete preparation, ERROR otherwise; destroy dataset in either case. */
cgai_status cgai_chat_prepare_dataset(const cgai_chat_model *model,
                                      const cgai_chat_example *examples, size_t count,
                                      cgai_chat_dataset *dataset);
/** @brief Release all prepared records and reset ownership.
 * @param dataset Borrowed zeroed or partially prepared owned descriptor. */
void cgai_chat_destroy_dataset(cgai_chat_dataset *dataset);
#endif
