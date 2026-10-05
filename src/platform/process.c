/** Native argv execution. No command text is interpreted by a shell. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
#endif

#define PROCESS_PATH 4096u
#define PROCESS_ARGUMENTS 64u
#define PROCESS_COMMAND 32768u
#define PROCESS_TIMEOUT 3600000u
#define PROCESS_RAW_LIMIT (64u * 1024u * 1024u)

typedef struct {
  FILE *file;
  size_t length;
  c_status status;
} raw_capture;

static c_status capture_failure(const raw_capture *raw) {
  return raw && raw->status != C_OK ? raw->status : C_IO;
}

static int valid_options(const c_process_options *o) {
  size_t total = 1;
  if (!o || !o->program || !*o->program || strlen(o->program) >= PROCESS_PATH ||
      !o->argv || !o->argv[0] || !*o->argv[0] || !o->timeout_ms ||
      o->timeout_ms > PROCESS_TIMEOUT || o->output_limit > C_MAX_FILE_BYTES ||
      (o->cwd && (!*o->cwd || strlen(o->cwd) >= PROCESS_PATH)))
    return 0;
  for (size_t i = 0; i <= PROCESS_ARGUMENTS; ++i) {
    if (!o->argv[i])
      return 1;
    size_t n = strlen(o->argv[i]);
    if (i == PROCESS_ARGUMENTS || n >= PROCESS_PATH ||
        !c_checked_add(total, 2 * n + 3, &total) || total >= PROCESS_COMMAND)
      return 0;
  }
  return 0;
}

static int retain(c_process_result *r, size_t limit, const unsigned char *bytes,
                  size_t count, raw_capture *raw) {
  size_t available = limit - r->length;
  size_t copy = count < available ? count : available;
  if (copy)
    memcpy(r->output + r->length, bytes, copy);
  r->length += copy;
  if (SIZE_MAX - r->observed_bytes < count)
    r->observed_bytes = SIZE_MAX;
  else
    r->observed_bytes += count;
  if (copy != count)
    r->output_truncated = 1;
  r->output[r->length] = 0;
  if (raw && raw->status == C_OK) {
    size_t room = PROCESS_RAW_LIMIT - raw->length;
    size_t write = count < room ? count : room;
    if (write && fwrite(bytes, 1, write, raw->file) != write)
      raw->status = C_IO;
    else {
      raw->length += write;
      if (write != count)
        raw->status = C_LIMIT;
    }
  }
  return !raw || raw->status == C_OK;
}

#ifdef _WIN32
typedef struct {
  HANDLE input, read, write, job;
  PROCESS_INFORMATION child;
} native_process;

static void close_native(native_process *p) {
  HANDLE handles[] = {p->input, p->read,          p->write,
                      p->job,   p->child.hThread, p->child.hProcess};
  for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i)
    if (handles[i] && handles[i] != INVALID_HANDLE_VALUE)
      CloseHandle(handles[i]);
}

static char *quote_fragment(char *out, const char **arg) {
  size_t slashes = 0;
  while (**arg == '\\') {
    ++slashes;
    ++*arg;
  }
  size_t copies = (**arg == '"' || !**arg) ? 2 * slashes : slashes;
  for (size_t i = 0; i < copies; ++i)
    *out++ = '\\';
  if (**arg == '"')
    *out++ = '\\';
  if (**arg)
    *out++ = *(*arg)++;
  return out;
}

static char *quote_argument(char *out, const char *arg) {
  if (*arg && !strpbrk(arg, " \t\r\n\v\f\"")) {
    size_t length = strlen(arg);
    memcpy(out, arg, length);
    out += length;
    *out++ = ' ';
    return out;
  }
  *out++ = '"';
  while (*arg)
    out = quote_fragment(out, &arg);
  *out++ = '"';
  *out++ = ' ';
  return out;
}

static char *command_line(const char *const *argv) {
  size_t size = 1;
  for (size_t i = 0; argv[i]; ++i)
    size += 2 * strlen(argv[i]) + 3;
  char *line = malloc(size), *end = line;
  if (!line)
    return NULL;
  for (size_t i = 0; argv[i]; ++i)
    end = quote_argument(end, argv[i]);
  *end = 0;
  return line;
}

static int resolve_program(const c_process_options *o,
                           char path[PROCESS_PATH]) {
  if (strchr(o->program, '/') || strchr(o->program, '\\') ||
      strchr(o->program, ':')) {
    if (o->cwd && o->program[0] != '/' && o->program[0] != '\\' &&
        !(o->program[0] && o->program[1] == ':')) {
      int n = snprintf(path, PROCESS_PATH, "%s/%s", o->cwd, o->program);
      return n > 0 && (size_t)n < PROCESS_PATH;
    }
    memcpy(path, o->program, strlen(o->program) + 1);
    return 1;
  }
  DWORD n = SearchPathA(NULL, o->program, ".exe", PROCESS_PATH, path, NULL);
  return n && n < PROCESS_PATH;
}

static int prepare_native(native_process *p) {
  SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
  p->input =
      CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (p->input == INVALID_HANDLE_VALUE ||
      !CreatePipe(&p->read, &p->write, &security, 0) ||
      !SetHandleInformation(p->read, HANDLE_FLAG_INHERIT, 0))
    return 0;
  p->job = CreateJobObjectA(NULL, NULL);
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  return p->job &&
         SetInformationJobObject(p->job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits));
}

static int start_native(native_process *p, const c_process_options *o) {
  char executable[PROCESS_PATH];
  char *line = command_line(o->argv);
  STARTUPINFOA startup = {0};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = p->input;
  startup.hStdOutput = startup.hStdError = p->write;
  int okay = line && resolve_program(o, executable) &&
             CreateProcessA(executable, line, NULL, NULL, TRUE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, o->cwd,
                            &startup, &p->child);
  free(line);
  if (!okay)
    return 0;
  if (!AssignProcessToJobObject(p->job, p->child.hProcess) ||
      ResumeThread(p->child.hThread) == (DWORD)-1) {
    TerminateProcess(p->child.hProcess, 125);
    WaitForSingleObject(p->child.hProcess, 5000);
    return 0;
  }
  CloseHandle(p->write);
  p->write = NULL;
  return 1;
}

static int drain_native(native_process *p, c_process_result *r, size_t limit,
                        raw_capture *raw) {
  unsigned char bytes[8192];
  DWORD available = 0, count = 0;
  for (unsigned round = 0; round < 32; ++round) {
    if (!PeekNamedPipe(p->read, NULL, 0, NULL, &available, NULL))
      return GetLastError() == ERROR_BROKEN_PIPE;
    if (!available)
      return 1;
    DWORD requested =
        available < sizeof(bytes) ? available : (DWORD)sizeof(bytes);
    if (!ReadFile(p->read, bytes, requested, &count, NULL) || !count)
      return 0;
    if (!retain(r, limit, bytes, count, raw))
      return 0;
  }
  return 1;
}

static c_status execute(const c_process_options *o, c_process_result *r,
                        raw_capture *raw) {
  native_process p = {0};
  c_status status = C_IO;
  uint64_t started = c_monotonic_ms();
  if (!prepare_native(&p) || !start_native(&p, o)) {
    close_native(&p);
    return status;
  }
  for (;;) {
    if (!drain_native(&p, r, o->output_limit, raw)) {
      status = capture_failure(raw);
      break;
    }
    DWORD waited = WaitForSingleObject(p.child.hProcess, 0);
    if (waited == WAIT_OBJECT_0) {
      DWORD code = 0;
      if (GetExitCodeProcess(p.child.hProcess, &code) && code <= INT_MAX) {
        r->exit_code = (int)code;
        status = C_OK;
      }
      break;
    }
    if (waited == WAIT_FAILED)
      break;
    if (c_monotonic_ms() - started >= o->timeout_ms) {
      r->timed_out = 1;
      status = C_OK;
      break;
    }
    Sleep(5);
  }
  TerminateJobObject(p.job, 125);
  WaitForSingleObject(p.child.hProcess, 5000);
  if (!drain_native(&p, r, o->output_limit, raw))
    status = capture_failure(raw);
  r->elapsed_ms = c_monotonic_ms() - started;
  close_native(&p);
  return status;
}
#else
typedef struct {
  pid_t pid;
  int read, launch_read;
} native_process;

static int configure_pipe(int descriptors[2], int nonblocking) {
  if (pipe(descriptors))
    return 0;
  if (fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) == -1 ||
      fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) == -1 ||
      (nonblocking && fcntl(descriptors[0], F_SETFL, O_NONBLOCK) == -1)) {
    close(descriptors[0]);
    close(descriptors[1]);
    return 0;
  }
  return 1;
}

static void execute_path(const char *program, char *const *argv) {
  if (strchr(program, '/')) {
    execve(program, argv, environ);
    return;
  }
  const char *path = getenv("PATH");
  if (!path)
    path = "/usr/bin:/bin";
  int last = ENOENT;
  for (;;) {
    const char *separator = strchr(path, ':');
    size_t length = separator ? (size_t)(separator - path) : strlen(path);
    char candidate[PROCESS_PATH];
    if (length + strlen(program) + 2 < sizeof(candidate)) {
      if (length)
        snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)length, path,
                 program);
      else
        snprintf(candidate, sizeof(candidate), "./%s", program);
      execve(candidate, argv, environ);
      if (errno != ENOENT && errno != ENOTDIR)
        last = errno;
      if (errno == ENOEXEC)
        break;
    } else
      last = ENAMETOOLONG;
    if (!separator)
      break;
    path = separator + 1;
  }
  errno = last;
}

static _Noreturn void child_execute(const c_process_options *o, int output[2],
                                    int launch[2]) {
  close(output[0]);
  close(launch[0]);
  int input = open("/dev/null", O_RDONLY);
  if (setpgid(0, 0) || input < 0 || (o->cwd && chdir(o->cwd)) ||
      dup2(input, STDIN_FILENO) < 0 || dup2(output[1], STDOUT_FILENO) < 0 ||
      dup2(output[1], STDERR_FILENO) < 0)
    goto failed;
  if (input > STDERR_FILENO)
    close(input);
  if (output[1] > STDERR_FILENO)
    close(output[1]);
  execute_path(o->program, (char *const *)o->argv);
failed:;
  int error = errno;
  size_t sent = 0;
  while (sent < sizeof(error)) {
    ssize_t written = write(launch[1], (const unsigned char *)&error + sent,
                            sizeof(error) - sent);
    if (written > 0)
      sent += (size_t)written;
    else if (written < 0 && errno == EINTR)
      continue;
    else
      break;
  }
  _exit(127);
}

static int start_native(native_process *p, const c_process_options *o) {
  int output[2], launch[2];
  if (!configure_pipe(output, 1))
    return 0;
  if (!configure_pipe(launch, 0)) {
    close(output[0]);
    close(output[1]);
    return 0;
  }
  pid_t child = fork();
  if (!child)
    child_execute(o, output, launch);
  close(output[1]);
  close(launch[1]);
  if (child < 0) {
    close(output[0]);
    close(launch[0]);
    return 0;
  }
  (void)setpgid(child, child);
  p->pid = child;
  p->read = output[0];
  p->launch_read = launch[0];
  return 1;
}

static int drain_native(native_process *p, c_process_result *r, size_t limit,
                        raw_capture *raw) {
  unsigned char bytes[8192];
  for (unsigned round = 0; round < 32; ++round) {
    ssize_t count = read(p->read, bytes, sizeof(bytes));
    if (count > 0) {
      if (!retain(r, limit, bytes, (size_t)count, raw))
        return 0;
      continue;
    }
    if (!count || errno == EAGAIN || errno == EWOULDBLOCK)
      return 1;
    if (errno != EINTR)
      return 0;
  }
  return 1;
}

static c_status execute(const c_process_options *o, c_process_result *r,
                        raw_capture *raw) {
  native_process p = {0, -1, -1};
  uint64_t started = c_monotonic_ms();
  int wait_status = 0, finished = 0;
  c_status status = C_OK;
  if (!start_native(&p, o))
    return C_IO;
  while (!finished) {
    if (!drain_native(&p, r, o->output_limit, raw)) {
      status = capture_failure(raw);
      break;
    }
    pid_t waited = waitpid(p.pid, &wait_status, WNOHANG);
    if (waited == p.pid) {
      finished = 1;
      break;
    }
    if (waited < 0 && errno != EINTR) {
      status = C_IO;
      break;
    }
    if (c_monotonic_ms() - started >= o->timeout_ms) {
      r->timed_out = 1;
      break;
    }
    struct timespec pause = {0, 5000000L};
    (void)nanosleep(&pause, NULL);
  }
  (void)kill(-p.pid, SIGKILL);
  if (!finished) {
    pid_t waited;
    do {
      waited = waitpid(p.pid, &wait_status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != p.pid)
      status = C_IO;
  }
  if (!r->timed_out && WIFEXITED(wait_status))
    r->exit_code = WEXITSTATUS(wait_status);
  if (!drain_native(&p, r, o->output_limit, raw))
    status = capture_failure(raw);
  int launch_error = 0;
  if (read(p.launch_read, &launch_error, sizeof(launch_error)) > 0) {
    status = C_IO;
    r->exit_code = -1;
  }
  r->elapsed_ms = c_monotonic_ms() - started;
  close(p.read);
  close(p.launch_read);
  return status;
}
#endif

static c_status process_run(const c_process_options *options,
                            c_process_result *result, raw_capture *raw) {
  if (!result || !valid_options(options))
    return C_INVALID;
  c_process_result local = {0};
  local.exit_code = -1;
  local.output = malloc(options->output_limit + 1);
  if (!local.output)
    return C_NOMEM;
  local.output[0] = 0;
  c_status status = execute(options, &local, raw);
  *result = local;
  return status;
}

c_status c_process_run(const c_process_options *options,
                       c_process_result *result) {
  return process_run(options, result, NULL);
}

c_status c_process_capture_file(const c_process_options *options,
                                const char *path, c_process_result *result) {
  if (!result || !valid_options(options) || !path || !*path ||
      strlen(path) >= PROCESS_PATH)
    return C_INVALID;
  raw_capture raw = {0};
  raw.file = fopen(path, "wbx");
  if (!raw.file)
    return C_IO;
  c_status status = process_run(options, result, &raw);
  if (fclose(raw.file))
    status = C_IO;
  return status;
}

void c_process_dispose(c_process_result *result) {
  if (!result)
    return;
  free(result->output);
  memset(result, 0, sizeof(*result));
}
