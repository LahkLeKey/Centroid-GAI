/** @file cli.c @brief Command dispatch and usage. */

#include "internal/cli.h"
#include "internal/cli_commands.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Print the CLI syntax to a caller-selected standard stream.
 *
 * Help goes to stdout for an invocation without a command, while invalid syntax is reported on
 * stderr. Adjacent C string literals form one format string at compilation. This routine borrows
 * the stream and does not close it.
 *
 * @param stream Non-NULL writable stream, normally stdout or stderr.
 */
static void usage(FILE *stream) {
    /* Step 1: Print both command forms and mark optional positional arguments with brackets. */
    fprintf(stream, "Centroid-GAI: compact centroid-based text generation\n\n"
                    "Usage:\n"
                    "  cgai train <corpus.txt> <model.cgai> [centroids]\n"
                    "  cgai generate <model.cgai> <prompt> [max-tokens] [temperature] [seed]\n");
}

/**
 * @brief Validate the command shape and dispatch to training or generation.
 *
 * argc includes the executable name at argv[0], so the first command word is argv[1]. Argument
 * counts are checked before handlers index positional values. This layer checks command syntax;
 * command-specific parsers and the core validate numeric/model constraints.
 *
 * @param argc Process argument count, including the executable name.
 * @param argv Borrowed process argument vector containing argc NUL-terminated strings.
 * @return Zero for success/help, two for command syntax errors, or a handler's runtime-failure status.
 */
int cgai_cli_run(int argc, char **argv) {
    /* Step 1: Recognize training only when its required paths and optional centroid count fit. */
    if (argc >= 2 && strcmp(argv[1], "train") == 0 && (argc == 4 || argc == 5)) {
        return cgai_cli_train(argc, argv);
    }
    /* Step 2: Recognize generation with its required model/prompt and optional numeric values. */
    if (argc >= 4 && strcmp(argv[1], "generate") == 0 && argc <= 7) {
        return cgai_cli_generate(argc, argv);
    }
    /* Step 3: Print usage to the appropriate stream when no valid command matched. */
    usage(argc > 1 ? stderr : stdout);
    return argc > 1 ? 2 : 0;
}
