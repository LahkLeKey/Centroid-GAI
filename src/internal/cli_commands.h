/** @file cli_commands.h @brief Private command handlers and numeric argument parsing. */
#ifndef CGAI_CLI_COMMANDS_H
#define CGAI_CLI_COMMANDS_H
#include <stddef.h>
/**
 * @brief Run neural training with strict numeric options and optional held-out reporting.
 *
 * The command owns both corpus buffers. Failed validation-file loading stops training before
 * any model is created; omitting validation leaves its pointer NULL deliberately.
 * @param argc Dispatcher-validated count between four and seven.
 * @param argv Borrowed train/model paths and optional epochs, rate, and held-out path.
 * @return Zero for success, two for invalid options, or one for execution failure.
 */
int cgai_cli_neural_train(int argc, char **argv);
/**
 * @brief Load a neural artifact and evaluate a separate sequence with its frozen vocabulary.
 *
 * The dispatcher has already validated two path arguments. This command owns its model and
 * text buffer and releases both even when reading or evaluation fails.
 * @param argc Validated argument count of four; retained for the shared CLI handler shape.
 * @param argv Borrowed arguments with model path at two and held-out text path at three.
 * @return Zero for success or one after a runtime diagnostic.
 */
int cgai_cli_neural_evaluate(int argc, char **argv);
/**
 * @brief Generate from a neural artifact with optional count, temperature, and seed.
 *
 * Command syntax is checked before opening the artifact. The loaded model is immutable during
 * generation and is destroyed on the single completion path.
 * @param argc Dispatcher-validated count between four and seven.
 * @param argv Borrowed model path, prompt, and optional generation arguments.
 * @return Zero for success, two for invalid options, or one for runtime failure.
 */
int cgai_cli_neural_generate(int argc, char **argv);
/**
 * @brief Execute native compiled-knowledge discovery and lookup commands.
 * @param argc Process argument count, at least two.
 * @param argv Borrowed arguments with knowledge at index one.
 * @return Zero on success, two for syntax errors, or one for native query failure.
 */
int cgai_cli_knowledge(int argc, char **argv);
/**
 * @brief Convert a fully consumed decimal argument into a positive native size.
 *
 * strtoull returns both a numeric result and an end pointer. errno detects reported conversion
 * range errors; comparing end with text detects an input with no digits. This function follows
 * strtoull's lexical rules, including accepted whitespace/sign prefixes, rather than implementing
 * a digits-only parser. The destination is left unchanged unless all checks pass.
 *
 * @param text Non-NULL borrowed NUL-terminated numeric text.
 * @param value Non-NULL writable destination for a positive size_t value.
 * @return One after storing an accepted value, otherwise zero.
 */
int cgai_cli_parse_size(const char *text, size_t *value);
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
int cgai_cli_train(int argc, char **argv);
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
int cgai_cli_generate(int argc, char **argv);
#endif
