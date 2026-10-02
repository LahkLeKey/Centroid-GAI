/** @file centroid_gai_neural.h
 * @brief Experimental differentiable centroid language model, independent of format-v1 models.
 *
 * Handles own their vocabulary and parameters. Mutation requires exclusive access;
 * evaluation, generation and saving may run concurrently on an immutable model.
 * Failures use cgai_last_error(). Text uses the existing normalized word tokenizer.
 */
#ifndef CENTROID_GAI_NEURAL_H
#define CENTROID_GAI_NEURAL_H
#include "centroid_gai.h"
#ifdef __cplusplus
extern "C" {
#endif

/** Owned opaque neural model; release with cgai_neural_destroy(). */
typedef struct cgai_neural_model cgai_neural_model;
/** Fixed network shape, copied at creation. */
typedef struct cgai_neural_config {
    size_t embedding_dimensions; /**< Learned coordinates per token, 1..64. */
    size_t hidden_dimensions;    /**< Encoder output coordinates, 1..128. */
    size_t centroid_count;       /**< Mixture components, 1..128. */
    size_t context_window;       /**< Ordered preceding tokens, 1..256. */
    uint64_t seed;               /**< Reproducible initialization and training shuffle seed. */
    double routing_temperature;  /**< Positive finite distance scale, 0.01..100. */
} cgai_neural_config;
/** Adam settings; continuation calls retain optimizer state in the model. */
typedef struct cgai_neural_training {
    size_t epochs;        /**< Full shuffled passes, 1..10000. */
    double learning_rate; /**< Finite Adam step size, greater than zero and at most one. */
    double gradient_clip; /**< Positive finite global gradient norm limit, at most 1000. */
} cgai_neural_training;
/** Independent task record; only its single target token contributes a training update. */
typedef struct cgai_neural_example {
    const char *prompt; /**< Borrowed nonempty prompt of at most context_window known tokens. */
    const char *target; /**< Borrowed single known lexical output token, excluding controls. */
} cgai_neural_example;
/** Completed continuation work; legacy training clears these counters. */
typedef struct cgai_neural_progress {
    uint64_t epochs; /**< Successfully completed continuation passes. */
    uint64_t steps;  /**< Successfully completed continuation target updates. */
} cgai_neural_progress;
/** Teacher-forced next-token metrics, including one final EOS target. */
typedef struct cgai_neural_metrics {
    size_t tokens;         /**< Number of scored targets including EOS. */
    size_t unknown_tokens; /**< Text tokens mapped to the frozen vocabulary's UNK ID. */
    double cross_entropy;  /**< Mean negative log probability in natural units. */
    double perplexity;     /**< exp(cross_entropy), or infinity on floating-point overflow. */
    double accuracy;       /**< Fraction of targets selected by greedy prediction. */
} cgai_neural_metrics;

/** @brief Return a small reproducible network configuration.
 * @return Configuration value needing no cleanup. */
cgai_neural_config cgai_neural_default_config(void);
/** @brief Return default Adam settings.
 * @return Settings value needing no cleanup. */
cgai_neural_training cgai_neural_default_training(void);
/** @brief Initialize weights and a frozen vocabulary from training text only.
 * @param requested Borrowed shape, or NULL for defaults.
 * @param vocabulary_text Borrowed nonempty training text; held-out text must not be included.
 * @return Owned model, or NULL with a diagnostic. Limits: 8192 vocabulary entries,
 * two million scalar parameters, and 1 MiB per token spelling. */
cgai_neural_model *cgai_neural_create(const cgai_neural_config *requested,
                                      const char *vocabulary_text);
/** @brief Release a model and every owned allocation.
 * @param model Owned handle, or NULL; invalid after this call. */
void cgai_neural_destroy(cgai_neural_model *model);
/** @brief Report the frozen vocabulary size, including BOS, EOS and UNK.
 * @param model Borrowed handle, or NULL.
 * @return Vocabulary size, or zero for NULL. */
size_t cgai_neural_vocabulary_size(const cgai_neural_model *model);
/** @brief Train all parameter groups with clipped Adam and shuffled causal windows.
 * @param model Mutable handle; prior updates remain on failure.
 * @param text Borrowed nonempty training sequence; unknown words map to UNK.
 * @param requested Borrowed settings, or NULL for defaults.
 * @return OK after all passes, ERROR with a diagnostic otherwise. Each call resets
 * Adam moments. Text is one sequence with BOS padding and a final EOS target. */
cgai_status cgai_neural_train(cgai_neural_model *model, const char *text,
                              const cgai_neural_training *requested);
/** @brief Continue clipped Adam training with model-owned moments and shuffle state.
 * @param model Mutable handle; prior updates remain on failure.
 * @param text Borrowed nonempty sequence; vocabulary remains frozen.
 * @param requested Borrowed settings, or NULL for defaults; epochs are additional passes.
 * @return OK after all passes, ERROR with a diagnostic otherwise. Successful calls on the
 * same text with identical rate and clipping reproduce one combined call in the same build
 * and platform. Exact continuation is guaranteed only after successful complete passes.
 * @note cgai_neural_train() starts fresh and clears this state when optimization begins.
 * Ordinary .cgnn save/load preserves weights only; use checkpoints to retain continuation. */
cgai_status cgai_neural_train_continue(cgai_neural_model *model, const char *text,
                                       const cgai_neural_training *requested);
/** @brief Continue deterministic Adam using independent prompt-to-single-target records.
 * @param model Mutable initialized model with a frozen vocabulary.
 * @param examples Borrowed records; prompts and targets remain readable throughout this call.
 * @param count Number of independent records, 1..10000.
 * @param requested Borrowed settings, or NULL for defaults; epochs are additional passes.
 * @return OK after all passes, ERROR with a diagnostic otherwise. Each prompt is BOS-padded;
 * no preceding record, prompt-token loss or EOS target enters the training context or objective.
 * All records and scratch storage are prepared before optimizer or parameter mutation. Invalid
 * records preserve model state. Numeric failures retain successful earlier updates.
 * @note Exact whole-pass continuation requires the same ordered examples, rate, clipping,
 * build and platform. Checkpoint save/load retains the shared Adam, counters and shuffle state. */
cgai_status cgai_neural_train_examples_continue(cgai_neural_model *model,
                                                const cgai_neural_example *examples, size_t count,
                                                const cgai_neural_training *requested);
/** @brief Report accumulated continuation passes and target updates without mutation.
 * @param model Borrowed handle, or NULL.
 * @return Current counters, or both zero for NULL or a fresh model. */
cgai_neural_progress cgai_neural_get_progress(const cgai_neural_model *model);
/** @brief Score a sequence without changing vocabulary or parameters.
 * @param model Borrowed initialized handle.
 * @param text Borrowed nonempty sequence, typically separately held-out text.
 * @param metrics Borrowed writable result; only published on success.
 * @return OK on success, ERROR with a diagnostic otherwise. */
cgai_status cgai_neural_evaluate(const cgai_neural_model *model, const char *text,
                                 cgai_neural_metrics *metrics);
/** @brief Generate a continuation with EOS stopping and seeded sampling.
 * @param model Borrowed initialized handle.
 * @param prompt Borrowed string; empty text uses BOS padding, unknown tokens use UNK.
 * @param max_tokens Upper bound on emitted tokens, at most one million; zero is allowed.
 * @param temperature Finite sampling temperature, 0..100; zero selects greedy output.
 * @param seed Sampling seed; zero uses the model seed.
 * @param output Borrowed writable buffer; holds a terminated prefix on a capacity error.
 * @param output_size Capacity in bytes including NUL; must be positive.
 * @return OK on EOS or token limit, ERROR with a diagnostic otherwise. */
cgai_status cgai_neural_generate(const cgai_neural_model *model, const char *prompt,
                                 size_t max_tokens, double temperature, uint64_t seed, char *output,
                                 size_t output_size);
/** Requested owned heap bytes and dense forward work for one immutable model.
 * Byte counts exclude allocator overhead, stack, caller output and prompt temporaries.
 * Work counts describe the current implementation; they are not latency estimates. */
typedef struct cgai_neural_resources {
    cgai_neural_config config;      /**< Copied network shape. */
    size_t vocabulary_size;         /**< All input token spellings, including controls. */
    size_t output_size;             /**< Predictable vocabulary prefix, including masked BOS. */
    size_t parameter_count;         /**< Scalar trainable doubles. */
    size_t parameter_bytes;         /**< Weight allocation bytes. */
    size_t optimizer_bytes;         /**< Resident Adam allocations; zero for weights-only loads. */
    size_t model_bytes;             /**< Model shell, vocabulary, weights and resident optimizer. */
    size_t workspace_bytes;         /**< One scratch owner and all its numeric slices. */
    size_t session_bytes;           /**< One reusable generation session including workspace. */
    uint64_t encoder_multiply_adds; /**< H*W*D encoder terms per forward pass. */
    uint64_t routing_coordinates;   /**< K*H squared-distance coordinates per forward pass. */
    uint64_t expert_logits;         /**< K*(output_size-1) dense expert scores per forward pass. */
} cgai_neural_resources;
/** @brief Inspect resident allocations and dense work without allocating or mutating.
 * @param model Borrowed initialized immutable model.
 * @param resources Borrowed writable result; unchanged on error.
 * @return OK after publication, ERROR for missing arguments or size overflow. */
cgai_status cgai_neural_get_resources(const cgai_neural_model *model,
                                      cgai_neural_resources *resources);
/** Exclusive mutable generation state borrowing one immutable model and a caller buffer. */
typedef struct cgai_neural_session cgai_neural_session;
/** State of a reusable standalone generation session. */
typedef enum cgai_neural_generation_finish {
    CGAI_NEURAL_FINISH_IDLE = 0,    /**< No request has been started. */
    CGAI_NEURAL_FINISH_RUNNING = 1, /**< More forward passes may be scheduled. */
    CGAI_NEURAL_FINISH_EOS = 2,     /**< Model selected EOS, which emits no text. */
    CGAI_NEURAL_FINISH_LIMIT = 3,   /**< Requested output token ceiling reached. */
    CGAI_NEURAL_FINISH_ERROR = 4    /**< Terminal preparation, numeric or buffer failure. */
} cgai_neural_generation_finish;
/** Cumulative accounting for the current request, including zero-work status queries. */
typedef struct cgai_neural_generation_result {
    cgai_neural_generation_finish finish; /**< Current state. */
    size_t generated_tokens;              /**< Successfully appended output tokens. */
    size_t forward_passes;                /**< Attempted passes; EOS and failed passes count. */
    size_t prompt_tokens;                 /**< Prompt tokens before suffix truncation. */
    size_t unknown_prompt_tokens; /**< Prompt spellings absent from the frozen vocabulary. */
} cgai_neural_generation_result;
/** @brief Allocate reusable scratch before scheduling gameplay work.
 * @param model Borrowed immutable model; must outlive every associated session.
 * @param max_session_bytes Requested owned heap cap; zero disables the cap.
 * @return Owned idle session, or NULL with a diagnostic; destroy to release it.
 * @note One session requires exclusive access. Separate sessions may share an immutable model.
 * The cap includes session/workspace allocations, excluding model/output/allocator overhead. */
cgai_neural_session *cgai_neural_session_create(const cgai_neural_model *model,
                                                size_t max_session_bytes);
/** @brief Release scratch without releasing its borrowed model or caller output.
 * @param session Owned session, or NULL; invalid after this call. */
void cgai_neural_session_destroy(cgai_neural_session *session);
/** @brief Start or replace a request while retaining the allocated numerical scratch.
 * @param session Borrowed exclusive session.
 * @param prompt Borrowed terminated text; tokenization allocates temporary storage.
 * @param max_tokens Emitted token ceiling, at most one million; zero skips prompt preparation.
 * @param temperature Finite sampling temperature, 0..100; zero selects greedy output.
 * @param seed Local sampling seed; zero uses the model seed.
 * @param output Borrowed buffer kept writable and alive until completion or replacement.
 * @param output_size Positive capacity including NUL; prefix stays terminated on step failure.
 * @return OK on prepared request, ERROR otherwise. Invalid arguments preserve the prior request;
 * preparation failure terminates the new request. Prompt/output must not overlap. */
cgai_status cgai_neural_session_begin(cgai_neural_session *session, const char *prompt,
                                      size_t max_tokens, double temperature, uint64_t seed,
                                      char *output, size_t output_size);
/** @brief Advance at most a caller-chosen number of full forward passes without allocations/I/O.
 * @param session Borrowed exclusive session with an immutable live model and live output buffer.
 * @param max_forward_passes Work quantum; zero queries status without advancing.
 * @param result Borrowed cumulative accounting, published for valid sessions even on step failure.
 * @return OK on idle/running/complete requests, ERROR on missing arguments or terminal failure.
 * @note One pass is indivisible; this bounds work, not wall-clock latency. Completed steps are
 * idempotent. Failure is terminal until begin succeeds, preserving a terminated output prefix. */
cgai_status cgai_neural_session_step(cgai_neural_session *session, size_t max_forward_passes,
                                     cgai_neural_generation_result *result);
/** @brief Save a standalone versioned neural artifact, distinct from .cgai files.
 * @param model Borrowed immutable handle.
 * @param path Borrowed destination path, normally ending in .cgnn; existing contents replaced.
 * @return OK on complete write/close, ERROR otherwise; replacement is not atomic.
 * @note Artifacts are capped at 64 MiB. Experimental native numeric representation;
 * use trusted files on the same architecture. */
cgai_status cgai_neural_save(const cgai_neural_model *model, const char *path);
/** @brief Load a bounded versioned neural artifact with validated vocabulary and finite weights.
 * @param path Borrowed path to an artifact produced on the same architecture.
 * @return Owned model, or NULL with a diagnostic; release with cgai_neural_destroy(). */
cgai_neural_model *cgai_neural_load(const char *path);
/** @brief Save standalone weights and continuation state in a version-one text checkpoint.
 * @param model Borrowed immutable standalone model, including accumulated continuation state.
 * @param path Borrowed checkpoint destination; existing contents are replaced nonatomically.
 * @return OK after complete write and close, ERROR with a diagnostic otherwise.
 * @note Checkpoints are capped at 256 MiB and encode exact hexadecimal doubles and token bytes.
 * Adam moments, counters and shuffle state are preserved. Resume successful whole epochs using
 * the same text or ordered independent examples and settings within one build/platform.
 * The C numeric locale is required; this operation does not change the global process locale.
 * Chat-specific output masks are unsupported; use the separate chat artifact codec for chat. */
cgai_status cgai_neural_checkpoint_save(const cgai_neural_model *model, const char *path);
/** @brief Load exact standalone state from a bounded version-one text checkpoint.
 * @param path Borrowed nonempty checkpoint path; LF and CRLF line endings are accepted.
 * @return Owned model, or NULL with a diagnostic; release with cgai_neural_destroy().
 * @note Requires the C numeric locale without mutating it. Shapes, spellings, finite scalars,
 * optimizer state and exact file length are validated before publishing the owned model. */
cgai_neural_model *cgai_neural_checkpoint_load(const char *path);
#ifdef __cplusplus
}
#endif
#endif
