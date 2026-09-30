/** @file cli_neural_generation.c @brief Reproducible neural continuation command. */
#include "internal/cli_commands.h"
#include "internal/cli_neural.h"
#include "internal/neural_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Parsed neural generation values, owned on the command stack. */
typedef struct cgai_neural_cli_generation {
    size_t max_tokens;  /**< Maximum emitted tokens, including an accepted zero request. */
    double temperature; /**< Zero for greedy output, otherwise a sampling scale up to 100. */
    uint64_t seed;      /**< Reproducible sample seed; zero uses the model seed. */
} cgai_neural_cli_generation;

/**
 * @brief Validate optional generation settings without changing the loaded model.
 *
 * Decimal parsing rejects signs and overflow before narrowing to size_t. Sampling temperature
 * has its own finite bound and seed retains the full uint64_t range.
 * @param argc Dispatcher-validated count between four and seven.
 * @param argv Borrowed arguments with optional token count, temperature, and seed.
 * @param options Borrowed default-initialized settings, modified during successful parsing.
 * @return Nonzero for accepted options, otherwise zero.
 */
static int cgai_neural_generation_args(int argc, char **argv, cgai_neural_cli_generation *options) {
    /* Step 1: Validate the requested token count before converting it to a native size. */
    uint64_t count = options->max_tokens;
    if (argc >= 5 && !cgai_cli_neural_unsigned(argv[4], CGAI_NEURAL_MAX_TOKENS, &count))
        return 0;
    options->max_tokens = (size_t)count;
    /* Step 2: Parse each remaining optional value while preserving the first failure. */
    if (argc >= 6 && !cgai_cli_neural_real(argv[5], 100.0, &options->temperature))
        return 0;
    return argc < 7 || cgai_cli_neural_unsigned(argv[6], UINT64_MAX, &options->seed);
}

/**
 * @brief Compute a bounded output capacity from the model's longest token.
 *
 * One separator per emitted token and a final NUL safely bound the generated string. The CLI
 * refuses a worst-case allocation above 64 MiB even when early EOS might produce less text.
 * @param model Borrowed validated model with initialized vocabulary.
 * @param max_tokens Requested maximum emitted token count.
 * @return Required byte capacity, or zero when its worst case exceeds the CLI bound.
 */
static size_t cgai_neural_output_capacity(const cgai_neural_model *model, size_t max_tokens) {
    /* Step 1: Find the longest spelling among all possible output IDs. */
    size_t longest = 0U;
    for (size_t i = 0U; i < model->vocabulary_size; ++i) {
        const size_t length = strlen(model->vocabulary[i]);
        if (length > longest)
            longest = length;
    }
    /* Step 2: Prove the multiplication stays inside a 64 MiB budget before evaluating it. */
    if (max_tokens > (64U * 1024U * 1024U - 1U) / (longest + 1U))
        return 0U;
    return max_tokens * (longest + 1U) + 1U;
}

/**
 * @brief Allocate sufficient bounded output storage and print one continuation.
 *
 * The model and prompt remain borrowed. Output owns its allocation until generation completes,
 * and zero-token requests print an empty continuation line successfully.
 * @param model Borrowed immutable loaded model.
 * @param prompt Borrowed prompt text; empty prompts are supported.
 * @param options Borrowed validated generation settings.
 * @return Zero after successful generation or one with a stderr diagnostic.
 */
static int cgai_neural_generate_loaded(const cgai_neural_model *model, const char *prompt,
                                       const cgai_neural_cli_generation *options) {
    /* Step 1: Bound output storage using vocabulary lengths before allocation. */
    const size_t capacity = cgai_neural_output_capacity(model, options->max_tokens);
    if (capacity == 0U) {
        fprintf(stderr, "neural output exceeds the 64 MiB CLI buffer limit\n");
        return 1;
    }
    char *output = (char *)malloc(capacity);
    if (output == NULL) {
        fprintf(stderr, "could not allocate neural output\n");
        return 1;
    }
    /* Step 2: Produce only a continuation; the caller already owns its prompt text. */
    const int ok = cgai_neural_generate(model, prompt, options->max_tokens, options->temperature,
                                        options->seed, output, capacity) == CGAI_STATUS_OK;
    if (ok)
        printf("%s\n", output);
    else
        fprintf(stderr, "neural generation failed: %s\n", cgai_last_error());
    /* Step 3: Release the temporary output after printing or diagnosing the result. */
    free(output);
    return ok ? 0 : 1;
}

/**
 * @brief Generate from a neural artifact with optional count, temperature, and seed.
 *
 * Command syntax is checked before opening the artifact. The loaded model is immutable during
 * generation and is destroyed on the single completion path.
 * @param argc Dispatcher-validated count between four and seven.
 * @param argv Borrowed model path, prompt, and optional generation arguments.
 * @return Zero for success, two for invalid options, or one for runtime failure.
 */
int cgai_cli_neural_generate(int argc, char **argv) {
    /* Step 1: Start from small reproducible defaults and validate all optional arguments. */
    cgai_neural_cli_generation options = {40U, 0.8, 0U};
    if (!cgai_neural_generation_args(argc, argv, &options)) {
        fprintf(stderr, "invalid neural generation options: tokens 0..1000000, temperature 0..100, "
                        "unsigned seed\n");
        return 2;
    }
    /* Step 2: Load independent owned model state only after valid parsing. */
    cgai_neural_model *model = cgai_neural_load(argv[2]);
    if (model == NULL) {
        fprintf(stderr, "neural generation failed: %s\n", cgai_last_error());
        return 1;
    }
    const int status = cgai_neural_generate_loaded(model, argv[3], &options);
    /* Step 3: Release model ownership after the generation helper finishes. */
    cgai_neural_destroy(model);
    return status;
}
