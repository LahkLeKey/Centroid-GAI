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
                    "  cgai generate <model.cgai> <prompt> [max-tokens] [temperature] [seed]\n"
                    "  cgai neural-train <train.txt> <model.cgnn> [epochs] [learning-rate] "
                    "[validation.txt]\n"
                    "  cgai neural-init <train.txt> <checkpoint.txt>\n"
                    "  cgai neural-step <checkpoint.txt> <train.txt> <heldout.txt> "
                    "<candidate.txt> [epochs] [learning-rate]\n"
                    "  cgai neural-candidate <checkpoint.txt> <train.txt> <development.txt> "
                    "<candidate.txt> [epochs] [learning-rate]\n"
                    "  cgai neural-score <checkpoint.txt> <corpus.txt>\n"
                    "  cgai neural-replay <checkpoint.txt> <train.txt> <replay.txt> "
                    "[learning-rate]\n"
                    "  cgai neural-export <checkpoint.txt> <model.cgnn>\n"
                    "  cgai neural-evaluate <model.cgnn> <heldout.txt>\n"
                    "  cgai neural-generate <model.cgnn> <prompt> [max-tokens] [temperature] "
                    "[seed]\n"
                    "  cgai knowledge [categories | list <category-key> | find <id> | "
                    "nearest <id> [limit] [category-key]]\n");
}

/** @brief Route checkpoint workflow commands after validating their argument counts.
 * @param argc Process argument count, including the executable name.
 * @param argv Borrowed argument vector; index one is read only when counts permit it.
 * @return The handler status, or minus one when no checkpoint command shape matches. */
static int checkpoint_command(int argc, char **argv) {
    /* Step 1: Route the source-controlled full-epoch training workflow. */
    if (argc == 4 && strcmp(argv[1], "neural-init") == 0)
        return cgai_cli_neural_init(argc, argv);
    if (argc >= 6 && argc <= 8 && strcmp(argv[1], "neural-step") == 0)
        return cgai_cli_neural_step(argc, argv);
    if (argc >= 6 && argc <= 8 && strcmp(argv[1], "neural-candidate") == 0)
        return cgai_cli_neural_candidate(argc, argv);
    if (argc == 4 && strcmp(argv[1], "neural-score") == 0)
        return cgai_cli_neural_score(argc, argv);
    if (argc >= 5 && argc <= 6 && strcmp(argv[1], "neural-replay") == 0)
        return cgai_cli_neural_replay(argc, argv);
    if (argc == 4 && strcmp(argv[1], "neural-export") == 0)
        return cgai_cli_neural_export(argc, argv);
    return -1;
}

/** @brief Route existing model and knowledge commands with bounded positional arguments.
 * @param argc Process argument count, including the executable name.
 * @param argv Borrowed argument vector; index one is read only when counts permit it.
 * @return The handler status, or minus one when no existing command shape matches. */
static int model_command(int argc, char **argv) {
    /* Step 1: Route neural commands only after checking their positional argument counts. */
    if (argc >= 4 && argc <= 7 && strcmp(argv[1], "neural-train") == 0)
        return cgai_cli_neural_train(argc, argv);
    if (argc == 4 && strcmp(argv[1], "neural-evaluate") == 0)
        return cgai_cli_neural_evaluate(argc, argv);
    if (argc >= 4 && argc <= 7 && strcmp(argv[1], "neural-generate") == 0)
        return cgai_cli_neural_generate(argc, argv);
    /* Step 2: Preserve the existing native knowledge command and baseline model commands. */
    if (argc >= 2 && strcmp(argv[1], "knowledge") == 0)
        return cgai_cli_knowledge(argc, argv);
    /* Step 3: Recognize training only when its required paths and optional centroid count fit. */
    if (argc >= 2 && strcmp(argv[1], "train") == 0 && (argc == 4 || argc == 5)) {
        return cgai_cli_train(argc, argv);
    }
    /* Step 4: Recognize generation with its required model/prompt and optional numeric values. */
    if (argc >= 4 && strcmp(argv[1], "generate") == 0 && argc <= 7) {
        return cgai_cli_generate(argc, argv);
    }
    return -1;
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
 * @return Zero for success/help, two for command syntax errors, or a handler's runtime-failure
 * status.
 */
int cgai_cli_run(int argc, char **argv) {
    /* Step 1: Delegate independently bounded command families before reporting invalid syntax. */
    int status = checkpoint_command(argc, argv);
    if (status >= 0)
        return status;
    status = model_command(argc, argv);
    if (status >= 0)
        return status;
    /* Step 2: Print usage to the appropriate stream when no valid command matched. */
    usage(argc > 1 ? stderr : stdout);
    return argc > 1 ? 2 : 0;
}
