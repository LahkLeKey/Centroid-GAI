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
/** Adam settings; optimizer state is fresh on each training call. */
typedef struct cgai_neural_training {
    size_t epochs;        /**< Full shuffled passes, 1..10000. */
    double learning_rate; /**< Finite Adam step size, greater than zero and at most one. */
    double gradient_clip; /**< Positive finite global gradient norm limit, at most 1000. */
} cgai_neural_training;
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
#ifdef __cplusplus
}
#endif
#endif
