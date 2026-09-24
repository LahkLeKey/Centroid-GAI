/** @file cli_commands.h @brief Private command handlers and numeric argument parsing. */
#ifndef CGAI_CLI_COMMANDS_H
#define CGAI_CLI_COMMANDS_H
#include <stddef.h>
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
