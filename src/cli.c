/** @file cli.c @brief Command-line parsing and command execution. */

#include "internal/cli.h"

#include "centroid_gai.h"
#include "internal/constants.h"
#include "internal/file_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Prints command usage to the requested stream. */
static void usage(FILE *stream) {
    fprintf(stream, "Centroid-GAI: compact centroid-based text generation\n\n"
                    "Usage:\n"
                    "  cgai train <corpus.txt> <model.cgai> [centroids]\n"
                    "  cgai generate <model.cgai> <prompt> [max-tokens] [temperature] [seed]\n");
}

/** Parses a positive decimal size without accepting trailing characters. */
static int parse_size(const char *text, size_t *value) {
    char *end = NULL;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0U || parsed > SIZE_MAX) {
        return 0;
    }
    *value = (size_t)parsed;
    return 1;
}

/** Parses the optional training centroid count. */
static int parse_training_args(int argc, char **argv, cgai_config *config) {
    /* The fourth positional argument is optional; the default remains stable. */
    if (argc == 5 && !parse_size(argv[4], &config->centroid_count)) {
        fprintf(stderr, "invalid centroid count: %s\n", argv[4]);
        return 0;
    }
    return 1;
}

/** Trains and saves a model from an already loaded corpus buffer. */
static int run_training(const char *corpus_path, const char *model_path,
                        const cgai_config *config) {
    /* Read the corpus once so the model layer receives plain NUL-terminated text. */
    uint8_t *text = NULL;
    size_t text_size = 0U;
    if (cgai_file_read_all(corpus_path, &text, &text_size) != CGAI_STATUS_OK) {
        fprintf(stderr, "could not read corpus: %s\n", corpus_path);
        return 1;
    }
    /* Keep the command owner responsible for both model and corpus cleanup. */
    cgai_model *model = cgai_model_create(config);
    const int ok = model != NULL && cgai_model_train_text(model, (const char *)text) &&
                   cgai_model_save(model, model_path);
    if (!ok) {
        fprintf(stderr, "training failed: %s\n", cgai_last_error());
    } else {
        printf("saved %s (%zu tokens, %zu examples)\n", model_path,
               cgai_model_vocabulary_size(model), cgai_model_examples_seen(model));
    }
    cgai_model_destroy(model);
    free(text);
    (void)text_size;
    return ok ? 0 : 1;
}

typedef struct cli_generation_options {
    size_t max_tokens;
    double temperature;
    uint64_t seed;
} cli_generation_options;

/** Parses one nonnegative floating-point temperature argument. */
static int parse_temperature(const char *text, double *temperature) {
    char *end = NULL;
    *temperature = strtod(text, &end);
    return end != text && *end == '\0' && *temperature >= 0.0;
}

/** Parses one unsigned decimal random seed argument. */
static int parse_seed(const char *text, uint64_t *seed) {
    char *end = NULL;
    *seed = (uint64_t)strtoull(text, &end, 10);
    return end != text && *end == '\0';
}

/** Rejects generation sizes that cannot fit the CLI output buffer. */
static int valid_generation_size(size_t max_tokens) {
    return max_tokens <= CGAI_MAX_GENERATION_TOKENS &&
           max_tokens <= (SIZE_MAX - 1U) / CGAI_OUTPUT_BYTES_PER_TOKEN;
}

/** Parses the optional maximum token count and checks its output capacity. */
static int parse_max_tokens(int argc, char **argv, size_t *max_tokens) {
    if (argc >= 5 && !parse_size(argv[4], max_tokens)) {
        fprintf(stderr, "invalid max token count: %s\n", argv[4]);
        return 0;
    }
    if (!valid_generation_size(*max_tokens)) {
        fprintf(stderr, "max token count is too large\n");
        return 0;
    }
    return 1;
}

/** Parses and validates optional generation arguments. */
static int parse_generation_args(int argc, char **argv, cli_generation_options *options) {
    /* Parse each optional value independently so errors identify the exact argument. */
    if (!parse_max_tokens(argc, argv, &options->max_tokens)) {
        return 0;
    }
    if (argc >= 6) {
        if (!parse_temperature(argv[5], &options->temperature)) {
            fprintf(stderr, "invalid temperature: %s\n", argv[5]);
            return 0;
        }
    }
    if (argc >= 7) {
        if (!parse_seed(argv[6], &options->seed)) {
            fprintf(stderr, "invalid seed: %s\n", argv[6]);
            return 0;
        }
    }
    return 1;
}

/** Loads a model, generates output, and prints the continuation. */
static int run_generation(const char *model_path, const char *prompt,
                          const cli_generation_options *options) {
    /* Reserve conservative output space because the core API requires caller storage. */
    cgai_model *model = cgai_model_load(model_path);
    char *output = (char *)malloc(options->max_tokens * CGAI_OUTPUT_BYTES_PER_TOKEN + 1U);
    const int ok =
        model != NULL && output != NULL &&
        cgai_model_generate(model, prompt, options->max_tokens, options->temperature, options->seed,
                            output, options->max_tokens * CGAI_OUTPUT_BYTES_PER_TOKEN + 1U);
    if (!ok) {
        fprintf(stderr, "generation failed: %s\n",
                output == NULL ? "out of memory" : cgai_last_error());
    } else {
        printf("%s%s%s\n", prompt, output[0] != '\0' ? " " : "", output);
    }
    free(output);
    cgai_model_destroy(model);
    return ok ? 0 : 1;
}

/** Implements the training command and its model-file output. */
static int train_command(int argc, char **argv) {
    cgai_config config = cgai_default_config();
    if (!parse_training_args(argc, argv, &config)) {
        return 2;
    }
    return run_training(argv[2], argv[3], &config);
}

/** Implements the generation command and its model-file input. */
static int generate_command(int argc, char **argv) {
    cli_generation_options options = {40U, 0.8, 0U};
    if (!parse_generation_args(argc, argv, &options)) {
        return 2;
    }
    return run_generation(argv[2], argv[3], &options);
}

/** Dispatches command-line arguments to the selected CLI command. */
int cgai_cli_run(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "train") == 0 && (argc == 4 || argc == 5)) {
        return train_command(argc, argv);
    }
    if (argc >= 4 && strcmp(argv[1], "generate") == 0 && argc <= 7) {
        return generate_command(argc, argv);
    }
    usage(argc > 1 ? stderr : stdout);
    return argc > 1 ? 2 : 0;
}