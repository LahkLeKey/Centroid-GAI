/** @file cli_neural_step.c @brief Propose deterministic training steps with a held-out gate. */
#include "internal/cli_commands.h"
#include "internal/cli_neural.h"
#include "internal/error.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** All owned inputs and measured evidence for one in-memory candidate. */
typedef struct neural_candidate {
    cgai_neural_model *model; /**< Owned parent model, mutated only in memory. */
    char *train;              /**< Owned training corpus. */
    char *heldout; /**< Owned validation corpus, never used in vocabulary construction. */
    cgai_neural_metrics train_before; /**< Training score before the proposal. */
    cgai_neural_metrics train_after;  /**< Training score after the proposal. */
    cgai_neural_metrics before;       /**< Held-out score before the proposal. */
    cgai_neural_metrics after;        /**< Held-out score after the proposal. */
    cgai_neural_progress parent;      /**< Parent full-epoch progress. */
} neural_candidate;

/** @brief Parse a positive epoch count and learning rate before touching any files.
 * @param argc Dispatcher-validated count of six through eight.
 * @param argv Borrowed command arguments; options start at six.
 * @param training Writable default settings.
 * @return Nonzero for supported values; settings are scratch on failure. */
static int step_options(int argc, char **argv, cgai_neural_training *training) {
    uint64_t epochs = 1U;
    training->learning_rate = 0.015;
    if (argc >= 7 && (!cgai_cli_neural_unsigned(argv[6], 10000U, &epochs) || epochs == 0U))
        return 0;
    training->epochs = (size_t)epochs;
    return argc < 8 || (cgai_cli_neural_real(argv[7], 1.0, &training->learning_rate) &&
                        training->learning_rate > 0.0);
}

/** @brief Score a fully covered corpus and reject nonfinite or UNK-dominated evidence.
 * @param model Borrowed immutable checkpoint model.
 * @param text Borrowed nonempty corpus.
 * @param metrics Writable score, published by the neural evaluator.
 * @return Nonzero for finite loss with every spelling in the frozen vocabulary. */
static int covered_score(const cgai_neural_model *model, const char *text,
                         cgai_neural_metrics *metrics) {
    if (cgai_neural_evaluate(model, text, metrics) != CGAI_STATUS_OK)
        return 0;
    if (metrics->unknown_tokens != 0U || !isfinite(metrics->cross_entropy))
        return cgai_fail("candidate corpus must have zero unknown tokens and finite loss");
    return 1;
}

/** @brief Measure the parent, continue its optimizer, then apply the held-out acceptance gate.
 * @param candidate Borrowed prepared candidate and owned buffers.
 * @param training Borrowed validated settings; held-out text is excluded from updates.
 * @return Nonzero only if held-out cross-entropy strictly decreases. */
static int fit_candidate(neural_candidate *candidate, const cgai_neural_training *training) {
    /* Step 1: Validate corpus coverage before the first update. */
    if (!covered_score(candidate->model, candidate->train, &candidate->train_before) ||
        !covered_score(candidate->model, candidate->heldout, &candidate->before))
        return 0;
    candidate->parent = cgai_neural_get_progress(candidate->model);
    /* Step 2: Continue full epochs with persistent moments and shuffle state. */
    if (cgai_neural_train_continue(candidate->model, candidate->train, training) !=
            CGAI_STATUS_OK ||
        !covered_score(candidate->model, candidate->train, &candidate->train_after) ||
        !covered_score(candidate->model, candidate->heldout, &candidate->after))
        return 0;
    if (candidate->after.cross_entropy >= candidate->before.cross_entropy)
        return cgai_fail("candidate rejected: held-out cross-entropy did not decrease");
    return 1;
}

/** @brief Print deterministic tab-separated evidence without timestamps or machine paths.
 * @param label Borrowed row label.
 * @param progress Completed training counters.
 * @param train Borrowed training metrics.
 * @param heldout Borrowed validation metrics. */
static void print_score(const char *label, cgai_neural_progress progress,
                        const cgai_neural_metrics *train, const cgai_neural_metrics *heldout) {
    printf("%s\t%" PRIu64 "\t%" PRIu64 "\t%.17g\t%.17g\t%.17g\t%zu\n", label, progress.epochs,
           progress.steps, train->cross_entropy, heldout->cross_entropy, heldout->accuracy,
           heldout->unknown_tokens);
}

/** @brief Publish an accepted candidate and print evidence after the complete save succeeds.
 * @param candidate Borrowed fitted candidate.
 * @param path Borrowed checkpoint destination.
 * @return Nonzero after the accepted checkpoint is completely written. */
static int publish_candidate(const neural_candidate *candidate, const char *path) {
    if (cgai_neural_checkpoint_save(candidate->model, path) != CGAI_STATUS_OK)
        return 0;
    printf("stage\tepochs\tsteps\ttrain_cross_entropy\theldout_cross_entropy\theldout_accuracy"
           "\theldout_unknown\n");
    print_score("before", candidate->parent, &candidate->train_before, &candidate->before);
    print_score("after", cgai_neural_get_progress(candidate->model), &candidate->train_after,
                &candidate->after);
    return 1;
}

/** @brief Release candidate-owned inputs and model after success or failure.
 * @param candidate Borrowed zero-initialized or prepared candidate; ownership ends here. */
static void destroy_candidate(neural_candidate *candidate) {
    /* Step 1: Release each independent allocation after all evidence has been reported. */
    free(candidate->heldout);
    free(candidate->train);
    cgai_neural_destroy(candidate->model);
}

/** @brief Load independent corpora and propose a measured continuation of a saved model.
 * @param argc Dispatcher-validated count of six through eight.
 * @param argv Borrowed checkpoint, training, held-out, destination and optional settings.
 * @return Zero for acceptance, two for invalid settings, otherwise one. Rejections never
 * open the destination; write failures follow the checkpoint save contract. */
int cgai_cli_neural_step(int argc, char **argv) {
    cgai_neural_training training = cgai_neural_default_training();
    if (!step_options(argc, argv, &training)) {
        fprintf(stderr, "invalid step options: epochs 1..10000, learning rate >0..1\n");
        return 2;
    }
    neural_candidate candidate = {0};
    candidate.model = cgai_neural_checkpoint_load(argv[2]);
    if (candidate.model == NULL) {
        fprintf(stderr, "checkpoint load failed: %s\n", cgai_last_error());
        return 1;
    }
    candidate.train = cgai_cli_neural_text(argv[3]);
    candidate.heldout = cgai_cli_neural_text(argv[4]);
    const int loaded = candidate.train != NULL && candidate.heldout != NULL;
    const int ok =
        loaded && fit_candidate(&candidate, &training) && publish_candidate(&candidate, argv[5]);
    if (!ok && loaded)
        fprintf(stderr, "neural step failed: %s\n", cgai_last_error());
    destroy_candidate(&candidate);
    return ok ? 0 : 1;
}
