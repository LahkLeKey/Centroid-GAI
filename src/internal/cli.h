/** @file cli.h @brief Private command-line application entry point. */

#ifndef CGAI_CLI_H
#define CGAI_CLI_H

/**
 * @brief Runs the Centroid-GAI command-line application.
 * @param argc Standard argument count.
 * @param argv Standard NUL-terminated argument vector.
 * @return Process-style status: zero for success/help, two for syntax errors, one for runtime
 * errors.
 */
int cgai_cli_run(int argc, char **argv);

#endif