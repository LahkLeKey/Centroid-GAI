/** @file cli_neural_candidate.c @brief JSON evidence for unattended checkpoint proposals. */
#include "internal/cli_commands.h"
#include "internal/cli_neural.h"
#include "internal/error.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** Owned inputs and read-only measurements for an unpromoted checkpoint proposal. */
typedef struct candidate_measurements {
    cgai_neural_model *model;               /**< Owned parent, updated privately in memory. */
    char *training;                         /**< Owned training corpus; vocabulary stays frozen. */
    char *development;                      /**< Owned development corpus; never used in updates. */
    cgai_neural_metrics training_before;    /**< Training score before optimization. */
    cgai_neural_metrics training_after;     /**< Training score after optimization. */
    cgai_neural_metrics development_before; /**< Development score before optimization. */
    cgai_neural_metrics development_after;  /**< Development score after optimization. */
} candidate_measurements;

/** @brief Validate full-epoch settings before loading or writing any file.
 * @param argc Dispatcher-validated count of six through eight.
 * @param argv Borrowed arguments with optional epochs and rate at six and seven.
 * @param training Writable settings initialized with the native defaults.
 * @return Nonzero for valid options; settings are scratch on failure. */
static int candidate_options(int argc, char **argv, cgai_neural_training *training) {
    /* Step 1: Preserve the checkpoint workflow's one-epoch and fixed-rate defaults. */
    uint64_t epochs = 1U;
    training->learning_rate = 0.015;
    if (argc >= 7 && (!cgai_cli_neural_unsigned(argv[6], 10000U, &epochs) || epochs == 0U))
        return 0;
    training->epochs = (size_t)epochs;
    /* Step 2: Reject nonfinite, zero, or out-of-range explicit learning rates. */
    return argc < 8 || (cgai_cli_neural_real(argv[7], 1.0, &training->learning_rate) &&
                        training->learning_rate > 0.0);
}

/** @brief Score a corpus without applying a development vocabulary or improvement gate.
 * @param model Borrowed immutable checkpoint model.
 * @param text Borrowed nonempty corpus; unknown words remain unknown.
 * @param metrics Writable measured result, meaningful only on success.
 * @return Nonzero for finite JSON-representable loss and accuracy, otherwise zero. */
static int finite_score(const cgai_neural_model *model, const char *text,
                        cgai_neural_metrics *metrics) {
    /* Step 1: Use the core evaluator without adding vocabulary or changing weights. */
    if (cgai_neural_evaluate(model, text, metrics) != CGAI_STATUS_OK)
        return 0;
    /* Step 2: Exclude values JSON cannot represent before printing any evidence. */
    if (!isfinite(metrics->cross_entropy) || !isfinite(metrics->accuracy))
        return cgai_fail("checkpoint score requires finite loss and accuracy");
    return 1;
}

/** @brief Print one finite score as JSON without a newline or surrounding document.
 * @param metrics Borrowed finite score; token count includes EOS, unknown count does not. */
static void print_metrics(const cgai_neural_metrics *metrics) {
    /* Step 1: Retain enough decimal digits to round-trip native double measurements. */
    printf("{\"tokens\":%zu,\"unknownTokens\":%zu,\"crossEntropy\":%.17g,\"accuracy\":%.17g}",
           metrics->tokens, metrics->unknown_tokens, metrics->cross_entropy, metrics->accuracy);
}

/** @brief Continue the parent only after measuring both corpora and training coverage.
 * @param candidate Borrowed prepared model and owned corpus buffers.
 * @param training Borrowed full-epoch settings; only training text contributes updates.
 * @return Nonzero after complete optimization and finite measurements; no promotion implied. */
static int measure_candidate(candidate_measurements *candidate,
                             const cgai_neural_training *training) {
    /* Step 1: Refuse unseen training spellings before the first optimizer update. */
    if (!finite_score(candidate->model, candidate->training, &candidate->training_before) ||
        !finite_score(candidate->model, candidate->development, &candidate->development_before))
        return 0;
    if (candidate->training_before.unknown_tokens != 0U)
        return cgai_fail("candidate training corpus must have zero unknown tokens");
    /* Step 2: Keep development novelty visible while retaining exact continuation state. */
    return cgai_neural_train_continue(candidate->model, candidate->training, training) ==
               CGAI_STATUS_OK &&
           finite_score(candidate->model, candidate->training, &candidate->training_after) &&
           finite_score(candidate->model, candidate->development, &candidate->development_after);
}

/** @brief Save a fully measured candidate and emit its JSON evidence after a complete save.
 * @param candidate Borrowed fitted proposal; no acceptance policy has been applied.
 * @param path Borrowed checkpoint destination, replaced nonatomically by the save API.
 * @return Nonzero after successful save; save failure may leave an incomplete destination. */
static int save_candidate(const candidate_measurements *candidate, const char *path) {
    /* Step 1: Never open the destination before all measurements have succeeded. */
    if (cgai_neural_checkpoint_save(candidate->model, path) != CGAI_STATUS_OK)
        return 0;
    const cgai_neural_progress progress = cgai_neural_get_progress(candidate->model);
    /* Step 2: Emit one machine-readable document without timestamps or machine paths. */
    printf("{\"version\":1,\"before\":{\"training\":");
    print_metrics(&candidate->training_before);
    printf(",\"development\":");
    print_metrics(&candidate->development_before);
    printf("},\"after\":{\"training\":");
    print_metrics(&candidate->training_after);
    printf(",\"development\":");
    print_metrics(&candidate->development_after);
    printf("},\"epochs\":%" PRIu64 ",\"steps\":%" PRIu64 "}\n", progress.epochs, progress.steps);
    return 1;
}

/** @brief Release all proposal ownership after success or failure.
 * @param candidate Borrowed partially initialized proposal; fields become unusable. */
static void destroy_measurements(candidate_measurements *candidate) {
    /* Step 1: Release the independent corpora and model allocations on the common exit path. */
    free(candidate->development);
    free(candidate->training);
    cgai_neural_destroy(candidate->model);
}

/** @brief Produce an unpromoted full-epoch candidate with finite JSON measurements.
 * @param argc Dispatcher-validated count of six through eight.
 * @param argv Borrowed checkpoint, training, development, destination and optional settings.
 * @return Zero on success, two for invalid settings, otherwise one with a stderr diagnostic.
 * Validation failures preserve the destination; save failures follow the non-atomic API. */
int cgai_cli_neural_candidate(int argc, char **argv) {
    /* Step 1: Check settings before acquiring any file or model ownership. */
    cgai_neural_training training = cgai_neural_default_training();
    if (!candidate_options(argc, argv, &training)) {
        fprintf(stderr, "invalid candidate options: epochs 1..10000, learning rate >0..1\n");
        return 2;
    }
    candidate_measurements candidate = {0};
    candidate.model = cgai_neural_checkpoint_load(argv[2]);
    if (candidate.model == NULL) {
        fprintf(stderr, "checkpoint load failed: %s\n", cgai_last_error());
        return 1;
    }
    /* Step 2: Load independent corpora, measure privately, then save the complete proposal. */
    candidate.training = cgai_cli_neural_text(argv[3]);
    candidate.development = cgai_cli_neural_text(argv[4]);
    const int loaded = candidate.training != NULL && candidate.development != NULL;
    const int ok =
        loaded && measure_candidate(&candidate, &training) && save_candidate(&candidate, argv[5]);
    if (!ok && loaded)
        fprintf(stderr, "neural candidate failed: %s\n", cgai_last_error());
    destroy_measurements(&candidate);
    return ok ? 0 : 1;
}

/** @brief Score a frozen-vocabulary checkpoint and emit finite JSON evidence.
 * @param argc Dispatcher-validated argument count of four.
 * @param argv Borrowed checkpoint and nonempty evaluation corpus paths.
 * @return Zero on success, otherwise one; failures print no JSON and retain no ownership. */
int cgai_cli_neural_score(int argc, char **argv) {
    /* Step 1: Acquire model and text ownership; unknown evaluation words are permitted. */
    (void)argc;
    cgai_neural_model *model = cgai_neural_checkpoint_load(argv[2]);
    if (model == NULL) {
        fprintf(stderr, "checkpoint score load failed: %s\n", cgai_last_error());
        return 1;
    }
    char *text = cgai_cli_neural_text(argv[3]);
    cgai_neural_metrics metrics = {0};
    const int ok = text != NULL && finite_score(model, text, &metrics);
    /* Step 2: Emit only a complete successful score and release every owned allocation. */
    if (ok) {
        print_metrics(&metrics);
        printf("\n");
    } else if (text != NULL)
        fprintf(stderr, "checkpoint score failed: %s\n", cgai_last_error());
    free(text);
    cgai_neural_destroy(model);
    return ok ? 0 : 1;
}
