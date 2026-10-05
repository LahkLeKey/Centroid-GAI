#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "internal.h"
#include <errno.h>
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
#include <time.h>
#include <unistd.h>
#endif

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                  \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define PATH_BYTES 4096u
#define SPECIAL "quoted \"value\" \\ ; & % $ ! | >"

static void pause_ms(unsigned milliseconds) {
#ifdef _WIN32
  Sleep(milliseconds);
#else
  struct timespec t = {(time_t)(milliseconds / 1000),
                       (long)(milliseconds % 1000) * 1000000L};
  while (nanosleep(&t, &t) && errno == EINTR) {
  }
#endif
}

static void binary_output(void) {
#ifdef _WIN32
  CHECK(_setmode(_fileno(stdout), _O_BINARY) != -1);
  CHECK(_setmode(_fileno(stderr), _O_BINARY) != -1);
#endif
}

static void path_join(char *out, const char *directory, const char *name) {
  int n = snprintf(out, PATH_BYTES, "%s/%s", directory, name);
  CHECK(n > 0 && (size_t)n < PATH_BYTES);
}

static void full_path(char *out, const char *path) {
#ifdef _WIN32
  CHECK(_fullpath(out, path, PATH_BYTES));
#else
  CHECK(realpath(path, out));
#endif
}

static void check_cwd(const char *expected) {
  char actual[PATH_BYTES], resolved[PATH_BYTES];
  full_path(resolved, expected);
#ifdef _WIN32
  CHECK(_getcwd(actual, sizeof(actual)) && !strcmp(actual, resolved));
#else
  CHECK(getcwd(actual, sizeof(actual)) && !strcmp(actual, resolved));
#endif
}

static unsigned pid_number(void) {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return (unsigned)getpid();
#endif
}

static int marker_child(const char *path) {
  pause_ms(500);
  FILE *file = fopen(path, "wb");
  CHECK(file);
  CHECK(fputs("descendant survived", file) >= 0 && fclose(file) == 0);
  return 0;
}

static void spawn_marker(const char *self, const char *marker) {
#ifdef _WIN32
  char line[2 * PATH_BYTES + 64];
  STARTUPINFOA startup = {0};
  PROCESS_INFORMATION child = {0};
  int n = snprintf(line, sizeof(line), "\"%s\" --marker \"%s\"", self, marker);
  CHECK(n > 0 && (size_t)n < sizeof(line));
  startup.cb = sizeof(startup);
  CHECK(CreateProcessA(self, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                       NULL, &startup, &child));
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
#else
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) {
    execl(self, self, "--marker", marker, (char *)NULL);
    _exit(127);
  }
#endif
}

static int child_mode(int argc, char **argv) {
  binary_output();
  if (!strcmp(argv[1], "--marker")) {
    CHECK(argc == 3);
    return marker_child(argv[2]);
  }
  if (!strcmp(argv[1], "--echo")) {
    CHECK(argc == 6 && !strcmp(argv[2], SPECIAL) && !*argv[3] &&
          !strcmp(argv[4], "tail\\"));
    check_cwd(argv[5]);
    CHECK(fgetc(stdin) == EOF);
    CHECK(fputs("stdin-eof\n", stdout) >= 0 && fflush(stdout) == 0);
    CHECK(fputs("stderr-marker\n", stderr) >= 0 && fflush(stderr) == 0);
    return 7;
  }
  if (!strcmp(argv[1], "--long") || !strcmp(argv[1], "--busy")) {
    unsigned char block[1024];
    memset(block, 'x', sizeof(block));
    block[0] = 0;
    block[1] = 1;
    block[2] = 27;
    size_t rounds = !strcmp(argv[1], "--busy") ? SIZE_MAX : 256;
    for (size_t i = 0; i < rounds; ++i)
      CHECK(fwrite(block, 1, sizeof(block), stdout) == sizeof(block));
    return 0;
  }
  if (!strcmp(argv[1], "--tree") || !strcmp(argv[1], "--tree-exit")) {
    CHECK(argc == 4);
    spawn_marker(argv[2], argv[3]);
    if (!strcmp(argv[1], "--tree-exit"))
      return 0;
  }
  pause_ms(10000);
  return 0;
}

static void check_echo(const char *self, const char *directory) {
  const char *argv[] = {self, "--echo", SPECIAL, "", "tail\\", directory, NULL};
  c_process_options options = {self, argv, directory, 5000, 1024};
  c_process_result r = {0};
  CHECK(c_process_run(&options, &r) == C_OK);
  CHECK(r.exit_code == 7 && !r.timed_out && !r.output_truncated);
  CHECK(r.observed_bytes == 24 && r.length == 24);
  CHECK(!memcmp(r.output, "stdin-eof\nstderr-marker\n", 24));
  c_process_dispose(&r);
}

static void check_long(const char *self) {
  const char *argv[] = {self, "--long", NULL};
  c_process_options options = {self, argv, NULL, 5000, 17};
  c_process_result r = {0};
  CHECK(c_process_run(&options, &r) == C_OK);
  CHECK(r.exit_code == 0 && !r.timed_out && r.output_truncated);
  CHECK(r.observed_bytes == 256u * 1024u && r.length == 17);
  CHECK(r.output[0] == 0 && r.output[1] == 1 && r.output[2] == 27 &&
        r.output[17] == 0);
  c_process_dispose(&r);
  options.output_limit = 0;
  CHECK(c_process_run(&options, &r) == C_OK && r.length == 0 &&
        r.output_truncated);
  CHECK(r.observed_bytes == 256u * 1024u);
  c_process_dispose(&r);
}

static void check_timeout(const char *self) {
  const char *argv[] = {self, "--busy", NULL};
  c_process_options options = {self, argv, NULL, 100, 128};
  c_process_result r = {0};
  CHECK(c_process_run(&options, &r) == C_OK);
  CHECK(r.timed_out && r.exit_code == -1 && r.elapsed_ms >= 100 &&
        r.elapsed_ms < 3000);
  CHECK(r.output_truncated && r.observed_bytes > r.length);
  c_process_dispose(&r);
}

static int file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return 0;
  CHECK(fclose(file) == 0);
  return 1;
}

static void check_tree(const char *self, const char *directory, int normal) {
  char marker[PATH_BYTES];
  path_join(marker, directory,
            normal ? "normal-child.marker" : "timeout-child.marker");
  const char *argv[] = {self, normal ? "--tree-exit" : "--tree", self, marker,
                        NULL};
  c_process_options options = {self, argv, NULL, normal ? 3000u : 100u, 1024};
  c_process_result r = {0};
  CHECK(c_process_run(&options, &r) == C_OK);
  CHECK(normal ? r.exit_code == 0 && !r.timed_out
               : r.exit_code == -1 && r.timed_out);
  c_process_dispose(&r);
  pause_ms(700);
  CHECK(!file_exists(marker));
}

static void set_path(const char *value) {
#ifdef _WIN32
  CHECK(_putenv_s("PATH", value) == 0);
#else
  CHECK(setenv("PATH", value, 1) == 0);
#endif
}

static void check_path(const char *self, const char *directory) {
  char parent[PATH_BYTES];
  memcpy(parent, self, strlen(self) + 1);
  char *separator = strrchr(parent, '/'), *backslash = strrchr(parent, '\\');
  if (!separator || (backslash && backslash > separator))
    separator = backslash;
  CHECK(separator);
  *separator = 0;
  const char *name = self + (separator - parent) + 1;
  const char *old = getenv("PATH");
  char *saved = malloc(old ? strlen(old) + 1 : 1);
  CHECK(saved);
  strcpy(saved, old ? old : "");
  set_path(parent);
  check_echo(name, directory);
  set_path(saved);
  free(saved);
}

static void check_rejection(const char *self, const char *directory) {
  const char *argv[] = {self, NULL};
  c_process_options options = {self, argv, NULL, 0, 1};
  c_process_result r = {0}, before = r;
  CHECK(c_process_run(&options, &r) == C_INVALID &&
        !memcmp(&r, &before, sizeof(r)));
  options.timeout_ms = 100;
  options.output_limit = C_MAX_FILE_BYTES + 1u;
  CHECK(c_process_run(&options, &r) == C_INVALID);
  char missing[PATH_BYTES];
  path_join(missing, directory, "no-such-program");
  options.program = missing;
  options.output_limit = 1024;
  CHECK(c_process_run(&options, &r) == C_IO && r.exit_code == -1);
  c_process_dispose(&r);
  char script[PATH_BYTES];
  path_join(script, directory, "shell-fallback.txt");
  CHECK(c_write_atomic(script, "exit 0\n", 7) == C_OK);
#ifndef _WIN32
  CHECK(chmod(script, 0700) == 0);
#endif
  const char *script_argv[] = {script, NULL};
  options.program = script;
  options.argv = script_argv;
  CHECK(c_process_run(&options, &r) == C_IO && r.exit_code == -1);
  c_process_dispose(&r);
  CHECK(remove(script) == 0);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strncmp(argv[1], "--", 2))
    return child_mode(argc, argv);
  CHECK(argc == 2);
  char directory[PATH_BYTES], build[PATH_BYTES], self[PATH_BYTES], name[128];
  full_path(build, argv[1]);
  full_path(self, argv[0]);
  int n = snprintf(name, sizeof(name), "process fixture %u", pid_number());
  CHECK(n > 0 && (size_t)n < sizeof(name));
  path_join(directory, build, name);
  CHECK(c_make_directory(directory) == C_OK);
  check_echo(self, directory);
  check_path(self, directory);
  check_long(self);
  check_timeout(self);
  check_tree(self, directory, 0);
  check_tree(self, directory, 1);
  check_rejection(self, directory);
#ifdef _WIN32
  CHECK(_rmdir(directory) == 0);
#else
  CHECK(rmdir(directory) == 0);
#endif
  puts("Native argv, PATH, null stdin, bounded bytes, timeout and child "
       "lifetime passed");
  return 0;
}
