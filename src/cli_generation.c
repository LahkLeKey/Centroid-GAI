/** @file cli_generation.c @brief Model generation command. */

#include "centroid_gai.h"
#include "internal/cli_commands.h"
#include "internal/constants.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Command-local defaults progressively replaced by supplied arguments.
 *
 * Parsing mutates this stack value. A failed parse can leave earlier fields
 * changed, so the command uses it only after the whole parser reports success.
 * The execution helper borrows the final settings and owns its separate buffers.
 */
typedef struct cli_generation_options {
    size_t max_tokens; /**< Token cap used to choose estimated output capacity. */
    double temperature; /**< Nonnegative parsed value; core also requires finiteness. */
    uint64_t seed; /**< Sampling seed; zero uses the model's configured seed. */
} cli_generation_options;

/**
 * @brief Convert a completely consumed floating-point argument to temperature.
 *
 * strtod writes the first unconsumed position through end. This parser requires some consumed text,
 * no suffix, and a nonnegative result. It does not reject every range/nonfinite case itself;
 * the generation API performs the final finite-temperature check. The output is assigned even
 * when this parser subsequently returns failure.
 *
 * @param text Borrowed NUL-terminated argument.
 * @param temperature Non-NULL writable result, potentially changed on failure.
 * @return Nonzero for a fully consumed nonnegative value, otherwise zero.
 */
static int parse_temperature(const char *text, double *temperature) {
    /* Step 1: Prepare storage for the parser's stopping position. */
    char *end = NULL;
    /* Step 2: Convert and store the value, then check consumption and sign. */
    *temperature = strtod(text, &end);
    return end != text && *end == '\0' && *temperature >= 0.0;
}

/**
 * @brief Convert a fully consumed decimal argument to a 64-bit seed.
 *
 * This follows strtoull's accepted lexical forms and casts its result to uint64_t. Unlike the size
 * parser, it does not inspect errno; the success result describes consumed syntax rather than a
 * lossless range proof. Zero is permitted and means use the model seed in generation.
 *
 * @param text Borrowed NUL-terminated seed argument.
 * @param seed Non-NULL writable result assigned by the conversion.
 * @return Nonzero if some text was consumed and no suffix remains, otherwise zero.
 */
static int parse_seed(const char *text, uint64_t *seed) {
    /* Step 1: Prepare to observe where unsigned decimal conversion stops. */
    char *end = NULL;
    /* Step 2: Store the converted word, then require full argument consumption. */
    *seed = (uint64_t)strtoull(text, &end, 10);
    return end != text && *end == '\0';
}

/**
 * @brief Check the CLI token cap and estimated output-buffer arithmetic.
 *
 * The text allocation is max_tokens * bytes_per_token + one NUL byte. Rearranging the bound into
 * a division avoids evaluating that product before checking it. Passing this check only proves
 * that the estimate fits; unusually long token spellings can still exhaust the output buffer.
 *
 * @param max_tokens Requested upper bound on generated tokens.
 * @return Nonzero if both the CLI cap and allocation arithmetic bound hold, otherwise zero.
 */
static int valid_generation_size(size_t max_tokens) {
    /* Step 1: Require both the configured request cap and a representable estimated byte capacity. */
    return max_tokens <= CGAI_MAX_GENERATION_TOKENS &&
           max_tokens <= (SIZE_MAX - 1U) / CGAI_OUTPUT_BYTES_PER_TOKEN;
}

/**
 * @brief Read an optional token limit and validate the resulting buffer estimate.
 *
 * The destination initially holds the default token count. An absent argv[4] preserves that value.
 * Explicit values use the positive-size parser, so an explicit zero is rejected here even though
 * the core API can generate zero tokens. Failures are described on stderr.
 *
 * @param argc Dispatcher-validated generation argument count.
 * @param argv Borrowed arguments with optional token count at index four.
 * @param max_tokens Non-NULL default-initialized token limit, replaced by a valid override.
 * @return One for an accepted default or override, otherwise zero.
 */
static int parse_max_tokens(int argc, char **argv, size_t *max_tokens) {
    /* Step 1: Parse the override only when its argument exists. */
    if (argc >= 5 && !cgai_cli_parse_size(argv[4], max_tokens)) {
        fprintf(stderr, "invalid max token count: %s\n", argv[4]);
        return 0;
    }
    /* Step 2: Reject limits that exceed CLI policy or the allocation-size bound. */
    if (!valid_generation_size(*max_tokens)) {
        fprintf(stderr, "max token count is too large\n");
        return 0;
    }
    /* Step 3: Confirm the chosen token count can be used to reserve output storage. */
    return 1;
}

/**
 * @brief Apply optional generation arguments to default command settings.
 *
 * Options are processed in command-line order so diagnostics identify the first rejected value.
 * A successful earlier override remains in the structure if a later argument fails; the caller
 * must not execute generation after a zero result.
 *
 * @param argc Validated count containing model and prompt arguments.
 * @param argv Borrowed vector with optional token count, temperature, and seed.
 * @param options Non-NULL mutable settings initialized with command defaults.
 * @return One when all supplied values parse, otherwise zero after a diagnostic.
 */
static int parse_generation_args(int argc, char **argv, cli_generation_options *options) {
    /* Parse each optional value independently so errors identify the exact argument. */
    /* Step 1: Validate the token limit and estimated output capacity first. */
    if (!parse_max_tokens(argc, argv, &options->max_tokens)) {
        return 0;
    }
    /* Step 2: Parse temperature only when that optional argument is present. */
    if (argc >= 6) {
        if (!parse_temperature(argv[5], &options->temperature)) {
            fprintf(stderr, "invalid temperature: %s\n", argv[5]);
            return 0;
        }
    }
    /* Step 3: Parse the optional seed after temperature has been accepted. */
    if (argc >= 7) {
        if (!parse_seed(argv[6], &options->seed)) {
            fprintf(stderr, "invalid seed: %s\n", argv[6]);
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Load a model, generate its continuation, print the result, and clean up.
 *
 * The command owns the loaded model and temporary output allocation. The prompt remains borrowed.
 * Short-circuit AND prevents calling generation without both resources. A successful print joins
 * prompt and continuation with a separator only when the continuation is nonempty.
 *
 * @param model_path Borrowed NUL-terminated artifact path.
 * @param prompt Borrowed NUL-terminated prompt to display and pass to the model.
 * @param options Non-NULL settings whose token count passed the CLI allocation bound.
 * @return Zero after successful generation, otherwise one after printing a diagnostic.
 */
static int run_generation(const char *model_path, const char *prompt,
                          const cli_generation_options *options) {
    /* Reserve conservative output space because the core API requires caller storage. */
    /* Step 1: Load owned model state and allocate the checked output estimate. */
    cgai_model *model = cgai_model_load(model_path);
    char *output = (char *)malloc(options->max_tokens * CGAI_OUTPUT_BYTES_PER_TOKEN + 1U);
    /* Step 2: Generate only if both resources exist, preserving core success as a boolean. */
    const int ok =
        model != NULL && output != NULL &&
        cgai_model_generate(model, prompt, options->max_tokens, options->temperature, options->seed,
                            output, options->max_tokens * CGAI_OUTPUT_BYTES_PER_TOKEN + 1U);
    /* Step 3: Print either a failure diagnostic or prompt plus continuation. */
    if (!ok) {
        fprintf(stderr, "generation failed: %s\n",
                output == NULL ? "out of memory" : cgai_last_error());
    } else {
        printf("%s%s%s\n", prompt, output[0] != '\0' ? " " : "", output);
    }
    /* Step 4: Release output and model ownership on the common completion path. */
    free(output);
    cgai_model_destroy(model);
    return ok ? 0 : 1;
}

/**
 * @brief Run generation with default settings and optional positional overrides.
 *
 * The dispatcher validates argument positions before this handler reads model path and prompt.
 * Settings live on the stack and are borrowed by the execution helper. Parsing failure exits with
 * a syntax status before any model is loaded.
 *
 * @param argc Validated generation-command argument count.
 * @param argv Borrowed command vector with model path at index two and prompt at index three.
 * @return Zero for success, two for invalid options, or one for runtime failure.
 */
int cgai_cli_generate(int argc, char **argv) {
    /* Step 1: Initialize token limit, temperature, and seed defaults, then parse overrides. */
    cli_generation_options options = {40U, 0.8, 0U};
    if (!parse_generation_args(argc, argv, &options)) {
        return 2;
    }
    /* Step 2: Execute the request only after option parsing has succeeded. */
    return run_generation(argv[2], argv[3], &options);
}
