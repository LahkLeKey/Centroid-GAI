/** @file cli_neural_evaluation.c @brief Frozen-vocabulary held-out neural evaluation. */
#include "internal/cli_commands.h"
#include "internal/cli_neural.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Score one sequence and display its next-token quality and vocabulary coverage.
 *
 * Metrics include the final EOS target. Unknown coverage counts only input tokens, making it
 * visible when a held-out score primarily reflects mapping unfamiliar words to UNK.
 * @param model Borrowed immutable initialized model.
 * @param text Borrowed nonempty sequence to evaluate.
 * @param label Borrowed display label identifying the dataset and training stage.
 * @return Nonzero after successful scoring, otherwise zero with the API diagnostic preserved.
 */
int cgai_cli_neural_report(const cgai_neural_model *model, const char *text, const char *label) {
    /* Step 1: Compute teacher-forced metrics without adding vocabulary entries. */
    cgai_neural_metrics metrics = {0};
    if (cgai_neural_evaluate(model, text, &metrics) != CGAI_STATUS_OK)
        return 0;
    /* Step 2: Print natural-log cross-entropy and target-level accuracy with coverage counts. */
    printf("%s: targets=%zu cross-entropy=%.6f perplexity=%.6f accuracy=%.2f%% unknown=%zu\n",
           label, metrics.tokens, metrics.cross_entropy, metrics.perplexity,
           metrics.accuracy * 100.0, metrics.unknown_tokens);
    return 1;
}

/**
 * @brief Load a neural artifact and evaluate a separate sequence with its frozen vocabulary.
 *
 * The dispatcher has already validated two path arguments. This command owns its model and
 * text buffer and releases both even when reading or evaluation fails.
 * @param argc Validated argument count of four; retained for the shared CLI handler shape.
 * @param argv Borrowed arguments with model path at two and held-out text path at three.
 * @return Zero for success or one after a runtime diagnostic.
 */
int cgai_cli_neural_evaluate(int argc, char **argv) {
    /* Step 1: Acquire model and corpus ownership independently for common cleanup. */
    (void)argc;
    cgai_neural_model *model = cgai_neural_load(argv[2]);
    if (model == NULL) {
        fprintf(stderr, "evaluation failed: %s\n", cgai_last_error());
        return 1;
    }
    char *text = cgai_cli_neural_text(argv[3]);
    /* Step 2: Score only a complete text buffer and preserve the native failure explanation. */
    const int ok = text != NULL && cgai_cli_neural_report(model, text, "held-out");
    if (!ok && text != NULL)
        fprintf(stderr, "evaluation failed: %s\n", cgai_last_error());
    /* Step 3: Release both owned resources before returning process-style status. */
    free(text);
    cgai_neural_destroy(model);
    return ok ? 0 : 1;
}
