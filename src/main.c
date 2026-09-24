/** @file main.c @brief Process entry point for the command-line application. */

#include "internal/cli.h"

/**
 * @brief Pass process arguments to the CLI and return its exit status.
 *
 * The operating system supplies the argument vector. Keeping main as a small forwarding entry
 * point lets parsing and command execution live in focused modules. Returning the dispatcher's
 * integer passes its success or failure status back to the invoking shell.
 *
 * @param argc Argument count including the executable name.
 * @param argv Process-owned argument strings, borrowed for the lifetime of main.
 * @return The CLI's process-style exit code: zero for success, one for runtime failure, two for syntax errors.
 */
int main(int argc, char **argv) { /* Step 1: Delegate parsing and execution, preserving the resulting process status. */
 return cgai_cli_run(argc, argv); }
