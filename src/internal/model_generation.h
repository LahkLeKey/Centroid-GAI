/** @file model_generation.h @brief Private generation workspace lifetime. */
#ifndef CGAI_MODEL_GENERATION_H
#define CGAI_MODEL_GENERATION_H
#include "cgai_internal.h"
#include "tokenizer.h"

/**
 * @brief Allocations owned by one generation call, independent of model storage.
 *
 * Initialize this descriptor with {0} before preparation. A failed allocation can
 * leave a mixture of valid and NULL pointers; the destructor handles both. History
 * capacity is allocated up front, but a separate history_count tracks which entries
 * have actually been filled. Neither the model nor the borrowed prompt is owned here.
 */
typedef struct generation_workspace {
    cgai_token_list prompt_tokens; /**< Owned normalized prompt spellings and pointer list. */
    cgai_token_id *history; /**< Owned IDs for initial context plus all possible generated tokens. */
    float *embedding; /**< Owned dimension-sized vector for the current averaged context. */
    float *scratch; /**< Owned dimension-sized vector reused for each token's embedding. */
} generation_workspace;

/**
 * @brief Release every temporary allocation used to prepare and generate a continuation.
 *
 * Zero initialization makes cleanup safe after any failed preparation step. Token spellings and
 * numeric/ID arrays have distinct allocations. The model and prompt are borrowed and are not freed.
 * The numeric pointers are not reset; callers must discard the workspace after this one cleanup.
 *
 * @param workspace Non-NULL zero-initialized, partially initialized, or complete workspace.
 */
void cgai_generation_workspace_destroy(generation_workspace *workspace);
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
                                    generation_workspace *workspace);
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
                                     size_t *history_count);
#endif
