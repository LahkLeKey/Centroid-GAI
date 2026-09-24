/** @file model_generation_workspace.c @brief Prompt history and generation workspace ownership. */
#include "internal/error.h"
#include "internal/model_generation.h"
#include "internal/size_utils.h"
#include "internal/vocabulary.h"
#include <stdlib.h>

/**
 * @brief Release every temporary allocation used to prepare and generate a continuation.
 *
 * Zero initialization makes cleanup safe after any failed preparation step. Token spellings and
 * numeric/ID arrays have distinct allocations. The model and prompt are borrowed and are not freed.
 * The numeric pointers are not reset; callers must discard the workspace after this one cleanup.
 *
 * @param workspace Non-NULL zero-initialized, partially initialized, or complete workspace.
 */
void cgai_generation_workspace_destroy(generation_workspace *workspace) {
    /* The workspace may be partially initialized when validation or allocation fails. */
    /* Step 1: Release history and both vector allocations, accepting any still-NULL pointers. */
    free(workspace->history);
    free(workspace->embedding);
    free(workspace->scratch);
    /* Step 2: Free copied prompt spellings and their pointer list. */
    cgai_token_list_destroy(&workspace->prompt_tokens);
}

/**
 * @brief Allocate checked history storage and reusable generation vectors.
 *
 * History capacity is expressed in token IDs, so it must be multiplied by sizeof(cgai_token_id)
 * before allocating bytes. Keeping at least one slot avoids zero-size allocation ambiguity.
 * The dimensions come from a validated model. On failure, the caller still owns partial workspace
 * allocations and must invoke the workspace destructor.
 *
 * @param model Non-NULL model supplying the vector dimension count.
 * @param history_capacity Number of token-ID slots requested for initial context and generated tokens.
 * @param workspace Non-NULL workspace with empty numeric-pointer fields.
 * @return CGAI_STATUS_OK when all allocations succeed, otherwise CGAI_STATUS_ERROR with a diagnostic.
 */
static cgai_status generation_workspace_allocate(const cgai_model *model, size_t history_capacity,
                                                 generation_workspace *workspace) {
    /* Keep one slot available even when max_tokens is zero. */
    /* Step 1: Choose a nonzero slot count and check its conversion to an allocation byte count. */
    const size_t history_slots = history_capacity == 0U ? 1U : history_capacity;
    size_t history_bytes = 0U;
    if (!cgai_size_mul(history_slots, sizeof(cgai_token_id), &history_bytes)) {
        return cgai_fail("generation history is too large");
    }
    /* Step 2: Allocate history and two independent float vectors. */
    workspace->history = (cgai_token_id *)malloc(history_bytes);
    workspace->embedding = (float *)malloc(model->config.dimensions * sizeof(float));
    workspace->scratch = (float *)malloc(model->config.dimensions * sizeof(float));
    /* Step 3: Detect any failed allocation while leaving partial cleanup to the caller. */
    if (workspace->history == NULL || workspace->embedding == NULL || workspace->scratch == NULL) {
        return cgai_fail("could not allocate generation workspace");
    }
    return CGAI_STATUS_OK;
}

/**
 * @brief Tokenize the prompt and reserve enough history for the complete request.
 *
 * Prompt tokenization determines the initial number of IDs. The allocation includes the context
 * window for an empty prompt's BOS prefix, all prompt tokens, and every possible generated token.
 * Checked additions reject wraparound before allocation. This function allocates storage but does
 * not initialize the history IDs; cgai_generation_prepare_history() performs that second phase.
 *
 * @param model Non-NULL validated model, borrowed without mutation.
 * @param prompt Non-NULL borrowed NUL-terminated prompt.
 * @param max_tokens Maximum generated IDs to reserve in addition to initial context.
 * @param workspace Non-NULL zero-initialized workspace receiving ownership, even on partial failure.
 * @return CGAI_STATUS_OK when storage is ready, otherwise CGAI_STATUS_ERROR; always destroy the workspace.
 */
cgai_status cgai_generation_prepare(const cgai_model *model, const char *prompt, size_t max_tokens,
                                    generation_workspace *workspace) {
    /* Prompt tokenization determines how many history slots are needed. */
    /* Step 1: Copy prompt tokens into workspace-owned storage. */
    if (cgai_tokenize(prompt, &workspace->prompt_tokens) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 2: Add the context, prompt, and output counts with overflow detection. */
    size_t history_capacity = 0U;
    if (!cgai_size_add(model->config.context_window, workspace->prompt_tokens.count,
                       &history_capacity) ||
        !cgai_size_add(history_capacity, max_tokens, &history_capacity)) {
        return cgai_fail("generation history is too large");
    }
    /* Allocation is delayed until the overflow check above has succeeded. */
    /* Step 3: Allocate ID and vector arrays only after the total slot count is valid. */
    return generation_workspace_allocate(model, history_capacity, workspace);
}

/**
 * @brief Fill the initialized prefix of history from prompt tokens or BOS context.
 *
 * Prompt lookup never adds vocabulary during generation. Unknown spellings map to UNKNOWN so all
 * history IDs remain valid. If tokenization found no prompt tokens, BOS fills an entire context
 * window. The post-increment expression writes at the old count, then advances the count.
 *
 * @param model Non-NULL borrowed model with reserved vocabulary entries.
 * @param workspace Prepared workspace containing prompt tokens and sufficient writable history slots.
 * @param history_count Non-NULL output receiving the number of initialized history IDs.
 */
void cgai_generation_prepare_history(const cgai_model *model, generation_workspace *workspace,
                                     size_t *history_count) {
    /* Step 1: Start the initialized prefix at zero before writing any IDs. */
    *history_count = 0U;
    /* Step 2: Use repeated BOS IDs when the prompt has no tokens, then finish early. */
    if (workspace->prompt_tokens.count == 0U) {
        /* An empty prompt starts with a full BOS context window. */
        /* Step 3: Look up each prompt spelling and append its ID or the UNKNOWN replacement. */
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
