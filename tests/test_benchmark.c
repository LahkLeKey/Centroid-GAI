#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
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
      fprintf(stderr, "benchmark check failed %s:%d: %s\n", __FILE__,          \
              __LINE__, #value);                                               \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define TEST_PATH 4096u

typedef struct {
  size_t count[3][3], correct[3][3], eos[3];
  double loss[3][3];
} raw_results;

static unsigned long process_id(void) {
#ifdef _WIN32
  return (unsigned long)_getpid();
#else
  return (unsigned long)getpid();
#endif
}

static void path_join(char path[TEST_PATH], const char *root,
                      const char *name) {
  int length = snprintf(path, TEST_PATH, "%s/%s", root, name);
  CHECK(length > 0 && (size_t)length < TEST_PATH);
}

static void remove_directory(const char *path) {
#ifdef _WIN32
  CHECK(_rmdir(path) == 0);
#else
  CHECK(rmdir(path) == 0);
#endif
}

static char *read_artifact(const char *directory, const char *name) {
  char path[TEST_PATH];
  unsigned char *bytes = NULL;
  size_t length = 0;
  path_join(path, directory, name);
  CHECK(c_read_file(path, &bytes, &length) == C_OK);
  char *text = malloc(length + 1);
  CHECK(text != NULL);
  memcpy(text, bytes, length);
  text[length] = '\0';
  free(bytes);
  return text;
}

static double metric(const char *directory, const char *name) {
  char path[TEST_PATH], line[512];
  path_join(path, directory, "metrics.tsv");
  FILE *file = fopen(path, "rb");
  CHECK(file != NULL);
  size_t length = strlen(name);
  while (fgets(line, sizeof(line), file) != NULL) {
    if (strncmp(line, name, length) == 0 && line[length] == '\t') {
      char *end = NULL;
      double result = strtod(line + length + 1, &end);
      CHECK(end != line + length + 1 && isfinite(result));
      CHECK(fclose(file) == 0);
      return result;
    }
  }
  CHECK(0);
  return 0;
}

static unsigned condition_index(const char *name) {
  if (strcmp(name, "INITIAL") == 0)
    return 0;
  if (strcmp(name, "LIFE_TRAINED") == 0)
    return 1;
  CHECK(strcmp(name, "FROZEN") == 0);
  return 2;
}

static unsigned split_index(const char *name) {
  if (strcmp(name, "TRAIN") == 0)
    return 0;
  if (strcmp(name, "AUDIT") == 0)
    return 1;
  CHECK(strcmp(name, "RETENTION") == 0);
  return 2;
}

static char *next_field(void) {
  char *field = strtok(NULL, "\t\r\n");
  CHECK(field != NULL);
  return field;
}

static double number(const char *text) {
  char *end = NULL;
  double value = strtod(text, &end);
  CHECK(end != text && *end == '\0' && isfinite(value));
  return value;
}

static void parse_prediction(char *line, raw_results *results) {
  char *field = strtok(line, "\t\r\n");
  CHECK(field != NULL);
  unsigned condition = condition_index(field),
           split = split_index(next_field());
  const char *family = next_field();
  CHECK(strcmp(family, "range") == 0 || strcmp(family, "clamp") == 0 ||
        strcmp(family, "counted-loop") == 0 ||
        strcmp(family, "array-lookup") == 0 ||
        strcmp(family, "retention-range") == 0 ||
        strcmp(family, "retention-clamp") == 0);
  const char *head = next_field();
  int text = strcmp(head, "TEXT") == 0;
  CHECK(text || strcmp(head, "CODE") == 0);
  CHECK((text && split < 2) || (!text && split == 2));
  (void)number(next_field());
  unsigned target = (unsigned)number(next_field()),
           predicted = (unsigned)number(next_field());
  double target_probability = number(next_field()), loss = number(next_field());
  unsigned actions = text ? C_TEXT_ACTIONS : C_CODE_ACTIONS, maximum = 0;
  double sum = 0, maximum_value = -1, actual_target = 0;
  CHECK(target < actions && predicted < actions);
  for (unsigned action = 0; action < C_TEXT_ACTIONS; ++action) {
    const char *probability = next_field();
    if (action >= actions) {
      CHECK(strcmp(probability, "NA") == 0);
      continue;
    }
    double value = number(probability);
    CHECK(value >= 0 && value <= 1);
    sum += value;
    if (value > maximum_value) {
      maximum = action;
      maximum_value = value;
    }
    if (action == target)
      actual_target = value;
  }
  CHECK(strtok(NULL, "\t\r\n") == NULL && fabs(sum - 1) < 1e-12);
  CHECK(maximum == predicted && actual_target == target_probability &&
        actual_target > 0);
  CHECK(fabs(loss + log(actual_target)) < 1e-12);
  ++results->count[condition][split];
  results->correct[condition][split] += target == predicted ? 1u : 0u;
  results->loss[condition][split] += loss;
  results->eos[condition] += target == C_EOS ? 1u : 0u;
}

static raw_results read_predictions(const char *directory) {
  char path[TEST_PATH], line[8192];
  raw_results results = {0};
  path_join(path, directory, "predictions.tsv");
  FILE *file = fopen(path, "rb");
  CHECK(file != NULL);
  CHECK(fgets(line, sizeof(line), file) != NULL &&
        strncmp(line, "condition\tsplit\tfamily\thead\toffset", 34) == 0);
  while (fgets(line, sizeof(line), file) != NULL) {
    CHECK(strchr(line, '\n') != NULL);
    parse_prediction(line, &results);
  }
  CHECK(!ferror(file) && fclose(file) == 0);
  for (unsigned condition = 0; condition < 3; ++condition)
    CHECK(results.count[condition][0] == 16 &&
          results.count[condition][1] == 16 &&
          results.count[condition][2] == 2 && results.eos[condition] == 4);
  return results;
}

static void test_reported_denominators(const char *directory,
                                       const raw_results *results) {
  CHECK(metric(directory, "train_cases") == 16 &&
        metric(directory, "audit_cases") == 16);
  CHECK(metric(directory, "conditions") == 3 &&
        metric(directory, "text_rows") == 96);
  CHECK(metric(directory, "code_retention_cases") == 2 &&
        metric(directory, "code_rows") == 6);
  static const char *const conditions[] = {"initial", "trained", "frozen"};
  static const char *const splits[] = {"train", "audit", "code"};
  char name[128];
  for (unsigned condition = 0; condition < 3; ++condition)
    for (unsigned split = 0; split < 3; ++split) {
      CHECK(snprintf(name, sizeof(name), "%s_%s_loss", splits[split],
                     conditions[condition]) > 0);
      double expected = results->loss[condition][split] /
                        (double)results->count[condition][split];
      CHECK(fabs(metric(directory, name) - expected) < 1e-12);
      CHECK(snprintf(name, sizeof(name), "%s_%s_accuracy", splits[split],
                     conditions[condition]) > 0);
      expected = (double)results->correct[condition][split] /
                 (double)results->count[condition][split];
      CHECK(metric(directory, name) == expected);
    }
  CHECK(metric(directory, "train_initial_loss") ==
        metric(directory, "train_frozen_loss"));
  CHECK(metric(directory, "audit_initial_loss") ==
        metric(directory, "audit_frozen_loss"));
  CHECK(metric(directory, "code_initial_loss") ==
        metric(directory, "code_frozen_loss"));
}

static void test_quality_and_costs(const char *directory) {
  CHECK(metric(directory, "source_updates") == 64 &&
        metric(directory, "source_updates_budget") == 64);
  CHECK(metric(directory, "contacts") > 0 &&
        metric(directory, "generations") >= 64 &&
        metric(directory, "generations") <= 1024);
  CHECK(metric(directory, "group_0_updates") == 64 &&
        metric(directory, "group_1_updates") == 64);
  CHECK(metric(directory, "changed_train_outputs") == 16);
  CHECK(metric(directory, "train_trained_loss") <
        metric(directory, "train_initial_loss"));
  CHECK(metric(directory, "train_loss_gain") > 0);
  CHECK(metric(directory, "source_context_records") == 2 &&
        metric(directory, "audit_context_records") == 0);
  CHECK(metric(directory, "code_head_weights_unchanged") == 1);
  CHECK(metric(directory, "checkpoint_bytes") > 0 &&
        metric(directory, "model_fixed_bound_bytes") == sizeof(c_model));
  CHECK(metric(directory, "resident_model_pair_bytes") == 2 * sizeof(c_model));
  CHECK(metric(directory, "training_ms") >= 0 &&
        metric(directory, "total_ms") >= metric(directory, "training_ms"));
  char *protocol = read_artifact(directory, "protocol.md"),
       *report = read_artifact(directory, "report.md");
  CHECK(strstr(protocol, "published before any AUDIT prediction") != NULL);
  CHECK(strstr(protocol, "exactly 64 source task updates") != NULL);
  CHECK(strstr(report, "Life optimality") != NULL &&
        strstr(report, "remain pending") != NULL);
  CHECK(strstr(report, "negative means deterioration") != NULL);
  free(protocol);
  free(report);
}

static void test_checkpoint_quarantine(const char *directory) {
  char path[TEST_PATH];
  c_trainer *initial = NULL, *trained = NULL;
  path_join(path, directory, "initial.checkpoint");
  CHECK(c_trainer_load(path, &initial) == C_OK);
  path_join(path, directory, "trained.checkpoint");
  CHECK(c_trainer_load(path, &trained) == C_OK);
  CHECK(c_context_count(c_trainer_context(trained)) == 2);
  for (size_t i = 0; i < 2; ++i) {
    c_record record = {0};
    CHECK(c_context_record(c_trainer_context(trained), i, &record) == C_OK);
    CHECK(record.kind == C_SOURCE && record.split == C_TRAIN &&
          record.current && record.version == 1);
    CHECK(strncmp(record.path, "train/", 6) == 0);
  }
  c_record result = {0};
  double score = -1;
  CHECK(c_context_retrieve(c_trainer_context(trained),
                           (const unsigned char *)"sum_count", 9, &result,
                           &score) == C_NOT_FOUND);
  CHECK(c_context_retrieve(c_trainer_context(trained),
                           (const unsigned char *)"lookup_value", 12, &result,
                           &score) == C_NOT_FOUND);
  for (unsigned group = 0; group < 2; ++group) {
    char before[C_DIGEST_HEX], after[C_DIGEST_HEX];
    CHECK(c_model_fingerprint(c_trainer_model(initial), group, before) == C_OK);
    CHECK(c_model_fingerprint(c_trainer_model(trained), group, after) == C_OK);
    CHECK(strcmp(before, after) != 0 &&
          c_model_group_clock(c_trainer_model(trained), group) == 64);
  }
  c_trainer_destroy(initial);
  c_trainer_destroy(trained);
}

static void test_immutable_output(const char *directory) {
  char before[C_DIGEST_HEX], after[C_DIGEST_HEX];
  char *report = read_artifact(directory, "report.md");
  c_hash(report, strlen(report), before);
  free(report);
  CHECK(c_benchmark_run(directory) == C_INVALID);
  report = read_artifact(directory, "report.md");
  c_hash(report, strlen(report), after);
  free(report);
  CHECK(strcmp(before, after) == 0);
}

static void cleanup(const char *directory) {
  static const char *const files[] = {
      "protocol.md",         "predictions.tsv",    "metrics.tsv",
      "report.md",           "initial.checkpoint", "trained.checkpoint",
      "train/range.c",       "train/clamp.c",      "audit/counted-loop.c",
      "audit/array-lookup.c"};
  char path[TEST_PATH];
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
    path_join(path, directory, files[i]);
    CHECK(remove(path) == 0);
  }
  path_join(path, directory, "train");
  remove_directory(path);
  path_join(path, directory, "audit");
  remove_directory(path);
  remove_directory(directory);
}

int main(void) {
  char directory[128];
  CHECK(snprintf(directory, sizeof(directory), "test-source-benchmark-%lu",
                 process_id()) > 0);
  CHECK(c_benchmark_run(NULL) == C_INVALID && c_benchmark_run("") == C_INVALID);
  CHECK(c_benchmark_run(directory) == C_OK);
  raw_results results = read_predictions(directory);
  test_reported_denominators(directory, &results);
  test_quality_and_costs(directory);
  test_checkpoint_quarantine(directory);
  test_immutable_output(directory);
  cleanup(directory);
  puts("Native fixed source benchmark: all predictions, gains, contacts, "
       "quarantine and immutable outputs passed");
  return 0;
}
