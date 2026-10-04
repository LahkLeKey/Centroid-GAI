/** @file neural_generation.h @brief Private generation from an already encoded context. */
#ifndef CGAI_NEURAL_GENERATION_H
#define CGAI_NEURAL_GENERATION_H

#include "internal/token_contract.h"
#include "neural_contract.h"

/** Terminal conditions shared by encoded consumers without conversation dependencies. */
typedef enum cgai_neural_encoded_finish {
    CGAI_NEURAL_ENCODED_EOS,       /**< EOS consumed a forward pass without emitting text. */
    CGAI_NEURAL_ENCODED_LIMIT,     /**< Requested output token limit reached. */
    CGAI_NEURAL_ENCODED_REPETITION /**< Four repeated suffix periods after eight tokens. */
} cgai_neural_encoded_finish;

/** Borrowed encoded window and bounded output settings; no model mutation. */
typedef struct cgai_neural_encoded_request {
    const cgai_token_id *context; /**< Exactly the model's context-window initialized IDs. */
    size_t fixed;                 /**< Immutable prefix length, below the context-window size. */
    size_t max_tokens;            /**< Maximum emitted tokens, at most one million. */
    double temperature;           /**< Finite sampling temperature, 0..100. */
    uint64_t seed;                /**< Local RNG seed; zero uses the neural model seed. */
    char *output;                 /**< Writable destination, independent from context storage. */
    size_t output_size;           /**< Positive byte capacity, including the final NUL. */
} cgai_neural_encoded_request;

/** Success-only accounting for one completed encoded request. */
typedef struct cgai_neural_encoded_result {
    size_t generated_tokens;           /**< Complete emitted tokens, excluding EOS. */
    size_t forward_passes;             /**< Attempted passes, including the stopping EOS. */
    cgai_neural_encoded_finish finish; /**< EOS, limit or bounded suffix repetition. */
} cgai_neural_encoded_result;

/** @brief Sample from fixed-prefix and rolling-suffix token positions.
 * @param model Borrowed initialized immutable model.
 * @param request Borrowed encoded input and destination settings.
 * @param result Writable accounting, published only on success.
 * @return OK at a stopping condition, ERROR with a terminated partial output otherwise.
 * Invalid arguments leave output and result unchanged. A valid zero-token request still
 * prepares numerical scratch. A zero fixed prefix disables suffix repetition detection. */
cgai_status cgai_neural_generate_encoded(const cgai_neural_model *model,
                                         const cgai_neural_encoded_request *request,
                                         cgai_neural_encoded_result *result);

#endif
