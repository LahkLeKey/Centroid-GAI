/** @file neural_evaluation.c @brief Read-only teacher-forced next-token evaluation. */
#include "internal/error.h"
#include "internal/neural_internal.h"
#include "internal/neural_math.h"
#include <math.h>
#include <stdlib.h>

/** @brief Select the largest output probability with stable first-ID tie breaking.
 * @param probabilities Borrowed output probabilities; BOS is masked.
 * @param count Vocabulary size, at least three.
 * @return Greedy output ID excluding BOS. */
cgai_token_id cgai_neural_argmax(const double *probabilities, size_t count) {
    /* Step 1: Start at EOS so even exact ties cannot select BOS. */
    size_t selected = CGAI_TOKEN_EOS;
    for (size_t i = 2U; i < count; ++i) {
        if (probabilities[i] > probabilities[selected])
            selected = i;
    }
    return cgai_token_id_from_size(selected);
}

/** @brief Score each target using only its preceding ground-truth tokens.
 * @param model Borrowed immutable model.
 * @param sequence Borrowed targets ending in EOS.
 * @param workspace Borrowed mutable numerical scratch space.
 * @param metrics Borrowed initialized metrics with token count populated.
 * @return OK for finite losses, ERROR otherwise; partial metrics are private to caller. */
static cgai_status score_sequence(const cgai_neural_model *model, const cgai_token_id *sequence,
                                  cgai_neural_workspace *workspace, cgai_neural_metrics *metrics) {
    /* Step 1: Reuse one bounded context array for all positions. */
    cgai_token_id context[CGAI_NEURAL_MAX_CONTEXT];
    for (size_t i = 0U; i < metrics->tokens; ++i) {
        cgai_neural_context(model, sequence, i, context);
        if (!cgai_neural_forward(model, context, workspace))
            return CGAI_STATUS_ERROR;
        const double loss = cgai_neural_loss(model, workspace, sequence[i]);
        if (!isfinite(loss))
            return cgai_fail("nonfinite neural evaluation loss");
        /* Update the mean directly so individually finite losses cannot overflow a sum. */
        metrics->cross_entropy += (loss - metrics->cross_entropy) / (double)(i + 1U);
        metrics->accuracy +=
            cgai_neural_argmax(workspace->probabilities, model->vocabulary_size).value ==
                    sequence[i].value
                ? 1.0
                : 0.0;
    }
    /* Step 2: Normalize accuracy and exponentiate the already averaged loss. */
    metrics->accuracy /= (double)metrics->tokens;
    metrics->perplexity = exp(metrics->cross_entropy);
    return CGAI_STATUS_OK;
}

/** @brief Own inference scratch while scoring a nonempty sequence.
 * @param model Borrowed immutable model.
 * @param sequence Borrowed targets including EOS.
 * @param metrics Borrowed private result with target count populated.
 * @return OK on successful scoring, ERROR otherwise. */
static cgai_status evaluate_sequence(const cgai_neural_model *model, const cgai_token_id *sequence,
                                     cgai_neural_metrics *metrics) {
    /* Step 1: Reject empty text before allocating scratch storage. */
    if (metrics->tokens < 2U)
        return cgai_fail("neural evaluation text is empty");
    cgai_neural_workspace *workspace = cgai_neural_workspace_create(model);
    if (workspace == NULL)
        return CGAI_STATUS_ERROR;
    /* Step 2: Score and release scratch on both successful and failed calculation. */
    const cgai_status status = score_sequence(model, sequence, workspace, metrics);
    cgai_neural_workspace_destroy(workspace);
    return status;
}

/** @brief Score a sequence without changing vocabulary or parameters.
 * @param model Borrowed initialized handle.
 * @param text Borrowed nonempty sequence, typically separately held-out text.
 * @param metrics Borrowed writable result; only published on success.
 * @return OK on success, ERROR with a diagnostic otherwise. */
cgai_status cgai_neural_evaluate(const cgai_neural_model *model, const char *text,
                                 cgai_neural_metrics *metrics) {
    /* Step 1: Validate pointers before acquiring sequence and numerical scratch ownership. */
    cgai_error_clear();
    if (model == NULL || text == NULL || metrics == NULL)
        return cgai_fail("neural model, evaluation text and metrics required");
    cgai_neural_metrics result = {0};
    cgai_token_id *sequence =
        cgai_neural_sequence(model, text, &result.tokens, &result.unknown_tokens);
    if (sequence == NULL)
        return CGAI_STATUS_ERROR;
    /* Step 2: Score privately; an empty sequence is not a meaningful language evaluation. */
    const cgai_status status = evaluate_sequence(model, sequence, &result);
    if (status)
        *metrics = result;
    /* Step 3: Release only scratch ownership, keeping model and caller output alive. */
    free(sequence);
    return status;
}
