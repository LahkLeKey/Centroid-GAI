/** @file neural_internal.h @brief Owned neural parameters and shared vocabulary helpers. */
#ifndef CGAI_NEURAL_INTERNAL_H
#define CGAI_NEURAL_INTERNAL_H
#include "centroid_gai_neural.h"
#include "cgai_internal.h"
#include "tokenizer.h"
/** Maximum frozen vocabulary entries, including controls. */
#define CGAI_NEURAL_MAX_VOCABULARY 8192U
/** Maximum scalar parameters, bounding parameter and optimizer allocations. */
#define CGAI_NEURAL_MAX_PARAMETERS 2000000U
/** Maximum text targets per operation, excluding final EOS. */
#define CGAI_NEURAL_MAX_TOKENS 1000000U
/** Maximum bytes in a token spelling, excluding NUL. */
#define CGAI_NEURAL_MAX_TOKEN_BYTES 1048576U
/** Maximum ordered input slots across persistent prompt and rolling response. */
#define CGAI_NEURAL_MAX_CONTEXT 256U
/** Private owned handle. Slices alias parameters; they must not be freed separately. */
struct cgai_neural_model {
    cgai_neural_config config; /**< Fixed copied architecture. */
    char **vocabulary;         /**< Owned unique normalized spellings, controls first. */
    size_t vocabulary_size;    /**< Initialized vocabulary entries. */
    size_t output_size;       /**< Predictable prefix; trailing dialogue controls are input-only. */
    size_t parameter_count;   /**< Scalar doubles in parameters. */
    double *parameters;       /**< Owned contiguous trainable parameter block. */
    double *embeddings;       /**< V by D slice. */
    double *encoder;          /**< H by (W*D) slice. */
    double *bias;             /**< H slice. */
    double *centroids;        /**< K by H slice. */
    double *logits;           /**< K by V slice, BOS output masked. */
    double *adam_first;       /**< Owned continuation first moments, or NULL before training. */
    double *adam_second;      /**< Owned continuation second moments, or NULL before training. */
    uint64_t training_step;   /**< Completed continuation optimizer updates. */
    uint64_t training_epochs; /**< Completed continuation full passes. */
    uint64_t training_shuffle; /**< Continuation RNG state, initialized from config.seed. */
};
/** @brief Clear continuation moments and counters before fresh optimization.
 * @param model Borrowed mutable initialized model; weights remain unchanged. */
void cgai_neural_reset_training(cgai_neural_model *model);
/** @brief Count requested reusable generation heap bytes without allocating.
 * @param model Borrowed initialized model, or NULL.
 * @return Session and workspace bytes, or zero on invalid shape/overflow. */
size_t cgai_neural_session_bytes(const cgai_neural_model *model);
/** @brief Bound every dimension before computing allocation sizes.
 * @param config Borrowed non-NULL configuration.
 * @return OK on supported dimensions and routing scale, ERROR otherwise. */
cgai_status cgai_neural_validate_config(const cgai_neural_config *config);
/** @brief Allocate the single parameter block and assign borrowed slices.
 * @param model Mutable shell with valid config and initialized vocabulary.
 * @return OK on allocation, ERROR otherwise; caller owns cleanup. */
cgai_status cgai_neural_allocate_parameters(cgai_neural_model *model);
/** @brief Break centroid and expert symmetry while keeping encoder activations moderate.
 * @param model Borrowed mutable fully allocated model. */
void cgai_neural_initialize_parameters(cgai_neural_model *model);
/** @brief Copy one previously absent spelling into owned vocabulary storage.
 * @param model Mutable shell with capacity for the maximum pointer count.
 * @param spelling Borrowed source string.
 * @return OK on append, ERROR without publishing an incomplete string. */
cgai_status cgai_neural_append_spelling(cgai_neural_model *model, const char *spelling);
/** @brief Tokenize training text and establish the frozen vocabulary.
 * @param model Mutable empty shell.
 * @param text Borrowed training text.
 * @return OK on completion, ERROR otherwise; model owns partial vocabulary. */
cgai_status cgai_neural_build_vocabulary(cgai_neural_model *model, const char *text);
/** @brief Search the fixed vocabulary without changing it.
 * @param model Borrowed handle.
 * @param spelling Borrowed normalized token string.
 * @return Existing token ID, or the shared UNK ID. */
cgai_token_id cgai_neural_lookup(const cgai_neural_model *model, const char *spelling);
/** @brief Tokenize a sequence using only the model's existing vocabulary.
 * @param model Borrowed handle.
 * @param text Borrowed text.
 * @param count Writable ID count including final EOS.
 * @param unknown Writable UNK count excluding EOS.
 * @return Owned sequence or NULL; caller frees the result. */
cgai_token_id *cgai_neural_sequence(const cgai_neural_model *model, const char *text, size_t *count,
                                    size_t *unknown);
/** @brief Preserve token order and exclude the prediction target from context.
 * @param model Borrowed handle.
 * @param sequence Borrowed initialized prefix of at least position IDs.
 * @param position Number of preceding tokens, also the target position.
 * @param context Writable fixed-window array. */
void cgai_neural_context(const cgai_neural_model *model, const cgai_token_id *sequence,
                         size_t position, cgai_token_id *context);
/** @brief Select the largest output probability with stable first-ID tie breaking.
 * @param probabilities Borrowed output probabilities; BOS is masked.
 * @param count Vocabulary size, at least three.
 * @return Greedy output ID excluding BOS. */
cgai_token_id cgai_neural_argmax(const double *probabilities, size_t count);
#endif
