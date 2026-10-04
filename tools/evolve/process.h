/** @file process.h @brief Bounded native process and filesystem path helpers. */
#ifndef CGAI_EVOLVE_PROCESS_H
#define CGAI_EVOLVE_PROCESS_H

#include <stddef.h>
#include <stdint.h>

#define EVOLVE_PROCESS_MAX_ARGUMENTS 32U
#define EVOLVE_PROCESS_MAX_PATH_BYTES 4096U
#define EVOLVE_PROCESS_MAX_TIMEOUT_SECONDS 1800U

/** Run an executable directly without a shell. Arguments exclude argv[0], are
 * NULL terminated, and may themselves be NULL for no arguments. Each argument
 * and path is bounded at 4096 bytes excluding NUL. Timeout is 1..1800 seconds.
 * Standard input is the null device; stdout and stderr share a replaced log.
 * Descendants remain within the invocation's lifetime. Returns one only when
 * a normal, representable exit code was recorded, including nonzero codes.
 * Launch, timeout and signal failures return zero without changing exit_code.
 * All strings are borrowed, terminated native strings; exit_code is required. */
int evolve_process_run(const char *exe, const char *const *arguments, const char *log_path,
                       uint32_t timeout_seconds, int *exit_code);

/** Join a directory and relative name with a native separator. Input and result
 * are bounded at 4096 bytes excluding NUL. Failure leaves the output unchanged. */
int evolve_path_join(char *output, size_t capacity, const char *directory, const char *name);

/** Create one directory without creating parents. An existing directory succeeds;
 * an existing file or an invalid/overlong path fails. */
int evolve_make_directory(const char *path);

/** Create one new directory without creating parents. Any existing path fails. */
int evolve_create_directory(const char *path);

/** Resolve an existing native path to an absolute path. POSIX resolves links;
 * Windows applies GetFullPathName normalization and verifies existence.
 * Failure, including insufficient capacity, leaves the output unchanged. */
int evolve_absolute_path(const char *input, char *output, size_t capacity);

#endif
