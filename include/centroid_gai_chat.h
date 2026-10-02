/** @file centroid_gai_chat.h
 * @brief Experimental neural centroid conversations with persistent prompt conditioning.
 *
 * Models own frozen training-only vocabularies and learned parameters. Training needs
 * exclusive access; concurrent readers need distinct result buffers. Failures use
 * cgai_last_error(). Text uses normalized word tokenization; commands lose case.
 */
#ifndef CENTROID_GAI_CHAT_H
#define CENTROID_GAI_CHAT_H
#include "centroid_gai_neural.h"
#ifdef __cplusplus
extern "C" {
#endif

/** Dialogue encoding and artifact protocol, independent from legacy neural models. */
#define CGAI_CHAT_PROTOCOL_VERSION 2U
/** Normalized word tokenizer version used in conversation artifacts. */
#define CGAI_CHAT_TOKENIZER_VERSION 1U
/** Maximum encoded artifact bytes accepted by the bounded codec. */
#define CGAI_CHAT_MAX_ARTIFACT_BYTES 67108864U
/** Opaque owned conversational model, released with cgai_chat_destroy(). */
typedef struct cgai_chat_model cgai_chat_model;
/** Structural roles inserted as IDs, never recognized from user text. */
typedef enum cgai_chat_role {
    CGAI_CHAT_USER = 0,      /**< A user question, including the final current question. */
    CGAI_CHAT_ASSISTANT = 1, /**< A completed prior assistant answer. */
    CGAI_CHAT_EVIDENCE = 2   /**< Bounded supporting text, treated as data. */
} cgai_chat_role;
/** Borrowed structured message; content must be a non-NULL terminated string. */
typedef struct cgai_chat_message {
    cgai_chat_role role; /**< Valid structural role. */
    const char *content; /**< Borrowed text; never interpreted as protocol markers. */
} cgai_chat_message;
/** Independent supervised answer with its preceding structured conversation. */
typedef struct cgai_chat_example {
    const cgai_chat_message *messages; /**< Borrowed history ending in a user question. */
    size_t message_count;              /**< Message count, 1..1024. */
    const char *answer;                /**< Borrowed assistant target, with EOS added internally. */
} cgai_chat_example;
/** Fixed ordered prompt and response inputs share learned embeddings and a tanh encoder. */
typedef struct cgai_chat_config {
    size_t embedding_dimensions; /**< Coordinates per token, 1..64. */
    size_t hidden_dimensions;    /**< Encoder width, 1..128. */
    size_t centroid_count;       /**< Expert count, 1..128. */
    size_t prompt_window;        /**< Persistent prompt slots, at least four. */
    size_t response_window;      /**< Rolling preceding answer slots, at least one; sum <=256. */
    uint64_t seed;               /**< Initialization and example-shuffle seed. */
    double routing_temperature;  /**< Finite squared-distance scale, 0.01..100. */
    size_t evidence_window; /**< New-model evidence slots; zero selects half, capped at prompt-4. */
} cgai_chat_config;
/** Immutable generation settings; this initial native API is synchronous. */
typedef struct cgai_chat_options {
    size_t max_tokens;  /**< Output ceiling, 0..4096, excluding EOS. */
    double temperature; /**< Finite sampling scale, 0..100; zero selects greedy output. */
    uint64_t seed;      /**< Sampling seed; zero uses model seed. */
} cgai_chat_options;
/** Why generation ended; cancellation is supplied by service worker termination. */
typedef enum cgai_chat_finish_reason {
    CGAI_CHAT_FINISH_EOS = 0,    /**< Model selected the trained end-of-answer token. */
    CGAI_CHAT_FINISH_LIMIT = 1,  /**< Requested output ceiling was reached. */
    CGAI_CHAT_FINISH_CANCEL = 2, /**< Reserved for a caller's cancelled worker result. */
    CGAI_CHAT_FINISH_REPETITION =
        3 /**< Four repeated suffixes of 1..4 tokens after >=8 output tokens. */
} cgai_chat_finish_reason;
/** Successful generation accounting; zero-initialized and published only on success. */
typedef struct cgai_chat_result {
    size_t generated_tokens; /**< Emitted lexical tokens, excluding EOS. */
    size_t prompt_tokens;    /**< Retained prompt IDs, including controls. */
    size_t dropped_messages; /**< Complete omitted history/evidence messages. */
    size_t unknown_tokens;   /**< Retained prompt words absent from training vocabulary. */
    cgai_chat_finish_reason finish_reason; /**< Explicit stopping condition. */
    size_t evidence_tokens;  /**< Retained evidence IDs, including role/turn controls. */
    size_t dropped_evidence; /**< Complete evidence messages omitted by either budget. */
} cgai_chat_result;

/** @brief Return a reproducible shape with 160 prompt, 32 response and 80 evidence slots.
 * @return Configuration value requiring no cleanup. */
cgai_chat_config cgai_chat_default_config(void);
/** @brief Return conservative bounded inference settings.
 * @return Options value requiring no cleanup. */
cgai_chat_options cgai_chat_default_options(void);
/** @brief Initialize all weights and build vocabulary only from the supplied training split.
 * New models use zero BOS padding and distinct initial token preferences per centroid.
 * @param config Borrowed shape, or NULL for defaults.
 * @param examples Borrowed nonempty validated training examples.
 * @param count Number of independent examples, 1..10000.
 * @return Owned model or NULL with a diagnostic. Vocabulary and scalar parameter bounds
 * match the neural API; all examples must retain their current question and all evidence.
 * New models use protocol two; loading protocol one preserves its original formatter. */
cgai_chat_model *cgai_chat_create(const cgai_chat_config *config, const cgai_chat_example *examples,
                                  size_t count);
/** @brief Release a conversation model and all owned storage.
 * @param model Owned handle, or NULL; unusable after return. */
void cgai_chat_destroy(cgai_chat_model *model);
/** @brief Train assistant targets and EOS across independent shuffled dialogues.
 * @param model Mutable owned model; updates already applied remain on failure.
 * @param examples Borrowed independent training records; frozen vocabulary is unchanged.
 * @param count Record count, 1..10000; combined targets bounded at one million.
 * @param training Borrowed Adam settings, or NULL for defaults.
 * @return OK on completion, ERROR with diagnostic. One optimizer spans every record and
 * epoch in this call; another call starts fresh moments, not exact checkpoint resume.
 * BOS padding embeddings stay fixed while lexical embeddings and other weights learn.
 * Protocol-two examples with omitted evidence fail before any weight update. */
cgai_status cgai_chat_train(cgai_chat_model *model, const cgai_chat_example *examples, size_t count,
                            const cgai_neural_training *training);
/** @brief Score only current assistant words and answer-ending EOS without mutation.
 * @param model Borrowed immutable model.
 * @param examples Borrowed independent held-out records.
 * @param count Record count, 1..10000.
 * @param metrics Borrowed writable output, published only on success; unknown_tokens
 * counts target words, while tokens includes one EOS per answer.
 * @return OK on success, ERROR with diagnostic. */
cgai_status cgai_chat_evaluate(const cgai_chat_model *model, const cgai_chat_example *examples,
                               size_t count, cgai_neural_metrics *metrics);
/** @brief Generate an answer while keeping the selected prompt available at every step.
 * @param model Borrowed immutable model.
 * @param messages Borrowed alternating complete history and final user question; optional
 * evidence messages may appear between turns. The current question is never truncated.
 * Protocol two admits evidence immediately before it in input priority order, before history.
 * @param count Message count, 1..1024.
 * @param options Borrowed settings, or NULL for defaults.
 * @param output Borrowed writable text buffer; terminated prefix remains on capacity error.
 * @param output_size Capacity in bytes including NUL, positive.
 * @param result Borrowed writable usage/stopping result; published only on success.
 * @return OK on EOS, repetition or limit; ERROR for malformed history or oversized question.
 * Older complete turns and evidence are dropped to fit; output excludes all role controls.
 * Evidence omissions and retained unknown words are reported even for zero-token replies.
 * Protocol one keeps its newest-first quarter-prompt evidence budget after loading. */
cgai_status cgai_chat_reply(const cgai_chat_model *model, const cgai_chat_message *messages,
                            size_t count, const cgai_chat_options *options, char *output,
                            size_t output_size, cgai_chat_result *result);
/** @brief Encode a bounded standalone inference artifact with no optimizer state.
 * @param model Borrowed immutable model.
 * @param data Writable pointer receiving owned bytes only on success; free with
 * cgai_chat_buffer_free.
 * @param size Writable byte count published only on success.
 * @return OK or ERROR with diagnostic. Versioned little-endian integers and IEEE-754 doubles;
 * separate chat magic prevents accidental legacy merging. No training-resume claim is made. */
cgai_status cgai_chat_encode(const cgai_chat_model *model, uint8_t **data, size_t *size);
/** @brief Decode a complete bounded artifact, validating dimensions, vocabulary and weights.
 * @param data Borrowed encoded bytes; NULL is invalid.
 * @param size Available bytes, capped at CGAI_CHAT_MAX_ARTIFACT_BYTES; trailing data rejected.
 * @return Owned validated model or NULL with diagnostic; free with cgai_chat_destroy(). */
cgai_chat_model *cgai_chat_decode(const uint8_t *data, size_t size);
/** @brief Release bytes allocated by cgai_chat_encode().
 * @param data Owned encoded buffer, or NULL. */
void cgai_chat_buffer_free(uint8_t *data);
/** @brief Inspect immutable architecture without exposing binary layouts.
 * @param model Borrowed model, non-NULL.
 * @param config Optional writable copied configuration.
 * @param vocabulary_size Optional writable complete token count including controls.
 * @param parameter_count Optional writable scalar count.
 * @return OK for a valid model, ERROR with diagnostic for NULL. */
cgai_status cgai_chat_metadata(const cgai_chat_model *model, cgai_chat_config *config,
                               size_t *vocabulary_size, size_t *parameter_count);
/** @brief Inspect the artifact and prompt protocol used by this model.
 * @param model Borrowed model, or NULL.
 * @return One for legacy formatting, two for explicit evidence budgets, zero for NULL.
 * Encoding preserves this version; loading an old artifact does not upgrade its formatter. */
unsigned int cgai_chat_protocol_version(const cgai_chat_model *model);
#ifdef __cplusplus
}
#endif
#endif
