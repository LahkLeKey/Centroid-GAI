/** @file cli_neural_training.c @brief Train a neural centroid model and report before/after loss.
 */
#include "internal/cli_commands.h"
#include "internal/cli_neural.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Parse optional training settings without accepting negative or overflowing values.
 *
 * Architecture defaults remain in the public API. The command exposes epochs and learning
 * rate while keeping gradient clipping at its documented default.
 * @param argc Dispatcher-validated count between four and seven.
 * @param argv Borrowed arguments with optional epochs at four and learning rate at five.
 * @param settings Borrowed default-initialized training settings, modified during parsing.
 * @return Nonzero for valid settings, otherwise zero.
 */
static int cgai_neural_training_args(int argc, char **argv, cgai_neural_training *settings) {
    /* Step 1: Parse a bounded epoch count before converting it to size_t. */
    uint64_t epochs = settings->epochs;
    if (argc >= 5 && (!cgai_cli_neural_unsigned(argv[4], 10000U, &epochs) || epochs == 0U))
        return 0;
    settings->epochs = (size_t)epochs;
    /* Step 2: Require a finite strictly positive learning rate no greater than one. */
    if (argc >= 6 && (!cgai_cli_neural_real(argv[5], 1.0, &settings->learning_rate) ||
                      settings->learning_rate == 0.0))
        return 0;
    return 1;
}

/**
 * @brief Train only on the training sequence, reporting optional independent validation.
 *
 * Validation never enters vocabulary construction or gradient updates. Each failed stage
 * short-circuits later stages; earlier successful training updates are not rolled back.
 * @param model Borrowed mutable model initialized from training text alone.
 * @param train Borrowed nonempty training sequence.
 * @param validation Borrowed held-out sequence, or NULL when not requested.
 * @param settings Borrowed validated training settings.
 * @return Nonzero after all requested evaluations and training succeed, otherwise zero.
 */
static int cgai_neural_train_report(cgai_neural_model *model, const char *train,
                                    const char *validation, const cgai_neural_training *settings) {
    /* Step 1: Score initial predictions before any gradient update. */
    if (!cgai_cli_neural_report(model, train, "training before") ||
        (validation != NULL && !cgai_cli_neural_report(model, validation, "held-out before")))
        return 0;
    /* Step 2: Fit only training windows, then score both sequences using frozen vocabulary. */
    return cgai_neural_train(model, train, settings) == CGAI_STATUS_OK &&
           cgai_cli_neural_report(model, train, "training after") &&
           (validation == NULL || cgai_cli_neural_report(model, validation, "held-out after"));
}

/**
 * @brief Construct, train, evaluate, and save a model from already loaded text.
 *
 * Model ownership stays here until all phases finish. Saving occurs only after successful
 * training and scoring, so evaluation errors never publish an artifact as a successful run.
 * @param train Borrowed training text used to construct the frozen vocabulary.
 * @param validation Borrowed optional held-out text, or NULL.
 * @param path Borrowed destination artifact path.
 * @param settings Borrowed validated optimizer settings.
 * @return Zero after training and saving, otherwise one with a stderr diagnostic.
 */
static int cgai_neural_train_loaded(const char *train, const char *validation, const char *path,
                                    const cgai_neural_training *settings) {
    /* Step 1: Initialize only from training text, then perform dependent model operations. */
    cgai_neural_model *model = cgai_neural_create(NULL, train);
    const int ok = model != NULL && cgai_neural_train_report(model, train, validation, settings) &&
                   cgai_neural_save(model, path) == CGAI_STATUS_OK;
    /* Step 2: Describe success or preserve the first native failure before destroying state. */
    if (ok)
        printf("saved %s (%zu vocabulary entries, %zu epochs)\n", path,
               cgai_neural_vocabulary_size(model), settings->epochs);
    else
        fprintf(stderr, "neural training failed: %s\n", cgai_last_error());
    cgai_neural_destroy(model);
    return ok ? 0 : 1;
}

/**
 * @brief Run neural training with strict numeric options and optional held-out reporting.
 *
 * The command owns both corpus buffers. Failed validation-file loading stops training before
 * any model is created; omitting validation leaves its pointer NULL deliberately.
 * @param argc Dispatcher-validated count between four and seven.
 * @param argv Borrowed train/model paths and optional epochs, rate, and held-out path.
 * @return Zero for success, two for invalid options, or one for execution failure.
 */
int cgai_cli_neural_train(int argc, char **argv) {
    /* Step 1: Validate options before performing any file or training work. */
    cgai_neural_training settings = cgai_neural_default_training();
    if (!cgai_neural_training_args(argc, argv, &settings)) {
        fprintf(stderr, "invalid neural training options: epochs must be 1..10000, rate >0..1\n");
        return 2;
    }
    /* Step 2: Load independent text buffers and train only when every requested file exists. */
    char *train = cgai_cli_neural_text(argv[2]);
    char *validation = argc == 7 ? cgai_cli_neural_text(argv[6]) : NULL;
    int status = 1;
    if (train != NULL && (argc != 7 || validation != NULL))
        status = cgai_neural_train_loaded(train, validation, argv[3], &settings);
    /* Step 3: Release corpus ownership regardless of execution status. */
    free(validation);
    free(train);
    return status;
}
