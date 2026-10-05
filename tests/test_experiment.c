#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
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

static unsigned pid_number(void) {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return (unsigned)getpid();
#endif
}

static unsigned char *read_artifact(const char *directory, const char *name,
                                    size_t *length) {
  char path[PATH_BYTES];
  unsigned char *bytes = NULL;
  path_join(path, directory, name);
  CHECK(c_read_file(path, &bytes, length) == C_OK);
  unsigned char *terminated = realloc(bytes, *length + 1);
  CHECK(terminated);
  terminated[*length] = 0;
  return terminated;
}

#include "contact_contract.h"

static void no_contact_deferred(const char *directory) {
  char output[PATH_BYTES];
  int n = snprintf(output, sizeof(output), "%s-no-contact", directory);
  CHECK(n > 0 && (size_t)n < sizeof(output));
  c_trainer *trainer = NULL;
  CHECK(c_trainer_create(4u, 79u, &trainer) == C_OK);
  CHECK(c_context_admit(trainer->context, C_SOURCE, C_TRAIN, "request.c",
                        "preflight fixture", (const unsigned char *)"range", 5u,
                        NULL) == C_OK);
  memset(trainer->cells, 0, sizeof(trainer->cells));
  c_trainer *before = malloc(sizeof(*before));
  c_model *model = malloc(sizeof(*model));
  CHECK(before && model);
  memcpy(before, trainer, sizeof(*before));
  memcpy(model, trainer->model, sizeof(*model));
  unsigned char *context_before = NULL, *context_after = NULL;
  size_t first = 0, second = 0;
  CHECK(c_context_pack(trainer->context, &context_before, &first) == C_OK);
  CHECK(c_experiment_run(trainer, "centroid-no-such-native-compiler", output) ==
        C_DEFERRED);
  CHECK(!memcmp(before, trainer, sizeof(*before)) &&
        !memcmp(model, trainer->model, sizeof(*model)));
  CHECK(c_context_pack(trainer->context, &context_after, &second) == C_OK &&
        first == second && !memcmp(context_before, context_after, first));
#ifdef _WIN32
  CHECK(GetFileAttributesA(output) == INVALID_FILE_ATTRIBUTES);
#else
  struct stat info;
  CHECK(lstat(output, &info) != 0);
#endif
  /* A partial physical pair cannot authorize shared-representation trials. */
  trainer->cells[0] = 1u;
  trainer->cells[1] = 2u;
  trainer->model->shared_enabled = 1u;
  c_experiment_contact contact, sentinel;
  memset(&contact, 0xa5, sizeof(contact));
  memcpy(&sentinel, &contact, sizeof(sentinel));
  CHECK(c_experiment_contact_preflight(trainer, 15u, &contact) == C_DEFERRED &&
        !memcmp(&contact, &sentinel, sizeof(contact)));
  free(context_before);
  free(context_after);
  free(before);
  free(model);
  c_trainer_destroy(trainer);
}

static void verify_cases(const char *directory) {
  for (unsigned action = 0; action < C_CODE_ACTIONS; ++action) {
    char name[128];
    size_t length = 0;
    snprintf(name, sizeof(name), "candidate-%u/train.log", action);
    unsigned char *train = read_artifact(directory, name, &length);
    CHECK(strstr((const char *)train, "total=13"));
    CHECK(strstr((const char *)train, "TRAIN\ttrain-symmetric"));
    CHECK(strstr((const char *)train, "TRAIN\ttrain-singleton"));
    CHECK(strstr((const char *)train, "TRAIN\ttrain-reversed"));
    CHECK(!strstr((const char *)train, "audit-"));
    free(train);
    snprintf(name, sizeof(name), "candidate-%u/audit.log", action);
    unsigned char *audit = read_artifact(directory, name, &length);
    CHECK(strstr((const char *)audit, "total=17"));
    CHECK(strstr((const char *)audit, "AUDIT\taudit-negative"));
    CHECK(strstr((const char *)audit, "AUDIT\taudit-wide"));
    CHECK(strstr((const char *)audit, "AUDIT\taudit-extremes"));
    CHECK(!strstr((const char *)audit, "train-"));
    free(audit);
    snprintf(name, sizeof(name), "candidate-%u/candidate.c", action);
    unsigned char *source = read_artifact(directory, name, &length);
    CHECK(length && strstr((const char *)source, "int candidate("));
    free(source);
    snprintf(name, sizeof(name), "candidate-%u/compile.log", action);
    unsigned char *compiler_log = read_artifact(directory, name, &length);
    free(compiler_log);
  }
}

static void verify_quarantine(const c_trainer *trainer) {
  size_t audits = 0, measurements = 0;
  for (size_t i = 0;
       i < c_context_count(c_trainer_context((c_trainer *)trainer)); ++i) {
    c_record r;
    CHECK(c_context_record(c_trainer_context((c_trainer *)trainer), i, &r) ==
          C_OK);
    if (r.kind == C_AUDIT) {
      ++audits;
      CHECK(r.split == C_HOLDOUT);
    }
    if (r.kind == C_TRAIN_MEASUREMENT) {
      ++measurements;
      CHECK(r.split == C_TRAIN);
    }
    if (r.kind == C_ACTIVITY && r.split == C_TRAIN)
      CHECK(!strstr(r.path, "audit.log") && !strstr(r.path, "evaluator.c"));
  }
  CHECK(audits == 6 && measurements == 4);
  CHECK(trainer->receipt_count == 3);
  for (size_t i = 0; i < trainer->task_count; ++i)
    CHECK(trainer->tasks[i].head == C_CODE && trainer->tasks[i].target == 3 &&
          trainer->tasks[i].done);
}

static void verify_prediction(const char *directory, const c_trainer *trainer) {
  size_t length = 0;
  unsigned char *input = read_artifact(directory, "input.bin", &length);
  CHECK(!strstr((const char *)input, "FORBIDDEN_AUDIT_TARGET"));
  CHECK(!strstr((const char *)input, "audit-negative") &&
        !strstr((const char *)input, "failures="));
  CHECK(strstr((const char *)input, "Input role=LLM_PROPOSAL record="));
  CHECK(strstr((const char *)input, "Unverified hypothesis: choose action0"));
  double features[C_FEATURES], mass[C_MAX_GROUPS] = {1, 1, 1, 1};
  double before[C_CODE_ACTIONS], after[C_CODE_ACTIONS];
  c_model *fresh = NULL;
  CHECK(c_model_create(4, 79, &fresh) == C_OK);
  CHECK(c_encode(input, length, features) == C_OK);
  CHECK(c_model_predict(fresh, C_CODE, features, 15, mass, before,
                        C_CODE_ACTIONS) == C_OK);
  CHECK(c_model_predict(c_trainer_model(trainer), C_CODE, features, 15, mass,
                        after, C_CODE_ACTIONS) == C_OK);
  CHECK(after[3] > before[3] && -log(after[3]) < -log(before[3]));
  c_model_destroy(fresh);
  free(input);
}

static void verify_checkpoint(const char *directory, c_trainer *trainer) {
  char path[PATH_BYTES], first[PATH_BYTES], second[PATH_BYTES];
  c_trainer *loaded = NULL;
  path_join(path, directory, "memory.centroid");
  CHECK(c_trainer_load(path, &loaded) == C_OK);
  c_training_report a, b;
  CHECK(c_trainer_report(trainer, &a) == C_OK &&
        c_trainer_report(loaded, &b) == C_OK);
  CHECK(a.updates == b.updates && a.completed == b.completed &&
        a.generation == b.generation);
  for (unsigned i = 0; i < 4; ++i) {
    char x[C_DIGEST_HEX], y[C_DIGEST_HEX];
    CHECK(c_model_fingerprint(c_trainer_model(trainer), i, x) == C_OK);
    CHECK(c_model_fingerprint(c_trainer_model(loaded), i, y) == C_OK);
    CHECK(!strcmp(x, y));
  }
  CHECK(c_trainer_step(trainer, 9, &a) == C_OK &&
        c_trainer_step(loaded, 9, &b) == C_OK);
  path_join(first, directory, "continued-a.centroid");
  path_join(second, directory, "continued-b.centroid");
  CHECK(c_trainer_save(trainer, first) == C_OK &&
        c_trainer_save(loaded, second) == C_OK);
  unsigned char *x = NULL, *y = NULL;
  size_t nx = 0, ny = 0;
  CHECK(c_read_file(first, &x, &nx) == C_OK &&
        c_read_file(second, &y, &ny) == C_OK);
  CHECK(nx == ny && !memcmp(x, y, nx));
  free(x);
  free(y);
  c_trainer_destroy(loaded);
}

static void verify_report(const char *directory) {
  size_t length = 0;
  unsigned char *input = read_artifact(directory, "input.bin", &length);
  CHECK(strstr((const char *)input, "Input role=LLM_PROPOSAL record="));
  CHECK(strstr((const char *)input, "Unverified hypothesis"));
  CHECK(!strstr((const char *)input, "FORBIDDEN_AUDIT_TARGET"));
  free(input);
  unsigned char *report = read_artifact(directory, "report.md", &length);
  CHECK(strstr((const char *)report, "Winner action: 3"));
  CHECK(strstr((const char *)report, "| 0 | OR baseline | 7 | 8 |"));
  CHECK(strstr((const char *)report, "| 3 | Both inclusive | 0 | 0 |"));
  CHECK(strstr((const char *)report,
               "scoped to an authored integer-range fixture"));
  CHECK(strstr((const char *)report,
               "AUDIT targets and outcomes stay in HOLDOUT"));
  free(report);
  unsigned char *winner = read_artifact(directory, "winner.c", &length);
  CHECK(!strcmp(
      (const char *)winner,
      "int candidate(int x, int lo, int hi) { return x >= lo && x <= hi; }\n"));
  free(winner);
  unsigned char *receipts =
      read_artifact(directory, "train-receipts.txt", &length);
  CHECK(strstr((const char *)receipts, "family=0 target=3"));
  CHECK(strstr((const char *)receipts, "family=1 target=3"));
  CHECK(strstr((const char *)receipts, "family=2 target=3"));
  CHECK(!strstr((const char *)receipts, "AUDIT"));
  free(receipts);
}

static void existing_immutable(c_trainer *trainer, const char *compiler,
                               const char *directory) {
  size_t length = 0;
  unsigned char *before = read_artifact(directory, "accepted.sha256", &length);
  CHECK(length == 64);
  char model[C_DIGEST_HEX], after_model[C_DIGEST_HEX];
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0, model) == C_OK);
  CHECK(c_experiment_run(trainer, compiler, directory) == C_INVALID);
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0, after_model) == C_OK &&
        !strcmp(model, after_model));
  size_t after_length = 0;
  unsigned char *after =
      read_artifact(directory, "accepted.sha256", &after_length);
  CHECK(length == after_length && !memcmp(before, after, length));
  free(before);
  free(after);
}

static void infrastructure_failure(c_trainer *trainer, const char *directory) {
  char failed[PATH_BYTES];
  int n = snprintf(failed, sizeof(failed), "%s-failed", directory);
  CHECK(n > 0 && (size_t)n < sizeof(failed));
  c_training_report progress;
  CHECK(c_trainer_step(trainer, (size_t)((8u - trainer->generation % 8u) % 8u),
                       &progress) == C_OK);
  c_model *original_model = malloc(sizeof(*original_model));
  unsigned char original_cells[C_WORLD_CELLS];
  CHECK(original_model);
  memcpy(original_model, trainer->model, sizeof(*original_model));
  memcpy(original_cells, trainer->cells, sizeof(original_cells));
  const uint64_t original_generation = trainer->generation;
  char before[C_DIGEST_HEX], after[C_DIGEST_HEX];
  size_t records = c_context_count(c_trainer_context(trainer));
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0, before) == C_OK);
  CHECK(c_experiment_run(trainer, "centroid-no-such-native-compiler", failed) ==
        C_IO);
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0, after) == C_OK &&
        !strcmp(before, after));
  CHECK(c_context_count(c_trainer_context(trainer)) == records);
  size_t length = 0;
  unsigned char *report = read_artifact(failed, "report.md", &length);
  CHECK(strstr((const char *)report, "Attempted candidates: 4 / 4"));
  CHECK(strstr((const char *)report, "Verified native evaluations: 0 / 4"));
  CHECK(strstr((const char *)report, "No accepted result is published"));
  free(report);
  check_contact_artifacts(failed, original_model, original_cells,
                          original_generation, 0u);
  free(original_model);
  for (unsigned action = 0; action < C_CODE_ACTIONS; ++action) {
    char name[128];
    snprintf(name, sizeof(name), "candidate-%u/candidate.c", action);
    unsigned char *source = read_artifact(failed, name, &length);
    CHECK(length);
    free(source);
    snprintf(name, sizeof(name), "candidate-%u/compile.log", action);
    unsigned char *log = read_artifact(failed, name, &length);
    free(log);
  }
  char accepted[PATH_BYTES];
  path_join(accepted, failed, "accepted.sha256");
  FILE *file = fopen(accepted, "rb");
  CHECK(!file);
}

int main(int argc, char **argv) {
  CHECK(argc == 3);
  char build[PATH_BYTES], directory[PATH_BYTES], name[128];
  full_path(build, argv[1]);
  int n = snprintf(name, sizeof(name), "experiment fixture %u-%llu",
                   pid_number(), (unsigned long long)c_monotonic_ms());
  CHECK(n > 0 && (size_t)n < sizeof(name));
  path_join(directory, build, name);
  no_contact_deferred(directory);
  c_trainer *trainer = NULL;
  uint64_t admitted = 0;
  CHECK(c_trainer_create(4, 79, &trainer) == C_OK);
  static const unsigned char proposal[] =
      "Unverified hypothesis: choose action0 OR for the range predicate with "
      "inclusive lower and upper bounds. Independent measurement required.\n";
  static const unsigned char audit[] =
      "FORBIDDEN_AUDIT_TARGET range predicate lower upper inclusive C repair "
      "action=0\n";
  CHECK(c_context_admit(c_trainer_context(trainer), C_LLM_PROPOSAL, C_TRAIN,
                        "proposal.txt",
                        "explicit LLM proposal, never outcome authority",
                        proposal, sizeof(proposal) - 1, &admitted) == C_OK);
  CHECK(c_context_admit(c_trainer_context(trainer), C_AUDIT, C_HOLDOUT,
                        "quarantined.txt", "independent audit", audit,
                        sizeof(audit) - 1, &admitted) == C_OK);
  c_model *original_model = malloc(sizeof(*original_model));
  unsigned char original_cells[C_WORLD_CELLS];
  CHECK(original_model);
  memcpy(original_model, trainer->model, sizeof(*original_model));
  memcpy(original_cells, trainer->cells, sizeof(original_cells));
  CHECK(c_experiment_run(trainer, argv[2], directory) == C_OK);
  check_contact_artifacts(directory, original_model, original_cells, 0u, 3u);
  free(original_model);
  c_training_report report;
  CHECK(c_trainer_report(trainer, &report) == C_OK);
  CHECK(report.generation == 256 && report.updates == 3 &&
        report.completed == 3 && report.queued == 0 && report.contacts > 0);
  verify_cases(directory);
  verify_report(directory);
  verify_quarantine(trainer);
  verify_prediction(directory, trainer);
  existing_immutable(trainer, argv[2], directory);
  verify_checkpoint(directory, trainer);
  infrastructure_failure(trainer, directory);
  c_trainer_destroy(trainer);
  printf("Native C experiment, frozen prediction, TRAIN receipts and AUDIT "
         "quarantine passed; artifacts: %s\n",
         directory);
  return 0;
}
