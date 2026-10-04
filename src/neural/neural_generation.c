/** @file neural_generation.c @brief Autoregressive neural continuation with bounded local history.
 */
#include "neural_generation.h"
#include "internal/error.h"
#include "internal/model_random.h"
#include "internal/neural_internal.h"
#include "internal/neural_math.h"
#include "internal/size_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Borrowed request plus local sampling state and owned inference scratch. */
typedef struct neural_generation {
    const cgai_neural_model *model;                 /**< Borrowed immutable network. */
    cgai_neural_workspace *workspace;               /**< Owned inference buffers. */
    cgai_token_id context[CGAI_NEURAL_MAX_CONTEXT]; /**< Ordered current context. */
    size_t max_tokens;                              /**< Maximum output token count. */
    double temperature;           /**< Sampling temperature, distinct from routing temperature. */
    uint64_t state;               /**< Local reproducible RNG state. */
    char *output;                 /**< Borrowed destination. */
    size_t capacity;              /**< Destination byte capacity, including NUL. */
    size_t length;                /**< Current text bytes, excluding NUL. */
    size_t fixed;                 /**< Persistent prefix size; zero for standalone generation. */
    size_t generated;             /**< Successfully emitted tokens. */
    size_t forward_passes;        /**< Attempted forward calls, including EOS and failures. */
    size_t prompt_tokens;         /**< Full prompt tokens before suffix truncation. */
    size_t unknown_prompt_tokens; /**< Unknown spellings across the full prompt. */
    cgai_token_id recent[16];     /**< Last emitted IDs in a bounded repetition ring. */
    cgai_neural_encoded_finish finish; /**< EOS, requested length or suffix repetition. */
} neural_generation;

/** Owned numerical scratch and resumable request borrowing an immutable model. */
struct cgai_neural_session {
    neural_generation request;            /**< Current caller-buffer and sampling state. */
    cgai_neural_generation_finish finish; /**< Idle, running or terminal request state. */
};

/** @brief Prepare a BOS-padded suffix from a prompt without retaining its whole history.
 * @param request Borrowed mutable generation state.
 * @param prompt Borrowed prompt, which may be empty.
 * @return OK on preparation, ERROR otherwise; caller releases any workspace. */
static cgai_status prepare_context(neural_generation *request, const char *prompt) {
    /* Step 1: Map unknown words into the frozen vocabulary, discarding the appended EOS. */
    size_t count = 0U;
    size_t unknown = 0U;
    cgai_token_id *sequence = cgai_neural_sequence(request->model, prompt, &count, &unknown);
    if (sequence == NULL)
        return CGAI_STATUS_ERROR;
    cgai_neural_context(request->model, sequence, count - 1U, request->context);
    request->prompt_tokens = count - 1U;
    request->unknown_prompt_tokens = unknown;
    free(sequence);
    return CGAI_STATUS_OK;
}

/** @brief Prepare context and scratch for a one-shot standalone request.
 * @param request Borrowed request; owns workspace after successful allocation.
 * @param prompt Borrowed terminated text, independent from the destination.
 * @return OK on complete preparation, ERROR otherwise; caller releases workspace. */
static cgai_status prepare_generation(neural_generation *request, const char *prompt) {
    /* Step 1: Map the prompt before allocating numerical buffers. */
    if (!prepare_context(request, prompt))
        return CGAI_STATUS_ERROR;
    /* Step 2: Give each independent request its own numerical scratch. */
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

/** @brief Detect four copies of a one-to-four-token suffix after eight emitted tokens.
 * @param request Borrowed generation state after emitting a token.
 * @return Nonzero for a repeated suffix; standalone continuations are unaffected. */
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
    /* Step 2: Bound repetitive fixed-prefix output without changing standalone continuations. */
    if (repeated_suffix(request))
        request->finish = CGAI_NEURAL_ENCODED_REPETITION;
}

/** @brief Attempt one allocation-free next-token prediction and append.
 * @param request Borrowed prepared state retaining its immutable model and scratch.
 * @return OK on token or EOS, ERROR with a terminal valid output prefix otherwise. */
static cgai_status generate_next(neural_generation *request) {
    /* Step 1: Count attempted work even when a numerical failure or EOS emits no token. */
    ++request->forward_passes;
    if (!cgai_neural_forward(request->model, request->context, request->workspace))
        return CGAI_STATUS_ERROR;
    const cgai_token_id token = select_token(request);
    if (!cgai_token_id_is_valid(token))
        return CGAI_STATUS_ERROR;
    if (token.value == CGAI_TOKEN_EOS) {
        request->finish = CGAI_NEURAL_ENCODED_EOS;
        return CGAI_STATUS_OK;
    }
    /* Step 2: Append ordinary text before advancing the retained causal history. */
    if (!append_token(request, request->model->vocabulary[token.value]))
        return CGAI_STATUS_ERROR;
    advance_context(request, token);
    return CGAI_STATUS_OK;
}

/** @brief Generate a continuation by repeatedly conditioning on prior output.
 * @param request Borrowed prepared state.
 * @param budget Maximum attempted forward passes for this work quantum.
 * @return OK at EOS, token or work limit, ERROR on numerical or output failure. */
static cgai_status generate_tokens(neural_generation *request, size_t budget) {
    /* Step 1: Bound both attempted network work and successfully emitted output. */
    for (size_t i = 0U; i < budget && request->generated < request->max_tokens &&
                        request->finish != CGAI_NEURAL_ENCODED_REPETITION;
         ++i) {
        if (!generate_next(request))
            return CGAI_STATUS_ERROR;
        /* Step 2: EOS consumes work without appending text or shifting history. */
        if (request->finish == CGAI_NEURAL_ENCODED_EOS)
            return CGAI_STATUS_OK;
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
                                 .capacity = output_size,
                                 .finish = CGAI_NEURAL_ENCODED_LIMIT};
    cgai_status status = prepare_generation(&request, prompt);
    if (status)
        status = generate_tokens(&request, max_tokens);
    cgai_neural_workspace_destroy(request.workspace);
    return status;
}

/** @brief Count requested reusable generation heap bytes without allocating.
 * @param model Borrowed initialized model, or NULL.
 * @return Session and workspace bytes, or zero on invalid shape/overflow. */
size_t cgai_neural_session_bytes(const cgai_neural_model *model) {
    /* Step 1: Ask the allocator's shared workspace sizing helper for its exact payload. */
    const size_t workspace = cgai_neural_workspace_bytes(model);
    size_t bytes = 0U;
    /* Step 2: Account for the request owner, including its fixed context arrays. */
    return workspace != 0U && cgai_size_add(workspace, sizeof(cgai_neural_session), &bytes) ? bytes
                                                                                            : 0U;
}

/** @brief Release scratch without releasing its borrowed model or caller output.
 * @param session Owned session, or NULL; invalid after this call. */
void cgai_neural_session_destroy(cgai_neural_session *session) {
    /* Step 1: Accept constructor cleanup before a session owner exists. */
    if (session == NULL)
        return;
    /* Step 2: Release the numerical owner followed by the resumable request. */
    cgai_neural_workspace_destroy(session->request.workspace);
    free(session);
}

/** @brief Allocate reusable scratch before scheduling gameplay work.
 * @param model Borrowed immutable model that outlives its sessions.
 * @param max_session_bytes Requested heap cap, or zero for no cap.
 * @return Owned idle session, or NULL with a diagnostic. */
cgai_neural_session *cgai_neural_session_create(const cgai_neural_model *model,
                                                size_t max_session_bytes) {
    /* Step 1: Check the complete requested payload before any allocation. */
    cgai_error_clear();
    const size_t bytes = cgai_neural_session_bytes(model);
    if (bytes == 0U || (max_session_bytes != 0U && bytes > max_session_bytes)) {
        cgai_fail("neural session exceeds its memory cap or has an invalid model");
        return NULL;
    }
    cgai_neural_session *session = calloc(1U, sizeof(*session));
    if (session == NULL) {
        cgai_fail("could not allocate neural generation session");
        return NULL;
    }
    /* Step 2: Allocate scratch once, retaining an immutable borrowed model. */
    session->request.model = model;
    session->request.workspace = cgai_neural_workspace_create(model);
    if (session->request.workspace == NULL) {
        cgai_neural_session_destroy(session);
        return NULL;
    }
    return session;
}

/** @brief Start or replace a request using an existing numerical workspace.
 * @param session Borrowed exclusive session.
 * @param prompt Borrowed terminated text, independent from the destination.
 * @param max_tokens Emitted token ceiling, at most one million.
 * @param temperature Finite sampling temperature, 0..100.
 * @param seed Local seed, or zero to use the model seed.
 * @param output Borrowed writable buffer kept alive until replacement or completion.
 * @param output_size Positive destination capacity including NUL.
 * @return OK on preparation, ERROR on invalid arguments or terminal preparation failure. */
cgai_status cgai_neural_session_begin(cgai_neural_session *session, const char *prompt,
                                      size_t max_tokens, double temperature, uint64_t seed,
                                      char *output, size_t output_size) {
    /* Step 1: Reject malformed input without discarding the previous request. */
    cgai_error_clear();
    if (session == NULL || prompt == NULL || output == NULL || output_size == 0U ||
        max_tokens > CGAI_NEURAL_MAX_TOKENS || !isfinite(temperature) || temperature < 0.0 ||
        temperature > 100.0)
        return cgai_fail("invalid neural session generation arguments");
    const cgai_neural_model *model = session->request.model;
    cgai_neural_workspace *workspace = session->request.workspace;
    /* Step 2: Replace request state while keeping the numerical allocation. */
    session->request = (neural_generation){.model = model,
                                           .workspace = workspace,
                                           .max_tokens = max_tokens,
                                           .temperature = temperature,
                                           .state = seed != 0U ? seed : model->config.seed,
                                           .output = output,
                                           .capacity = output_size,
                                           .finish = CGAI_NEURAL_ENCODED_LIMIT};
    session->finish = max_tokens == 0U ? CGAI_NEURAL_FINISH_LIMIT : CGAI_NEURAL_FINISH_RUNNING;
    output[0] = '\0';
    /* Step 3: Tokenize outside scheduled steps; preparation failure is terminal. */
    if (max_tokens != 0U && !prepare_context(&session->request, prompt)) {
        session->finish = CGAI_NEURAL_FINISH_ERROR;
        return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

/** @brief Advance a running session and choose its running or terminal state.
 * @param session Borrowed running session with prepared context and scratch.
 * @param budget Positive maximum attempted passes.
 * @return OK after completed work, ERROR after a terminal failure. */
static cgai_status advance_session(cgai_neural_session *session, size_t budget) {
    /* Step 1: Keep numerical and capacity errors terminal after attempted work. */
    const cgai_status status = generate_tokens(&session->request, budget);
    if (!status)
        session->finish = CGAI_NEURAL_FINISH_ERROR;
    /* Step 2: Successful work either finishes or preserves the pending state. */
    else if (session->request.finish == CGAI_NEURAL_ENCODED_EOS)
        session->finish = CGAI_NEURAL_FINISH_EOS;
    else if (session->request.generated == session->request.max_tokens)
        session->finish = CGAI_NEURAL_FINISH_LIMIT;
    return status;
}

/** @brief Advance at most a caller-selected quantum of complete forward passes.
 * @param session Borrowed exclusive session retaining a live immutable model and output.
 * @param max_forward_passes Work quantum; zero queries status.
 * @param result Borrowed cumulative accounting, including terminal failures.
 * @return OK for idle, running or completed work, ERROR for invalid input or failed work. */
cgai_status cgai_neural_session_step(cgai_neural_session *session, size_t max_forward_passes,
                                     cgai_neural_generation_result *result) {
    /* Step 1: Require valid owners before publishing or advancing any state. */
    cgai_error_clear();
    if (session == NULL || result == NULL)
        return cgai_fail("neural session step requires a session and result");
    cgai_status status = CGAI_STATUS_OK;
    if (session->finish == CGAI_NEURAL_FINISH_ERROR)
        status = cgai_fail("neural session has failed; begin a new request");
    /* Step 2: Count attempted passes independently from successfully appended tokens. */
    if (session->finish == CGAI_NEURAL_FINISH_RUNNING && max_forward_passes != 0U)
        status = advance_session(session, max_forward_passes);
    /* Step 3: Always publish accounting for a valid session, including a failed forward. */
    *result = (cgai_neural_generation_result){
        session->finish, session->request.generated, session->request.forward_passes,
        session->request.prompt_tokens, session->request.unknown_prompt_tokens};
    return status;
}

cgai_status cgai_neural_generate_encoded(const cgai_neural_model *model,
                                         const cgai_neural_encoded_request *encoded,
                                         cgai_neural_encoded_result *result) {
    if (model == NULL || encoded == NULL || result == NULL || encoded->context == NULL ||
        encoded->output == NULL || encoded->output_size == 0U ||
        encoded->fixed >= model->config.context_window ||
        encoded->max_tokens > CGAI_NEURAL_MAX_TOKENS || !isfinite(encoded->temperature) ||
        encoded->temperature < 0.0 || encoded->temperature > 100.0)
        return cgai_fail("invalid encoded neural generation arguments");
    neural_generation request = {.model = model,
                                 .max_tokens = encoded->max_tokens,
                                 .temperature = encoded->temperature,
                                 .state = encoded->seed ? encoded->seed : model->config.seed,
                                 .output = encoded->output,
                                 .capacity = encoded->output_size,
                                 .fixed = encoded->fixed,
                                 .finish = CGAI_NEURAL_ENCODED_LIMIT};
    memcpy(request.context, encoded->context,
           model->config.context_window * sizeof(*request.context));
    request.output[0] = '\0';
    request.workspace = cgai_neural_workspace_create(model);
    const cgai_status status =
        request.workspace ? generate_tokens(&request, encoded->max_tokens) : CGAI_STATUS_ERROR;
    cgai_neural_workspace_destroy(request.workspace);
    if (status)
        *result =
            (cgai_neural_encoded_result){request.generated, request.forward_passes, request.finish};
    return status;
}
