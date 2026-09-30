/** @file neural_vocabulary.c @brief Frozen neural vocabulary and causal context construction. */
#include "internal/error.h"
#include "internal/neural_internal.h"
#include <stdlib.h>
#include <string.h>

/** @brief Bound text and token counts before network work.
 * @param text Borrowed NUL-terminated text.
 * @param tokens Borrowed empty list receiving owned token strings.
 * @return OK on tokenization, ERROR otherwise; caller destroys the list. */
static cgai_status tokenize_bounded(const char *text, cgai_token_list *tokens) {
    /* Step 1: Bound input bytes as well as the number of prediction targets. */
    if (strlen(text) > 16777216U)
        return cgai_fail("neural text exceeds 16 MiB");
    if (!cgai_tokenize(text, tokens))
        return CGAI_STATUS_ERROR;
    if (tokens->count > CGAI_NEURAL_MAX_TOKENS)
        return cgai_fail("too many neural text tokens");
    /* Step 2: Keep individual spellings bounded for later artifact storage. */
    for (size_t i = 0U; i < tokens->count; ++i) {
        if (strlen(tokens->items[i]) > CGAI_NEURAL_MAX_TOKEN_BYTES)
            return cgai_fail("neural token spelling is too long");
    }
    return CGAI_STATUS_OK;
}

/** @brief Search the fixed vocabulary without changing it.
 * @param model Borrowed handle.
 * @param spelling Borrowed normalized token string.
 * @return Existing token ID, or the shared UNK ID. */
cgai_token_id cgai_neural_lookup(const cgai_neural_model *model, const char *spelling) {
    /* Step 1: Compare spellings; first-occurrence insertion keeps IDs deterministic. */
    for (size_t i = 0U; i < model->vocabulary_size; ++i) {
        if (strcmp(model->vocabulary[i], spelling) == 0)
            return cgai_token_id_from_size(i);
    }
    return cgai_token_id_from_size(CGAI_TOKEN_UNKNOWN);
}

/** @brief Copy one previously absent spelling into owned vocabulary storage.
 * @param model Mutable shell with capacity for the maximum pointer count.
 * @param spelling Borrowed source string.
 * @return OK on append, ERROR without publishing an incomplete string. */
cgai_status cgai_neural_append_spelling(cgai_neural_model *model, const char *spelling) {
    /* Step 1: Enforce the vocabulary resource bound before allocation. */
    if (model->vocabulary_size >= CGAI_NEURAL_MAX_VOCABULARY)
        return cgai_fail("neural vocabulary limit exceeded");
    const size_t bytes = strlen(spelling) + 1U;
    char *copy = malloc(bytes);
    if (copy == NULL)
        return cgai_fail("could not allocate neural token");
    /* Step 2: Publish only a complete owned spelling. */
    memcpy(copy, spelling, bytes);
    model->vocabulary[model->vocabulary_size++] = copy;
    return CGAI_STATUS_OK;
}

/** @brief Initialize control spellings and append unique training words.
 * @param model Mutable empty shell receiving owned vocabulary.
 * @param tokens Borrowed nonempty normalized training tokens.
 * @return OK on completion, ERROR with partial state owned by model. */
static cgai_status build_spellings(cgai_neural_model *model, const cgai_token_list *tokens) {
    /* Step 1: Reserve stable IDs for context padding, sequence end and unknown words. */
    model->vocabulary = calloc(CGAI_NEURAL_MAX_VOCABULARY, sizeof(char *));
    if (model->vocabulary == NULL)
        return cgai_fail("could not allocate neural vocabulary");
    if (!cgai_neural_append_spelling(model, "<bos>") ||
        !cgai_neural_append_spelling(model, "<eos>") ||
        !cgai_neural_append_spelling(model, "<unk>"))
        return CGAI_STATUS_ERROR;
    /* Step 2: Grow only from training text; later evaluation never inserts words. */
    for (size_t i = 0U; i < tokens->count; ++i) {
        if (cgai_neural_lookup(model, tokens->items[i]).value == CGAI_TOKEN_UNKNOWN &&
            strcmp(tokens->items[i], "<unk>") != 0 &&
            !cgai_neural_append_spelling(model, tokens->items[i]))
            return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

/** @brief Tokenize training text and establish the frozen vocabulary.
 * @param model Mutable empty shell.
 * @param text Borrowed training text.
 * @return OK on completion, ERROR otherwise; model owns partial vocabulary. */
cgai_status cgai_neural_build_vocabulary(cgai_neural_model *model, const char *text) {
    /* Step 1: Own temporary normalized spellings separately from the model. */
    cgai_token_list tokens = {0};
    cgai_status status = tokenize_bounded(text, &tokens);
    if (status && tokens.count == 0U)
        status = cgai_fail("neural vocabulary text is empty");
    /* Step 2: Copy unique entries, then release all temporary spellings. */
    if (status)
        status = build_spellings(model, &tokens);
    cgai_token_list_destroy(&tokens);
    return status;
}

/** @brief Convert normalized spellings into frozen IDs and add EOS.
 * @param model Borrowed handle.
 * @param tokens Borrowed list of input tokens.
 * @param unknown Writable count of UNK mappings.
 * @return Owned ID array or NULL; caller frees it. */
static cgai_token_id *map_sequence(const cgai_neural_model *model, const cgai_token_list *tokens,
                                   size_t *unknown) {
    /* Step 1: Reserve one additional target for the end of this sequence. */
    cgai_token_id *sequence = malloc((tokens->count + 1U) * sizeof(*sequence));
    if (sequence == NULL) {
        cgai_fail("could not allocate neural token sequence");
        return NULL;
    }
    *unknown = 0U;
    /* Step 2: Track unknown words explicitly for held-out diagnostics. */
    for (size_t i = 0U; i < tokens->count; ++i) {
        sequence[i] = cgai_neural_lookup(model, tokens->items[i]);
        *unknown += sequence[i].value == CGAI_TOKEN_UNKNOWN ? 1U : 0U;
    }
    sequence[tokens->count] = cgai_token_id_from_size(CGAI_TOKEN_EOS);
    return sequence;
}

/** @brief Tokenize a sequence using only the model's existing vocabulary.
 * @param model Borrowed handle.
 * @param text Borrowed text.
 * @param count Writable ID count including final EOS.
 * @param unknown Writable UNK count excluding EOS.
 * @return Owned sequence or NULL; caller frees the result. */
cgai_token_id *cgai_neural_sequence(const cgai_neural_model *model, const char *text, size_t *count,
                                    size_t *unknown) {
    /* Step 1: Tokenize into temporary ownership before allocating IDs. */
    cgai_token_list tokens = {0};
    cgai_token_id *sequence = NULL;
    *count = 0U;
    *unknown = 0U;
    if (tokenize_bounded(text, &tokens)) {
        sequence = map_sequence(model, &tokens, unknown);
        if (sequence != NULL)
            *count = tokens.count + 1U;
    }
    /* Step 2: Retain only IDs; normalized spellings are no longer needed. */
    cgai_token_list_destroy(&tokens);
    return sequence;
}

/** @brief Preserve token order and exclude the prediction target from context.
 * @param model Borrowed handle.
 * @param sequence Borrowed initialized prefix of at least position IDs.
 * @param position Number of preceding tokens, also the target position.
 * @param context Writable fixed-window array. */
void cgai_neural_context(const cgai_neural_model *model, const cgai_token_id *sequence,
                         size_t position, cgai_token_id *context) {
    /* Step 1: Left-pad absent history; recent real tokens occupy their own positions. */
    const size_t window = model->config.context_window;
    for (size_t i = 0U; i < window; ++i) {
        context[i] = position + i < window ? cgai_token_id_from_size(CGAI_TOKEN_BOS)
                                           : sequence[position + i - window];
    }
}
