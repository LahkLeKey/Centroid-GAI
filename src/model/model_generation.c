/** @file model_generation.c @brief Centroid-conditioned text generation. */

#include "centroid_gai.h"

#include "internal/cgai_internal.h"
#include "internal/error.h"
#include "internal/model_centroid.h"
#include "internal/model_embedding.h"
#include "internal/model_generation.h"
#include "internal/model_sampling.h"
#include "internal/tokenizer.h"

#include <math.h>
#include <string.h>

/** One-byte tokens that attach to preceding generated text without a separator. */
static const char CGAI_ATTACHED_PUNCTUATION[] = ".,!?;:)]}";

/**
 * @brief Borrowed settings and destination for the generation loop.
 *
 * This stack value owns no allocations. Grouping related fields keeps the loop's
 * argument list small while preserving explicit destination capacity. The model
 * and temporary workspace remain separate because their lifetimes differ.
 */
typedef struct generation_request {
    size_t max_tokens;  /**< Upper bound on emitted tokens. */
    double temperature; /**< Finite nonnegative sampling setting; zero means greedy. */
    uint64_t seed;      /**< Caller seed, with zero selecting the configured model seed. */
    char *output;       /**< Borrowed writable continuation buffer. */
    size_t output_size; /**< Destination bytes including room for the final NUL. */
} generation_request;

/**
 * @brief Check generation arguments and require learned state before sampling.
 *
 * NULL pointers, zero destination capacity, and negative/nonfinite temperatures are rejected before
 * output is touched. Once basic arguments are accepted, output is set to an empty C string. A model
 * must contain learned examples and at least one initialized centroid, including for a zero-token
 * request.
 *
 * @param model Borrowed model pointer to validate.
 * @param prompt Borrowed prompt pointer to validate.
 * @param temperature Requested sampling temperature.
 * @param output Caller-owned writable character buffer.
 * @param output_size Buffer capacity in bytes, including space for a terminator.
 * @return CGAI_STATUS_OK for usable inputs, otherwise CGAI_STATUS_ERROR with a diagnostic.
 */
static cgai_status validate_generation(const cgai_model *model, const char *prompt,
                                       double temperature, char *output, size_t output_size) {
    /* Reject invalid caller state before touching the output buffer. */
    /* Step 1: Validate pointers, output capacity, and the temperature's numeric domain. */
    if (model == NULL || prompt == NULL || output == NULL || output_size == 0U ||
        temperature < 0.0 || !isfinite(temperature)) {
        return cgai_fail("invalid generation arguments");
    }
    /* Step 2: Initialize the continuation to an empty terminated string. */
    output[0] = '\0';
    /* A valid model must contain both learned examples and initialized centroids. */
    /* Step 3: Require learned statistics before centroid lookup is possible. */
    if (model->examples_seen == 0U || model->initialized_centroids == 0U) {
        return cgai_fail("model has not been trained");
    }
    return CGAI_STATUS_OK;
}

/**
 * @brief Decide whether a one-byte punctuation token should omit a preceding space.
 *
 * Only spellings of length one are eligible. strchr searches the fixed punctuation string for that
 * single byte; a non-NULL pointer means the character was found. Multi-byte tokens and punctuation
 * outside this list follow the ordinary word-spacing rule.
 *
 * @param token Non-NULL borrowed NUL-terminated token spelling.
 * @return Nonzero if this token should touch the preceding generated text, otherwise zero.
 */
static int is_attached_punctuation(const char *token) {
    /* Step 1: Require one byte and membership in the attached-punctuation set. */
    return strlen(token) == 1U && strchr(CGAI_ATTACHED_PUNCTUATION, token[0]) != NULL;
}

/**
 * @brief Append one token while preserving termination and readable punctuation spacing.
 *
 * The initialized output prefix has length bytes and already ends with NUL. A separator is added
 * for ordinary tokens after the first token, but punctuation can attach directly. Capacity includes
 * the future NUL byte. A short buffer is rejected before writing this token, leaving the previously
 * generated prefix intact rather than erasing it.
 *
 * @param output Non-NULL writable destination containing the current continuation.
 * @param output_size Total available destination bytes.
 * @param length Non-NULL current text length, excluding NUL; updated on success.
 * @param token Borrowed NUL-terminated spelling that does not overlap output.
 * @param first Nonzero if no earlier generated token has been appended.
 * @return CGAI_STATUS_OK after append, otherwise CGAI_STATUS_ERROR with the previous prefix
 * preserved.
 */
static cgai_status append_token(char *output, size_t output_size, size_t *length, const char *token,
                                int first) {
    /* Words receive separators; punctuation attaches to the previous word. */
    /* Step 1: Decide whether this token needs a separating space. */
    const int needs_space = !first && !is_attached_punctuation(token);
    const size_t token_length = strlen(token);
    /* Step 2: Include existing text, separator, token bytes, and the final NUL in the capacity
     * check. */
    const size_t required = *length + (size_t)needs_space + token_length + 1U;
    if (required > output_size) {
        /* Refuse before writing so callers never receive a partial overflow. */
        return cgai_fail("generation output buffer is too small");
    }
    /* Step 3: Write the optional separator only after confirming the full token will fit. */
    if (needs_space) {
        /* Reserve the separator before copying token bytes. */
        output[(*length)++] = ' ';
    }
    /* Step 4: Copy the token's bytes, advance the text length, and restore the trailing NUL. */
    memcpy(output + *length, token, token_length);
    /* Advance the cursor over the copied token and restore the NUL terminator. */
    *length += token_length;
    output[*length] = '\0';
    return CGAI_STATUS_OK;
}

/**
 * @brief Repeatedly select the next token and feed it back into the context history.
 *
 * The model is read-only: generation records new IDs only in call-local history. The random state
 * is also local, so the same seed and state produce reproducible output. EOS is a control signal
 * and is not appended. On output-capacity failure, any previous continuation remains in the buffer.
 *
 * @param model Non-NULL trained model with stable vocabulary and counts.
 * @param workspace Prepared vectors and sufficient history slots, with its initial prefix already
 * filled.
 * @param request Non-NULL borrowed generation settings and caller-owned output buffer descriptor.
 * @param history_count Non-NULL count of initialized IDs; advanced after each emitted non-EOS
 * token.
 * @return CGAI_STATUS_OK after reaching EOS or the token limit, otherwise CGAI_STATUS_ERROR.
 */
static cgai_status generate_tokens(const cgai_model *model, generation_workspace *workspace,
                                   const generation_request *request, size_t *history_count) {
    /* Step 1: Track continuation length separately from history length and choose the effective
     * seed. */
    size_t output_length = 0U;
    uint64_t random_state = request->seed != 0U ? request->seed : model->config.seed;
    /* Feed each selected token back into history so later choices see prior output. */
    /* Step 2: Repeat at most max_tokens times, rebuilding context after every generated token. */
    for (size_t generated = 0; generated < request->max_tokens; ++generated) {
        /* Recompute context because the previous generated token changed history. */
        cgai_context_embedding(model, workspace->history, *history_count, workspace->embedding,
                               workspace->scratch);
        /* Step 3: Find the nearest learned context and sample its next-token distribution. */
        const cgai_centroid_id cluster = cgai_nearest_centroid(model, workspace->embedding);
        const cgai_token_id token =
            cgai_select_token(model, cluster, request->temperature, &random_state);
        /* Step 4: Stop without writing the EOS control token into user-visible text. */
        if (token.value == CGAI_TOKEN_EOS) {
            /* EOS is a control token, not user-visible output. */
            break;
        }
        /* Step 5: Append the chosen spelling, stopping on insufficient output capacity. */
        if (append_token(request->output, request->output_size, &output_length,
                         model->vocabulary[token.value], generated == 0U) != CGAI_STATUS_OK) {
            return CGAI_STATUS_ERROR;
        }
        /* Step 6: Add the chosen ID to context for the next iteration. */
        workspace->history[(*history_count)++] = token;
        /* The next iteration sees this token as the newest context element. */
    }
    return CGAI_STATUS_OK;
}

/**
 * @brief Generate a continuation into caller-owned storage without changing the model.
 *
 * Only the continuation is written; callers display the original prompt separately if desired.
 * The operation borrows prompt/model/output, allocates a temporary workspace, and frees it on every
 * exit after preparation. Zero tokens yields an empty string for a valid trained model. Callers may
 * read one model concurrently only while no thread trains or destroys it.
 *
 * @param model Borrowed trained model; NULL or an untrained model is rejected.
 * @param prompt Borrowed NUL-terminated prompt; empty text uses BOS context.
 * @param max_tokens Upper limit on emitted tokens and on reserved generated-history slots.
 * @param temperature Finite nonnegative temperature; zero uses greedy selection.
 * @param seed Sampling seed; zero selects the model's configured seed.
 * @param output Writable caller-owned buffer for continuation bytes and NUL.
 * @param output_size Total capacity of output in bytes.
 * @return CGAI_STATUS_OK on completion, otherwise CGAI_STATUS_ERROR with cgai_last_error()
 * describing failure.
 */
cgai_status cgai_model_generate(const cgai_model *model, const char *prompt, size_t max_tokens,
                                double temperature, uint64_t seed, char *output,
                                size_t output_size) {
    /* Step 1: Reset diagnostics and validate arguments before allocating temporary state. */
    cgai_error_clear();
    if (validate_generation(model, prompt, temperature, output, output_size) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 2: Package borrowed output/settings and zero-initialize the workspace ownership fields.
     */
    const generation_request request = {max_tokens, temperature, seed, output, output_size};
    generation_workspace workspace = {0};
    /* Step 3: Prepare prompt tokens and buffers, releasing partial workspace on failure. */
    if (cgai_generation_prepare(model, prompt, max_tokens, &workspace) != CGAI_STATUS_OK) {
        cgai_generation_workspace_destroy(&workspace);
        return CGAI_STATUS_ERROR;
    }
    /* Step 4: Resolve initial prompt/BOS history into valid model token IDs. */
    size_t history_count = 0U;
    cgai_generation_prepare_history(model, &workspace, &history_count);
    /* Step 5: Run the generation loop and clean up if it cannot complete. */
    if (generate_tokens(model, &workspace, &request, &history_count) != CGAI_STATUS_OK) {
        cgai_generation_workspace_destroy(&workspace);
        return CGAI_STATUS_ERROR;
    }
    /* Step 6: Release temporary storage while preserving the caller's generated string. */
    cgai_generation_workspace_destroy(&workspace);
    return CGAI_STATUS_OK;
}
