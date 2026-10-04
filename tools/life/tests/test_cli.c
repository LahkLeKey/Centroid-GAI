/** @file test_cli.c @brief Native CLI continuation, inspection and rejection checks. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#define CLI_PATH_CAPACITY 4096U
#define CLI_ARGUMENT_CAPACITY 32U
#define CHECK(condition, message)                                                                  \
    ((condition) ? (void)0 : check_failed(__FILE__, __LINE__, (message)))

static _Noreturn void check_failed(const char *file, int line, const char *message) {
    fprintf(stderr, "%s:%d: %s\n", file, line, message);
    exit(EXIT_FAILURE);
}

typedef struct cli_fixture {
    const char *executable;
    char directory[CLI_PATH_CAPACITY];
    char log[CLI_PATH_CAPACITY];
} cli_fixture;

typedef struct cli_report {
    uint32_t generation;
    uint32_t population;
    uint32_t collisions;
    uint64_t updates;
    uint64_t merges;
    uint64_t hash;
} cli_report;

static int join_path(char *result, const char *directory, const char *name) {
    const int written = snprintf(result, CLI_PATH_CAPACITY, "%s/%s", directory, name);
    return written >= 0 && (size_t)written < CLI_PATH_CAPACITY;
}

static int suffix_path(char *result, const char *prefix, const char *suffix) {
    const int written = snprintf(result, CLI_PATH_CAPACITY, "%s%s", prefix, suffix);
    return written >= 0 && (size_t)written < CLI_PATH_CAPACITY;
}

static int make_directory(const char *path) {
#ifdef _WIN32
    if (_mkdir(path) == 0)
        return 1;
    const DWORD attributes = GetFileAttributesA(path);
    return errno == EEXIST && attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
#else
    if (mkdir(path, 0700) == 0)
        return 1;
    struct stat information;
    return errno == EEXIST && stat(path, &information) == 0 && S_ISDIR(information.st_mode);
#endif
}

#ifdef _WIN32
/* Follow the Windows C argv quoting rules, including terminal backslashes. */
static char *quote_fragment(char *output, const char **argument) {
    size_t slashes = 0U;
    while (**argument == '\\') {
        ++slashes;
        ++*argument;
    }
    const size_t copies = (**argument == '"' || **argument == '\0') ? slashes * 2U : slashes;
    for (size_t i = 0U; i < copies; ++i)
        *output++ = '\\';
    if (**argument == '"')
        *output++ = '\\';
    if (**argument != '\0')
        *output++ = *(*argument)++;
    return output;
}

static char *quote_argument(char *output, const char *argument) {
    *output++ = '"';
    while (*argument != '\0')
        output = quote_fragment(output, &argument);
    *output++ = '"';
    return output;
}

static size_t command_capacity(const char *executable, const char *const *arguments) {
    size_t capacity = 1U;
    const char *part = executable;
    for (size_t i = 0U; part != NULL; part = arguments[i++]) {
        const size_t length = strlen(part);
        if (capacity > SIZE_MAX - 3U || length > (SIZE_MAX - capacity - 3U) / 2U)
            return 0U;
        capacity += length * 2U + 3U;
    }
    return capacity;
}

static char *command_line(const char *executable, const char *const *arguments) {
    const size_t capacity = command_capacity(executable, arguments);
    if (capacity == 0U)
        return NULL;
    char *line = malloc(capacity);
    if (line == NULL)
        return NULL;
    char *end = quote_argument(line, executable);
    for (size_t i = 0U; arguments[i] != NULL; ++i) {
        *end++ = ' ';
        end = quote_argument(end, arguments[i]);
    }
    *end = '\0';
    return line;
}

typedef struct native_child {
    HANDLE log;
    HANDLE input;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
} native_child;

static int prepare_child(native_child *child, const char *log_path) {
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    child->log = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    child->input = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (child->log == INVALID_HANDLE_VALUE || child->input == INVALID_HANDLE_VALUE)
        return 0;
    child->startup.cb = sizeof(child->startup);
    child->startup.dwFlags = STARTF_USESTDHANDLES;
    child->startup.hStdInput = child->input;
    child->startup.hStdOutput = child->log;
    child->startup.hStdError = child->log;
    return 1;
}

static int await_child(native_child *child, int *status) {
    DWORD result = 0U;
    if (WaitForSingleObject(child->process.hProcess, 60000U) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(child->process.hProcess, &result) && result <= (DWORD)INT_MAX) {
        *status = (int)result;
        return 1;
    }
    (void)TerminateProcess(child->process.hProcess, 1U);
    (void)WaitForSingleObject(child->process.hProcess, 5000U);
    return 0;
}

static void close_handle(HANDLE handle) {
    if (handle != NULL && handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
}

static int launch(const cli_fixture *fixture, const char *const *arguments, int *status) {
    native_child child = {0};
    char *line = command_line(fixture->executable, arguments);
    if (line == NULL)
        return 0;
    const int okay = prepare_child(&child, fixture->log) &&
                     CreateProcessA(fixture->executable, line, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                                    NULL, NULL, &child.startup, &child.process) &&
                     await_child(&child, status);
    close_handle(child.process.hThread);
    close_handle(child.process.hProcess);
    close_handle(child.log);
    close_handle(child.input);
    free(line);
    return okay;
}
#else
static int child_descriptor(const char *path, int flags) {
    const int descriptor = open(path, flags, 0600);
    if (descriptor < 0 || descriptor > STDERR_FILENO)
        return descriptor;
    const int moved = fcntl(descriptor, F_DUPFD, STDERR_FILENO + 1);
    close(descriptor);
    return moved;
}

static int prepare_arguments(const cli_fixture *fixture, const char *const *arguments,
                             char **child_arguments) {
    size_t count = 1U;
    child_arguments[0] = (char *)fixture->executable;
    for (size_t i = 0U; arguments[i] != NULL; ++i) {
        if (count >= CLI_ARGUMENT_CAPACITY - 1U)
            return 0;
        child_arguments[count++] = (char *)arguments[i];
    }
    child_arguments[count] = NULL;
    return 1;
}

static _Noreturn void execute_child(const cli_fixture *fixture, char *const *arguments) {
    const int log = child_descriptor(fixture->log, O_WRONLY | O_CREAT | O_TRUNC);
    const int input = child_descriptor("/dev/null", O_RDONLY);
    if (log < 0 || input < 0 || dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0 ||
        dup2(input, STDIN_FILENO) < 0)
        _exit(126);
    close(log);
    close(input);
    execv(fixture->executable, arguments);
    _exit(127);
}

static int terminate_child(pid_t child) {
    int result = 0;
    pid_t waited;
    (void)kill(child, SIGKILL);
    do {
        waited = waitpid(child, &result, 0);
    } while (waited < 0 && errno == EINTR);
    return 0;
}

static int child_expired(const struct timespec *started) {
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec - started->tv_sec >= 60;
}

static int child_status(int result, int *status) {
    if (!WIFEXITED(result))
        return 0;
    *status = WEXITSTATUS(result);
    return *status != 126 && *status != 127;
}

static int await_child(pid_t child, int *status) {
    int result = 0;
    struct timespec started;
    const struct timespec pause = {0, 10000000L};
    if (clock_gettime(CLOCK_MONOTONIC, &started) != 0)
        return terminate_child(child);
    for (;;) {
        const pid_t waited = waitpid(child, &result, WNOHANG);
        if (waited == child)
            return child_status(result, status);
        if (waited < 0 && errno != EINTR)
            return 0;
        if (child_expired(&started))
            return terminate_child(child);
        (void)nanosleep(&pause, NULL);
    }
}

static int launch(const cli_fixture *fixture, const char *const *arguments, int *status) {
    char *child_arguments[CLI_ARGUMENT_CAPACITY];
    if (!prepare_arguments(fixture, arguments, child_arguments))
        return 0;
    const pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0)
        execute_child(fixture, child_arguments);
    return await_child(child, status);
}
#endif

static int command(const cli_fixture *fixture, const char *const *arguments, int expected) {
    int status = -1;
    if (launch(fixture, arguments, &status) && status == expected)
        return 1;
    fprintf(stderr, "CLI command failed: expected %d, received %d; output: %s\n", expected, status,
            fixture->log);
    FILE *log = fopen(fixture->log, "rb");
    if (log != NULL) {
        int character;
        while ((character = fgetc(log)) != EOF)
            fputc(character, stderr);
        fclose(log);
    }
    return 0;
}

static int close_pair(FILE *a, FILE *b, int okay) {
    if (a != NULL && fclose(a) != 0)
        okay = 0;
    if (b != NULL && fclose(b) != 0)
        okay = 0;
    return okay;
}

static int streams_equal(FILE *a, FILE *b) {
    unsigned char first[4096];
    unsigned char second[4096];
    while (!feof(a) && !feof(b)) {
        const size_t na = fread(first, 1U, sizeof(first), a);
        const size_t nb = fread(second, 1U, sizeof(second), b);
        if (na != nb || memcmp(first, second, na) != 0 || ferror(a) || ferror(b))
            return 0;
    }
    return feof(a) && feof(b);
}

static int files_equal(const char *left, const char *right) {
    FILE *a = fopen(left, "rb");
    FILE *b = fopen(right, "rb");
    return close_pair(a, b, a != NULL && b != NULL && streams_equal(a, b));
}

static int copy_stream(FILE *input, FILE *output) {
    unsigned char buffer[4096];
    while (!feof(input)) {
        const size_t count = fread(buffer, 1U, sizeof(buffer), input);
        if (fwrite(buffer, 1U, count, output) != count || ferror(input))
            return 0;
    }
    return 1;
}

static int copy_file(const char *source, const char *destination, const char *trailer) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(destination, "wb");
    int okay = input != NULL && output != NULL && copy_stream(input, output);
    if (okay && trailer != NULL && fputs(trailer, output) < 0)
        okay = 0;
    return close_pair(input, output, okay);
}

static int read_report(const char *path, cli_report *report) {
    FILE *file = fopen(path, "rb");
    char line[CLI_PATH_CAPACITY];
    int okay = 0;
    if (file == NULL)
        return 0;
    while (fgets(line, sizeof(line), file) != NULL)
        if (sscanf(line,
                   "generation=%" SCNu32 " population=%" SCNu32 " collisions=%" SCNu32
                   " training_updates=%" SCNu64 " merges=%" SCNu64 " hash=%" SCNx64,
                   &report->generation, &report->population, &report->collisions, &report->updates,
                   &report->merges, &report->hash) == 6) {
            okay = 1;
            break;
        }
    if (ferror(file))
        okay = 0;
    if (fclose(file) != 0)
        okay = 0;
    return okay;
}

static int reports_equal(const cli_report *a, const cli_report *b) {
    return a->generation == b->generation && a->population == b->population &&
           a->collisions == b->collisions && a->updates == b->updates && a->merges == b->merges &&
           a->hash == b->hash;
}

static int equal_artifact(const char *left, const char *right, const char *suffix) {
    char a[CLI_PATH_CAPACITY];
    char b[CLI_PATH_CAPACITY];
    return suffix_path(a, left, suffix) && suffix_path(b, right, suffix) && files_equal(a, b);
}

typedef struct trace_check {
    uint32_t first_tick;
    uint32_t last_tick;
    uint32_t frames;
    unsigned int entities;
    int in_frame;
    int grid;
    int ended;
    int has_header;
} trace_check;

static int read_line(FILE *file, char *line, size_t capacity) {
    if (feof(file) || ferror(file) || capacity > (size_t)INT_MAX ||
        fgets(line, (int)capacity, file) == NULL)
        return 0;
    const size_t length = strlen(line);
    if (length >= 2U && line[length - 2U] == '\r' && line[length - 1U] == '\n') {
        line[length - 2U] = '\n';
        line[length - 1U] = '\0';
    }
    return 1;
}

static int trace_header(FILE *file) {
    char line[2048];
    return read_line(file, line, sizeof(line)) && strcmp(line, "CGAI_LIFE_TRACE 1\n") == 0 &&
           read_line(file, line, sizeof(line)) && strcmp(line, "SHAPE 32 32 4\n") == 0;
}

static int trace_begin(trace_check *check, uint32_t tick) {
    if (check->in_frame || check->ended || tick != check->first_tick + check->frames ||
        tick > check->last_tick)
        return 0;
    check->in_frame = 1;
    check->grid = 0;
    check->entities = 0U;
    return 1;
}

static int grid_line_valid(const char *line) {
    if (strlen(line) != 33U || line[32] != '\n')
        return 0;
    for (size_t i = 0U; i < 32U; ++i)
        if (!((line[i] >= '0' && line[i] <= '9') || (line[i] >= 'a' && line[i] <= 'f')))
            return 0;
    return 1;
}

static int trace_grid(FILE *file, trace_check *check) {
    char line[2048];
    if (!check->in_frame || check->grid)
        return 0;
    check->grid = 1;
    for (unsigned int row = 0U; row < 32U; ++row)
        if (!read_line(file, line, sizeof(line)) || !grid_line_valid(line))
            return 0;
    return 1;
}

static int trace_entity(trace_check *check, const char *line) {
    unsigned int index = 0U;
    if (!check->in_frame || sscanf(line, "ENTITY %u", &index) != 1 || index != check->entities)
        return 0;
    ++check->entities;
    return 1;
}

static int trace_end_frame(trace_check *check) {
    if (!check->in_frame || !check->grid || check->entities != 4U)
        return 0;
    check->in_frame = 0;
    ++check->frames;
    return 1;
}

static int trace_end(trace_check *check, uint32_t frames) {
    if (!check->has_header || check->in_frame || check->ended || frames != check->frames)
        return 0;
    check->ended = 1;
    return 1;
}

static int trace_frame_line(trace_check *check, const char *line) {
    uint32_t tick = 0U;
    uint64_t hash = 0U;
    return sscanf(line, "FRAME %" SCNu32 " %" SCNx64, &tick, &hash) == 2 &&
           trace_begin(check, tick);
}

static int trace_record(FILE *file, trace_check *check, const char *line) {
    uint32_t number = 0U;
    if (check->ended)
        return 0;
    if (strncmp(line, "FRAME ", 6U) == 0)
        return trace_frame_line(check, line);
    if (strcmp(line, "GRID\n") == 0)
        return trace_grid(file, check);
    if (strncmp(line, "ENTITY ", 7U) == 0)
        return trace_entity(check, line);
    if (strcmp(line, "END_FRAME\n") == 0)
        return trace_end_frame(check);
    if (sscanf(line, "END_TRACE %" SCNu32, &number) == 1)
        return trace_end(check, number);
    return 1;
}

static int trace_frames(const char *path, uint32_t first_tick, uint32_t last_tick, int has_header) {
    FILE *file = fopen(path, "rb");
    char line[2048];
    trace_check check = {first_tick, last_tick, 0U, 0U, 0, 0, 0, has_header};
    int okay = file != NULL && (!has_header || trace_header(file));
    while (okay && read_line(file, line, sizeof(line)))
        okay = trace_record(file, &check, line);
    if (file != NULL) {
        okay = okay && !ferror(file) && !check.in_frame &&
               check.frames == last_tick - first_tick + 1U && (!has_header || check.ended);
        if (fclose(file) != 0)
            okay = 0;
    }
    return okay;
}
static int trace_artifact(const char *prefix, uint32_t first_tick, uint32_t last_tick) {
    char path[CLI_PATH_CAPACITY];
    return suffix_path(path, prefix, ".trace") && trace_frames(path, first_tick, last_tick, 1);
}

static int find_training_section(FILE *file, char line[2048]) {
    while (fgets(line, 2048, file) != NULL)
        if (strncmp(line, "RUN_STATS ", 10U) == 0)
            return 1;
    return 0;
}

static int training_sections_equal(const char *left, const char *right) {
    FILE *a = fopen(left, "rb");
    FILE *b = fopen(right, "rb");
    char first[2048];
    char second[2048];
    int okay = a != NULL && b != NULL && find_training_section(a, first) &&
               find_training_section(b, second);
    while (okay) {
        if (strncmp(first, "HASH ", 5U) == 0 || strncmp(second, "HASH ", 5U) == 0) {
            okay = strncmp(first, "HASH ", 5U) == 0 && strncmp(second, "HASH ", 5U) == 0;
            break;
        }
        okay = strcmp(first, second) == 0 && fgets(first, sizeof(first), a) != NULL &&
               fgets(second, sizeof(second), b) != NULL;
    }
    if (a != NULL && fclose(a) != 0)
        okay = 0;
    if (b != NULL && fclose(b) != 0)
        okay = 0;
    return okay;
}

static int metrics_headers_equal(FILE *a, FILE *b) {
    char first[2048];
    char second[2048];
    return read_line(a, first, sizeof(first)) && read_line(b, second, sizeof(second)) &&
           strcmp(first, second) == 0 && strncmp(first, "generation\tpopulation\thash\t", 27U) == 0;
}

static int skip_metrics(FILE *file, uint32_t count) {
    char line[2048];
    for (uint32_t tick = 0U; tick < count; ++tick) {
        uint32_t actual = 0U;
        if (!read_line(file, line, sizeof(line)) || sscanf(line, "%" SCNu32, &actual) != 1 ||
            actual != tick)
            return 0;
    }
    return 1;
}

static int metric_rows_equal(FILE *a, FILE *b, uint32_t tick) {
    char first[2048];
    char second[2048];
    uint32_t actual = 0U;
    return read_line(a, first, sizeof(first)) && read_line(b, second, sizeof(second)) &&
           sscanf(first, "%" SCNu32, &actual) == 1 && actual == tick && strcmp(first, second) == 0;
}

static int metrics_streams_equal(FILE *a, FILE *b, uint32_t first_tick, uint32_t last_tick) {
    if (!metrics_headers_equal(a, b) || !skip_metrics(a, first_tick))
        return 0;
    for (uint32_t tick = first_tick; tick <= last_tick; ++tick)
        if (!metric_rows_equal(a, b, tick))
            return 0;
    return fgetc(a) == EOF && fgetc(b) == EOF && !ferror(a) && !ferror(b);
}

static int metrics_continuation(const char *whole, const char *resumed, uint32_t first_tick,
                                uint32_t last_tick) {
    char a_path[CLI_PATH_CAPACITY];
    char b_path[CLI_PATH_CAPACITY];
    CHECK(suffix_path(a_path, whole, ".tsv") && suffix_path(b_path, resumed, ".tsv"),
          "metrics paths overflowed");
    FILE *a = fopen(a_path, "rb");
    FILE *b = fopen(b_path, "rb");
    const int okay = a != NULL && b != NULL && metrics_streams_equal(a, b, first_tick, last_tick);
    return close_pair(a, b, okay);
}
static int continuation_artifacts(const char *whole, const char *resumed, const char *replayed) {
    CHECK(equal_artifact(whole, resumed, ".snapshot") &&
              equal_artifact(whole, replayed, ".snapshot"),
          "canonical continuation bundles differ");
    CHECK(metrics_continuation(whole, resumed, 7U, 16U) &&
              equal_artifact(resumed, replayed, ".tsv") &&
              equal_artifact(resumed, replayed, ".trace"),
          "continuation trace or metrics differ");
    CHECK(trace_artifact(whole, 0U, 16U) && trace_artifact(resumed, 7U, 16U),
          "native trace has invalid frames, grids or entities");
    return 1;
}

static int continuation(const cli_fixture *fixture, char *part_prefix) {
    char whole[CLI_PATH_CAPACITY];
    char resumed[CLI_PATH_CAPACITY];
    char replayed[CLI_PATH_CAPACITY];
    char snapshot[CLI_PATH_CAPACITY];
    cli_report whole_report = {0};
    cli_report other = {0};
    CHECK(join_path(whole, fixture->directory, "whole output") &&
              join_path(part_prefix, fixture->directory, "part output") &&
              join_path(resumed, fixture->directory, "resumed output") &&
              join_path(replayed, fixture->directory, "replayed output") &&
              suffix_path(snapshot, part_prefix, ".snapshot"),
          "continuation paths overflowed");
    const char *run_whole[] = {"run", "--scenario", "crowd", "--steps", "16", "--epochs",
                               "1",   "--no-merge", "--out", whole,     NULL};
    const char *run_part[] = {"run", "--scenario", "crowd", "--steps",   "7", "--epochs",
                              "1",   "--no-merge", "--out", part_prefix, NULL};
    const char *resume[] = {"resume", snapshot, "--steps", "9", "--out", resumed, NULL};
    const char *replay[] = {"replay", snapshot, "--steps", "9", "--out", replayed, NULL};
    CHECK(command(fixture, run_whole, 0) && read_report(fixture->log, &whole_report),
          "whole run failed");
    CHECK(whole_report.generation == 16U && whole_report.updates != 0U && whole_report.merges == 0U,
          "whole run did not perform collision training");
    CHECK(command(fixture, run_part, 0), "partial run failed");
    CHECK(command(fixture, resume, 0) && read_report(fixture->log, &other) &&
              reports_equal(&whole_report, &other),
          "resumed report differs from continuous execution");
    CHECK(command(fixture, replay, 0) && read_report(fixture->log, &other) &&
              reports_equal(&whole_report, &other),
          "replayed report differs from continuous execution");
    CHECK(continuation_artifacts(whole, resumed, replayed), "continuation artifacts differ");
    CHECK(command(fixture, resume, 0) && equal_artifact(whole, resumed, ".snapshot"),
          "overwriting an existing checkpoint changed the saved result");
    return 1;
}

typedef struct evaluation_paths {
    char source[CLI_PATH_CAPACITY];
    char frozen[CLI_PATH_CAPACITY];
    char repeated[CLI_PATH_CAPACITY];
    char resumed[CLI_PATH_CAPACITY];
    char backup[CLI_PATH_CAPACITY];
    char frozen_snapshot[CLI_PATH_CAPACITY];
} evaluation_paths;

static int make_evaluation_paths(const cli_fixture *fixture, const char *part,
                                 evaluation_paths *paths) {
    return suffix_path(paths->source, part, ".snapshot") &&
           join_path(paths->frozen, fixture->directory, "frozen output") &&
           join_path(paths->repeated, fixture->directory, "repeated frozen output") &&
           join_path(paths->resumed, fixture->directory, "after frozen output") &&
           join_path(paths->backup, fixture->directory, "unchanged source.snapshot") &&
           suffix_path(paths->frozen_snapshot, paths->frozen, ".snapshot");
}

static int frozen_evaluation(const cli_fixture *fixture, const char *part) {
    evaluation_paths paths;
    cli_report baseline = {0};
    cli_report evaluated = {0};
    CHECK(make_evaluation_paths(fixture, part, &paths), "evaluation paths overflowed");
    const char *zero[] = {"resume", paths.source, "--steps", "0", "--out", paths.resumed, NULL};
    const char *evaluate[] = {"evaluate", paths.source, "--steps", "2",
                              "--out",    paths.frozen, NULL};
    const char *repeat[] = {"evaluate", paths.source,   "--steps", "2",
                            "--out",    paths.repeated, NULL};
    const char *inspect[] = {"inspect", paths.source, NULL};
    const char *continue_after[] = {"resume", paths.frozen_snapshot, "--steps", "1",
                                    "--out",  paths.resumed,         NULL};
    CHECK(copy_file(paths.source, paths.backup, NULL), "could not preserve source snapshot");
    CHECK(command(fixture, zero, 0) && read_report(fixture->log, &baseline) &&
              equal_artifact(part, paths.resumed, ".snapshot"),
          "zero-step continuation changed state");
    CHECK(command(fixture, evaluate, 0) && read_report(fixture->log, &evaluated),
          "frozen evaluation failed");
    CHECK(evaluated.generation == baseline.generation + 2U &&
              evaluated.updates == baseline.updates && evaluated.merges == baseline.merges &&
              training_sections_equal(paths.source, paths.frozen_snapshot),
          "frozen evaluation changed policy, optimizer, replay or training counters");
    CHECK(trace_artifact(paths.frozen, 7U, 9U), "frozen native trace is malformed");
    CHECK(command(fixture, repeat, 0) &&
              equal_artifact(paths.frozen, paths.repeated, ".snapshot") &&
              equal_artifact(paths.frozen, paths.repeated, ".trace"),
          "frozen evaluation is not deterministic");
    CHECK(command(fixture, inspect, 0) && trace_frames(fixture->log, 7U, 7U, 0) &&
              files_equal(paths.source, paths.backup),
          "inspection changed source or omitted native grid diagnostics");
    CHECK(command(fixture, continue_after, 0) && read_report(fixture->log, &evaluated) &&
              evaluated.generation == 10U,
          "frozen evaluation snapshot cannot continue training");
    CHECK(files_equal(paths.source, paths.backup), "evaluation mutated its source snapshot");
    return 1;
}
static int output_absent(const char *prefix) {
    static const char *const suffixes[] = {".snapshot", ".snapshot.policy", ".tsv", ".trace",
                                           ".html"};
    for (size_t i = 0U; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        char path[CLI_PATH_CAPACITY];
        if (!suffix_path(path, prefix, suffixes[i]))
            return 0;
        FILE *file = fopen(path, "rb");
        if (file != NULL) {
            fclose(file);
            return 0;
        }
        if (errno != ENOENT)
            return 0;
    }
    return 1;
}

static int invalid_case(const cli_fixture *fixture, const char *prefix, const char *const *bad) {
    const char *arguments[CLI_ARGUMENT_CAPACITY] = {"run", "--out", prefix};
    size_t count = 3U;
    for (size_t j = 0U; bad[j] != NULL; ++j) {
        CHECK(count < CLI_ARGUMENT_CAPACITY - 1U, "invalid-case arguments overflowed");
        arguments[count++] = bad[j];
    }
    arguments[count] = NULL;
    CHECK(command(fixture, arguments, 2), "invalid CLI arguments were accepted");
    CHECK(output_absent(prefix), "invalid arguments created output artifacts");
    return 1;
}

static int invalid_arguments(const cli_fixture *fixture) {
    static const char *const bad[][5] = {{"--steps", "-1", NULL},
                                         {"--steps", "100001", NULL},
                                         {"--steps", "184467440737095516160", NULL},
                                         {"--steps", "2junk", NULL},
                                         {"--steps", "2", "--steps", "3", NULL},
                                         {"--steps", NULL},
                                         {"--epochs", "0", NULL},
                                         {"--epochs", "65", NULL},
                                         {"--seed", "4294967296", NULL},
                                         {"--scenario", "unknown", NULL},
                                         {"--mode", "unknown", NULL},
                                         {"--no-merge", "--no-merge", NULL},
                                         {"--viewer", "retired.html", NULL},
                                         {"--unknown", "value", NULL}};
    char prefix[CLI_PATH_CAPACITY];
    CHECK(join_path(prefix, fixture->directory, "invalid output"), "invalid path overflowed");
    for (size_t i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(invalid_case(fixture, prefix, bad[i]), "invalid option case failed");
    const char *empty[] = {NULL};
    const char *unknown[] = {"unknown", NULL};
    const char *no_snapshot[] = {"resume", NULL};
    CHECK(command(fixture, empty, 2) && command(fixture, unknown, 2) &&
              command(fixture, no_snapshot, 2),
          "invalid command was accepted");
    return 1;
}

static int write_truncated_snapshot(const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    int okay = fputs("LIFE_SNAPSHOT 2\nSHAPE 32", file) >= 0;
    if (fclose(file) != 0)
        okay = 0;
    return okay;
}

static int help_commands(const cli_fixture *fixture) {
    const char *help[] = {"--help", NULL};
    const char *short_help[] = {"-h", NULL};
    const char *extra[] = {"--help", "run", NULL};
    CHECK(command(fixture, help, 0) && command(fixture, short_help, 0) &&
              command(fixture, extra, 2),
          "help flags returned incorrect exit statuses");
    return 1;
}

static int rewrite_embedded_policy(FILE *input, FILE *output, int truncated) {
    char line[2048];
    int found = 0;
    while (read_line(input, line, sizeof(line))) {
        if (strcmp(line, "CGAI_LIFE_POLICY 2\n") == 0) {
            found = 1;
            if (fputs(truncated ? "CGAI_LIFE_POLICY 1\n42 " : "CGAI_LIFE_POLICY 9\n", output) < 0)
                return 0;
            if (truncated)
                return 1;
        } else if (fputs(line, output) < 0) {
            return 0;
        }
    }
    return found && !ferror(input);
}

static int bad_embedded_policy(const char *source, const char *destination, int truncated) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(destination, "wb");
    const int okay =
        input != NULL && output != NULL && rewrite_embedded_policy(input, output, truncated);
    return close_pair(input, output, okay);
}

static int checkpoint_is_v3(const char *path) {
    FILE *file = fopen(path, "rb");
    char line[2048];
    int okay = file != NULL && read_line(file, line, sizeof(line)) &&
               strcmp(line, "LIFE_SNAPSHOT 3\n") == 0;
    if (file != NULL && fclose(file) != 0)
        okay = 0;
    return okay;
}

static int standalone_checkpoint(const cli_fixture *fixture, const char *source) {
    char moved[CLI_PATH_CAPACITY];
    char prefix[CLI_PATH_CAPACITY];
    char sidecar[CLI_PATH_CAPACITY];
    CHECK(join_path(moved, fixture->directory, "standalone input.snapshot") &&
              join_path(prefix, fixture->directory, "standalone output") &&
              suffix_path(sidecar, moved, ".policy"),
          "standalone checkpoint paths overflowed");
    (void)remove(sidecar);
    CHECK(copy_file(source, moved, NULL) && checkpoint_is_v3(moved),
          "checkpoint writer did not produce a self-contained v2 snapshot");
    const char *resume[] = {"resume", moved, "--steps", "0", "--out", prefix, NULL};
    CHECK(command(fixture, resume, 0), "relocated checkpoint required a policy sidecar");
    char output[CLI_PATH_CAPACITY];
    CHECK(suffix_path(output, prefix, ".snapshot") && files_equal(moved, output),
          "relocated checkpoint lost embedded training state");
    return 1;
}

static int malformed_snapshots(const cli_fixture *fixture, const char *part) {
    char source[CLI_PATH_CAPACITY];
    char bad[CLI_PATH_CAPACITY];
    char prefix[CLI_PATH_CAPACITY];
    CHECK(suffix_path(source, part, ".snapshot") &&
              join_path(bad, fixture->directory, "bad input.snapshot") &&
              join_path(prefix, fixture->directory, "invalid output"),
          "malformed snapshot path overflowed");
    const char *resume[] = {"resume", bad, "--steps", "1", "--out", prefix, NULL};
    (void)remove(bad);
    CHECK(command(fixture, resume, 1) && output_absent(prefix), "missing snapshot was accepted");
    CHECK(write_truncated_snapshot(bad), "could not write truncated snapshot");
    CHECK(command(fixture, resume, 1) && output_absent(prefix), "truncated snapshot was accepted");
    CHECK(copy_file(source, bad, "unexpected trailing bytes\n"),
          "could not create snapshot with trailing data");
    CHECK(command(fixture, resume, 1) && output_absent(prefix), "trailing data was accepted");
    CHECK(bad_embedded_policy(source, bad, 0),
          "could not create unsupported embedded policy version");
    CHECK(command(fixture, resume, 1) && output_absent(prefix),
          "unsupported embedded policy version was accepted");
    CHECK(bad_embedded_policy(source, bad, 1), "could not create truncated embedded policy");
    CHECK(command(fixture, resume, 1) && output_absent(prefix),
          "truncated embedded policy was accepted");
    CHECK(standalone_checkpoint(fixture, source), "standalone checkpoint check failed");
    return 1;
}
int main(int argc, char **argv) {
    if (argc != 3) {
        fputs("usage: centroid_life_cli_tests LIFE_EXECUTABLE WORK_DIRECTORY\n", stderr);
        return 2;
    }
    cli_fixture fixture = {0};
    fixture.executable = argv[1];
    char part[CLI_PATH_CAPACITY];
    if (!make_directory(argv[2]) || !join_path(fixture.directory, argv[2], "native cli 'quoted'") ||
        !make_directory(fixture.directory) ||
        !join_path(fixture.log, fixture.directory, "command output.log")) {
        fputs("could not create native CLI check directory\n", stderr);
        return 1;
    }
    if (!continuation(&fixture, part) || !frozen_evaluation(&fixture, part) ||
        !invalid_arguments(&fixture) || !help_commands(&fixture) ||
        !malformed_snapshots(&fixture, part))
        return 1;
    puts("Native Life CLI continuation, frozen evaluation, traces and malformed input checks "
         "passed");
    return 0;
}
