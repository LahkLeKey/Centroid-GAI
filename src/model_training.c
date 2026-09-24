/** @file model_training.c @brief Online centroid training operations. */

#include "centroid_gai.h"

#include "internal/cgai_internal.h"
#include "internal/error.h"
#include "internal/model_centroid.h"
#include "internal/model_embedding.h"
#include "internal/tokenizer.h"
#include "internal/vocabulary.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Temporary allocations owned by one training call.
 *
 * Initialize with {0} so partial preparation can be cleaned up uniformly. Sequence
 * contains targets; history contains only the context available before the next
 * target. Keeping those arrays distinct prevents the target from leaking into its
 * own input context. The workspace never owns the model or original corpus text.
 */
typedef struct training_workspace {
    cgai_token_list tokens; /**< Owned normalized corpus spellings. */
    cgai_token_id *sequence; /**< Owned target IDs, ending with one additional EOS target. */
    cgai_token_id *history; /**< Owned BOS prefix followed by already-observed target IDs. */
    float *embedding; /**< Owned current dimension-sized context average. */
    float *scratch; /**< Owned dimension-sized temporary token vector. */
} training_workspace;

/**
 * @brief Release the temporary allocations owned by one training attempt.
 *
 * The workspace starts with all pointers zero, so this cleanup also handles preparation failures.
 * It frees numeric buffers and token strings, but not the model or borrowed corpus text. Numeric
 * pointers are not reset here; destroy this workspace once, then stop using it.
 *
 * @param workspace Non-NULL zero-initialized or partially/fully prepared workspace.
 */
static void training_workspace_destroy(training_workspace *workspace) {
    /* Release arrays in reverse conceptual order; free(NULL) remains safe for partial setup. */
    /* Step 1: Free the independently allocated history, sequence, and vector buffers; free accepts NULL. */
    free(workspace->history);
    free(workspace->sequence);
    free(workspace->embedding);
    free(workspace->scratch);
    /* Step 2: Release copied token spellings and their pointer array. */
    cgai_token_list_destroy(&workspace->tokens);
}

/**
 * @brief Allocate token-ID and vector buffers for a training corpus.
 *
 * Sequence stores corpus targets plus one EOS target. History has additional leading slots for
 * BOS context and grows as targets are observed. Embedding and scratch each hold one dimension-sized
 * float vector. No contents are filled yet. Partial allocations remain workspace-owned on failure
 * and must be released by the caller's cleanup path.
 *
 * @param model Non-NULL model with constructor-validated dimensions and context size.
 * @param workspace Mutable workspace whose token list has already been populated.
 * @return CGAI_STATUS_OK when every buffer exists, otherwise CGAI_STATUS_ERROR with a diagnostic.
 */
static cgai_status training_workspace_allocate(const cgai_model *model,
                                               training_workspace *workspace) {
    /* One sequence entry is reserved for the terminal EOS target. */
    /* Step 1: Reserve one ID per token plus the final end-of-sequence target. */
    workspace->sequence =
        (cgai_token_id *)malloc((workspace->tokens.count + 1U) * sizeof(cgai_token_id));
    /* Step 2: Allocate separate context and temporary-token vectors. */
    workspace->embedding = (float *)malloc(model->config.dimensions * sizeof(float));
    workspace->scratch = (float *)malloc(model->config.dimensions * sizeof(float));
    /* Step 3: Reserve initial BOS context plus all training targets. */
    workspace->history = (cgai_token_id *)malloc(
        (workspace->tokens.count + model->config.context_window + 1U) * sizeof(cgai_token_id));
    /* A failed allocation is reported once; the caller owns cleanup of partial state. */
    /* Step 4: Check every allocation; the caller will release partial ownership if any failed. */
    if (workspace->sequence == NULL || workspace->embedding == NULL || workspace->scratch == NULL ||
        workspace->history == NULL) {
        return cgai_fail("could not allocate training workspace");
    }
    return CGAI_STATUS_OK;
}

/**
 * @brief Convert copied token spellings into stable IDs and append EOS.
 *
 * Vocabulary insertion can expand both the vocabulary and its count matrix. Sequence stores typed
 * IDs rather than string pointers, so subsequent vocabulary-array growth does not invalidate the
 * sequence. A failure can leave newly inserted vocabulary entries in the model.
 *
 * @param model Non-NULL mutable model receiving any previously unseen spellings.
 * @param workspace Prepared workspace with tokens.count + 1 writable sequence slots.
 * @return CGAI_STATUS_OK after filling the sequence, otherwise CGAI_STATUS_ERROR without rolling back vocabulary growth.
 */
static cgai_status resolve_training_sequence(cgai_model *model, training_workspace *workspace) {
    /* Step 1: Resolve each corpus spelling through insertion-or-lookup. */
    for (size_t i = 0; i < workspace->tokens.count; ++i) {
        /* Vocabulary insertion returns the stable typed ID used by count arrays. */
        workspace->sequence[i] = cgai_vocabulary_add(model, workspace->tokens.items[i]);
        /* Step 2: Stop if an insertion could not allocate its required storage. */
        if (!cgai_token_id_is_valid(workspace->sequence[i])) {
            return CGAI_STATUS_ERROR;
        }
    }
    /* EOS closes the final transition so generation can terminate naturally. */
    /* Step 3: Append EOS so the corpus teaches a final stopping transition. */
    workspace->sequence[workspace->tokens.count] = cgai_token_id_from_size(CGAI_TOKEN_EOS);
    return CGAI_STATUS_OK;
}

/**
 * @brief Fill the left edge of training history with BOS identifiers.
 *
 * Before the first word, no real previous tokens exist. Repeating the reserved beginning-of-sequence
 * identifier fills the context window so the first transition still has a defined context vector.
 * The returned count tells later code which prefix of the allocated history is initialized.
 *
 * @param model Non-NULL model supplying the positive context-window length.
 * @param workspace Prepared workspace with enough history slots for that window.
 * @return Number of initialized BOS entries.
 */
static size_t seed_training_history(const cgai_model *model, training_workspace *workspace) {
    /* Step 1: Initialize each leading history slot with the BOS control token. */
    for (size_t i = 0; i < model->config.context_window; ++i) {
        /* BOS fills missing left context before the first corpus token. */
        workspace->history[i] = cgai_token_id_from_size(CGAI_TOKEN_BOS);
    }
    /* Step 2: Publish how many history entries the generation-independent training loop may read. */
    return model->config.context_window;
}

/**
 * @brief Learn one context-to-target example and append its target to history.
 *
 * The context is computed before the target is appended, preventing the target from predicting
 * itself. Early examples initialize unused centroids; later ones choose the nearest existing mean.
 * The update mean += (sample - mean) / count is an online average requiring no stored past samples.
 * All model statistics and the caller's history count are mutated in place.
 *
 * @param model Non-NULL mutable model with resolved vocabulary and allocated numeric arrays.
 * @param workspace Prepared buffers containing an initialized history prefix and target sequence.
 * @param sequence_index Index of the target to learn, including the final EOS slot.
 * @param history_count Non-NULL count of initialized history entries; incremented after the update.
 */
static void train_one_transition(cgai_model *model, training_workspace *workspace,
                                 size_t sequence_index, size_t *history_count) {
    /* Represent the current history before selecting its centroid. */
    /* Step 1: Turn only the preceding history into a weighted context vector. */
    cgai_context_embedding(model, workspace->history, *history_count, workspace->embedding,
                           workspace->scratch);
    cgai_centroid_id cluster;
    /* Step 2: Use a fresh centroid when capacity remains; otherwise find the closest learned centroid. */
    if (model->initialized_centroids < model->config.centroid_count) {
        /* Early examples seed unused centroids directly from observed contexts. */
        cluster = cgai_centroid_id_from_size(model->initialized_centroids++);
        memcpy(model->centroids + cluster.value * model->config.dimensions, workspace->embedding,
               model->config.dimensions * sizeof(float));
    } else {
        /* Once full, assign the context to the nearest existing centroid. */
        cluster = cgai_nearest_centroid(model, workspace->embedding);
    }
    /* Step 3: Increment this centroid's observation count and compute its one-over-count learning rate. */
    const uint64_t new_size = ++model->cluster_sizes[cluster.value];
    /* Online k-means uses 1/n so each observation contributes equally over time. */
    const float rate = 1.0F / (float)new_size;
    /* Step 4: Update each component of the selected centroid's running mean. */
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        /* Move the selected centroid toward the current context by the online rate. */
        float *value = &model->centroids[cluster.value * model->config.dimensions + d];
        *value += rate * (workspace->embedding[d] - *value);
    }
    /* Step 5: Record this centroid's target frequency and the total number of learned examples. */
    ++model->token_counts[cluster.value * model->vocabulary_size +
                          workspace->sequence[sequence_index].value];
    /* Record that this centroid predicted the current target token. */
    ++model->examples_seen;
    /* Step 6: Append the observed target so the next transition can use it as context. */
    workspace->history[(*history_count)++] = workspace->sequence[sequence_index];
}

/**
 * @brief Validate a corpus, tokenize it, and prepare the training buffers and IDs.
 *
 * Whitespace-only input produces no tokens and is rejected here even though tokenization succeeds.
 * The caller initializes the workspace to zero and always destroys it after this helper returns.
 * The final ID-resolution phase can mutate vocabulary before a later allocation failure.
 *
 * @param model Model to mutate; NULL is rejected.
 * @param text Borrowed NUL-terminated corpus; NULL is rejected.
 * @param workspace Non-NULL zero-initialized workspace receiving temporary ownership.
 * @return CGAI_STATUS_OK only when training can start; otherwise CGAI_STATUS_ERROR with cleanup left to the caller.
 */
static cgai_status prepare_training_workspace(cgai_model *model, const char *text,
                                              training_workspace *workspace) {
    /* Step 1: Reject missing model or corpus before passing them to private helpers. */
    if (model == NULL || text == NULL) {
        return cgai_fail("model and text are required");
    }
    /* Step 2: Build owned token spellings and require at least one token. */
    if (cgai_tokenize(text, &workspace->tokens) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    if (workspace->tokens.count == 0U) {
        return cgai_fail("training text contains no tokens");
    }
    /* Step 3: Allocate numeric buffers, then resolve spellings into the model's vocabulary IDs. */
    if (training_workspace_allocate(model, workspace) != CGAI_STATUS_OK ||
        resolve_training_sequence(model, workspace) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 4: Confirm every buffer and target is ready for the training loop. */
    return CGAI_STATUS_OK;
}

/**
 * @brief Learn centroid means and next-token frequencies from a complete corpus.
 *
 * Each token is a supervised target predicted from earlier context; EOS adds one more transition.
 * Temporary token strings and numeric arrays belong to this call and are freed before return.
 * The model retains learned state across calls. Training requires exclusive access and does not
 * roll back vocabulary or learned updates when an operation fails.
 *
 * @param model Mutable live model, or NULL to receive an argument error.
 * @param text Borrowed NUL-terminated corpus; must contain at least one token.
 * @return CGAI_STATUS_OK on completion, otherwise CGAI_STATUS_ERROR; cgai_last_error() supplies a diagnostic.
 */
cgai_status cgai_model_train_text(cgai_model *model, const char *text) {
    /* Step 1: Clear the old diagnostic and zero-initialize ownership for safe partial cleanup. */
    cgai_error_clear();
    training_workspace workspace = {0};
    /* Step 2: Prepare tokens and IDs, releasing any partial workspace on failure. */
    if (prepare_training_workspace(model, text, &workspace) != CGAI_STATUS_OK) {
        training_workspace_destroy(&workspace);
        return CGAI_STATUS_ERROR;
    }
    /* Seed each example with BOS context before appending observed targets. */
    /* Step 3: Supply BOS context before learning the first corpus target. */
    size_t history_count = seed_training_history(model, &workspace);
    /* Each step embeds context, assigns a centroid, updates its mean, and records a count. */
    /* Step 4: Learn every corpus token and the EOS target at index tokens.count. */
    for (size_t i = 0; i <= workspace.tokens.count; ++i) {
        train_one_transition(model, &workspace, i, &history_count);
    }
    /* Step 5: Release only temporary storage; the model keeps its learned statistics. */
    training_workspace_destroy(&workspace);
    return CGAI_STATUS_OK;
}
