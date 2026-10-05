#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "context/context_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <process.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CHECK(value)                                                           \
  do {                                                                         \
    if (!(value)) {                                                            \
      fprintf(stderr, "context check failed %s:%d: %s\n", __FILE__, __LINE__,  \
              #value);                                                         \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define TEST_PATH_BYTES (C_CONTEXT_PROVENANCE_BYTES + 1u)

typedef struct {
  char root[TEST_PATH_BYTES];
  int file_link, directory_link;
} fixture;

static const char *const language_files[] = {
    "unit.cc",  "unit.cjs",  "unit.cpp",   "unit.cs",  "unit.go",
    "unit.hpp", "unit.java", "unit.js",    "unit.jsx", "unit.kt",
    "unit.lua", "unit.mjs",  "unit.ps1",   "unit.py",  "unit.rb",
    "unit.rs",  "unit.sh",   "unit.swift", "unit.ts",  "unit.tsx"};

static const char *const excluded_directories[] = {
    ".git",      ".agents",     ".codex",
    ".aws",      "build-debug", "cmake-build-release",
    "generated", "deps",        "node_modules",
    "data",      "audit",       "tests",
    "research",  "experiments", "runs",
    "models",    "outputs"};

static const char *const excluded_files[] = {
    ".env",        "secrets.py", "credentials.ts", "archive.checkpoint.json",
    "weights.bin", "output.tmp"};

static const unsigned char binary_source[] = {'A',  'l', 'p',  'h',  'a', 'B',
                                              'r',  'i', 'd',  'g',  'e', '\r',
                                              '\n', 0,   0xff, 0x80, 'X'};

static const char executable_source[] =
    "import pathlib\n"
    "pathlib.Path(__file__).with_name('execution-sentinel').write_text('"
    "executed')\n";

static void path_join(char *out, const char *root, const char *name) {
  int length = snprintf(out, TEST_PATH_BYTES, "%s/%s", root, name);
  CHECK(length > 0 && (size_t)length < TEST_PATH_BYTES);
}

static int make_directory(const char *path) {
#ifdef _WIN32
  return _mkdir(path);
#else
  return mkdir(path, 0700);
#endif
}

static void remove_directory(const char *path) {
#ifdef _WIN32
  CHECK(_rmdir(path) == 0);
#else
  CHECK(rmdir(path) == 0);
#endif
}

static unsigned long process_id(void) {
#ifdef _WIN32
  return (unsigned long)_getpid();
#else
  return (unsigned long)getpid();
#endif
}

static void create_fixture_root(fixture *value) {
  memset(value, 0, sizeof(*value));
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    int length = snprintf(value->root, sizeof(value->root),
                          "test-context-%lu-%u", process_id(), attempt);
    CHECK(length > 0 && (size_t)length < sizeof(value->root));
    if (make_directory(value->root) == 0)
      return;
    CHECK(errno == EEXIST);
  }
  CHECK(0);
}

static void write_bytes(const char *path, const void *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  CHECK(length == 0 || fwrite(bytes, 1, length, file) == length);
  CHECK(fclose(file) == 0);
}

static void fixture_write(const fixture *value, const char *name,
                          const void *bytes, size_t length) {
  char path[TEST_PATH_BYTES];
  path_join(path, value->root, name);
  write_bytes(path, bytes, length);
}

static void fixture_string(const fixture *value, const char *name,
                           const char *text) {
  fixture_write(value, name, text, strlen(text));
}

static void fixture_remove(const fixture *value, const char *name) {
  char path[TEST_PATH_BYTES];
  path_join(path, value->root, name);
  CHECK(remove(path) == 0);
}

static void fixture_directory(const fixture *value, const char *name) {
  char path[TEST_PATH_BYTES];
  path_join(path, value->root, name);
  CHECK(make_directory(path) == 0);
}

static int make_link(const char *path, const char *target, int directory) {
#ifdef _WIN32
  DWORD flags = (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0) |
                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  return CreateSymbolicLinkA(path, target, flags) != 0;
#else
  (void)directory;
  return symlink(target, path) == 0;
#endif
}

static void fixture_links(fixture *value) {
  char path[TEST_PATH_BYTES];
  path_join(path, value->root, "src/link.py");
  value->file_link = make_link(path, "unit.py", 0);
  path_join(path, value->root, "linked-source");
  value->directory_link = make_link(path, "src", 1);
  if (!value->file_link || !value->directory_link)
    puts("Some native context symlink fixtures unavailable");
}

static void fixture_sources(fixture *value) {
  char relative[128];
  fixture_directory(value, "src");
  fixture_string(value, "README.md", "OnlyOriginalName source evidence\r\n");
  for (size_t i = 0; i < sizeof(language_files) / sizeof(language_files[0]);
       ++i) {
    int length =
        snprintf(relative, sizeof(relative), "src/%s", language_files[i]);
    CHECK(length > 0 && (size_t)length < sizeof(relative));
    fixture_write(value, relative, binary_source, sizeof(binary_source));
  }
  fixture_write(value, "src/upper.PY", binary_source, sizeof(binary_source));
  fixture_string(value, "src/execute.py", executable_source);
  fixture_links(value);
}

static void fixture_exclusions(const fixture *value) {
  char relative[128];
  for (size_t i = 0;
       i < sizeof(excluded_directories) / sizeof(excluded_directories[0]);
       ++i) {
    fixture_directory(value, excluded_directories[i]);
    int length = snprintf(relative, sizeof(relative), "%s/heldout.py",
                          excluded_directories[i]);
    CHECK(length > 0 && (size_t)length < sizeof(relative));
    fixture_string(value, relative, "NeverSearchHeldoutAnswer\n");
  }
  for (size_t i = 0; i < sizeof(excluded_files) / sizeof(excluded_files[0]);
       ++i)
    fixture_string(value, excluded_files[i], "NeverSearchSecretAnswer\n");
}

static uint64_t admit_string(c_context *context, c_record_kind kind,
                             c_split split, const char *path,
                             const char *attribution, const char *text) {
  uint64_t id = 0;
  CHECK(c_context_admit(context, kind, split, path, attribution,
                        (const unsigned char *)text, strlen(text),
                        &id) == C_OK);
  return id;
}

static void assert_abstains(c_context *context, const char *query) {
  c_record result = {0};
  result.id = 999;
  double score = -7;
  CHECK(c_context_retrieve(context, (const unsigned char *)query, strlen(query),
                           &result, &score) == C_NOT_FOUND);
  CHECK(result.id == 999 && score == -7);
}

static void assert_answer(c_context *context, const char *query, uint64_t id) {
  c_record result = {0};
  double score = -7;
  CHECK(c_context_retrieve(context, (const unsigned char *)query, strlen(query),
                           &result, &score) == C_OK);
  CHECK(result.id == id && result.current && result.split == C_TRAIN);
  CHECK(score >= 0 && score <= 1);
}

static c_context *test_versions(void) {
  c_context *context = NULL;
  unsigned char first[] = {'A', 'l', 'p', 'h', 'a',  'T',  'o',
                           'k', 'e', 'n', 0,   0xff, '\r', '\n'};
  uint64_t id = 0;
  c_record original = {0}, changed = {0}, restored = {0};
  CHECK(c_context_create(&context) == C_OK);
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, "code.c", "editor", first,
                        sizeof(first), &id) == C_OK);
  CHECK(id == 1 && c_context_count(context) == 1);
  CHECK(c_context_record(context, 0, &original) == C_OK);
  first[0] = 'X';
  CHECK(original.bytes[0] == 'A');
  first[0] = 'A';
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, "code.c", "editor", first,
                        sizeof(first), &id) == C_OK);
  CHECK(id == 1 && c_context_count(context) == 1);
  CHECK(admit_string(context, C_SOURCE, C_TRAIN, "code.c", "editor",
                     "OnlyNewToken second version") == 2);
  assert_abstains(context, "AlphaToken");
  assert_answer(context, "OnlyNewToken", 2);
  CHECK(c_context_record(context, 1, &changed) == C_OK &&
        changed.version == 2 && changed.current);
  CHECK(c_context_record(context, 0, &restored) == C_OK && !restored.current);
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, "code.c", "editor", first,
                        sizeof(first), &id) == C_OK);
  CHECK(id == 3 && original.bytes == restored.bytes &&
        memcmp(original.bytes, first, sizeof(first)) == 0);
  CHECK(c_context_record(context, 2, &restored) == C_OK &&
        restored.version == 3 && restored.current);
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, "code.c",
                        "different editor", first, sizeof(first), &id) == C_OK);
  CHECK(id == 4);
  assert_answer(context, "AlphaToken", 3);
  double score = 0;
  CHECK(c_context_retrieve(context, first, sizeof(first), &restored, &score) ==
        C_OK);
  CHECK(restored.id == 3 && score > 0.999999999999);
  assert_abstains(context, "OnlyNewToken");
  assert_abstains(context, "alphatoken");
  assert_abstains(context, "unrelatedZebraWord");
  assert_abstains(context, "");
  assert_abstains(context, "12345 !!!");
  return context;
}

static void test_quarantine(c_context *context) {
  c_record source = {0};
  uint64_t id = 999;
  CHECK(c_context_record(context, 0, &source) == C_OK);
  size_t before = c_context_count(context);
  CHECK(c_context_admit(context, C_SOURCE, C_DEV, "copy.c", "heldout",
                        source.bytes, source.length, &id) == C_INVALID);
  CHECK(id == 999 && c_context_count(context) == before);
  CHECK(c_context_admit(context, C_SOURCE, C_DEV, "code.c", "editor",
                        (const unsigned char *)"different", 9,
                        &id) == C_INVALID);
  CHECK(id == 999 && c_context_count(context) == before);
  (void)admit_string(context, C_SOURCE, C_DEV, "dev.c", "heldout",
                     "ReservedDevToken");
  (void)admit_string(context, C_DEVELOPMENT, C_DEV, "dev-result", "evaluator",
                     "DevelopmentOnlyToken");
  (void)admit_string(context, C_AUDIT, C_HOLDOUT, "audit.c", "independent",
                     "ReservedAuditToken");
  (void)admit_string(context, C_TRAIN_MEASUREMENT, C_TRAIN, "receipt",
                     "evaluator", "MeasuredOnlyToken");
  assert_abstains(context, "ReservedDevToken");
  assert_abstains(context, "DevelopmentOnlyToken");
  assert_abstains(context, "ReservedAuditToken");
  assert_abstains(context, "MeasuredOnlyToken");
  uint64_t proposal = admit_string(context, C_LLM_PROPOSAL, C_TRAIN, "proposal",
                                   "assistant", "ProposedToken");
  uint64_t activity = admit_string(context, C_ACTIVITY, C_TRAIN, "action",
                                   "visible local work", "WorkVisibleToken");
  assert_answer(context, "ProposedToken", proposal);
  assert_answer(context, "WorkVisibleToken", activity);
  before = c_context_count(context);
  CHECK(c_context_admit(context, C_AUDIT, C_TRAIN, "bad", "test", NULL, 0,
                        &id) == C_INVALID);
  CHECK(id == 999 && c_context_count(context) == before);
}

static void assert_kind_result(c_context *context, c_record_kind kind,
                               const char *query, c_status expected,
                               uint64_t expected_id) {
  c_record result = {0};
  result.id = 999;
  double score = -7;
  CHECK(c_context_retrieve_kind(context, kind, (const unsigned char *)query,
                                strlen(query), &result, &score) == expected);
  if (expected == C_OK) {
    CHECK(result.id == expected_id && result.kind == kind && result.current &&
          result.split == C_TRAIN && score >= 0 && score <= 1);
  } else {
    CHECK(result.id == 999 && score == -7);
  }
}

static void test_kind_retrieval(void) {
  static const char query[] = "KindRankToken exact query";
  c_context *context = NULL;
  c_record source = {0}, proposal = {0};
  double source_score = 0, proposal_score = 0;
  CHECK(c_context_create(&context) == C_OK);
  uint64_t source_id =
      admit_string(context, C_SOURCE, C_TRAIN, "source.c", "editor", query);
  uint64_t old_proposal =
      admit_string(context, C_LLM_PROPOSAL, C_TRAIN, "suggestion", "assistant",
                   "KindRankToken old proposal AncientProposalToken");
  CHECK(c_context_retrieve(context, (const unsigned char *)query, strlen(query),
                           &source, &source_score) == C_OK &&
        source.id == source_id);
  CHECK(c_context_retrieve_kind(context, C_LLM_PROPOSAL,
                                (const unsigned char *)query, strlen(query),
                                &proposal, &proposal_score) == C_OK);
  CHECK(proposal.id == old_proposal && proposal_score < source_score);
  assert_kind_result(context, C_SOURCE, query, C_OK, source_id);
  assert_kind_result(context, C_ACTIVITY, query, C_NOT_FOUND, 0);
  uint64_t current_proposal =
      admit_string(context, C_LLM_PROPOSAL, C_TRAIN, "suggestion", "assistant",
                   "KindRankToken current proposal FreshProposalToken");
  (void)admit_string(context, C_LLM_PROPOSAL, C_HOLDOUT, "heldout-proposal",
                     "evaluator",
                     "KindRankToken HoldoutProposalToken quarantined evidence");
  (void)admit_string(
      context, C_LLM_PROPOSAL, C_DEV, "development-proposal", "evaluator",
      "KindRankToken DevelopmentProposalToken reserved evidence");
  uint64_t activity = admit_string(
      context, C_ACTIVITY, C_TRAIN, "observed-work", "visible local work",
      "KindRankToken observed work ActivityVisibleToken");
  unsigned char *before = NULL, *after = NULL;
  size_t before_length = 0, after_length = 0;
  CHECK(c_context_pack(context, &before, &before_length) == C_OK);
  assert_kind_result(context, C_LLM_PROPOSAL, "FreshProposalToken", C_OK,
                     current_proposal);
  assert_kind_result(context, C_LLM_PROPOSAL, "AncientProposalToken",
                     C_NOT_FOUND, 0);
  assert_kind_result(context, C_LLM_PROPOSAL, "HoldoutProposalToken",
                     C_NOT_FOUND, 0);
  assert_kind_result(context, C_LLM_PROPOSAL, "DevelopmentProposalToken",
                     C_NOT_FOUND, 0);
  assert_kind_result(context, C_LLM_PROPOSAL, "UnrelatedProposalToken",
                     C_NOT_FOUND, 0);
  assert_kind_result(context, C_LLM_PROPOSAL, "", C_NOT_FOUND, 0);
  assert_kind_result(context, C_ACTIVITY, "ActivityVisibleToken", C_OK,
                     activity);
  assert_kind_result(context, C_SOURCE, "ActivityVisibleToken", C_NOT_FOUND, 0);
  assert_kind_result(context, C_TRAIN_MEASUREMENT, query, C_INVALID, 0);
  assert_kind_result(context, C_DEVELOPMENT, query, C_INVALID, 0);
  assert_kind_result(context, C_AUDIT, query, C_INVALID, 0);
  CHECK(c_context_pack(context, &after, &after_length) == C_OK);
  CHECK(before_length == after_length &&
        memcmp(before, after, before_length) == 0);
  free(before);
  free(after);
  c_context_destroy(context);
}

static void test_limits(void) {
  c_context *context = NULL;
  uint64_t id = 999;
  unsigned char *bytes = calloc((size_t)C_CONTEXT_RECORD_BYTES + 1, 1);
  char path[32], overlong[C_CONTEXT_PROVENANCE_BYTES + 2];
  CHECK(bytes != NULL && c_context_create(&context) == C_OK);
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, "oversized", "test", bytes,
                        (size_t)C_CONTEXT_RECORD_BYTES + 1, &id) == C_LIMIT);
  CHECK(id == 999 && c_context_count(context) == 0);
  memset(overlong, 'x', sizeof(overlong));
  overlong[sizeof(overlong) - 1] = '\0';
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, overlong, "test", bytes, 1,
                        &id) == C_LIMIT);
  CHECK(id == 999 && c_context_count(context) == 0);
  for (unsigned i = 0; i < C_CONTEXT_TOTAL_BYTES / C_CONTEXT_RECORD_BYTES;
       ++i) {
    CHECK(snprintf(path, sizeof(path), "aggregate-%u", i) > 0);
    CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, path, "test", bytes,
                          C_CONTEXT_RECORD_BYTES, &id) == C_OK);
  }
  size_t count = c_context_count(context);
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, "aggregate-0", "test",
                        bytes, C_CONTEXT_RECORD_BYTES, &id) == C_OK);
  CHECK(id == 1 && c_context_count(context) == count);
  bytes[0] = 1;
  id = 999;
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, "aggregate-0", "test",
                        bytes, 1, &id) == C_LIMIT);
  CHECK(id == 999 && c_context_count(context) == count);
  c_record result = {0};
  CHECK(c_context_record(context, 0, &result) == C_OK && result.current &&
        result.version == 1 && result.bytes[0] == 0);
  c_context_destroy(context);
  free(bytes);
}

static c_record find_path(c_context *context, const char *path, int current) {
  c_record result = {0};
  for (size_t i = 0; i < c_context_count(context); ++i) {
    CHECK(c_context_record(context, i, &result) == C_OK);
    if (strcmp(result.path, path) == 0 && result.current == current)
      return result;
  }
  CHECK(0);
  return result;
}

static void check_no_execution(const fixture *value) {
  char path[TEST_PATH_BYTES];
  path_join(path, value->root, "src/execution-sentinel");
  errno = 0;
  FILE *file = fopen(path, "rb");
  CHECK(file == NULL && errno == ENOENT);
}

static void check_language_records(c_context *context) {
  char path[128], digest[C_DIGEST_HEX];
  c_hash(binary_source, sizeof(binary_source), digest);
  for (size_t i = 0; i < sizeof(language_files) / sizeof(language_files[0]);
       ++i) {
    CHECK(snprintf(path, sizeof(path), "src/%s", language_files[i]) > 0);
    c_record record = find_path(context, path, 1);
    CHECK(record.kind == C_SOURCE && record.split == C_TRAIN &&
          record.version == 1);
    CHECK(record.length == sizeof(binary_source) &&
          memcmp(record.bytes, binary_source, sizeof(binary_source)) == 0);
    CHECK(strcmp(record.digest, digest) == 0);
  }
  c_record record = find_path(context, "src/upper.PY", 1);
  CHECK(record.length == sizeof(binary_source));
}

static void check_links(c_context *context, const fixture *value) {
  char path[TEST_PATH_BYTES];
  uint64_t id = 999;
  if (value->file_link) {
    path_join(path, value->root, "src/link.py");
    CHECK(c_context_admit_file(context, C_SOURCE, C_TRAIN, path, "explicit",
                               &id) == C_INVALID);
    CHECK(id == 999);
  }
  if (value->directory_link) {
    path_join(path, value->root, "linked-source/unit.py");
    CHECK(c_context_admit_file(context, C_SOURCE, C_TRAIN, path, "explicit",
                               &id) == C_INVALID);
    CHECK(id == 999);
    c_scan_report report;
    path_join(path, value->root, "linked-source");
    CHECK(c_context_scan(context, path, &report) == C_INVALID &&
          report.failed == 1);
  }
}

static void check_scan_limit(c_context *context, const fixture *value) {
  char path[TEST_PATH_BYTES];
  uint64_t id = 999;
  unsigned char *bytes = calloc((size_t)C_CONTEXT_RECORD_BYTES + 1, 1);
  CHECK(bytes != NULL);
  fixture_write(value, "oversized.py", bytes,
                (size_t)C_CONTEXT_RECORD_BYTES + 1);
  path_join(path, value->root, "oversized.py");
  size_t count = c_context_count(context);
  CHECK(c_context_admit_file(context, C_SOURCE, C_TRAIN, path, "explicit",
                             &id) == C_LIMIT);
  CHECK(id == 999 && c_context_count(context) == count);
  c_scan_report report;
  CHECK(c_context_scan(context, value->root, &report) == C_LIMIT);
  CHECK(report.failed == 1 && report.admitted == 0 && report.unchanged == 24 &&
        c_context_count(context) == count);
  fixture_remove(value, "oversized.py");
  free(bytes);
}

static void test_scan(fixture *value) {
  c_context *context = NULL;
  c_scan_report first, repeated, changed;
  CHECK(c_context_create(&context) == C_OK);
  CHECK(c_context_scan(context, value->root, &first) == C_OK);
  CHECK(first.admitted == 23 && first.unchanged == 0 && first.failed == 0);
  CHECK(first.excluded >= 23 && c_context_count(context) == 23);
  CHECK(first.bytes == sizeof(binary_source) * 21 + strlen(executable_source) +
                           strlen("OnlyOriginalName source evidence\r\n"));
  check_language_records(context);
  check_no_execution(value);
  assert_abstains(context, "NeverSearchHeldoutAnswer");
  assert_abstains(context, "NeverSearchSecretAnswer");
  c_record original = find_path(context, "README.md", 1);
  CHECK(original.id == 1 && original.version == 1);
  CHECK(c_context_scan(context, value->root, &repeated) == C_OK);
  CHECK(repeated.admitted == 0 && repeated.unchanged == 23 &&
        repeated.bytes == first.bytes);
  fixture_string(value, "README.md", "OnlyCurrentName dirty working bytes");
  CHECK(c_context_scan(context, value->root, &changed) == C_OK);
  CHECK(changed.admitted == 1 && changed.unchanged == 22 &&
        c_context_count(context) == 24);
  c_record current = find_path(context, "README.md", 1);
  CHECK(current.id == 24 && current.version == 2 &&
        current.length == strlen("OnlyCurrentName dirty working bytes"));
  CHECK(memcmp(current.bytes, "OnlyCurrentName dirty working bytes",
               current.length) == 0);
  CHECK(strcmp(current.digest, original.digest) != 0);
  assert_abstains(context, "OnlyOriginalName");
  assert_answer(context, "OnlyCurrentName", current.id);
  fixture_write(value, "empty.py", NULL, 0);
  CHECK(c_context_scan(context, value->root, &changed) == C_OK &&
        changed.admitted == 1 && changed.unchanged == 23);
  assert_abstains(context, "empty");
  check_links(context, value);
  check_scan_limit(context, value);
  check_no_execution(value);
  c_context_destroy(context);
}

static void check_corrupt(const unsigned char *bytes, size_t length) {
  c_context *out = NULL;
  CHECK(c_context_unpack(bytes, length, &out) == C_CORRUPT);
  CHECK(out == NULL);
}

static void test_canonical(c_context *context) {
  unsigned char *bytes = NULL, *copy = NULL;
  size_t length = 0, copy_length = 0;
  c_context *restored = NULL;
  CHECK(c_context_pack(context, &bytes, &length) == C_OK);
  CHECK(c_context_unpack(bytes, length, &restored) == C_OK);
  CHECK(c_context_pack(restored, &copy, &copy_length) == C_OK);
  CHECK(length == copy_length && memcmp(bytes, copy, length) == 0);
  assert_answer(restored, "AlphaToken", 3);
  assert_abstains(restored, "OnlyNewToken");
  assert_abstains(restored, "ReservedAuditToken");
  bytes[length - 1] ^= 1;
  check_corrupt(bytes, length);
  bytes[length - 1] ^= 1;
  bytes[20] ^= 1;
  check_corrupt(bytes, length);
  bytes[20] ^= 1;
  bytes[52] ^= 1;
  check_corrupt(bytes, length);
  bytes[52] ^= 1;
  check_corrupt(bytes, length - 1);
  unsigned char *trailing = malloc(length + 1);
  CHECK(trailing != NULL);
  memcpy(trailing, bytes, length);
  trailing[length] = 0;
  check_corrupt(trailing, length + 1);
  CHECK(c_context_count(context) == c_context_count(restored));
  free(bytes);
  free(copy);
  free(trailing);
  c_context_destroy(restored);
}

static void test_checkpoint(c_context *context, const fixture *value) {
  char path[TEST_PATH_BYTES];
  c_context *restored = NULL;
  path_join(path, value->root, "context.checkpoint");
  CHECK(c_context_save(context, path) == C_OK);
  CHECK(c_context_load(path, &restored) == C_OK);
  assert_answer(restored, "AlphaToken", 3);
  unsigned char *bytes = NULL;
  size_t length = 0;
  CHECK(c_read_file(path, &bytes, &length) == C_OK && length != 0);
  bytes[length - 1] ^= 1;
  write_bytes(path, bytes, length);
  c_context *candidate = NULL;
  CHECK(c_context_load(path, &candidate) == C_CORRUPT && candidate == NULL);
  c_context *incumbent = restored;
  CHECK(c_context_load(path, &restored) == C_INVALID && restored == incumbent);
  assert_answer(restored, "AlphaToken", 3);
  free(bytes);
  c_context_destroy(restored);
  fixture_remove(value, "context.checkpoint");
}

static void test_audit_header_boundary(void) {
  static const char infrastructure[] =
      "#include \"../../data/audit/benchmark_fixtures.h\"\n"
      "int benchmark_infrastructure(void) { return 0; }\n";
  static const char heldout_header[] =
      "static const char heldout[] = \"AuditHeaderHiddenTarget\";\n";
  fixture value;
  c_context *context = NULL;
  c_scan_report report;
  char path[TEST_PATH_BYTES];
  create_fixture_root(&value);
  fixture_directory(&value, "src");
  fixture_directory(&value, "src/experiment");
  fixture_directory(&value, "data");
  fixture_directory(&value, "data/audit");
  fixture_string(&value, "src/experiment/benchmark.c", infrastructure);
  fixture_string(&value, "data/audit/benchmark_fixtures.h", heldout_header);
  CHECK(c_context_create(&context) == C_OK);
  CHECK(c_context_scan(context, value.root, &report) == C_OK);
  CHECK(report.admitted == 1 && report.excluded == 1 && report.failed == 0);
  c_record record = {0};
  CHECK(c_context_count(context) == 1 &&
        c_context_record(context, 0, &record) == C_OK);
  CHECK(strcmp(record.path, "src/experiment/benchmark.c") == 0);
  CHECK(record.length == sizeof(infrastructure) - 1 &&
        memcmp(record.bytes, infrastructure, sizeof(infrastructure) - 1) == 0);
  assert_answer(context, "benchmark_infrastructure", record.id);
  assert_abstains(context, "AuditHeaderHiddenTarget");
  fixture_string(&value, "data/audit/benchmark_fixtures.h",
                 "ChangedHeldoutHeaderTarget\n");
  CHECK(c_context_scan(context, value.root, &report) == C_OK &&
        report.admitted == 0 && report.unchanged == 1 &&
        c_context_count(context) == 1);
  assert_abstains(context, "ChangedHeldoutHeaderTarget");
  c_context_destroy(context);
  fixture_remove(&value, "src/experiment/benchmark.c");
  fixture_remove(&value, "data/audit/benchmark_fixtures.h");
  path_join(path, value.root, "src/experiment");
  remove_directory(path);
  path_join(path, value.root, "src");
  remove_directory(path);
  path_join(path, value.root, "data/audit");
  remove_directory(path);
  path_join(path, value.root, "data");
  remove_directory(path);
  remove_directory(value.root);
}

static const char native_model_source[] =
    "int NativeCentroidModel(void) { return 0; }\r\n";

static void create_model_fixture(fixture *value) {
  create_fixture_root(value);
  fixture_directory(value, "src");
  fixture_directory(value, "src/model");
  fixture_directory(value, "src/model/checkpoints");
  fixture_directory(value, "models");
  fixture_string(value, "src/model/model.c", native_model_source);
  fixture_string(value, "src/model/model.json", "NeverSearchModelArtifact");
  fixture_string(value, "src/model/weights.json", "NeverSearchModelArtifact");
  fixture_string(value, "src/model/checkpoints/generated.c",
                 "NeverSearchModelArtifact");
  fixture_string(value, "models/generated.c", "NeverSearchModelArtifact");
}

static void cleanup_model_fixture(const fixture *value) {
  static const char *const files[] = {
      "src/model/model.c", "src/model/model.json", "src/model/weights.json",
      "src/model/checkpoints/generated.c", "models/generated.c"};
  static const char *const directories[] = {"src/model/checkpoints",
                                            "src/model", "src", "models"};
  char path[TEST_PATH_BYTES];
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
    fixture_remove(value, files[i]);
  for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
    path_join(path, value->root, directories[i]);
    remove_directory(path);
  }
  remove_directory(value->root);
}

static void test_native_model_scan(void) {
  fixture value;
  c_context *context = NULL;
  c_scan_report report;
  char digest[C_DIGEST_HEX];
  create_model_fixture(&value);
  CHECK(c_context_create(&context) == C_OK);
  CHECK(c_context_scan(context, value.root, &report) == C_OK);
  CHECK(report.admitted == 1 && report.excluded == 4 && report.failed == 0 &&
        report.bytes == sizeof(native_model_source) - 1 &&
        c_context_count(context) == 1);
  c_record record = find_path(context, "src/model/model.c", 1);
  CHECK(record.length == sizeof(native_model_source) - 1 &&
        memcmp(record.bytes, native_model_source, record.length) == 0);
  c_hash(native_model_source, sizeof(native_model_source) - 1, digest);
  CHECK(strcmp(record.digest, digest) == 0);
  assert_answer(context, "NativeCentroidModel", record.id);
  assert_abstains(context, "NeverSearchModelArtifact");
  CHECK(c_context_scan(context, value.root, &report) == C_OK &&
        report.admitted == 0 && report.unchanged == 1 && report.excluded == 4 &&
        c_context_count(context) == 1);
  c_context_destroy(context);
  cleanup_model_fixture(&value);
}

static void test_scan_absolute_aliases(void) {
  static const unsigned char original[] = "OnlyAbsoluteOriginal bytes";
  static const unsigned char dirty[] = {'O', 'n', 'l', 'y', 'A', 'b', 's',
                                        'o', 'l', 'u', 't', 'e', 'C', 'u',
                                        'r', 'r', 'e', 'n', 't', 0,   0xff};
  fixture value;
  c_context *context = NULL;
  char path[TEST_PATH_BYTES], absolute[TEST_PATH_BYTES], mixed[TEST_PATH_BYTES];
  char excluded[TEST_PATH_BYTES];
  char case_variant[TEST_PATH_BYTES];
  size_t case_alias = 0;
  c_scan_report report;
  create_fixture_root(&value);
  fixture_directory(&value, "src");
  fixture_directory(&value, "research");
  fixture_write(&value, "src/fresh.c", original, sizeof(original) - 1);
  fixture_string(&value, "research/heldout.c", "ExcludedAliasOriginal");
  path_join(path, value.root, "src/fresh.c");
  CHECK(c_context_absolute(path, absolute) == C_OK);
  memcpy(mixed, absolute, strlen(absolute) + 1);
#ifdef _WIN32
  for (size_t i = 0; mixed[i]; ++i)
    if (mixed[i] == '/' && (i & 1u))
      mixed[i] = '\\';
#else
  CHECK(snprintf(mixed, sizeof(mixed), "/%s", absolute) > 0);
#endif
  path_join(path, value.root, "research/heldout.c");
  CHECK(c_context_absolute(path, excluded) == C_OK);
  CHECK(c_context_create(&context) == C_OK);
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, absolute,
                        "captured absolute", original, sizeof(original) - 1,
                        NULL) == C_OK);
  CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, mixed, "captured mixed",
                        original, sizeof(original) - 1, NULL) == C_OK);
#ifdef _WIN32
  memcpy(case_variant, absolute, strlen(absolute) + 1);
  for (size_t i = 0; case_variant[i]; ++i)
    if (case_variant[i] >= 'a' && case_variant[i] <= 'z')
      case_variant[i] = (char)(case_variant[i] - ('a' - 'A'));
  if (strcmp(case_variant, absolute) &&
      c_context_no_links(case_variant, 0) == C_OK) {
    CHECK(c_context_admit(context, C_SOURCE, C_TRAIN, case_variant,
                          "captured case variant", original,
                          sizeof(original) - 1, NULL) == C_OK);
    case_alias = 1;
  }
#else
  case_variant[0] = '\0';
#endif
  uint64_t virtual_id =
      admit_string(context, C_SOURCE, C_TRAIN, "src/fresh.c",
                   "virtual observation", "VirtualOnlyMemory");
  uint64_t reserved_id =
      admit_string(context, C_SOURCE, C_DEV, absolute, "reserved observation",
                   "ReservedAliasMemory");
  uint64_t proposal_id =
      admit_string(context, C_LLM_PROPOSAL, C_TRAIN, absolute,
                   "proposal observation", "ProposalAliasMemory");
  uint64_t excluded_id =
      admit_string(context, C_SOURCE, C_TRAIN, excluded,
                   "trusted raw observation", "ExcludedAliasOriginal");
  c_record old = find_path(context, absolute, 1);
  fixture_write(&value, "src/fresh.c", dirty, sizeof(dirty));
  fixture_string(&value, "research/heldout.c", "ExcludedAliasChanged");
  CHECK(c_context_scan(context, value.root, &report) == C_OK);
  CHECK(report.admitted == 3 + case_alias && report.unchanged == 0 &&
        report.failed == 0 && report.excluded == 1 &&
        report.bytes == sizeof(dirty));
  size_t alias_versions = 0;
  for (size_t i = 0; i < c_context_count(context); ++i) {
    c_record record;
    CHECK(c_context_record(context, i, &record) == C_OK);
    if (record.id == old.id)
      CHECK(!record.current && record.version == 1 &&
            record.length == sizeof(original) - 1 &&
            !memcmp(record.bytes, original, record.length));
    if (record.current && record.kind == C_SOURCE && record.split == C_TRAIN &&
        (!strcmp(record.attribution, "captured absolute") ||
         !strcmp(record.attribution, "captured mixed") ||
         !strcmp(record.attribution, "captured case variant"))) {
      CHECK(record.version == 2 && record.length == sizeof(dirty) &&
            !memcmp(record.bytes, dirty, sizeof(dirty)));
      CHECK(!strcmp(record.path,
                    !strcmp(record.attribution, "captured absolute") ? absolute
                    : !strcmp(record.attribution, "captured mixed")
                        ? mixed
                        : case_variant));
      ++alias_versions;
    }
    if (record.id == virtual_id || record.id == reserved_id ||
        record.id == proposal_id || record.id == excluded_id)
      CHECK(record.current && record.version == 1);
  }
  CHECK(alias_versions == 2 + case_alias &&
        c_context_count(context) == 9 + 2 * case_alias);
  assert_abstains(context, "OnlyAbsoluteOriginal");
  size_t before = c_context_count(context);
  CHECK(c_context_scan(context, value.root, &report) == C_OK);
  CHECK(report.admitted == 0 && report.unchanged == 3 + case_alias &&
        report.failed == 0 && report.excluded == 1 &&
        report.bytes == sizeof(dirty) && c_context_count(context) == before);
  c_context_destroy(context);
  fixture_remove(&value, "src/fresh.c");
  fixture_remove(&value, "research/heldout.c");
  path_join(path, value.root, "src");
  remove_directory(path);
  path_join(path, value.root, "research");
  remove_directory(path);
  remove_directory(value.root);
}

static void reject_reserved_file(c_context *context, const fixture *value,
                                 const char *relative) {
  char path[TEST_PATH_BYTES];
  path_join(path, value->root, relative);
  size_t count = c_context_count(context);
  for (unsigned kind = C_SOURCE; kind <= C_ACTIVITY; ++kind) {
    uint64_t id = 999;
    CHECK(c_context_admit_file(context, (c_record_kind)kind, C_TRAIN, path,
                               "explicit import", &id) == C_INVALID);
    CHECK(id == 999 && c_context_count(context) == count);
  }
}

static void reject_reserved_root(c_context *context, const fixture *value,
                                 const char *relative) {
  char path[TEST_PATH_BYTES];
  c_scan_report report;
  path_join(path, value->root, relative);
  CHECK(c_context_scan(context, path, &report) == C_INVALID);
  CHECK(report.failed == 1 && report.admitted == 0 && report.unchanged == 0 &&
        report.bytes == 0);
}

static void reserved_directories(const fixture *value) {
  fixture_directory(value, "data");
  fixture_directory(value, "data/audit");
  fixture_directory(value, "data/development");
  fixture_directory(value, "data/train");
  fixture_directory(value, "tests");
  fixture_directory(value, "tests/fixtures");
  fixture_directory(value, "audit");
  fixture_directory(value, "runs");
}

static void cleanup_reserved_fixture(const fixture *value) {
  static const char *const files[] = {
      "data/audit/heldout.h", "data/development/reserved.h",
      "data/train/owned.c",   "tests/fixtures/reference.c",
      "audit/direct.h",       "runs/raw.log"};
  static const char *const directories[] = {
      "data/audit",     "data/development",
      "data/train",     "data",
      "tests/fixtures", "tests",
      "audit",          "runs"};
  char path[TEST_PATH_BYTES];
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
    fixture_remove(value, files[i]);
  for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
    path_join(path, value->root, directories[i]);
    remove_directory(path);
  }
  remove_directory(value->root);
}

#ifdef _WIN32
static void test_short_reserved_file(c_context *context, const fixture *value) {
  char development[TEST_PATH_BYTES], short_path[TEST_PATH_BYTES];
  char file[TEST_PATH_BYTES];
  path_join(development, value->root, "data/development");
  DWORD length = GetShortPathNameA(development, short_path, TEST_PATH_BYTES);
  if (!length || length >= TEST_PATH_BYTES)
    return;
  const char *last = strrchr(short_path, '\\');
  if (!last)
    last = strrchr(short_path, '/');
  if (!last || !_stricmp(last + 1, "development"))
    return;
  path_join(file, short_path, "reserved.h");
  size_t count = c_context_count(context);
  for (unsigned kind = C_SOURCE; kind <= C_ACTIVITY; ++kind) {
    uint64_t id = 999;
    CHECK(c_context_admit_file(context, (c_record_kind)kind, C_TRAIN, file,
                               "short evaluation name", &id) == C_INVALID);
    CHECK(id == 999 && c_context_count(context) == count);
  }
  c_scan_report report;
  CHECK(c_context_scan(context, short_path, &report) == C_INVALID &&
        report.failed == 1 && report.admitted == 0 && report.unchanged == 0);
}
#endif

static void test_explicit_reserved_files(void) {
  fixture value;
  c_context *context = NULL;
  unsigned char *before = NULL, *after = NULL;
  size_t before_length = 0, after_length = 0;
  create_fixture_root(&value);
  reserved_directories(&value);
  fixture_string(&value, "data/audit/heldout.h", "KnownReservedAuditToken");
  fixture_string(&value, "data/development/reserved.h",
                 "KnownReservedDevelopmentToken");
  fixture_string(&value, "tests/fixtures/reference.c",
                 "KnownReservedFixtureToken");
  fixture_string(&value, "audit/direct.h", "NestedReservedAuditToken");
  fixture_string(&value, "data/train/owned.c", "ExplicitTrainAllowedToken");
  fixture_string(&value, "runs/raw.log", "OrdinaryRawRunLogToken");
  CHECK(c_context_create(&context) == C_OK);
  (void)admit_string(context, C_SOURCE, C_TRAIN, "baseline.c", "trusted caller",
                     "BaselineContextToken");
  CHECK(c_context_pack(context, &before, &before_length) == C_OK);
  reject_reserved_file(context, &value, "data/audit/heldout.h");
  reject_reserved_file(context, &value, "data/development/reserved.h");
  reject_reserved_file(context, &value, "tests/fixtures/reference.c");
  reject_reserved_file(context, &value, "audit/direct.h");
  reject_reserved_file(context, &value, "data/train/../audit/heldout.h");
  reject_reserved_root(context, &value, "data/audit");
  reject_reserved_root(context, &value, "data/development");
  reject_reserved_root(context, &value, "tests/fixtures");
#ifdef _WIN32
  test_short_reserved_file(context, &value);
#endif
  CHECK(c_context_pack(context, &after, &after_length) == C_OK);
  CHECK(before_length == after_length &&
        memcmp(before, after, before_length) == 0);
  free(before);
  free(after);
  char path[TEST_PATH_BYTES];
  uint64_t id = 999;
  path_join(path, value.root, "data/audit/heldout.h");
  CHECK(c_context_admit_file(context, C_AUDIT, C_HOLDOUT, path,
                             "explicit audit", &id) == C_OK &&
        id == 2);
  path_join(path, value.root, "data/development/reserved.h");
  CHECK(c_context_admit_file(context, C_DEVELOPMENT, C_DEV, path,
                             "explicit development", &id) == C_OK &&
        id == 3);
  assert_abstains(context, "KnownReservedAuditToken");
  assert_abstains(context, "KnownReservedDevelopmentToken");
  path_join(path, value.root, "data/train/owned.c");
  CHECK(c_context_admit_file(context, C_SOURCE, C_TRAIN, path, "explicit train",
                             &id) == C_OK);
  assert_answer(context, "ExplicitTrainAllowedToken", id);
  path_join(path, value.root, "runs/raw.log");
  CHECK(c_context_admit_file(context, C_ACTIVITY, C_TRAIN, path,
                             "captured work", &id) == C_OK);
  assert_answer(context, "OrdinaryRawRunLogToken", id);
  /* Raw admission cannot infer a physical file from caller-declared metadata.
   */
  id = admit_string(context, C_SOURCE, C_TRAIN, "data/audit/caller-declared.c",
                    "raw caller split contract", "CallerDeclaredInputToken");
  assert_answer(context, "CallerDeclaredInputToken", id);
  c_context_destroy(context);
  cleanup_reserved_fixture(&value);
}

static void cleanup_links(const fixture *value) {
  char path[TEST_PATH_BYTES];
  if (value->file_link)
    fixture_remove(value, "src/link.py");
  if (value->directory_link) {
    path_join(path, value->root, "linked-source");
#ifdef _WIN32
    remove_directory(path);
#else
    CHECK(remove(path) == 0);
#endif
  }
}

static void cleanup_fixture(const fixture *value) {
  char relative[128], path[TEST_PATH_BYTES];
  cleanup_links(value);
  fixture_remove(value, "README.md");
  fixture_remove(value, "empty.py");
  fixture_remove(value, "src/upper.PY");
  fixture_remove(value, "src/execute.py");
  for (size_t i = 0; i < sizeof(language_files) / sizeof(language_files[0]);
       ++i) {
    CHECK(snprintf(relative, sizeof(relative), "src/%s", language_files[i]) >
          0);
    fixture_remove(value, relative);
  }
  path_join(path, value->root, "src");
  remove_directory(path);
  for (size_t i = 0; i < sizeof(excluded_files) / sizeof(excluded_files[0]);
       ++i)
    fixture_remove(value, excluded_files[i]);
  for (size_t i = 0;
       i < sizeof(excluded_directories) / sizeof(excluded_directories[0]);
       ++i) {
    CHECK(snprintf(relative, sizeof(relative), "%s/heldout.py",
                   excluded_directories[i]) > 0);
    fixture_remove(value, relative);
    path_join(path, value->root, excluded_directories[i]);
    remove_directory(path);
  }
  remove_directory(value->root);
}

int main(void) {
  c_context *context = test_versions();
  test_quarantine(context);
  test_kind_retrieval();
  test_limits();
  fixture value;
  create_fixture_root(&value);
  fixture_sources(&value);
  fixture_exclusions(&value);
  test_scan(&value);
  test_canonical(context);
  test_checkpoint(context, &value);
  test_audit_header_boundary();
  test_native_model_scan();
  test_scan_absolute_aliases();
  test_explicit_reserved_files();
  cleanup_fixture(&value);
  c_context_destroy(context);
  puts("Exact native context, provenance, quarantine, scanner and checkpoint "
       "checks passed");
  return 0;
}
