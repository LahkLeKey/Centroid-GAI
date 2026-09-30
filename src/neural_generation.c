/** @file neural_generation.c @brief Autoregressive neural continuation with bounded local history.
 */
#include "internal/chat_internal.h"
#include "internal/error.h"
#include "internal/model_random.h"
#include "internal/neural_internal.h"
#include "internal/neural_math.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Borrowed request plus local sampling state and owned inference scratch. */
typedef struct neural_generation {
    const cgai_neural_model *model;                 /**< Borrowed immutable network. */
    cgai_neural_workspace *workspace;               /**< Owned inference buffers. */
    cgai_token_id context[CGAI_NEURAL_MAX_CONTEXT]; /**< Ordered current context. */
    size_t max_tokens;                              /**< Maximum output token count. */
    double temperature;             /**< Sampling temperature, distinct from routing temperature. */
    uint64_t state;                 /**< Local reproducible RNG state. */
    char *output;                   /**< Borrowed destination. */
    size_t capacity;                /**< Destination byte capacity, including NUL. */
    size_t length;                  /**< Current text bytes, excluding NUL. */
    size_t fixed;                   /**< Persistent prefix size; zero for prototype generation. */
    size_t generated;               /**< Successfully emitted tokens. */
    cgai_token_id recent[16];       /**< Last emitted chat IDs in a bounded repetition ring. */
    cgai_chat_finish_reason finish; /**< EOS or requested length. */
} neural_generation;

/** @brief Prepare a BOS-padded suffix from a prompt without retaining its whole history.
 * @param request Borrowed mutable generation state.
 * @param prompt Borrowed prompt, which may be empty.
 * @return OK on preparation, ERROR otherwise; caller releases any workspace. */
static cgai_status prepare_generation(neural_generation *request, const char *prompt) {
    /* Step 1: Map unknown words into the frozen vocabulary, discarding the appended EOS. */
    size_t count = 0U;
    size_t unknown = 0U;
    cgai_token_id *sequence = cgai_neural_sequence(request->model, prompt, &count, &unknown);
    if (sequence == NULL)
        return CGAI_STATUS_ERROR;
    cgai_neural_context(request->model, sequence, count - 1U, request->context);
    free(sequence);
    /* Step 2: Allocate numerical scratch independently for each concurrent reader. */
    request->workspace = cgai_neural_workspace_create(request->model);
    return request->workspace != NULL ? CGAI_STATUS_OK : CGAI_STATUS_ERROR;
}

/** @brief Convert exact mixture log likelihoods to temperature-adjusted sampling weights.
 * @param request Borrowed inference state with a successful forward result.
 * @return Positive total weight, or negative one on nonfinite loss. */
static double sampling_weights(neural_generation *request) {
    /* Step 1: Store log probabilities, keeping rare events stable until temperature scaling. */
    double maximum = -HUGE_VAL;
    double *weights = request->workspace->probabilities;
    for (size_t i = 1U; i < request->model->output_size; ++i) {
        weights[i] =
            -cgai_neural_loss(request->model, request->workspace, cgai_token_id_from_size(i));
        if (!isfinite(weights[i]))
            return -1.0;
        maximum = fmax(maximum, weights[i]);
    }
    /* Step 2: Subtract before dividing; even tiny positive temperatures keep a unit maximum. */
    double total = 0.0;
    for (size_t i = 1U; i < request->model->output_size; ++i) {
        weights[i] = exp((weights[i] - maximum) / request->temperature);
        total += weights[i];
    }
    return total;
}

/** @brief Select a next token, excluding BOS and retaining EOS as a stopping event.
 * @param request Borrowed mutable sampling state with forward probabilities ready.
 * @return Sampled ID, or invalid sentinel on numerical failure. */
static cgai_token_id select_token(neural_generation *request) {
    /* Step 1: Greedy mode requires neither random draws nor temperature division. */
    if (request->temperature == 0.0)
        return cgai_neural_argmax(request->workspace->probabilities, request->model->output_size);
    const double total = sampling_weights(request);
    if (total < 0.0)
        return cgai_token_id_invalid();
    double draw = (double)(cgai_random_next(&request->state) >> 11U) * 0x1.0p-53 * total;
    /* Step 2: Inverse-CDF sampling advances only caller-owned random state. */
    for (size_t i = 1U; i < request->model->output_size; ++i) {
        draw -= request->workspace->probabilities[i];
        if (draw < 0.0)
            return cgai_token_id_from_size(i);
    }
    return cgai_neural_argmax(request->workspace->probabilities, request->model->output_size);
}

/** @brief Append a token while retaining a valid terminated prefix on capacity failure.
 * @param request Borrowed mutable output state.
 * @param token Borrowed NUL-terminated spelling.
 * @return OK on append, ERROR without changing the prefix otherwise. */
static cgai_status append_token(neural_generation *request, const char *token) {
    /* Step 1: Calculate punctuation spacing and check subtraction-safe capacity. */
    const size_t bytes = strlen(token);
    const size_t space =
        request->length > 0U && !(bytes == 1U && strchr(".,!?;:)]}", token[0])) ? 1U : 0U;
    if (bytes + space >= request->capacity - request->length)
        return cgai_fail("neural generation output buffer is too small");
    /* Step 2: Copy the complete token including its terminator. */
    if (space != 0U)
        request->output[request->length++] = ' ';
    memcpy(request->output + request->length, token, bytes + 1U);
    request->length += bytes;
    return CGAI_STATUS_OK;
}

/** @brief Compare the four most recent copies of a candidate suffix period.
 * @param request Borrowed generation state containing at least four complete periods.
 * @param period Candidate suffix width, 1..4 tokens.
 * @return Nonzero when all four suffix copies have identical IDs. */
static int matches_period(const neural_generation *request, size_t period) {
    for (size_t offset = 0U; offset < 3U * period; ++offset) {
        const size_t index = request->generated - 1U - offset;
        if (request->recent[index % 16U].value != request->recent[(index - period) % 16U].value)
            return 0;
    }
    return 1;
}

/** @brief Detect four copies of a one-to-four-token suffix after eight chat tokens.
 * @param request Borrowed generation state after emitting a token.
 * @return Nonzero for a repeated chat suffix; prototype continuations are unaffected. */
static int repeated_suffix(const neural_generation *request) {
    if (request->fixed == 0U || request->generated < 8U)
        return 0;
    for (size_t period = 1U; period <= 4U; ++period) {
        if (request->generated >= 4U * period && matches_period(request, period))
            return 1;
    }
    return 0;
}

/** @brief Feed a generated token into the fixed-size ordered context.
 * @param request Borrowed mutable history.
 * @param token Valid output ID, excluding EOS. */
static void advance_context(neural_generation *request, cgai_token_id token) {
    /* Step 1: Shift only the rolling suffix and append the latest token. */
    const size_t window = request->model->config.context_window;
    memmove(request->context + request->fixed, request->context + request->fixed + 1U,
            (window - request->fixed - 1U) * sizeof(cgai_token_id));
    request->context[window - 1U] = token;
    if (request->fixed != 0U)
        request->recent[request->generated % 16U] = token;
    ++request->generated;
    /* Step 2: Bound repetitive chat output without changing prototype continuations. */
    if (repeated_suffix(request))
        request->finish = CGAI_CHAT_FINISH_REPETITION;
}

/** @brief Generate a continuation by repeatedly conditioning on prior output.
 * @param request Borrowed prepared state.
 * @return OK at EOS or token limit, ERROR on numerical or output failure. */
static cgai_status generate_tokens(neural_generation *request) {
    /* Step 1: Recompute the context encoding after every emitted token. */
    for (size_t i = 0U; i < request->max_tokens && request->finish != CGAI_CHAT_FINISH_REPETITION;
         ++i) {
        if (!cgai_neural_forward(request->model, request->context, request->workspace))
            return CGAI_STATUS_ERROR;
        const cgai_token_id token = select_token(request);
        if (!cgai_token_id_is_valid(token))
            return CGAI_STATUS_ERROR;
        if (token.value == CGAI_TOKEN_EOS) {
            request->finish = CGAI_CHAT_FINISH_EOS;
            return CGAI_STATUS_OK;
        }
        /* Step 2: Keep EOS internal, append ordinary text, then advance causal history. */
        if (!append_token(request, request->model->vocabulary[token.value]))
            return CGAI_STATUS_ERROR;
        advance_context(request, token);
    }
    return CGAI_STATUS_OK;
}

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
                                 size_t output_size) {
    /* Step 1: Reject malformed input before touching the output buffer. */
    cgai_error_clear();
    if (model == NULL || prompt == NULL || output == NULL || output_size == 0U ||
        max_tokens > CGAI_NEURAL_MAX_TOKENS || !isfinite(temperature) || temperature < 0.0 ||
        temperature > 100.0)
        return cgai_fail("invalid neural generation arguments");
    output[0] = '\0';
    if (max_tokens == 0U)
        return CGAI_STATUS_OK;
    /* Step 2: Own per-request scratch, using no persistent model mutation. */
    neural_generation request = {.model = model,
                                 .max_tokens = max_tokens,
                                 .temperature = temperature,
                                 .state = seed != 0U ? seed : model->config.seed,
                                 .output = output,
                                 .capacity = output_size};
    cgai_status status = prepare_generation(&request, prompt);
    if (status)
        status = generate_tokens(&request);
    cgai_neural_workspace_destroy(request.workspace);
    return status;
}

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
    neural_generation request = {.model = model->network,
                                 .max_tokens = options.max_tokens,
                                 .temperature = options.temperature,
                                 .state = options.seed ? options.seed : model->config.seed,
                                 .output = output,
                                 .capacity = output_size,
                                 .fixed = model->config.prompt_window,
                                 .finish = CGAI_CHAT_FINISH_LIMIT};
    cgai_chat_context(model, &prompt, NULL, 0U, request.context);
    request.workspace = cgai_neural_workspace_create(model->network);
    cgai_status status = request.workspace ? generate_tokens(&request) : CGAI_STATUS_ERROR;
    cgai_neural_workspace_destroy(request.workspace);
    if (status) {
        const cgai_chat_result completed = {request.generated, prompt.count, prompt.dropped,
                                            prompt.unknown, request.finish};
        *result = completed;
    }
    return status;
}
