/** @file model_training.c @brief Online centroid training operations. */

#include "centroid_gai.h"

#include "internal/cgai_internal.h"
#include "internal/error.h"
#include "internal/model_math.h"
#include "internal/tokenizer.h"
#include "internal/vocabulary.h"

#include <stdlib.h>
#include <string.h>

typedef struct training_workspace {
    cgai_token_list tokens;
    cgai_token_id *sequence;
    cgai_token_id *history;
    float *embedding;
    float *scratch;
} training_workspace;

/** Releases all temporary storage used by one training operation. */
static void training_workspace_destroy(training_workspace *workspace) {
    /* Release arrays in reverse conceptual order; free(NULL) remains safe for partial setup. */
    free(workspace->history);
    free(workspace->sequence);
    free(workspace->embedding);
    free(workspace->scratch);
    cgai_token_list_destroy(&workspace->tokens);
}

/** Allocates the fixed-size numeric buffers used while training. */
static cgai_status training_workspace_allocate(const cgai_model *model,
                                               training_workspace *workspace) {
    /* One sequence entry is reserved for the terminal EOS target. */
    workspace->sequence =
        (cgai_token_id *)malloc((workspace->tokens.count + 1U) * sizeof(cgai_token_id));
    workspace->embedding = (float *)malloc(model->config.dimensions * sizeof(float));
    workspace->scratch = (float *)malloc(model->config.dimensions * sizeof(float));
    workspace->history = (cgai_token_id *)malloc(
        (workspace->tokens.count + model->config.context_window + 1U) * sizeof(cgai_token_id));
    /* A failed allocation is reported once; the caller owns cleanup of partial state. */
    if (workspace->sequence == NULL || workspace->embedding == NULL || workspace->scratch == NULL ||
        workspace->history == NULL) {
        return cgai_fail("could not allocate training workspace");
    }
    return CGAI_STATUS_OK;
}

/** Resolves token spellings and appends the terminal EOS target. */
static cgai_status resolve_training_sequence(cgai_model *model, training_workspace *workspace) {
    for (size_t i = 0; i < workspace->tokens.count; ++i) {
        /* Vocabulary insertion returns the stable typed ID used by count arrays. */
        workspace->sequence[i] = cgai_vocabulary_add(model, workspace->tokens.items[i]);
        if (!cgai_token_id_is_valid(workspace->sequence[i])) {
            return CGAI_STATUS_ERROR;
        }
    }
    /* EOS closes the final transition so generation can terminate naturally. */
    workspace->sequence[workspace->tokens.count] = cgai_token_id_from_size(CGAI_TOKEN_EOS);
    return CGAI_STATUS_OK;
}

/** Fills the initial history with the reserved BOS token. */
static size_t seed_training_history(const cgai_model *model, training_workspace *workspace) {
    for (size_t i = 0; i < model->config.context_window; ++i) {
        /* BOS fills missing left context before the first corpus token. */
        workspace->history[i] = cgai_token_id_from_size(CGAI_TOKEN_BOS);
    }
    return model->config.context_window;
}

/** Applies one context embedding to centroid means and transition counts. */
static void train_one_transition(cgai_model *model, training_workspace *workspace,
                                 size_t sequence_index, size_t *history_count) {
    /* Represent the current history before selecting its centroid. */
    cgai_context_embedding(model, workspace->history, *history_count, workspace->embedding,
                           workspace->scratch);
    cgai_centroid_id cluster;
    if (model->initialized_centroids < model->config.centroid_count) {
        /* Early examples seed unused centroids directly from observed contexts. */
        cluster = cgai_centroid_id_from_size(model->initialized_centroids++);
        memcpy(model->centroids + cluster.value * model->config.dimensions, workspace->embedding,
               model->config.dimensions * sizeof(float));
    } else {
        /* Once full, assign the context to the nearest existing centroid. */
        cluster = cgai_nearest_centroid(model, workspace->embedding);
    }
    const uint64_t new_size = ++model->cluster_sizes[cluster.value];
    /* Online k-means uses 1/n so each observation contributes equally over time. */
    const float rate = 1.0F / (float)new_size;
    for (size_t d = 0; d < model->config.dimensions; ++d) {
        /* Move the selected centroid toward the current context by the online rate. */
        float *value = &model->centroids[cluster.value * model->config.dimensions + d];
        *value += rate * (workspace->embedding[d] - *value);
    }
    ++model->token_counts[cluster.value * model->vocabulary_size +
                          workspace->sequence[sequence_index].value];
    /* Record that this centroid predicted the current target token. */
    ++model->examples_seen;
    workspace->history[(*history_count)++] = workspace->sequence[sequence_index];
}

/** Tokenizes input and allocates all workspace required by the training loop. */
static cgai_status prepare_training_workspace(cgai_model *model, const char *text,
                                              training_workspace *workspace) {
    if (model == NULL || text == NULL) {
        return cgai_fail("model and text are required");
    }
    if (cgai_tokenize(text, &workspace->tokens) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    if (workspace->tokens.count == 0U) {
        return cgai_fail("training text contains no tokens");
    }
    if (training_workspace_allocate(model, workspace) != CGAI_STATUS_OK ||
        resolve_training_sequence(model, workspace) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

/** Tokenizes text and updates centroid-conditioned transition counts online. */
cgai_status cgai_model_train_text(cgai_model *model, const char *text) {
    cgai_error_clear();
    training_workspace workspace = {0};
    if (prepare_training_workspace(model, text, &workspace) != CGAI_STATUS_OK) {
        training_workspace_destroy(&workspace);
        return CGAI_STATUS_ERROR;
    }
    /* Seed each example with BOS context before appending observed targets. */
    size_t history_count = seed_training_history(model, &workspace);
    /* Each step embeds context, assigns a centroid, updates its mean, and records a count. */
    for (size_t i = 0; i <= workspace.tokens.count; ++i) {
        train_one_transition(model, &workspace, i, &history_count);
    }
    training_workspace_destroy(&workspace);
    return CGAI_STATUS_OK;
}