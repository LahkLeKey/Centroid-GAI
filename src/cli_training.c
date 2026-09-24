/** @file cli_training.c @brief Corpus training command. */

#include "centroid_gai.h"
#include "internal/cli_commands.h"
#include "internal/file_utils.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Apply the optional centroid-count argument to default configuration.
 *
 * The dispatcher has already accepted the command's argument count. If argv[4] is absent, the
 * caller-supplied default centroid count remains unchanged. Parsing errors are explained on stderr;
 * full model configuration limits are enforced later by model construction.
 *
 * @param argc Validated argument count of four or five.
 * @param argv Borrowed command arguments, with optional centroid count at index four.
 * @param config Non-NULL mutable configuration initially containing defaults.
 * @return One if parsing succeeds or no override exists, otherwise zero.
 */
static int parse_training_args(int argc, char **argv, cgai_config *config) {
    /* The fourth positional argument is optional; the default remains stable. */
    /* Step 1: Parse the optional centroid count only when its argument is present. */
    if (argc == 5 && !cgai_cli_parse_size(argv[4], &config->centroid_count)) {
        fprintf(stderr, "invalid centroid count: %s\n", argv[4]);
        return 0;
    }
    /* Step 2: Keep the default or successfully parsed override for model construction. */
    return 1;
}

/**
 * @brief Read a corpus, train a new model, and save its binary artifact.
 *
 * The whole-file helper returns an extra terminating NUL so its byte buffer can be borrowed as
 * training text. This function owns both that buffer and the newly created model until cleanup.
 * The logical AND chain stops at the first failed phase; core success is nonzero, which permits
 * this boolean-style orchestration.
 *
 * @param corpus_path Borrowed NUL-terminated path to the text corpus.
 * @param model_path Borrowed NUL-terminated destination artifact path.
 * @param config Borrowed configuration copied by model creation.
 * @return Zero after training and saving succeed, otherwise one after a message is printed.
 */
static int run_training(const char *corpus_path, const char *model_path,
                        const cgai_config *config) {
    /* Read the corpus once so the model layer receives plain NUL-terminated text. */
    /* Step 1: Initialize corpus-buffer ownership and read the complete file. */
    uint8_t *text = NULL;
    size_t text_size = 0U;
    if (cgai_file_read_all(corpus_path, &text, &text_size) != CGAI_STATUS_OK) {
        fprintf(stderr, "could not read corpus: %s\n", corpus_path);
        return 1;
    }
    /* Keep the command owner responsible for both model and corpus cleanup. */
    /* Step 2: Create the model, train it, and save it only while preceding stages succeed. */
    cgai_model *model = cgai_model_create(config);
    const int ok = model != NULL && cgai_model_train_text(model, (const char *)text) &&
                   cgai_model_save(model, model_path);
    /* Step 3: Print either the core failure diagnostic or the saved model's summary. */
    if (!ok) {
        fprintf(stderr, "training failed: %s\n", cgai_last_error());
    } else {
        printf("saved %s (%zu tokens, %zu examples)\n", model_path,
               cgai_model_vocabulary_size(model), cgai_model_examples_seen(model));
    }
    /* Step 4: Release model and corpus allocations before returning process-style status. */
    cgai_model_destroy(model);
    free(text);
    (void)text_size;
    return ok ? 0 : 1;
}

/**
 * @brief Run the training command using defaults plus an optional centroid override.
 *
 * Argument positions have already been checked by the dispatcher. A configuration lives on the
 * stack for this command and is copied when a model is constructed. Syntax errors return two;
 * execution errors are reported by run_training().
 *
 * @param argc Validated training-command argument count.
 * @param argv Borrowed arguments: corpus path at index two, model path at index three.
 * @return Zero on success, two on numeric syntax failure, or one on runtime failure.
 */
int cgai_cli_train(int argc, char **argv) {
    /* Step 1: Start from core defaults and apply any supplied centroid-count override. */
    cgai_config config = cgai_default_config();
    if (!parse_training_args(argc, argv, &config)) {
        return 2;
    }
    /* Step 2: Execute corpus loading, training, saving, and cleanup with the accepted configuration. */
    return run_training(argv[2], argv[3], &config);
}
