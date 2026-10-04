/** @file process.c @brief Shell-free native launch, bounded lifetime and path handling. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "process.h"
#include <errno.h>
#include <limits.h>
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

typedef struct process_request {
    const char *exe;
    const char *const *arguments;
    size_t count;
    const char *log_path;
    uint32_t timeout;
} process_request;

static int bounded_length(const char *text, size_t *length) {
    if (text == NULL)
        return 0;
    for (size_t i = 0U; i <= EVOLVE_PROCESS_MAX_PATH_BYTES; ++i) {
        if (text[i] == '\0') {
            *length = i;
            return 1;
        }
    }
    return 0;
}

static int valid_path(const char *path) {
    size_t length;
    return bounded_length(path, &length) && length != 0U;
}

static int count_arguments(const char *const *arguments, size_t *count) {
    *count = 0U;
    if (arguments == NULL)
        return 1;
    for (size_t i = 0U; i <= EVOLVE_PROCESS_MAX_ARGUMENTS; ++i) {
        size_t length;
        if (arguments[i] == NULL) {
            *count = i;
            return 1;
        }
        if (i == EVOLVE_PROCESS_MAX_ARGUMENTS || !bounded_length(arguments[i], &length))
            return 0;
    }
    return 0;
}

static int trailing_separator(char value) {
#ifdef _WIN32
    return value == '/' || value == '\\';
#else
    return value == '/';
#endif
}

int evolve_path_join(char *output, size_t capacity, const char *directory, const char *name) {
    char joined[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    size_t dir_length, name_length;
    if (output == NULL || !bounded_length(directory, &dir_length) || dir_length == 0U ||
        !bounded_length(name, &name_length) || name_length == 0U)
        return 0;
    const size_t separator = trailing_separator(directory[dir_length - 1U]) ? 0U : 1U;
    const size_t length = dir_length + separator + name_length;
    if (length > EVOLVE_PROCESS_MAX_PATH_BYTES || length >= capacity)
        return 0;
    memcpy(joined, directory, dir_length);
    if (separator != 0U)
#ifdef _WIN32
        joined[dir_length] = '\\';
#else
        joined[dir_length] = '/';
#endif
    memcpy(joined + dir_length + separator, name, name_length + 1U);
    memmove(output, joined, length + 1U);
    return 1;
}

int evolve_create_directory(const char *path) {
    if (!valid_path(path))
        return 0;
#ifdef _WIN32
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0700) == 0;
#endif
}

int evolve_make_directory(const char *path) {
    if (!valid_path(path))
        return 0;
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

int evolve_absolute_path(const char *input, char *output, size_t capacity) {
    if (!valid_path(input) || output == NULL || capacity == 0U)
        return 0;
#ifdef _WIN32
    char resolved[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    if (GetFileAttributesA(input) == INVALID_FILE_ATTRIBUTES)
        return 0;
    const DWORD length = GetFullPathNameA(input, (DWORD)sizeof(resolved), resolved, NULL);
    if (length == 0U || length > EVOLVE_PROCESS_MAX_PATH_BYTES || (size_t)length >= capacity)
        return 0;
    memmove(output, resolved, (size_t)length + 1U);
    return 1;
#else
    char *resolved = realpath(input, NULL);
    if (resolved == NULL)
        return 0;
    const size_t length = strlen(resolved);
    const int valid = length <= EVOLVE_PROCESS_MAX_PATH_BYTES && length < capacity;
    if (valid)
        memmove(output, resolved, length + 1U);
    free(resolved);
    return valid;
#endif
}

#ifdef _WIN32
typedef struct native_child {
    HANDLE log;
    HANDLE input;
    HANDLE job;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
} native_child;

static void close_handle(HANDLE handle) {
    if (handle != NULL && handle != INVALID_HANDLE_VALUE)
        (void)CloseHandle(handle);
}

static void close_child(native_child *child) {
    close_handle(child->job);
    close_handle(child->process.hThread);
    close_handle(child->process.hProcess);
    close_handle(child->log);
    close_handle(child->input);
}

/** Encode runs of backslashes according to the Windows C runtime argv rules. */
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

static size_t command_capacity(const process_request *request) {
    size_t capacity = strlen(request->exe) * 2U + 4U;
    for (size_t i = 0U; i < request->count; ++i)
        capacity += strlen(request->arguments[i]) * 2U + 3U;
    return capacity;
}

static char *command_line(const process_request *request) {
    char *line = malloc(command_capacity(request));
    if (line == NULL)
        return NULL;
    char *end = quote_argument(line, request->exe);
    for (size_t i = 0U; i < request->count; ++i) {
        *end++ = ' ';
        end = quote_argument(end, request->arguments[i]);
    }
    *end = '\0';
    if ((size_t)(end - line) >= 32767U) {
        free(line);
        return NULL;
    }
    return line;
}

static int prepare_redirection(native_child *child, const char *log_path) {
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

static int prepare_job(native_child *child) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    child->job = CreateJobObjectA(NULL, NULL);
    return child->job != NULL &&
           SetInformationJobObject(child->job, JobObjectExtendedLimitInformation, &limits,
                                   (DWORD)sizeof(limits));
}

static void stop_child(native_child *child) {
    (void)TerminateProcess(child->process.hProcess, 1U);
    (void)WaitForSingleObject(child->process.hProcess, 5000U);
}

static int create_suspended(native_child *child, const process_request *request, char *line) {
    DWORD previous;
    const DWORD quiet = SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
    if (!SetThreadErrorMode(quiet, &previous))
        return 0;
    const BOOL started =
        CreateProcessA(request->exe, line, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                       NULL, NULL, &child->startup, &child->process);
    const BOOL restored = SetThreadErrorMode(previous, NULL);
    if (started && !restored)
        stop_child(child);
    return started && restored;
}

static int start_child(native_child *child, const process_request *request, char *line) {
    if (!create_suspended(child, request, line))
        return 0;
    if (AssignProcessToJobObject(child->job, child->process.hProcess) &&
        ResumeThread(child->process.hThread) != (DWORD)-1)
        return 1;
    stop_child(child);
    return 0;
}

static int await_child(native_child *child, uint32_t timeout, int *exit_code) {
    DWORD result = 0U;
    if (WaitForSingleObject(child->process.hProcess, timeout * 1000U) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(child->process.hProcess, &result) && result <= (DWORD)INT_MAX) {
        *exit_code = (int)result;
        return 1;
    }
    (void)TerminateJobObject(child->job, 1U);
    (void)TerminateProcess(child->process.hProcess, 1U);
    (void)WaitForSingleObject(child->process.hProcess, 5000U);
    return 0;
}

static int run_native(const process_request *request, int *exit_code) {
    native_child child = {0};
    char *line = command_line(request);
    if (line == NULL)
        return 0;
    const int valid = prepare_redirection(&child, request->log_path) && prepare_job(&child) &&
                      start_child(&child, request, line) &&
                      await_child(&child, request->timeout, exit_code);
    close_child(&child);
    free(line);
    return valid;
}
#else
typedef struct native_child {
    pid_t pid;
    int errors;
    struct timespec started;
} native_child;

static int move_descriptor(int descriptor) {
    if (descriptor < 0 || descriptor > STDERR_FILENO)
        return descriptor;
    const int moved = fcntl(descriptor, F_DUPFD, STDERR_FILENO + 1);
    (void)close(descriptor);
    return moved;
}

static int prepare_error_pipe(int descriptors[2]) {
    if (pipe(descriptors) != 0)
        return 0;
    descriptors[0] = move_descriptor(descriptors[0]);
    descriptors[1] = move_descriptor(descriptors[1]);
    if (descriptors[0] >= 0 && descriptors[1] >= 0 &&
        fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) == 0 &&
        fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) == 0 &&
        fcntl(descriptors[0], F_SETFL, O_NONBLOCK) == 0)
        return 1;
    if (descriptors[0] >= 0)
        (void)close(descriptors[0]);
    if (descriptors[1] >= 0)
        (void)close(descriptors[1]);
    return 0;
}

static _Noreturn void child_failure(int errors) {
    const int failure = errno != 0 ? errno : EIO;
    ssize_t written;
    do {
        written = write(errors, &failure, sizeof(failure));
    } while (written < 0 && errno == EINTR);
    _exit(127);
}

static int redirect_child(const char *log_path) {
    const int log = move_descriptor(open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600));
    const int input = move_descriptor(open("/dev/null", O_RDONLY));
    const int valid = log >= 0 && input >= 0 && dup2(log, STDOUT_FILENO) >= 0 &&
                      dup2(log, STDERR_FILENO) >= 0 && dup2(input, STDIN_FILENO) >= 0;
    if (log >= 0)
        (void)close(log);
    if (input >= 0)
        (void)close(input);
    return valid;
}

static int search_path(char *output, const char *directory, size_t length, const char *exe) {
    const size_t name_length = strlen(exe);
    const size_t separator = length == 0U ? 0U : 1U;
    if (length > EVOLVE_PROCESS_MAX_PATH_BYTES ||
        length + separator + name_length > EVOLVE_PROCESS_MAX_PATH_BYTES)
        return 0;
    memcpy(output, directory, length);
    if (separator != 0U)
        output[length] = '/';
    memcpy(output + length + separator, exe, name_length + 1U);
    return 1;
}

static int try_path(const char *exe, char *const *arguments, const char *directory, size_t length,
                    int *denied) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    if (!search_path(path, directory, length, exe))
        return 1;
    execv(path, arguments);
    if (errno == EACCES) {
        *denied = 1;
        return 1;
    }
    return errno == ENOENT || errno == ENOTDIR || errno == ENAMETOOLONG;
}

/** Native PATH lookup deliberately omits execvp's ENOEXEC shell fallback. */
static void execute_search(const char *exe, char *const *arguments) {
    const char *directory = getenv("PATH");
    int denied = 0;
    if (directory == NULL)
        directory = "/bin:/usr/bin";
    for (;;) {
        const char *end = strchr(directory, ':');
        const size_t length = end != NULL ? (size_t)(end - directory) : strlen(directory);
        if (!try_path(exe, arguments, directory, length, &denied))
            return;
        if (end == NULL)
            break;
        directory = end + 1U;
    }
    errno = denied ? EACCES : ENOENT;
}

static void execute_program(const char *exe, char *const *arguments) {
    if (strchr(exe, '/') != NULL)
        execv(exe, arguments);
    else
        execute_search(exe, arguments);
}

static _Noreturn void execute_child(const process_request *request, int errors) {
    char *arguments[EVOLVE_PROCESS_MAX_ARGUMENTS + 2U];
    arguments[0] = (char *)request->exe;
    for (size_t i = 0U; i < request->count; ++i)
        arguments[i + 1U] = (char *)request->arguments[i];
    arguments[request->count + 1U] = NULL;
    if (setpgid(0, 0) != 0 || !redirect_child(request->log_path))
        child_failure(errors);
    execute_program(request->exe, arguments);
    child_failure(errors);
}

static int expired(const native_child *child, uint32_t timeout) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 1;
    const time_t seconds = now.tv_sec - child->started.tv_sec;
    return seconds > (time_t)timeout ||
           (seconds == (time_t)timeout && now.tv_nsec >= child->started.tv_nsec);
}

static void kill_group(pid_t child) { (void)kill(-child, SIGKILL); }

static int terminate_child(pid_t child) {
    int result;
    pid_t waited;
    kill_group(child);
    (void)kill(child, SIGKILL);
    do {
        waited = waitpid(child, &result, 0);
    } while (waited < 0 && errno == EINTR);
    return 0;
}

static int record_exit(const native_child *child, int result, int *exit_code) {
    int failure;
    ssize_t received;
    do {
        received = read(child->errors, &failure, sizeof(failure));
    } while (received < 0 && errno == EINTR);
    if (received != 0 || !WIFEXITED(result))
        return 0;
    *exit_code = WEXITSTATUS(result);
    return 1;
}

static int await_child(const native_child *child, uint32_t timeout, int *exit_code) {
    const struct timespec pause = {0, 10000000L};
    int result;
    for (;;) {
        const pid_t waited = waitpid(child->pid, &result, WNOHANG);
        if (waited == child->pid) {
            kill_group(child->pid);
            return record_exit(child, result, exit_code);
        }
        if (waited < 0 && errno != EINTR)
            return terminate_child(child->pid);
        if (expired(child, timeout))
            return terminate_child(child->pid);
        (void)nanosleep(&pause, NULL);
    }
}

static pid_t fork_child(const process_request *request, const int descriptors[2]) {
    const pid_t child = fork();
    if (child == 0) {
        (void)close(descriptors[0]);
        execute_child(request, descriptors[1]);
    }
    return child;
}

static int run_native(const process_request *request, int *exit_code) {
    int descriptors[2];
    native_child child;
    if (clock_gettime(CLOCK_MONOTONIC, &child.started) != 0 || !prepare_error_pipe(descriptors))
        return 0;
    child.pid = fork_child(request, descriptors);
    (void)close(descriptors[1]);
    child.errors = descriptors[0];
    if (child.pid < 0) {
        (void)close(child.errors);
        return 0;
    }
    (void)setpgid(child.pid, child.pid);
    const int valid = await_child(&child, request->timeout, exit_code);
    (void)close(child.errors);
    return valid;
}
#endif

int evolve_process_run(const char *exe, const char *const *arguments, const char *log_path,
                       uint32_t timeout_seconds, int *exit_code) {
    process_request request = {exe, arguments, 0U, log_path, timeout_seconds};
    if (!valid_path(exe) || !valid_path(log_path) || exit_code == NULL || timeout_seconds == 0U ||
        timeout_seconds > EVOLVE_PROCESS_MAX_TIMEOUT_SECONDS ||
        !count_arguments(arguments, &request.count))
        return 0;
    return run_native(&request, exit_code);
}
