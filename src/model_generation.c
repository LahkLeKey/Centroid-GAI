/** @file model_generation.c @brief Centroid-conditioned text generation. */

#include "centroid_gai.h"

#include "internal/cgai_internal.h"
#include "internal/error.h"
#include "internal/model_math.h"
#include "internal/model_sampling.h"
#include "internal/tokenizer.h"
#include "internal/vocabulary.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char CGAI_ATTACHED_PUNCTUATION[] = ".,!?;:)]}";

typedef struct generation_workspace {
    cgai_token_list prompt_tokens;
    cgai_token_id *history;
    float *embedding;
    float *scratch;
} generation_workspace;

typedef struct generation_request {
    size_t max_tokens;
    double temperature;
    uint64_t seed;
    char *output;
    size_t output_size;
} generation_request;

/** Releases all temporary storage used by one generation operation. */
static void generation_workspace_destroy(generation_workspace *workspace) {
    /* The workspace may be partially initialized when validation or allocation fails. */
    free(workspace->history);
    free(workspace->embedding);
    free(workspace->scratch);
    cgai_token_list_destroy(&workspace->prompt_tokens);
}

/** Allocates the buffers required by the generation loop. */
static cgai_status generation_workspace_allocate(const cgai_model *model, size_t history_capacity,
                                                 generation_workspace *workspace) {
    /* Keep one slot available even when max_tokens is zero. */
    const size_t history_slots = history_capacity == 0U ? 1U : history_capacity;
    workspace->history = (cgai_token_id *)malloc(history_slots * sizeof(cgai_token_id));
    workspace->embedding = (float *)malloc(model->config.dimensions * sizeof(float));
    workspace->scratch = (float *)malloc(model->config.dimensions * sizeof(float));
    if (workspace->history == NULL || workspace->embedding == NULL || workspace->scratch == NULL) {
        return cgai_fail("could not allocate generation workspace");
    }
    return CGAI_STATUS_OK;
}

/** Tokenizes the prompt, validates capacity arithmetic, and allocates generation buffers. */
static cgai_status prepare_generation(const cgai_model *model, const char *prompt,
                                      const generation_request *request,
                                      generation_workspace *workspace) {
    /* Prompt tokenization determines how many history slots are needed. */
    if (cgai_tokenize(prompt, &workspace->prompt_tokens) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    if (workspace->prompt_tokens.count >
        SIZE_MAX - model->config.context_window - request->max_tokens) {
        return cgai_fail("generation history is too large");
    }
    const size_t history_capacity =
        model->config.context_window + workspace->prompt_tokens.count + request->max_tokens;
    /* Allocation is delayed until the overflow check above has succeeded. */
    return generation_workspace_allocate(model, history_capacity, workspace);
}

/** Validates generation inputs, initializes output, and requires a trained model. */
static cgai_status validate_generation(const cgai_model *model, const char *prompt,
                                       double temperature, char *output, size_t output_size) {
    /* Reject invalid caller state before touching the output buffer. */
    if (model == NULL || prompt == NULL || output == NULL || output_size == 0U ||
        temperature < 0.0 || !isfinite(temperature)) {
        return cgai_fail("invalid generation arguments");
    }
    output[0] = '\0';
    /* A valid model must contain both learned examples and initialized centroids. */
    if (model->examples_seen == 0U || model->initialized_centroids == 0U) {
        return cgai_fail("model has not been trained");
    }
    return CGAI_STATUS_OK;
}

/** Reports whether a token should touch the preceding generated text. */
static int is_attached_punctuation(const char *token) {
    return strlen(token) == 1U && strchr(CGAI_ATTACHED_PUNCTUATION, token[0]) != NULL;
}

/** Appends one generated token while preserving readable punctuation spacing. */
static cgai_status append_token(char *output, size_t output_size, size_t *length, const char *token,
                                int first) {
    /* Words receive separators; punctuation attaches to the previous word. */
    const int needs_space = !first && !is_attached_punctuation(token);
    const size_t token_length = strlen(token);
    const size_t required = *length + (size_t)needs_space + token_length + 1U;
    if (required > output_size) {
        /* Refuse before writing so callers never receive a partial overflow. */
        return cgai_fail("generation output buffer is too small");
    }
    if (needs_space) {
        /* Reserve the separator before copying token bytes. */
        output[(*length)++] = ' ';
    }
    memcpy(output + *length, token, token_length);
    /* Advance the cursor over the copied token and restore the NUL terminator. */
    *length += token_length;
    output[*length] = '\0';
    return CGAI_STATUS_OK;
}

/** Maps prompt tokens into history and substitutes UNKNOWN for unseen words. */
static void prepare_prompt_history(const cgai_model *model, generation_workspace *workspace,
                                   size_t *history_count) {
    *history_count = 0U;
    if (workspace->prompt_tokens.count == 0U) {
        /* An empty prompt starts with a full BOS context window. */
        for (size_t i = 0; i < model->config.context_window; ++i) {
            workspace->history[(*history_count)++] = cgai_token_id_from_size(CGAI_TOKEN_BOS);
        }
        return;
    }
    for (size_t i = 0; i < workspace->prompt_tokens.count; ++i) {
        /* Prompt tokens are looked up without mutating vocabulary during generation. */
        const cgai_token_id id = cgai_vocabulary_find(model, workspace->prompt_tokens.items[i]);
        workspace->history[(*history_count)++] =
            cgai_token_id_is_valid(id) ? id : cgai_token_id_from_size(CGAI_TOKEN_UNKNOWN);
    }
}

/** Runs the token-by-token generation loop and appends readable output. */
static cgai_status generate_tokens(const cgai_model *model, generation_workspace *workspace,
                                   const generation_request *request, size_t *history_count) {
    size_t output_length = 0U;
    uint64_t random_state = request->seed != 0U ? request->seed : model->config.seed;
    /* Feed each selected token back into history so later choices see prior output. */
    for (size_t generated = 0; generated < request->max_tokens; ++generated) {
        /* Recompute context because the previous generated token changed history. */
        cgai_context_embedding(model, workspace->history, *history_count, workspace->embedding,
                               workspace->scratch);
        const cgai_centroid_id cluster = cgai_nearest_centroid(model, workspace->embedding);
        const cgai_token_id token =
            cgai_select_token(model, cluster, request->temperature, &random_state);
        if (token.value == CGAI_TOKEN_EOS) {
            /* EOS is a control token, not user-visible output. */
            break;
        }
        if (append_token(request->output, request->output_size, &output_length,
                         model->vocabulary[token.value], generated == 0U) != CGAI_STATUS_OK) {
            return CGAI_STATUS_ERROR;
        }
        workspace->history[(*history_count)++] = token;
        /* The next iteration sees this token as the newest context element. */
    }
    return CGAI_STATUS_OK;
}

/** Generates a continuation from prompt context using the learned centroids. */
cgai_status cgai_model_generate(const cgai_model *model, const char *prompt, size_t max_tokens,
                                double temperature, uint64_t seed, char *output,
                                size_t output_size) {
    cgai_error_clear();
    if (validate_generation(model, prompt, temperature, output, output_size) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    const generation_request request = {max_tokens, temperature, seed, output, output_size};
    generation_workspace workspace = {0};
    if (prepare_generation(model, prompt, &request, &workspace) != CGAI_STATUS_OK) {
        generation_workspace_destroy(&workspace);
        return CGAI_STATUS_ERROR;
    }
    size_t history_count = 0U;
    prepare_prompt_history(model, &workspace, &history_count);
    if (generate_tokens(model, &workspace, &request, &history_count) != CGAI_STATUS_OK) {
        generation_workspace_destroy(&workspace);
        return CGAI_STATUS_ERROR;
    }
    generation_workspace_destroy(&workspace);
    return CGAI_STATUS_OK;
}