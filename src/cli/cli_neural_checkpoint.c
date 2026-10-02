/** @file cli_neural_checkpoint.c @brief Initialize, replay and export versioned training state. */
#include "internal/cli_commands.h"
#include "internal/cli_neural.h"
#include "internal/neural_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Compare exact vocabulary order without comparing structure padding.
 * @param first Borrowed reference model.
 * @param second Borrowed replay model with the same configuration.
 * @return Nonzero for identical vocabulary and parameter layout. */
static int same_vocabulary(const cgai_neural_model *first, const cgai_neural_model *second) {
    if (first->vocabulary_size != second->vocabulary_size ||
        first->parameter_count != second->parameter_count)
        return 0;
    for (size_t i = 0U; i < first->vocabulary_size; ++i)
        if (strcmp(first->vocabulary[i], second->vocabulary[i]) != 0)
            return 0;
    return 1;
}

/** @brief Compare all state needed for exact full-epoch continuation.
 * @param first Borrowed reference model.
 * @param second Borrowed replay model with identical configuration.
 * @return Nonzero for bit-identical parameters, moments, counters and shuffle state. */
static int same_training_state(const cgai_neural_model *first, const cgai_neural_model *second) {
    /* Step 1: Check counts before comparing any variable-length allocation. */
    if (!same_vocabulary(first, second) || first->training_step != second->training_step ||
        first->training_epochs != second->training_epochs ||
        first->training_shuffle != second->training_shuffle ||
        (first->adam_first == NULL) != (second->adam_first == NULL))
        return 0;
    const size_t bytes = first->parameter_count * sizeof(double);
    if (memcmp(first->parameters, second->parameters, bytes) != 0)
        return 0;
    /* Step 2: Initial checkpoints have no moments; trained ones compare both arrays. */
    return first->adam_first == NULL ||
           (memcmp(first->adam_first, second->adam_first, bytes) == 0 &&
            memcmp(first->adam_second, second->adam_second, bytes) == 0);
}

/** @brief Replay complete epochs in bounded API calls without resetting optimizer state.
 * @param model Borrowed mutable fresh model.
 * @param text Borrowed fixed training corpus.
 * @param epochs Number of complete epochs recorded by the reference.
 * @param rate Fixed recipe learning rate.
 * @return Nonzero after every requested epoch succeeds. */
static int replay_epochs(cgai_neural_model *model, const char *text, uint64_t epochs, double rate) {
    cgai_neural_training training = cgai_neural_default_training();
    training.learning_rate = rate;
    while (epochs != 0U) {
        training.epochs = (size_t)(epochs > 10000U ? 10000U : epochs);
        if (cgai_neural_train_continue(model, text, &training) != CGAI_STATUS_OK)
            return 0;
        epochs -= training.epochs;
    }
    return 1;
}

/** @brief Rebuild from a fixed corpus and compare before publishing a replay checkpoint.
 * @param reference Borrowed saved training state.
 * @param text Borrowed original training text.
 * @param path Borrowed replay destination, written only after exact equality.
 * @param rate Recipe learning rate used for all replay epochs.
 * @return Zero for an exact replay, otherwise one with a diagnostic. */
static int replay_model(const cgai_neural_model *reference, const char *text, const char *path,
                        double rate) {
    /* Step 1: Reconstruct the frozen vocabulary and initialization from the original corpus. */
    cgai_neural_model *model = cgai_neural_create(&reference->config, text);
    int ok = model != NULL && replay_epochs(model, text, reference->training_epochs, rate);
    /* Step 2: Refuse a mismatched recipe before touching the requested destination. */
    if (ok && !same_training_state(reference, model)) {
        fprintf(stderr, "replay differs: check corpus, learning rate and compiler/math library\n");
        ok = 0;
    } else if (!ok)
        fprintf(stderr, "replay failed: %s\n", cgai_last_error());
    if (ok) {
        ok = cgai_neural_checkpoint_save(model, path) == CGAI_STATUS_OK;
        if (!ok)
            fprintf(stderr, "replay save failed: %s\n", cgai_last_error());
    }
    cgai_neural_destroy(model);
    return ok ? 0 : 1;
}

/** @brief Initialize a diffable checkpoint using the documented default network.
 * @param argc Dispatcher-validated count of four.
 * @param argv Borrowed corpus and checkpoint paths at indices two and three.
 * @return Zero for success, otherwise one; all allocated state is released. */
int cgai_cli_neural_init(int argc, char **argv) {
    (void)argc;
    char *text = cgai_cli_neural_text(argv[2]);
    cgai_neural_model *model = text != NULL ? cgai_neural_create(NULL, text) : NULL;
    const int ok = model != NULL && cgai_neural_checkpoint_save(model, argv[3]) == CGAI_STATUS_OK;
    if (!ok && text != NULL)
        fprintf(stderr, "checkpoint initialization failed: %s\n", cgai_last_error());
    cgai_neural_destroy(model);
    free(text);
    return ok ? 0 : 1;
}

/** @brief Verify a fixed recipe reproduces every checkpoint weight and optimizer value.
 * @param argc Dispatcher-validated count of five or six.
 * @param argv Borrowed reference, training text, destination and optional rate arguments.
 * @return Zero for exact replay, two for invalid rate, otherwise one. */
int cgai_cli_neural_replay(int argc, char **argv) {
    double rate = 0.015;
    if (argc == 6 && (!cgai_cli_neural_real(argv[5], 1.0, &rate) || rate == 0.0))
        return 2;
    cgai_neural_model *model = cgai_neural_checkpoint_load(argv[2]);
    if (model == NULL) {
        fprintf(stderr, "checkpoint load failed: %s\n", cgai_last_error());
        return 1;
    }
    char *text = cgai_cli_neural_text(argv[3]);
    const int status = text != NULL ? replay_model(model, text, argv[4], rate) : 1;
    if (status == 0)
        printf("replay matched\tepochs=%" PRIu64 "\tsteps=%" PRIu64 "\n", model->training_epochs,
               model->training_step);
    free(text);
    cgai_neural_destroy(model);
    return status;
}

/** @brief Export inference weights from a training checkpoint to the existing binary format.
 * @param argc Dispatcher-validated count of four.
 * @param argv Borrowed checkpoint and binary destination paths.
 * @return Zero for success, otherwise one with a diagnostic. */
int cgai_cli_neural_export(int argc, char **argv) {
    (void)argc;
    cgai_neural_model *model = cgai_neural_checkpoint_load(argv[2]);
    const int ok = model != NULL && cgai_neural_save(model, argv[3]) == CGAI_STATUS_OK;
    if (!ok)
        fprintf(stderr, "checkpoint export failed: %s\n", cgai_last_error());
    cgai_neural_destroy(model);
    return ok ? 0 : 1;
}
