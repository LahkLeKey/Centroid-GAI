/** @file cli.h @brief Private command-line application entry point. */

#ifndef CGAI_CLI_H
#define CGAI_CLI_H

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
int cgai_cli_run(int argc, char **argv);

#endif