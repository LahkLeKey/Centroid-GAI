/** @file test_process.c @brief Native argv, redirection, exit and descendant-lifetime checks. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "process.h"
#include "test_utils.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#endif

#define CHECK(condition) TEST_CHECK(condition, "Native process helper check failed")
#define PATH_BYTES (EVOLVE_PROCESS_MAX_PATH_BYTES + 1U)

typedef struct process_fixture {
    char directory[PATH_BYTES];
    char executable[PATH_BYTES];
    char log[PATH_BYTES];
    char marker[PATH_BYTES];
} process_fixture;

static void pause_ms(uint32_t milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec remaining = {(time_t)(milliseconds / 1000U),
                                 (long)(milliseconds % 1000U) * 1000000L};
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
#endif
}

static unsigned int current_pid(void) {
#ifdef _WIN32
    return (unsigned int)GetCurrentProcessId();
#else
    return (unsigned int)getpid();
#endif
}

static int echo_child(int argc, char **argv) {
    for (int i = 2; i < argc; ++i) {
        const size_t length = strlen(argv[i]);
        CHECK(fprintf(stdout, "%zu:", length) >= 0);
        CHECK(fwrite(argv[i], 1U, length, stdout) == length);
        CHECK(fputc('\n', stdout) != EOF);
    }
    CHECK(fgetc(stdin) == EOF);
    CHECK(fputs("stdin-eof\n", stdout) >= 0 && fflush(stdout) == 0);
    CHECK(fputs("stderr-marker\n", stderr) >= 0);
    return 7;
}

static int marker_child(const char *path) {
    pause_ms(2500U);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fputs("orphan reached marker\n", file) >= 0);
    CHECK(fclose(file) == 0);
    return 0;
}

static int spawn_descendant(const char *executable, const char *marker) {
#ifdef _WIN32
    char line[2U * PATH_BYTES + 64U];
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process = {0};
    const int length =
        snprintf(line, sizeof(line), "\"%s\" --process-marker \"%s\"", executable, marker);
    startup.cb = sizeof(startup);
    if (length < 0 || (size_t)length >= sizeof(line) ||
        !CreateProcessA(executable, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup,
                        &process))
        return 0;
    CHECK(CloseHandle(process.hThread));
    CHECK(CloseHandle(process.hProcess));
    return 1;
#else
    const pid_t descendant = fork();
    if (descendant < 0)
        return 0;
    if (descendant == 0) {
        execl(executable, executable, "--process-marker", marker, (char *)NULL);
        _exit(127);
    }
    return 1;
#endif
}

static int tree_child(const char *executable, const char *marker) {
    CHECK(spawn_descendant(executable, marker));
    CHECK(fputs("descendant spawned\n", stdout) >= 0 && fflush(stdout) == 0);
    pause_ms(5000U);
    return 0;
}

static int copy_contents(FILE *input, FILE *output) {
    unsigned char bytes[4096];
    for (;;) {
        const size_t count = fread(bytes, 1U, sizeof(bytes), input);
        if (count != 0U && fwrite(bytes, 1U, count, output) != count)
            return 0;
        if (count < sizeof(bytes))
            return !ferror(input);
    }
}

static void copy_executable(const char *source, const char *destination) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(destination, "wb");
    CHECK(input != NULL && output != NULL);
    const int copied = copy_contents(input, output);
    const int input_closed = fclose(input) == 0;
    const int output_closed = fclose(output) == 0;
    CHECK(copied && input_closed && output_closed);
#ifndef _WIN32
    CHECK(chmod(destination, 0700) == 0);
#endif
}

static void init_fixture(process_fixture *fixture, const char *executable) {
    char directory[128], source[PATH_BYTES];
    const int length =
        snprintf(directory, sizeof(directory), "test evolve process %u's space", current_pid());
    CHECK(length > 0 && (size_t)length < sizeof(directory));
    CHECK(evolve_create_directory(directory));
    CHECK(!evolve_create_directory(directory) && evolve_make_directory(directory));
    CHECK(evolve_absolute_path(directory, fixture->directory, sizeof(fixture->directory)));
    CHECK(evolve_absolute_path(executable, source, sizeof(source)));
    CHECK(evolve_path_join(fixture->executable, sizeof(fixture->executable), fixture->directory,
                           "child with spaces.exe"));
    CHECK(
        evolve_path_join(fixture->log, sizeof(fixture->log), fixture->directory, "child log.txt"));
    CHECK(evolve_path_join(fixture->marker, sizeof(fixture->marker), fixture->directory,
                           "orphan marker.txt"));
    copy_executable(source, fixture->executable);
}

static void check_argument(FILE *file, const char *expected) {
    char actual[PATH_BYTES];
    size_t length;
    CHECK(fscanf(file, "%zu", &length) == 1 && fgetc(file) == ':');
    CHECK(length == strlen(expected) && length < sizeof(actual));
    CHECK(fread(actual, 1U, length, file) == length);
    CHECK(memcmp(actual, expected, length) == 0 && fgetc(file) == '\n');
}

static void argument_bytes(const process_fixture *fixture) {
    const char *const arguments[] = {"--process-echo",
                                     "",
                                     "white space",
                                     "two\tfields",
                                     "line\nbreak",
                                     "double\"quote",
                                     "tail\\",
                                     "space tail\\",
                                     "backslash\\\\\"quote",
                                     "x&|<>$`y",
                                     NULL};
    int result = 91;
    CHECK(evolve_process_run(fixture->executable, arguments, fixture->log, 10U, &result));
    CHECK(result == 7);
    FILE *file = fopen(fixture->log, "rb");
    char line[64];
    CHECK(file != NULL);
    for (size_t i = 1U; arguments[i] != NULL; ++i)
        check_argument(file, arguments[i]);
    CHECK(fgets(line, sizeof(line), file) != NULL && strcmp(line, "stdin-eof\n") == 0);
    CHECK(fgets(line, sizeof(line), file) != NULL && strcmp(line, "stderr-marker\n") == 0);
    CHECK(fgetc(file) == EOF && !ferror(file));
    CHECK(fclose(file) == 0);
}

static void argument_limits(const process_fixture *fixture) {
    const char *arguments[EVOLVE_PROCESS_MAX_ARGUMENTS + 2U];
    int result = 91;
    arguments[0] = "--process-exit127";
    for (size_t i = 1U; i <= EVOLVE_PROCESS_MAX_ARGUMENTS; ++i)
        arguments[i] = "";
    arguments[EVOLVE_PROCESS_MAX_ARGUMENTS] = NULL;
    arguments[EVOLVE_PROCESS_MAX_ARGUMENTS + 1U] = NULL;
    CHECK(evolve_process_run(fixture->executable, arguments, fixture->log, 10U, &result));
    CHECK(result == 127);
    arguments[EVOLVE_PROCESS_MAX_ARGUMENTS] = "";
    result = 91;
    CHECK(!evolve_process_run(fixture->executable, arguments, fixture->log, 10U, &result));
    CHECK(result == 91);
}

static void invalid_processes(const process_fixture *fixture) {
    int result = 91;
    CHECK(!evolve_process_run(fixture->executable, NULL, fixture->log, 0U, &result));
    CHECK(!evolve_process_run(fixture->executable, NULL, fixture->log,
                              EVOLVE_PROCESS_MAX_TIMEOUT_SECONDS + 1U, &result));
    CHECK(!evolve_process_run(NULL, NULL, fixture->log, 1U, &result));
    CHECK(!evolve_process_run(fixture->executable, NULL, fixture->directory, 1U, &result));
    CHECK(
        !evolve_process_run("no-such-evolve-process-executable", NULL, fixture->log, 1U, &result));
    CHECK(result == 91);
}

static void invalid_executable(const process_fixture *fixture) {
    const unsigned char bytes[] = {0x7fU, 'x', 0U, 0U, 'y', 0U};
    char path[PATH_BYTES];
    CHECK(evolve_path_join(path, sizeof(path), fixture->directory, "malformed binary.exe"));
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(bytes, 1U, sizeof(bytes), file) == sizeof(bytes));
    CHECK(fclose(file) == 0);
#ifndef _WIN32
    CHECK(chmod(path, 0700) == 0);
#endif
    int result = 91;
    CHECK(!evolve_process_run(path, NULL, fixture->log, 1U, &result));
    CHECK(result == 91);
    CHECK(remove(path) == 0);
}

static void path_boundaries(const process_fixture *fixture) {
    char output[PATH_BYTES] = "unchanged";
    char oversized[EVOLVE_PROCESS_MAX_PATH_BYTES + 2U];
    memset(oversized, 'x', sizeof(oversized) - 1U);
    oversized[sizeof(oversized) - 1U] = '\0';
    CHECK(!evolve_path_join(output, 2U, fixture->directory, "child"));
    CHECK(!evolve_path_join(output, sizeof(output), fixture->directory, oversized));
    CHECK(!evolve_absolute_path(fixture->directory, output, 2U));
    CHECK(strcmp(output, "unchanged") == 0);
    CHECK(!evolve_create_directory(fixture->executable));
    CHECK(!evolve_make_directory(fixture->executable));
    CHECK(evolve_absolute_path(fixture->executable, output, sizeof(output)));
    CHECK(strcmp(output, fixture->executable) == 0);
    CHECK(evolve_path_join(output, sizeof(output), fixture->directory, "aliased path"));
    CHECK(evolve_path_join(output, sizeof(output), output, "child"));
}

static void timeout_descendants(const process_fixture *fixture) {
    const char *const arguments[] = {"--process-tree", fixture->executable, fixture->marker, NULL};
    int result = 91;
    CHECK(!evolve_process_run(fixture->executable, arguments, fixture->log, 1U, &result));
    CHECK(result == 91);
    FILE *file = fopen(fixture->log, "rb");
    char line[64];
    CHECK(file != NULL && fgets(line, sizeof(line), file) != NULL);
    CHECK(strcmp(line, "descendant spawned\n") == 0);
    CHECK(fclose(file) == 0);
    pause_ms(2500U);
    file = fopen(fixture->marker, "rb");
    if (file != NULL)
        CHECK(fclose(file) == 0);
    CHECK(file == NULL);
}

static void cleanup_fixture(const process_fixture *fixture) {
    CHECK(remove(fixture->log) == 0);
    CHECK(remove(fixture->executable) == 0);
#ifdef _WIN32
    CHECK(_rmdir(fixture->directory) == 0);
#else
    CHECK(rmdir(fixture->directory) == 0);
#endif
}

static void binary_streams(void) {
#ifdef _WIN32
    CHECK(_setmode(_fileno(stdout), _O_BINARY) != -1);
    CHECK(_setmode(_fileno(stderr), _O_BINARY) != -1);
#endif
}

static int dispatch_child(int argc, char **argv, int *result) {
    binary_streams();
    if (argc >= 2 && strcmp(argv[1], "--process-echo") == 0) {
        *result = echo_child(argc, argv);
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "--process-exit127") == 0) {
        *result = 127;
        return 1;
    }
    if (argc == 3 && strcmp(argv[1], "--process-marker") == 0) {
        *result = marker_child(argv[2]);
        return 1;
    }
    if (argc == 4 && strcmp(argv[1], "--process-tree") == 0) {
        *result = tree_child(argv[2], argv[3]);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int child_result;
    if (dispatch_child(argc, argv, &child_result))
        return child_result;
    process_fixture fixture;
    CHECK(argc == 1);
    init_fixture(&fixture, argv[0]);
    argument_bytes(&fixture);
    argument_limits(&fixture);
    invalid_processes(&fixture);
    invalid_executable(&fixture);
    path_boundaries(&fixture);
    timeout_descendants(&fixture);
    cleanup_fixture(&fixture);
    puts("Native process arguments, paths, exit codes and timeout cleanup checks passed");
    return 0;
}
