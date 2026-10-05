#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../data/audit/research_fixtures.h"
#include "internal.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CHECK(value)                                                           \
  do {                                                                         \
    if (!(value)) {                                                            \
      fprintf(stderr, "research check failed %s:%d: %s\n", __FILE__, __LINE__, \
              #value);                                                         \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define T_CONDITIONS 13u
#define T_SEEDS 3u
#define T_PATH 4096u

static const char *const condition_names[] = {"LIFE",
                                              "FROZEN_LIFE",
                                              "ROUND_ROBIN",
                                              "SINGLE_EXPERT",
                                              "FOUR_SPECIALISTS",
                                              "PAIR_SUBWORDS",
                                              "SOURCE_ONLY",
                                              "SOURCE_LLM",
                                              "SOURCE_ACTIVITY",
                                              "SOURCE_LLM_ACTIVITY",
                                              "FROZEN_NO_LEARNING",
                                              "UNIFORM_ROUTING",
                                              "PREFIX_ELIGIBILITY"};
static const uint64_t seeds[] = {113, 271, 659};
typedef struct {
  const char *file;
  const unsigned char *bytes;
  size_t length;
} fixture;
static const fixture fixtures[] = {
    {"train/binary-search.c", r_train_search, sizeof(r_train_search) - 1},
    {"train/byte-reversal.c", r_train_reverse, sizeof(r_train_reverse) - 1},
    {"development/byte-count.c", r_dev_count, sizeof(r_dev_count) - 1},
    {"development/insertion-sort.c", r_dev_insert, sizeof(r_dev_insert) - 1},
    {"audit/ring-buffer.c", r_audit_ring, sizeof(r_audit_ring) - 1},
    {"audit/unsigned-gcd.c", r_audit_gcd, sizeof(r_audit_gcd) - 1}};
typedef struct {
  size_t count, correct, eos, rollout;
  double loss;
} summary;
static summary observed[T_CONDITIONS][T_SEEDS][2][4];
static size_t input_rows[T_CONDITIONS][T_SEEDS];

static unsigned long process_id(void) {
#ifdef _WIN32
  return (unsigned long)_getpid();
#else
  return (unsigned long)getpid();
#endif
}
static void path_join(char path[T_PATH], const char *root, const char *name) {
  int count = snprintf(path, T_PATH, "%s/%s", root, name);
  CHECK(count > 0 && (size_t)count < T_PATH);
}
static FILE *open_artifact(const char *root, const char *name) {
  char path[T_PATH];
  path_join(path, root, name);
  FILE *file = fopen(path, "rb");
  CHECK(file != NULL);
  return file;
}
static char *read_artifact(const char *root, const char *name) {
  char path[T_PATH];
  unsigned char *bytes = NULL;
  size_t length = 0;
  path_join(path, root, name);
  CHECK(c_read_file(path, &bytes, &length) == C_OK);
  char *result = malloc(length + 1);
  CHECK(result != NULL);
  memcpy(result, bytes, length);
  result[length] = '\0';
  free(bytes);
  return result;
}
static double number(const char *text) {
  char *end;
  double value = strtod(text, &end);
  CHECK(end != text && *end == '\0' && isfinite(value));
  return value;
}
static char *field(void) {
  char *result = strtok(NULL, "\t\r\n");
  CHECK(result != NULL);
  return result;
}
static unsigned condition(const char *text) {
  for (unsigned i = 0; i < T_CONDITIONS; ++i)
    if (strcmp(text, condition_names[i]) == 0)
      return i;
  CHECK(0);
  return 0;
}
static unsigned seed_index(const char *text) {
  uint64_t value = (uint64_t)number(text);
  for (unsigned i = 0; i < T_SEEDS; ++i)
    if (seeds[i] == value)
      return i;
  CHECK(0);
  return 0;
}
static unsigned split_index(const char *text) {
  const char *splits[] = {"TRAIN", "DEV", "AUDIT", "RETENTION"};
  for (unsigned i = 0; i < 4; ++i)
    if (strcmp(text, splits[i]) == 0)
      return i;
  CHECK(0);
  return 0;
}
static void parse_predictions(const char *root) {
  FILE *file = open_artifact(root, "predictions.tsv");
  char line[16384];
  CHECK(fgets(line, sizeof(line), file) != NULL &&
        strstr(line, "condition\tseed\tphase\tprobe\tsplit") == line);
  size_t rows = 0;
  while (fgets(line, sizeof(line), file)) {
    CHECK(strchr(line, '\n') != NULL);
    unsigned c = condition(strtok(line, "\t\r\n"));
    unsigned seed = seed_index(field());
    char *phase_name = field();
    unsigned phase = strcmp(phase_name, "TRAINED") == 0;
    CHECK(phase || strcmp(phase_name, "INITIAL") == 0);
    char *probe = field();
    unsigned rollout = strcmp(probe, "ROLLOUT") == 0;
    CHECK(rollout || strcmp(probe, "SELECTED") == 0 ||
          strcmp(probe, "CAUSAL_TAIL") == 0 || strcmp(probe, "RETENTION") == 0);
    unsigned split = split_index(field());
    (void)field(); /* family */
    size_t offset = (size_t)number(field());
    unsigned target = (unsigned)number(field());
    unsigned predicted = (unsigned)number(field());
    unsigned eligible = (unsigned)number(field());
    double target_probability = number(field()), loss = number(field());
    unsigned actions = split == 3 ? C_CODE_ACTIONS : C_TEXT_ACTIONS;
    double sum = 0, maximum = -1, actual_target = 0;
    unsigned argmax = 0;
    CHECK(target < actions && predicted < actions && eligible > 0 &&
          eligible < 16 && offset < 1024);
    for (unsigned action = 0; action < C_TEXT_ACTIONS; ++action) {
      const char *text = field();
      if (action >= actions) {
        CHECK(strcmp(text, "NA") == 0);
        continue;
      }
      double value = number(text);
      CHECK(value > 0 && value <= 1);
      sum += value;
      if (value > maximum) {
        maximum = value;
        argmax = action;
      }
      if (action == target)
        actual_target = value;
    }
    CHECK(strtok(NULL, "\t\r\n") == NULL && fabs(sum - 1) < 1e-12 &&
          predicted == argmax && target_probability == actual_target &&
          fabs(loss + log(actual_target)) < 1e-12);
    summary *result = &observed[c][seed][phase][split];
    if (rollout) {
      ++result->rollout;
    } else {
      ++result->count;
      result->correct += predicted == target;
      result->loss += loss;
      result->eos += target == C_EOS;
    }
    ++rows;
  }
  CHECK(!ferror(file) && fclose(file) == 0);
  CHECK(rows == 19404);
  for (unsigned c = 0; c < T_CONDITIONS; ++c)
    for (unsigned seed = 0; seed < T_SEEDS; ++seed)
      for (unsigned phase = c >= 11 ? 1u : 0u; phase < 2; ++phase) {
        for (unsigned split = 0; split < 3; ++split) {
          CHECK(observed[c][seed][phase][split].count == 58 &&
                observed[c][seed][phase][split].eos == 4);
          CHECK(observed[c][seed][phase][split].rollout ==
                (c >= 11 ? 0u : 34u));
        }
        CHECK(observed[c][seed][phase][3].count == 2 &&
              observed[c][seed][phase][3].eos == 0);
      }
}
static void test_tokenizer(const char *root) {
  unsigned short pairs[32];
  FILE *file = open_artifact(root, "dictionary.tsv");
  char line[256];
  CHECK(fgets(line, sizeof(line), file) != NULL);
  for (unsigned i = 0; i < 32; ++i) {
    unsigned token, first, second;
    CHECK(fgets(line, sizeof(line), file) != NULL &&
          sscanf(line, "%u\t%u\t%u", &token, &first, &second) == 3 &&
          token == 256 + i && first < 256 && second < 256);
    pairs[i] = (unsigned short)(first * 256u + second);
  }
  CHECK(fgets(line, sizeof(line), file) == NULL && fclose(file) == 0);
  size_t *counts = calloc(65536u, sizeof(*counts));
  CHECK(counts != NULL);
  for (unsigned family = 0; family < 2; ++family)
    for (size_t i = 1; i < fixtures[family].length; ++i)
      ++counts[(unsigned)fixtures[family].bytes[i - 1] * 256u +
               fixtures[family].bytes[i]];
  for (unsigned token = 0; token < 32; ++token) {
    CHECK(counts[pairs[token]] >= 2);
    for (size_t pair = 0; pair < 65536u; ++pair)
      CHECK(counts[pair] < counts[pairs[token]] ||
            (counts[pair] == counts[pairs[token]] && pair >= pairs[token]));
    counts[pairs[token]] = 0;
  }
  free(counts);
  for (unsigned family = 0; family < 6; ++family) {
    char name[128], path[T_PATH];
    CHECK(snprintf(name, sizeof(name), "%s.tokens", fixtures[family].file) > 0);
    path_join(path, root, name);
    unsigned char *wire = NULL;
    size_t length = 0, decoded = 0;
    CHECK(c_read_file(path, &wire, &length) == C_OK && length % 2 == 0);
    for (size_t i = 0; i < length; i += 2) {
      unsigned token = wire[i] | ((unsigned)wire[i + 1] << 8u);
      CHECK(token < 288);
      if (token < 256) {
        CHECK(decoded < fixtures[family].length &&
              fixtures[family].bytes[decoded++] == token);
      } else {
        unsigned pair = pairs[token - 256];
        CHECK(fixtures[family].length - decoded >= 2 &&
              fixtures[family].bytes[decoded++] == (pair >> 8u) &&
              fixtures[family].bytes[decoded++] == (pair & 255u));
      }
    }
    CHECK(decoded == fixtures[family].length && length / 2 < decoded);
    free(wire);
  }
}
static void test_inputs(const char *root) {
  FILE *file = open_artifact(root, "inputs.tsv");
  char line[4096];
  CHECK(fgets(line, sizeof(line), file) != NULL);
  size_t rows = 0;
  while (fgets(line, sizeof(line), file)) {
    CHECK(strchr(line, '\n') != NULL);
    unsigned c = condition(strtok(line, "\t\r\n"));
    unsigned seed = seed_index(field());
    CHECK(c < 10 && number(field()) < 4);
    const char *family = field();
    unsigned f = strcmp(family, "binary-search") == 0 ? 0u : 1u;
    CHECK(f == 0 || strcmp(family, "byte-reversal") == 0);
    size_t offset = (size_t)number(field());
    unsigned target = (unsigned)number(field());
    unsigned eligible = (unsigned)number(field());
    CHECK(offset <= fixtures[f].length &&
          target == (offset == fixtures[f].length ? C_EOS
                                                  : fixtures[f].bytes[offset]));
    CHECK(eligible == (c == 3 ? 1u : c == 4 ? 15u : 3u));
    double input[C_FEATURES], expected[C_FEATURES], norm = 0;
    for (unsigned i = 0; i < C_FEATURES; ++i) {
      input[i] = number(field());
      norm += input[i] * input[i];
    }
    CHECK(strtok(NULL, "\t\r\n") == NULL && fabs(norm - 1) < 1e-12);
    if (c <= 4) {
      CHECK(c_encode(fixtures[f].bytes, offset, expected) == C_OK &&
            memcmp(input, expected, sizeof(input)) == 0);
    }
    ++input_rows[c][seed];
    ++rows;
  }
  CHECK(!ferror(file) && fclose(file) == 0 && rows == 2880);
  for (unsigned c = 0; c < 10; ++c)
    for (unsigned seed = 0; seed < T_SEEDS; ++seed)
      CHECK(input_rows[c][seed] == 96);
}
static void test_metrics(const char *root) {
  FILE *file = open_artifact(root, "metrics.tsv");
  char header[8192], line[8192];
  CHECK(fgets(header, sizeof(header), file) != NULL);
  char *names[128];
  size_t columns = 0;
  for (char *name = strtok(header, "\t\r\n"); name;
       name = strtok(NULL, "\t\r\n")) {
    CHECK(columns < 128);
    names[columns++] = name;
  }
  size_t rows = 0;
  while (fgets(line, sizeof(line), file)) {
    char *values[128];
    size_t n = 0;
    for (char *value = strtok(line, "\t\r\n"); value;
         value = strtok(NULL, "\t\r\n")) {
      CHECK(n < 128);
      values[n++] = value;
    }
    CHECK(n == columns);
    unsigned c = condition(values[0]), seed = seed_index(values[1]);
    CHECK(strcmp(values[2], "ok") == 0);
    for (size_t column = 3; column < columns; ++column) {
      double actual = number(values[column]);
      const char *name = names[column];
      if (strcmp(name, "budget") == 0 || strcmp(name, "updates") == 0)
        CHECK(actual == (c < 10 ? 96 : 0));
      else if (strcmp(name, "context_records") == 0)
        CHECK(actual == 6);
      else if (strcmp(name, "dev_audit_records") == 0)
        CHECK(actual == 0);
      else if (strcmp(name, "code_unchanged") == 0)
        CHECK(actual == 1);
      else if (strcmp(name, "process_peak_bytes") == 0)
        CHECK(actual > 0);
      else if (strcmp(name, "owner0") == 0 || strcmp(name, "owner1") == 0) {
        if (c <= 3 || (c >= 5 && c <= 9))
          CHECK(actual == (c == 3 && strcmp(name, "owner1") == 0 ? 0 : 96));
      }
      for (unsigned phase = 0; phase < 2; ++phase)
        for (unsigned split = 0; split < 4; ++split) {
          const char *split_names[] = {"train", "dev", "audit", "retention"};
          char expected_name[128];
          const summary *r = &observed[c][seed][phase][split];
          CHECK(snprintf(expected_name, sizeof(expected_name), "%s_%s_cases",
                         phase ? "trained" : "initial",
                         split_names[split]) > 0);
          if (strcmp(name, expected_name) == 0)
            CHECK(actual == (double)r->count);
          CHECK(snprintf(expected_name, sizeof(expected_name), "%s_%s_correct",
                         phase ? "trained" : "initial",
                         split_names[split]) > 0);
          if (strcmp(name, expected_name) == 0)
            CHECK(actual == (double)r->correct);
          CHECK(snprintf(expected_name, sizeof(expected_name), "%s_%s_loss",
                         phase ? "trained" : "initial",
                         split_names[split]) > 0);
          if (strcmp(name, expected_name) == 0)
            CHECK(fabs(actual - (r->count ? r->loss / (double)r->count : 0)) <
                  1e-12);
        }
    }
    ++rows;
  }
  CHECK(!ferror(file) && fclose(file) == 0 && rows == 39);
  for (unsigned seed = 0; seed < T_SEEDS; ++seed)
    for (unsigned split = 0; split < 4; ++split) {
      CHECK(observed[10][seed][0][split].loss ==
            observed[10][seed][1][split].loss);
      CHECK(observed[0][seed][0][split].loss ==
            observed[1][seed][0][split].loss);
      CHECK(observed[0][seed][0][split].loss ==
            observed[2][seed][0][split].loss);
    }
}
static void test_research_boundary(const char *root) {
  char context_path[T_PATH];
  path_join(context_path, root, "context.pack");
  c_context *context = NULL;
  CHECK(c_context_load(context_path, &context) == C_OK &&
        c_context_count(context) == 6);
  unsigned proposal_count = 0;
  for (size_t i = 0; i < c_context_count(context); ++i) {
    c_record record;
    CHECK(c_context_record(context, i, &record) == C_OK &&
          record.split == C_TRAIN && record.kind != C_AUDIT &&
          record.kind != C_DEVELOPMENT);
    if (record.kind == C_LLM_PROPOSAL) {
      ++proposal_count;
      CHECK(strstr(record.attribution, "unverified") != NULL);
    }
  }
  CHECK(proposal_count == 1);
  c_record found;
  double score;
  CHECK(c_context_retrieve(context,
                           (const unsigned char *)"research_audit_unsigned_gcd",
                           sizeof("research_audit_unsigned_gcd") - 1, &found,
                           &score) == C_NOT_FOUND);
  c_context_destroy(context);
  c_trainer *owner = NULL;
  CHECK(c_trainer_create(1, 113, &owner) == C_INVALID);
  CHECK(c_trainer_research_create(1, 113, &owner) == C_OK);
  const unsigned char source[] = "native exact bytes";
  uint64_t id = 0;
  CHECK(c_context_admit(c_trainer_context(owner), C_SOURCE, C_TRAIN,
                        "train/boundary.c", "test", source, sizeof(source) - 1,
                        &id) == C_OK);
  double input[C_FEATURES];
  CHECK(c_encode(source, 4, input) == C_OK);
  CHECK(c_trainer_enqueue_source(owner, id, 4, 1) == C_INVALID);
  CHECK(c_trainer_research_enqueue(owner, id, 4, 1, input) == C_OK);
  c_training_report report;
  CHECK(c_trainer_step(owner, 1, &report) == C_INVALID);
  CHECK(c_trainer_research_step(owner, 1, 2, &report) == C_OK &&
        report.updates == 1 && report.contacts == 0);
  char path[T_PATH];
  path_join(path, root, "forbidden.checkpoint");
  CHECK(c_trainer_save(owner, path) == C_CORRUPT);
  c_trainer_destroy(owner);
  owner = NULL;
  CHECK(c_trainer_create(2, 113, &owner) == C_OK);
  CHECK(c_trainer_research_step(owner, 1, 2, &report) == C_INVALID);
  c_trainer_destroy(owner);
}
static void remove_directory(const char *path) {
#ifdef _WIN32
  CHECK(_rmdir(path) == 0);
#else
  CHECK(rmdir(path) == 0);
#endif
}
static void remove_artifact(const char *path) {
  uint64_t started = c_monotonic_ms();
  int error = 0;
  do {
    if (remove(path) == 0)
      return;
    error = errno;
    if (error != EACCES && error != EBUSY)
      break;
    /* WSL files can briefly be held by Windows file scanners after a large
     * artifact closes. Retry the exact file only, never recursive deletion. */
  } while (c_monotonic_ms() - started < 2000);
  fprintf(stderr, "cannot remove native test artifact %s: errno=%d\n", path,
          error);
  CHECK(0);
}
static void cleanup(const char *root) {
  const char *files[] = {"protocol.md",     "dictionary.tsv", "fixtures.tsv",
                         "predictions.tsv", "inputs.tsv",     "sequences.tsv",
                         "metrics.tsv",     "summary.tsv",    "report.md",
                         "context.pack"};
  char path[T_PATH], name[128];
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
    path_join(path, root, files[i]);
    remove_artifact(path);
  }
  for (size_t i = 0; i < 6; ++i) {
    path_join(path, root, fixtures[i].file);
    remove_artifact(path);
    CHECK(snprintf(name, sizeof(name), "%s.tokens", fixtures[i].file) > 0);
    path_join(path, root, name);
    remove_artifact(path);
  }
  const char *directories[] = {"train", "development", "audit"};
  for (unsigned i = 0; i < 3; ++i) {
    path_join(path, root, directories[i]);
    remove_directory(path);
  }
  remove_directory(root);
}
int main(void) {
  char root[128];
  CHECK(snprintf(root, sizeof(root), "test-matched-research-%lu",
                 process_id()) > 0);
  CHECK(c_research_run(NULL) == C_INVALID && c_research_run("") == C_INVALID);
  char oversized[3802];
  memset(oversized, 'x', sizeof(oversized));
  oversized[sizeof(oversized) - 1] = '\0';
  CHECK(c_research_run(oversized) == C_LIMIT);
  CHECK(c_research_run(root) == C_OK);
  test_tokenizer(root);
  test_inputs(root);
  parse_predictions(root);
  test_metrics(root);
  test_research_boundary(root);
  char *protocol = read_artifact(root, "protocol.md"),
       *report = read_artifact(root, "report.md");
  CHECK(strstr(protocol,
               "before model creation, fitting, DEV/AUDIT prediction") &&
        strstr(protocol, "96 joint optimizer-update budget") &&
        strstr(report, "FAIL_OR_INCONCLUSIVE") &&
        strstr(report, "No held-out result selects a recipe"));
  char before[C_DIGEST_HEX], after[C_DIGEST_HEX];
  c_hash(report, strlen(report), before);
  CHECK(c_research_run(root) == C_INVALID);
  free(report);
  report = read_artifact(root, "report.md");
  c_hash(report, strlen(report), after);
  CHECK(strcmp(before, after) == 0);
  free(protocol);
  free(report);
  cleanup(root);
  puts("Matched native research: exact budgets, three seeds, every "
       "distribution, "
       "lossless TRAIN-only subwords, independent targets and quarantine "
       "passed");
  return 0;
}
