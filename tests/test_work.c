#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "context/context_internal.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "work:%d: %s\n", __LINE__, #x);                          \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define PATH_BYTES 4096u
#define SPECIAL "literal \"quote\" \\ ; $ & !"

static void full_path(char *out, const char *path) {
#ifdef _WIN32
  CHECK(_fullpath(out, path, PATH_BYTES));
#else
  CHECK(realpath(path, out));
#endif
}
static unsigned pid_number(void) {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return (unsigned)getpid();
#endif
}
static void join(char *out, const char *directory, const char *name) {
  int n = snprintf(out, PATH_BYTES, "%s/%s", directory, name);
  CHECK(n > 0 && (size_t)n < PATH_BYTES);
}
static int child(int argc, char **argv) {
  if (!strcmp(argv[1], "--remove-directory")) {
    CHECK(argc == 3);
#ifdef _WIN32
    CHECK(_rmdir(argv[2]) == 0);
    CHECK(_setmode(_fileno(stdout), _O_BINARY) != -1);
#else
    CHECK(rmdir(argv[2]) == 0);
#endif
    static const unsigned char bytes[] = {'r', 'a', 'w', 0, 0xff, '\n'};
    CHECK(fwrite(bytes, 1, sizeof(bytes), stdout) == sizeof(bytes));
    return 0;
  }
  if (!strcmp(argv[1], "--marker")) {
    CHECK(argc == 3);
    FILE *file = fopen(argv[2], "wb");
    CHECK(file && fputs("child executed", file) >= 0 && fclose(file) == 0);
    return 0;
  }
  CHECK(argc == 3 && !strcmp(argv[2], SPECIAL));
#ifdef _WIN32
  CHECK(_setmode(_fileno(stdout), _O_BINARY) != -1);
#endif
  CHECK(fgetc(stdin) == EOF);
  unsigned char bytes[4096];
  memset(bytes, 'w', sizeof(bytes));
  bytes[0] = 0;
  bytes[1] = 1;
  bytes[2] = 27;
  size_t count = !strcmp(argv[1], "--overflow") ? SIZE_MAX : 300;
  for (size_t i = 0; i < count; ++i)
    CHECK(fwrite(bytes, 1, sizeof(bytes), stdout) == sizeof(bytes));
  return 7;
}
static c_record activity(const c_context *context, size_t index) {
  c_record record;
  CHECK(c_context_record(context, index, &record) == C_OK);
  CHECK(record.kind == C_ACTIVITY && record.split == C_TRAIN);
  return record;
}
static char *record_text(const c_record *record) {
  char *text = malloc(record->length + 1);
  CHECK(text);
  memcpy(text, record->bytes, record->length);
  text[record->length] = 0;
  return text;
}
static void captured(c_context *context, const char *self,
                     const char *directory) {
  char raw[PATH_BYTES];
  join(raw, directory, "complete.raw");
  const char *argv[] = {self, "--output", SPECIAL, NULL};
  c_process_options o = {self, argv, directory, 10000, 17};
  c_process_result r = {0};
  uint64_t id = 0;
  CHECK(c_context_capture_run(context, &o, "native test activity", raw, &id,
                              &r) == C_OK);
  CHECK(r.exit_code == 7 && !r.timed_out && r.output_truncated &&
        r.length == 17 && r.observed_bytes == 300u * 4096u);
  unsigned char *bytes = NULL;
  size_t n = 0;
  CHECK(c_read_file(raw, &bytes, &n) == C_OK && n == r.observed_bytes);
  CHECK(!memcmp(bytes, r.output, r.length));
  char digest[65];
  c_hash(bytes, n, digest);
  free(bytes);
  c_record view = activity(context, 0);
  CHECK(view.id == id);
  char *text = record_text(&view);
  CHECK(strstr(text, "exit-code=7\n") &&
        strstr(text, "raw-output-complete=1\n"));
  CHECK(strstr(text,
               "observed-output-bytes=1228800\nraw-output-bytes=1228800\n"));
  CHECK(strstr(text, "view-output-bytes=17\nview-omitted-bytes=1228783\n"));
  CHECK(strstr(text, digest) && strstr(text, "argv-2-bytes="));
  CHECK(strstr(text, "literal \"quote\" \\x5c ; $ & !") &&
        strstr(text, "\\x00\\x01\\x1b"));
  free(text);
  c_process_dispose(&r);
  size_t before = c_context_count(context);
  CHECK(c_context_capture_run(context, &o, "native test activity", raw, &id,
                              &r) == C_IO);
  CHECK(!r.output && c_context_count(context) == before);
  CHECK(c_read_file(raw, &bytes, &n) == C_OK);
  char unchanged[65];
  c_hash(bytes, n, unchanged);
  free(bytes);
  CHECK(!strcmp(digest, unchanged));
  CHECK(remove(raw) == 0);
}
static void overflow(c_context *context, const char *self,
                     const char *directory) {
  char raw[PATH_BYTES];
  join(raw, directory, "limit.raw");
  const char *argv[] = {self, "--overflow", SPECIAL, NULL};
  c_process_options o = {self, argv, NULL, 30000, 17};
  c_process_result r = {0};
  uint64_t id = 0;
  CHECK(c_context_capture_run(context, &o, "bounded raw capacity test", raw,
                              &id, &r) == C_LIMIT);
  CHECK(!r.timed_out && r.exit_code == -1 &&
        r.observed_bytes > 64u * 1024u * 1024u);
  c_record view = activity(context, 1);
  char *text = record_text(&view);
  CHECK(strstr(text, "process-status=2\n") &&
        strstr(text, "raw-output-complete=0\n"));
  CHECK(strstr(text, "raw-output-bytes=67108864\n"));
  free(text);
  c_process_dispose(&r);
  CHECK(remove(raw) == 0);
}

static void absent(const char *path) {
#ifdef _WIN32
  CHECK(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES);
#else
  FILE *file = fopen(path, "rb");
  if (file) {
    (void)fclose(file);
    CHECK(0);
  }
#endif
}

static void remove_directory(const char *path) {
#ifdef _WIN32
  CHECK(_rmdir(path) == 0);
#else
  CHECK(rmdir(path) == 0);
#endif
}

static void reject_capture(c_context *context, const char *self,
                           const char *cwd, const char *raw,
                           const char *marker) {
  const char *argv[] = {self, "--marker", marker, NULL};
  c_process_options options = {self, argv, cwd, 10000, 128};
  unsigned char borrowed[] = "preserved";
  c_process_result result = {42, 0, 0, borrowed, sizeof(borrowed), 21, 37};
  unsigned char before[sizeof(result)];
  memcpy(before, &result, sizeof(result));
  uint64_t id = 999;
  size_t count = c_context_count(context);
  CHECK(c_context_capture_run(context, &options, "rejected preflight", raw, &id,
                              &result) == C_INVALID);
  CHECK(id == 999 && c_context_count(context) == count &&
        !memcmp(before, &result, sizeof(result)) &&
        !strcmp((const char *)result.output, "preserved"));
  absent(marker);
  absent(raw);
}

static int directory_link(const char *path, const char *target) {
#ifdef _WIN32
  return CreateSymbolicLinkA(path, target, SYMBOLIC_LINK_FLAG_DIRECTORY | 2u) !=
         0;
#else
  CHECK(symlink(target, path) == 0);
  return 1;
#endif
}

static void preflight(const char *self, const char *directory) {
  static const char *const folders[] = {
      "data",  "data/audit",     "data/train", "data/development",
      "tests", "tests/fixtures", "research"};
  static const char *const reserved[] = {"data/audit", "data/development",
                                         "tests/fixtures",
                                         "data/train/../audit"};
  char path[PATH_BYTES], raw[PATH_BYTES], marker[PATH_BYTES], cwd[PATH_BYTES];
  c_context *context = NULL;
  CHECK(c_context_create(&context) == C_OK);
  for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); ++i) {
    join(path, directory, folders[i]);
    CHECK(c_make_directory(path) == C_OK);
  }
  join(marker, directory, "preflight-child.marker");
  for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
    join(cwd, directory, reserved[i]);
    join(raw, cwd, "reserved.raw");
    reject_capture(context, self, directory, raw, marker);
    join(raw, directory, "ordinary.raw");
    reject_capture(context, self, cwd, raw, marker);
  }
#ifdef _WIN32
  char short_directory[PATH_BYTES];
  join(cwd, directory, "data/development");
  DWORD short_length = GetShortPathNameA(cwd, short_directory, PATH_BYTES);
  if (short_length && short_length < PATH_BYTES) {
    const char *last = strrchr(short_directory, '\\');
    if (!last)
      last = strrchr(short_directory, '/');
    if (last && _stricmp(last + 1, "development")) {
      join(raw, short_directory, "reserved.raw");
      reject_capture(context, self, directory, raw, marker);
      join(raw, directory, "ordinary.raw");
      reject_capture(context, self, short_directory, raw, marker);
    }
  }
  static const char *const nonordinary[] = {
      "ads.raw:stream", "wild*.raw", "wild?.raw", "trailing.",
      "trailing ",      "CON",       "NUL.txt",   "COM1.log"};
  for (size_t i = 0; i < sizeof(nonordinary) / sizeof(nonordinary[0]); ++i) {
    join(raw, directory, nonordinary[i]);
    reject_capture(context, self, directory, raw, marker);
  }
#endif

  join(path, directory, "non-directory");
  CHECK(c_write_atomic(path, "regular", 7) == C_OK);
  join(raw, directory, "ordinary.raw");
  reject_capture(context, self, path, raw, marker);
  CHECK(remove(path) == 0);

  join(path, directory, "linked");
  join(cwd, directory, "research");
  if (directory_link(path, cwd)) {
    join(raw, path, "linked.raw");
    reject_capture(context, self, directory, raw, marker);
    join(raw, directory, "ordinary.raw");
    reject_capture(context, self, path, raw, marker);
#ifdef _WIN32
    remove_directory(path);
#else
    CHECK(remove(path) == 0);
#endif
  }

  /* Research is excluded from scanning, but ordinary attributed observations
   * there are allowed. Only declared physical held-out locations are barred.
   */
  join(raw, cwd, "ordinary.raw");
  const char *argv[] = {self, "--marker", marker, NULL};
  c_process_options options = {self, argv, cwd, 10000, 128};
  c_process_result result = {0};
  uint64_t id = 0;
  CHECK(c_context_capture_run(context, &options, "ordinary research work", raw,
                              &id, &result) == C_OK &&
        result.exit_code == 0 && id == 1 && c_context_count(context) == 1);
  c_process_dispose(&result);
  CHECK(remove(marker) == 0 && remove(raw) == 0);

  for (size_t i = sizeof(folders) / sizeof(folders[0]); i > 0; --i) {
    join(path, directory, folders[i - 1]);
    remove_directory(path);
  }
  c_context_destroy(context);
}

static void canonical_output(const char *self, const char *directory) {
  static const unsigned char expected[] = {'r', 'a', 'w', 0, 0xff, '\n'};
  char staging[PATH_BYTES], lexical[PATH_BYTES];
  char canonical[C_CONTEXT_PROVENANCE_BYTES + 1u];
  char raw[PATH_BYTES];
  join(staging, directory, "removed-staging");
  CHECK(c_make_directory(staging) == C_OK);
  join(lexical, staging, "../canonical.raw");
  join(raw, directory, "canonical.raw");
  const char *argv[] = {self, "--remove-directory", staging, NULL};
  c_process_options options = {self, argv, directory, 10000, 128};
  c_process_result result = {0};
  c_context *context = NULL;
  uint64_t id = 0;
  CHECK(c_context_create(&context) == C_OK);
  CHECK(c_context_capture_run(context, &options, "canonical capture test",
                              lexical, &id, &result) == C_OK &&
        result.exit_code == 0 && !result.timed_out &&
        !result.output_truncated && result.length == sizeof(expected) &&
        result.observed_bytes == sizeof(expected) &&
        !memcmp(result.output, expected, sizeof(expected)));
  CHECK(c_context_absolute(raw, canonical) == C_OK);
  c_record record = activity(context, 0);
  CHECK(record.id == id && !strcmp(record.path, canonical));
  char *text = record_text(&record);
  CHECK(strstr(text, canonical) && strstr(text, "raw-output-complete=1\n") &&
        strstr(text, "raw\\x00\\xff\\x0a"));
  free(text);
  unsigned char *bytes = NULL;
  size_t length = 0;
  CHECK(c_read_file(canonical, &bytes, &length) == C_OK &&
        length == sizeof(expected) && !memcmp(bytes, expected, length));
  free(bytes);
  c_process_dispose(&result);
  c_context_destroy(context);
  CHECK(remove(canonical) == 0);
}

static int contains(const unsigned char *bytes, size_t length,
                    const char *text) {
  size_t size = strlen(text);
  if (size > length)
    return 0;
  for (size_t i = 0; i <= length - size; ++i)
    if (!memcmp(bytes + i, text, size))
      return 1;
  return 0;
}

static void exact_text_field(const char *receipt, const char *name,
                             const char *value) {
  static const char hexadecimal[] = "0123456789abcdef";
  const size_t length = strlen(value);
  char digest[C_DIGEST_HEX], header[192];
  c_hash(value, length, digest);
  int count = snprintf(header, sizeof(header), "%s-bytes=%zu sha256=%s\n", name,
                       length, digest);
  CHECK(count > 0 && (size_t)count < sizeof(header));
  size_t capacity;
  CHECK(c_checked_mul(length, 4, &capacity) &&
        c_checked_add(capacity, (size_t)count + 2, &capacity));
  char *field = malloc(capacity);
  CHECK(field);
  memcpy(field, header, (size_t)count);
  size_t used = (size_t)count;
  for (size_t i = 0; i < length; ++i) {
    unsigned char byte = (unsigned char)value[i];
    if (byte < 32 || byte >= 127 || byte == '\\') {
      field[used++] = '\\';
      field[used++] = 'x';
      field[used++] = hexadecimal[byte >> 4];
      field[used++] = hexadecimal[byte & 15u];
    } else
      field[used++] = (char)byte;
  }
  field[used++] = '\n';
  field[used] = '\0';
  CHECK(strstr(receipt, field));
  free(field);
}

static void unchanged_training(const c_trainer *before,
                               const c_trainer *after) {
  c_trainer *copy = malloc(sizeof(*copy));
  CHECK(copy);
  memcpy(copy, before, sizeof(*copy));
  copy->model = after->model;
  copy->context = after->context;
  CHECK(!memcmp(copy, after, sizeof(*copy)) &&
        !memcmp(before->model, after->model, sizeof(*before->model)));
  free(copy);
}

/* An admitted failure receipt must survive the CLI process that observed it.
 * Successful persistence does not convert the capture's failure exit to zero.
 */
static void cli_durability(const char *cli, const char *self,
                           const char *directory) {
  char state[PATH_BYTES], raw[PATH_BYTES], missing[PATH_BYTES];
  char reserved[PATH_BYTES], rejected[PATH_BYTES], marker[PATH_BYTES];
  char canonical[C_CONTEXT_PROVENANCE_BYTES + 1u];
  char cwd[C_CONTEXT_PROVENANCE_BYTES + 1u];
  c_trainer *initial = NULL, *before = NULL, *after = NULL;
  c_training_report report;
  uint64_t source = 0;
  static const unsigned char guard[] = "source guard for pending Life work";
  join(state, directory, "cli-state.clife");
  join(raw, directory, "cli-overflow.raw");
  CHECK(c_trainer_create(2, 3987, &initial) == C_OK);
  CHECK(c_context_admit(initial->context, C_SOURCE, C_TRAIN, "guard.c",
                        "native CLI durability fixture", guard,
                        sizeof(guard) - 1, &source) == C_OK);
  CHECK(c_trainer_enqueue_source(initial, source, 0, 3) == C_OK);
  CHECK(c_trainer_step(initial, 16, &report) == C_OK && report.updates == 1);
  CHECK(c_trainer_enqueue_source(initial, source, 1, 3) == C_OK);
  CHECK(c_trainer_save(initial, state) == C_OK);
  c_trainer_destroy(initial);
  CHECK(c_trainer_load(state, &before) == C_OK);
  const char *overflow_argv[] = {cli,  "run",        state,   raw, "30000",
                                 self, "--overflow", SPECIAL, NULL};
  c_process_options options = {cli, overflow_argv, directory, 120000,
                               1024u * 1024u};
  c_process_result result = {0};
  CHECK(c_process_run(&options, &result) == C_OK && result.exit_code == 1 &&
        !result.timed_out && !result.output_truncated);
  CHECK(
      contains(result.output, result.length, "activity=2 exit=-1 timeout=0") &&
      contains(result.output, result.length, "capture_status=capacity limit") &&
      contains(result.output, result.length, "centroid: capacity limit"));
  c_process_dispose(&result);
  CHECK(c_trainer_load(state, &after) == C_OK);
  unchanged_training(before, after);
  CHECK(c_context_count(after->context) == 2);
  c_record record = activity(after->context, 1);
  CHECK(record.id == 2 && record.version == 1 && record.current &&
        !strcmp(record.attribution, "centroid CLI explicit capture"));
  CHECK(c_context_absolute(raw, canonical) == C_OK &&
        c_context_absolute(directory, cwd) == C_OK &&
        !strcmp(record.path, canonical));
  char *text = record_text(&record);
  CHECK(strstr(text, "process-status=2\nexit-code=-1\ntimed-out=0\n") &&
        strstr(text, "raw-output-bytes=67108864\nraw-output-complete=0\n") &&
        strstr(text, "view-output-bytes=65536\n"));
  exact_text_field(text, "program", self);
  exact_text_field(text, "argv-0", self);
  exact_text_field(text, "argv-1", "--overflow");
  exact_text_field(text, "argv-2", SPECIAL);
  exact_text_field(text, "working-directory", cwd);
  exact_text_field(text, "raw-output-path", canonical);
  unsigned char *bytes = NULL;
  size_t length = 0;
  CHECK(c_read_file(raw, &bytes, &length) == C_OK &&
        length == 64u * 1024u * 1024u);
  for (size_t i = 0; i < length; ++i) {
    size_t position = i % 4096u;
    unsigned char expected = position == 0   ? 0
                             : position == 1 ? 1
                             : position == 2 ? 27
                                             : 'w';
    CHECK(bytes[i] == expected);
  }
  char digest[C_DIGEST_HEX];
  c_hash(bytes, length, digest);
  CHECK(strstr(text, digest));
  free(bytes);
  free(text);
  c_trainer_destroy(after);
  after = NULL;
  CHECK(remove(raw) == 0);

  /* Launch failure has a complete empty raw artifact and an observed I/O
   * failure receipt. It is durable too, with numerical state still unchanged.
   */
  join(raw, directory, "cli-launch-failure.raw");
  join(missing, directory, "nonexistent-native-executable");
  const char *missing_argv[] = {cli, "run", state, raw, "10000", missing, NULL};
  options.argv = missing_argv;
  options.output_limit = 4096;
  CHECK(
      c_process_run(&options, &result) == C_OK && result.exit_code == 1 &&
      !result.timed_out && !result.output_truncated &&
      contains(result.output, result.length, "activity=3 exit=-1 timeout=0") &&
      contains(result.output, result.length, "capture_status=I/O failure") &&
      contains(result.output, result.length, "centroid: I/O failure"));
  c_process_dispose(&result);
  CHECK(c_trainer_load(state, &after) == C_OK);
  unchanged_training(before, after);
  CHECK(c_context_count(after->context) == 3);
  record = activity(after->context, 2);
  text = record_text(&record);
  CHECK(record.id == 3 && strstr(text, "process-status=3\n") &&
        strstr(text, "raw-output-bytes=0\nraw-output-complete=1\n"));
  CHECK(c_context_absolute(raw, canonical) == C_OK &&
        !strcmp(record.path, canonical));
  exact_text_field(text, "program", missing);
  exact_text_field(text, "argv-0", missing);
  exact_text_field(text, "working-directory", cwd);
  exact_text_field(text, "raw-output-path", canonical);
  free(text);
  CHECK(c_read_file(raw, &bytes, &length) == C_OK && length == 0);
  free(bytes);
  CHECK(remove(raw) == 0);
  c_trainer_destroy(after);
  after = NULL;

  /* Rejected preflight has no admitted record and must preserve the exact
   * on-disk checkpoint while launching no marker child or output artifact.
   */
  CHECK(c_read_file(state, &bytes, &length) == C_OK);
  join(reserved, directory, "audit");
  CHECK(c_make_directory(reserved) == C_OK);
  join(rejected, reserved, "cli-rejected.raw");
  join(marker, directory, "cli-rejected-child.marker");
  const char *rejected_argv[] = {cli,  "run",      state,  rejected, "10000",
                                 self, "--marker", marker, NULL};
  options.argv = rejected_argv;
  CHECK(c_process_run(&options, &result) == C_OK && result.exit_code == 1 &&
        !result.timed_out &&
        !contains(result.output, result.length, "activity="));
  c_process_dispose(&result);
  unsigned char *retained = NULL;
  size_t retained_length = 0;
  CHECK(c_read_file(state, &retained, &retained_length) == C_OK &&
        retained_length == length && !memcmp(bytes, retained, length));
  free(bytes);
  free(retained);
  absent(marker);
  absent(rejected);
  remove_directory(reserved);
  c_trainer_destroy(before);
  CHECK(remove(state) == 0);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strncmp(argv[1], "--", 2))
    return child(argc, argv);
  CHECK(argc == 3);
  char build[PATH_BYTES], self[PATH_BYTES], cli[PATH_BYTES];
  char directory[PATH_BYTES], name[128];
  full_path(build, argv[1]);
  full_path(self, argv[0]);
  full_path(cli, argv[2]);
  int n = snprintf(name, sizeof(name), "work fixture %u", pid_number());
  CHECK(n > 0 && (size_t)n < sizeof(name));
  join(directory, build, name);
  CHECK(c_make_directory(directory) == C_OK);
  c_context *context = NULL;
  CHECK(c_context_create(&context) == C_OK);
  captured(context, self, directory);
  overflow(context, self, directory);
  c_context_destroy(context);
  preflight(self, directory);
  canonical_output(self, directory);
  cli_durability(cli, self, directory);
#ifdef _WIN32
  CHECK(_rmdir(directory) == 0);
#else
  CHECK(rmdir(directory) == 0);
#endif
  puts("Full immutable raw work output, bounded provenance view, explicit raw "
       "capacity and fresh-process CLI failure durability passed");
  return 0;
}
