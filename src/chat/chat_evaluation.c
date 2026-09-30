/** @file chat_evaluation.c @brief Independent assistant-only dialogue scoring. */
#include "internal/chat_internal.h"
#include "internal/error.h"
#include <math.h>

/** @brief Accumulate losses for one independent record.
 * @param model Borrowed immutable model.
 * @param record Borrowed prepared record.
 * @param workspace Borrowed scratch.
 * @param result Writable running metrics.
 * @return OK or ERROR on nonfinite predictions. */
static cgai_status score_record(const cgai_chat_model *model, const cgai_chat_record *record,
                                cgai_neural_workspace *workspace, cgai_neural_metrics *result) {
    cgai_token_id context[CGAI_NEURAL_MAX_CONTEXT];
    for (size_t i = 0U; i < record->count; ++i) {
        cgai_chat_context(model, &record->prompt, record->answer, i, context);
        if (!cgai_neural_forward(model->network, context, workspace))
            return CGAI_STATUS_ERROR;
        const double loss = cgai_neural_loss(model->network, workspace, record->answer[i]);
        if (!isfinite(loss))
            return cgai_fail("nonfinite chat evaluation loss");
        ++result->tokens;
        result->cross_entropy += (loss - result->cross_entropy) / (double)result->tokens;
        result->accuracy +=
            cgai_neural_argmax(workspace->probabilities, model->network->output_size).value ==
                    record->answer[i].value
                ? 1.0
                : 0.0;
    }
    result->unknown_tokens += record->unknown;
    return CGAI_STATUS_OK;
}

/** @brief Publish normalized complete metrics.
 * @param result Owned-by-caller running totals.
 * @param metrics Writable successful result. */
static void publish_metrics(cgai_neural_metrics *result, cgai_neural_metrics *metrics) {
    result->accuracy /= (double)result->tokens;
    result->perplexity = exp(result->cross_entropy);
    *metrics = *result;
}

/** @brief Release per-operation evaluation scratch.
 * @param workspace Owned numerical workspace, possibly NULL.
 * @param dataset Mutable dataset owner. */
static void destroy_evaluation(cgai_neural_workspace *workspace, cgai_chat_dataset *dataset) {
    cgai_neural_workspace_destroy(workspace);
    cgai_chat_destroy_dataset(dataset);
}

cgai_status cgai_chat_evaluate(const cgai_chat_model *model, const cgai_chat_example *examples,
                               size_t count, cgai_neural_metrics *metrics) {
    if (model == NULL || metrics == NULL)
        return cgai_fail("chat model and metrics required");
    cgai_chat_dataset dataset = {0};
    cgai_neural_metrics result = {0};
    cgai_status status = cgai_chat_prepare_dataset(model, examples, count, &dataset);
    cgai_neural_workspace *workspace = status ? cgai_neural_workspace_create(model->network) : NULL;
    if (workspace == NULL)
        status = CGAI_STATUS_ERROR;
    for (size_t i = 0U; status && i < count; ++i)
        status = score_record(model, &dataset.records[i], workspace, &result);
    destroy_evaluation(workspace, &dataset);
    if (status)
        publish_metrics(&result, metrics);
    return status;
}
