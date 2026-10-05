/** An authored, finite C repair fixture with independently executed
 * measurements. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "internal.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#define EXP_PATH 4096u
#define EXP_INPUT 65536u
#define EXP_LOG (1024u * 1024u)
#define TRAIN_CASES 13u
#define AUDIT_CASES 17u

static const char *const candidates[C_CODE_ACTIONS] = {
    "int candidate(int x, int lo, int hi) { return x > lo || x < hi; }\n",
    "int candidate(int x, int lo, int hi) { (void)hi; return x >= lo; }\n",
    "int candidate(int x, int lo, int hi) { (void)lo; return x <= hi; }\n",
    "int candidate(int x, int lo, int hi) { return x >= lo && x <= hi; }\n"};

/* Authored case labels stay in the scanner-excluded audit data tree. */
#include "../../data/audit/code_evaluator.h"

typedef struct {
  char directory[EXP_PATH], compiler[EXP_PATH];
  char parent[C_DIGEST_HEX], evaluator[C_DIGEST_HEX],
      input_digest[C_DIGEST_HEX];
  unsigned char input[EXP_INPUT];
  size_t input_length;
  double features[C_FEATURES], before[C_CODE_ACTIONS], after[C_CODE_ACTIONS];
  unsigned train_failures[C_CODE_ACTIONS], audit_failures[C_CODE_ACTIONS];
  unsigned family_failures[C_CODE_ACTIONS][3];
  char train_log_digest[C_CODE_ACTIONS][C_DIGEST_HEX];
  c_status trial_status[C_CODE_ACTIONS];
  uint64_t compile_ms[C_CODE_ACTIONS], train_ms[C_CODE_ACTIONS],
      audit_ms[C_CODE_ACTIONS];
  size_t compile_bytes[C_CODE_ACTIONS], train_bytes[C_CODE_ACTIONS],
      audit_bytes[C_CODE_ACTIONS];
  size_t executable_bytes[C_CODE_ACTIONS];
  char executable_digest[C_CODE_ACTIONS][C_DIGEST_HEX];
  uint64_t started_ms;
  unsigned attempted, verified;
  unsigned winner, eligible, predicted;
  c_training_report start, finish;
  c_experiment_contact contact;
} experiment;

static c_status path_join(char out[EXP_PATH], const char *directory,
                          const char *name) {
  int n = snprintf(out, EXP_PATH, "%s/%s", directory, name);
  return n > 0 && (size_t)n < EXP_PATH ? C_OK : C_LIMIT;
}

static c_status artifact(const char *directory, const char *name,
                         const void *bytes, size_t length) {
  char path[EXP_PATH];
  c_status status = path_join(path, directory, name);
  return status == C_OK ? c_write_atomic(path, bytes, length) : status;
}

static c_status new_directory(const char *directory) {
#ifdef _WIN32
  return _mkdir(directory) == 0 ? C_OK : (errno == EEXIST ? C_INVALID : C_IO);
#else
  return mkdir(directory, 0700) == 0 ? C_OK
                                     : (errno == EEXIST ? C_INVALID : C_IO);
#endif
}

static c_status absolute_directory(const char *directory, char out[EXP_PATH]) {
#ifdef _WIN32
  DWORD n = GetFullPathNameA(directory, EXP_PATH, out, NULL);
  return n && n < EXP_PATH ? C_OK : C_IO;
#else
  return realpath(directory, out) ? C_OK : C_IO;
#endif
}

static c_status compiler_path(const char *compiler, char out[EXP_PATH]) {
  if (!strchr(compiler, '/') && !strchr(compiler, '\\')) {
    memcpy(out, compiler, strlen(compiler) + 1);
    return C_OK;
  }
  return absolute_directory(compiler, out);
}

static c_status append_input(experiment *e, const void *bytes, size_t length) {
  if (length > sizeof(e->input) - e->input_length)
    return C_LIMIT;
  memcpy(e->input + e->input_length, bytes, length);
  e->input_length += length;
  return C_OK;
}

static c_status freeze_evidence(c_trainer *trainer, experiment *e,
                                c_record_kind kind, const unsigned char *query,
                                size_t length) {
  c_record record;
  double similarity;
  char header[384];
  c_status status;
  const char *role = kind == C_SOURCE         ? "SOURCE"
                     : kind == C_LLM_PROPOSAL ? "LLM_PROPOSAL"
                                              : "ACTIVITY";
  status = c_context_retrieve_kind(c_trainer_context(trainer), kind, query,
                                   length, &record, &similarity);
  if (status == C_NOT_FOUND || status == C_DEFERRED) {
    int n = snprintf(header, sizeof(header),
                     "\nInput role=%s: no supported current TRAIN evidence.\n",
                     role);
    return n > 0 && (size_t)n < sizeof(header)
               ? append_input(e, header, (size_t)n)
               : C_LIMIT;
  }
  if (status != C_OK)
    return status;
  int n =
      snprintf(header, sizeof(header),
               "\nInput role=%s record=%llu version=%llu digest=%s "
               "byte-span=[0,%zu) path-bytes=%zu attribution-bytes=%zu\n",
               role, (unsigned long long)record.id,
               (unsigned long long)record.version, record.digest, record.length,
               strlen(record.path), strlen(record.attribution));
  if (n < 0 || (size_t)n >= sizeof(header))
    return C_LIMIT;
  status = append_input(e, header, (size_t)n);
  if (status == C_OK)
    status = append_input(e, record.path, strlen(record.path));
  if (status == C_OK)
    status = append_input(e, "\n", 1);
  if (status == C_OK)
    status = append_input(e, record.attribution, strlen(record.attribution));
  if (status == C_OK)
    status = append_input(e, "\nExact input bytes:\n", 20);
  if (status == C_OK)
    status = append_input(e, record.bytes, record.length);
  return status;
}

static c_status freeze_input(c_trainer *trainer, experiment *e) {
  static const unsigned char query[] = "range predicate lower upper inclusive "
                                       "C repair native codebase centroids";
  static const char prompt[] =
      "Finite authored C fixture: inclusive integer range predicate.\n"
      "Actions:0=OR baseline;1=lower only;2=upper only;3=both inclusive.\n"
      "Frozen publication gate: strictly fewer TRAIN failures than baseline "
      "and zero AUDIT failures.\n"
      "Parent exact bytes:\n";
  c_status status = append_input(e, prompt, sizeof(prompt) - 1);
  if (status == C_OK)
    status = append_input(e, candidates[0], strlen(candidates[0]));
  for (unsigned kind = C_SOURCE; kind <= C_ACTIVITY && status == C_OK; kind++)
    status = freeze_evidence(trainer, e, (c_record_kind)kind, query,
                             sizeof(query) - 1);
  if (status != C_OK)
    return status;
  for (unsigned i = 0; i < C_CODE_ACTIONS; ++i) {
    char digest[C_DIGEST_HEX], identity[128];
    c_hash(candidates[i], strlen(candidates[i]), digest);
    int n = snprintf(identity, sizeof(identity),
                     "\nCatalog action=%u source-digest=%s\n", i, digest);
    if (n < 0 || (size_t)n >= sizeof(identity))
      return C_LIMIT;
    status = append_input(e, identity, (size_t)n);
    if (status != C_OK)
      return status;
  }
  c_hash(e->input, e->input_length, e->input_digest);
  return c_encode(e->input, e->input_length, e->features);
}

static c_status predict(const c_trainer *trainer, experiment *e,
                        double probabilities[C_CODE_ACTIONS]) {
  double mass[C_MAX_GROUPS] = {1, 1, 1, 1};
  return c_model_predict(c_trainer_model(trainer), C_CODE, e->features,
                         e->eligible, mass, probabilities, C_CODE_ACTIONS);
}

static c_status prepare(c_trainer *trainer, experiment *e) {
  c_status status = freeze_input(trainer, e);
  if (status != C_OK)
    return status;
  e->eligible = (1u << c_trainer_model(trainer)->groups) - 1u;
  status = predict(trainer, e, e->before);
  if (status != C_OK)
    return status;
  for (unsigned i = 1; i < C_CODE_ACTIONS; ++i)
    if (e->before[i] > e->before[e->predicted])
      e->predicted = i;
  c_hash(candidates[0], strlen(candidates[0]), e->parent);
  c_hash(evaluator, sizeof(evaluator) - 1, e->evaluator);
  status = artifact(e->directory, "input.bin", e->input, e->input_length);
  if (status == C_OK)
    status = artifact(e->directory, "parent.c", candidates[0],
                      strlen(candidates[0]));
  if (status == C_OK)
    status =
        artifact(e->directory, "evaluator.c", evaluator, sizeof(evaluator) - 1);
  if (status == C_OK)
    status = c_trainer_report(trainer, &e->start);
  if (status == C_OK)
    status = c_experiment_contact_snapshot(trainer, e->directory, &e->contact);
  return status;
}

static int msvc_compiler(const char *compiler) {
  const char *base = compiler;
  for (const char *p = compiler; *p; ++p)
    if (*p == '/' || *p == '\\')
      base = p + 1;
  char lower[32];
  size_t n = strlen(base);
  if (n >= sizeof(lower))
    return 0;
  for (size_t i = 0; i <= n; ++i)
    lower[i] = base[i] >= 'A' && base[i] <= 'Z' ? (char)(base[i] + ('a' - 'A'))
                                                : base[i];
  return strcmp(lower, "cl") == 0 || strcmp(lower, "cl.exe") == 0 ||
         strcmp(lower, "clang-cl") == 0 || strcmp(lower, "clang-cl.exe") == 0;
}

static c_status compile_trial(experiment *e, unsigned action, const char *trial,
                              const char *source, const char *evaluator_path,
                              const char *executable) {
  const char *argv[16] = {e->compiler};
  char output[EXP_PATH + 8];
  if (msvc_compiler(e->compiler)) {
    int n = snprintf(output, sizeof(output), "/Fe:%s", executable);
    if (n < 0 || (size_t)n >= sizeof(output))
      return C_LIMIT;
    const char *msvc[] = {e->compiler, "/nologo", "/std:c11",     "/W4",
                          "/WX",       "/O1",     evaluator_path, source,
                          output,      NULL};
    for (size_t i = 0; i < sizeof(msvc) / sizeof(msvc[0]); ++i)
      argv[i] = msvc[i];
  } else {
    const char *posix[] = {
        e->compiler,    "-std=c11", "-Wall", "-Wextra",  "-Werror", "-O1",
        evaluator_path, source,     "-o",    executable, NULL};
    for (size_t i = 0; i < sizeof(posix) / sizeof(posix[0]); ++i)
      argv[i] = posix[i];
  }
  c_process_options options = {e->compiler, argv, trial, 60000, EXP_LOG};
  c_process_result result = {0};
  char log_path[EXP_PATH];
  c_status status = path_join(log_path, trial, "compile.log");
  if (status == C_OK)
    status = c_process_capture_file(&options, log_path, &result);
  e->compile_ms[action] = result.elapsed_ms;
  e->compile_bytes[action] = result.observed_bytes;
  if (status == C_OK &&
      (result.timed_out || result.exit_code != 0 || result.output_truncated))
    status = C_IO;
  c_process_dispose(&result);
  return status;
}

static c_status verified_rows(const char *bytes, const char *summary, int audit,
                              unsigned total, unsigned failures,
                              unsigned *family_failures) {
  static const char *const train_names[] = {
      "train-symmetric", "train-singleton", "train-reversed"};
  static const char *const audit_names[] = {"audit-negative", "audit-wide",
                                            "audit-extremes"};
  const char *const *names = audit ? audit_names : train_names;
  unsigned seen[3] = {0}, rows = 0, counted_failures = 0;
  for (const char *p = bytes; p < summary;) {
    char kind[16], family[32], outcome[8];
    unsigned index = 0;
    int lo = 0, hi = 0, x = 0, expected = 0, actual = 0;
    const char *end = strchr(p, '\n');
    if (!end || end >= summary ||
        sscanf(p, "%15s %31s %u %d %d %d %d %d %7s", kind, family, &index, &lo,
               &hi, &x, &expected, &actual, outcome) != 9 ||
        strcmp(kind, audit ? "AUDIT" : "TRAIN") || index != rows ||
        (expected != 0 && expected != 1) || (actual != 0 && actual != 1))
      return C_CORRUPT;
    unsigned f = 0;
    while (f < 3 && strcmp(family, names[f]))
      ++f;
    if (f == 3)
      return C_CORRUPT;
    unsigned failed = actual != expected;
    if (strcmp(outcome, failed ? "FAIL" : "PASS"))
      return C_CORRUPT;
    ++seen[f];
    ++rows;
    counted_failures += failed;
    if (family_failures)
      family_failures[f] += failed;
    p = end + 1;
  }
  if (rows != total || counted_failures != failures || seen[0] != 5 ||
      seen[1] != (audit ? 5u : 3u) || seen[2] != (audit ? 7u : 5u))
    return C_CORRUPT;
  return C_OK;
}

static c_status parse_cases(const c_process_result *result, const char *split,
                            unsigned denominator, unsigned *failures,
                            unsigned family_failures[3]) {
  char summary[96];
  unsigned total = 0, failed = 0;
  int n = snprintf(summary, sizeof(summary), "SUMMARY %s failures=", split);
  if (n < 0 || (size_t)n >= sizeof(summary) || result->timed_out ||
      result->output_truncated || !result->output ||
      (result->exit_code != 0 && result->exit_code != 1))
    return C_IO;
  const char *line = strstr((const char *)result->output, summary);
  if (!line ||
      sscanf(line + strlen(summary), "%u total=%u", &failed, &total) != 2 ||
      total != denominator || failed > total ||
      result->exit_code != (failed ? 1 : 0))
    return C_CORRUPT;
  c_status status =
      verified_rows((const char *)result->output, line, !strcmp(split, "audit"),
                    total, failed, family_failures);
  if (status == C_OK)
    *failures = failed;
  return status;
}

static c_status evaluate_trial(experiment *e, unsigned action,
                               const char *trial, const char *executable,
                               int audit) {
  const char *split = audit ? "audit" : "train";
  const char *argv[] = {executable, split, NULL};
  c_process_options options = {executable, argv, trial, 10000, EXP_LOG};
  c_process_result result = {0};
  char log_path[EXP_PATH];
  c_status status =
      path_join(log_path, trial, audit ? "audit.log" : "train.log");
  if (status == C_OK)
    status = c_process_capture_file(&options, log_path, &result);
  if (audit) {
    e->audit_ms[action] = result.elapsed_ms;
    e->audit_bytes[action] = result.observed_bytes;
  } else {
    e->train_ms[action] = result.elapsed_ms;
    e->train_bytes[action] = result.observed_bytes;
  }
  if (status == C_OK)
    status = parse_cases(&result, split, audit ? AUDIT_CASES : TRAIN_CASES,
                         audit ? &e->audit_failures[action]
                               : &e->train_failures[action],
                         audit ? NULL : e->family_failures[action]);
  if (!audit && status == C_OK)
    c_hash(result.output, result.length, e->train_log_digest[action]);
  c_process_dispose(&result);
  return status;
}

static c_status trial(experiment *e, unsigned action) {
  char name[32], directory[EXP_PATH], source[EXP_PATH], independent[EXP_PATH],
      executable[EXP_PATH];
  int n = snprintf(name, sizeof(name), "candidate-%u", action);
  if (n < 0 || (size_t)n >= sizeof(name))
    return C_LIMIT;
  c_status status = path_join(directory, e->directory, name);
  if (status == C_OK)
    status = new_directory(directory);
  if (status == C_OK)
    status = path_join(source, directory, "candidate.c");
  if (status == C_OK)
    status = path_join(independent, e->directory, "evaluator.c");
#ifdef _WIN32
  if (status == C_OK)
    status = path_join(executable, directory, "evaluate.exe");
#else
  if (status == C_OK)
    status = path_join(executable, directory, "evaluate");
#endif
  if (status == C_OK)
    status =
        c_write_atomic(source, candidates[action], strlen(candidates[action]));
  if (status == C_OK)
    status =
        compile_trial(e, action, directory, source, independent, executable);
  if (status == C_OK) {
    unsigned char *binary = NULL;
    size_t length = 0;
    status = c_read_file(executable, &binary, &length);
    if (status == C_OK) {
      e->executable_bytes[action] = length;
      c_hash(binary, length, e->executable_digest[action]);
    }
    free(binary);
  }
  if (status == C_OK)
    status = evaluate_trial(e, action, directory, executable, 0);
  if (status == C_OK)
    status = evaluate_trial(e, action, directory, executable, 1);
  return status;
}

static c_status admit_evidence(c_trainer *trainer, experiment *e) {
  char path[EXP_PATH], measurement[1024];
  uint64_t admitted = 0;
  c_status status = path_join(path, e->directory, "parent.c");
  if (status == C_OK)
    status = c_context_admit(c_trainer_context(trainer), C_SOURCE, C_TRAIN,
                             path, "authored finite range fixture parent",
                             (const unsigned char *)candidates[0],
                             strlen(candidates[0]), &admitted);
  if (status == C_OK)
    status = path_join(path, e->directory, "evaluator.c");
  if (status == C_OK)
    status = c_context_admit(
        c_trainer_context(trainer), C_AUDIT, C_HOLDOUT, path,
        "independent evaluator program; authored labels quarantined",
        (const unsigned char *)evaluator, sizeof(evaluator) - 1, &admitted);
  for (unsigned i = 0; i < C_CODE_ACTIONS && status == C_OK; ++i) {
    int n = snprintf(
        measurement, sizeof(measurement),
        "action=%u TRAIN failures=%u denominator=%u parent=%s "
        "evaluator=%s frozen-input=%s model-parent=%s contact=%s\n",
        i, e->train_failures[i], TRAIN_CASES, e->parent, e->evaluator,
        e->input_digest, e->contact.checkpoint_digest, e->contact.proof_digest);
    if (n < 0 || (size_t)n >= sizeof(measurement))
      return C_LIMIT;
    char name[64];
    snprintf(name, sizeof(name), "candidate-%u/train-measurement.txt", i);
    status = artifact(e->directory, name, measurement, (size_t)n);
    if (status == C_OK)
      status = path_join(path, e->directory, name);
    if (status == C_OK)
      status = c_context_admit(
          c_trainer_context(trainer), C_TRAIN_MEASUREMENT, C_TRAIN, path,
          "independent native TRAIN execution",
          (const unsigned char *)measurement, (size_t)n, &admitted);
    snprintf(name, sizeof(name), "candidate-%u/audit.log", i);
    if (status == C_OK)
      status = path_join(path, e->directory, name);
    if (status == C_OK)
      status = c_context_admit_file(
          c_trainer_context(trainer), C_AUDIT, C_HOLDOUT, path,
          "independent AUDIT; never a training target", &admitted);
  }
  return status;
}

static c_status enqueue_and_train(c_trainer *trainer, experiment *e) {
  char receipts[4096];
  size_t length = 0;
  for (unsigned family = 0; family < 3; ++family) {
    unsigned winner = 0;
    for (unsigned action = 1; action < C_CODE_ACTIONS; ++action)
      if (e->family_failures[action][family] <
          e->family_failures[winner][family])
        winner = action;
    char binding[768], receipt[C_DIGEST_HEX];
    int n = snprintf(
        binding, sizeof(binding),
        "TRAIN family=%u parent=%s evaluator=%s input=%s log=%s action=%u "
        "model-parent=%s contact=%s",
        family, e->parent, e->evaluator, e->input_digest,
        e->train_log_digest[winner], winner, e->contact.checkpoint_digest,
        e->contact.proof_digest);
    if (n < 0 || (size_t)n >= sizeof(binding))
      return C_LIMIT;
    c_hash(binding, (size_t)n, receipt);
    int written = snprintf(receipts + length, sizeof(receipts) - length,
                           "family=%u target=%u receipt=%s %s\n", family,
                           winner, receipt, binding);
    if (written < 0 || (size_t)written >= sizeof(receipts) - length)
      return C_LIMIT;
    length += (size_t)written;
    c_status status = c_trainer_enqueue_measurement(
        trainer, e->input, e->input_length, winner, e->eligible, e->parent,
        e->evaluator, receipt);
    if (status != C_OK)
      return status;
  }
  c_status status =
      artifact(e->directory, "train-receipts.txt", receipts, length);
  if (status == C_OK)
    status = c_trainer_step(trainer, 256, &e->finish);
  if (status == C_OK)
    status = predict(trainer, e, e->after);
  return status;
}

static c_status resources(experiment *e, char *report, size_t capacity,
                          size_t *length, size_t checkpoint_bytes,
                          const char *checkpoint_digest, int accepted) {
  int n = snprintf(report + *length, capacity - *length,
                   "\nAttempted candidates: %u / %u; verified native "
                   "evaluations: %u / %u.\n\n"
                   "Frozen publication gate: strictly fewer TRAIN failures "
                   "than baseline and zero AUDIT failures. "
                   "Gate passed: %s. Rejected candidates remain reviewable; "
                   "only a passed gate gets `accepted.sha256`.\n\n"
                   "Elapsed experiment time: %llu ms. Frozen input bytes: %zu. "
                   "Checkpoint bytes: %zu; SHA256: `%s`.\n\n"
                   "| Action | Compile ms | TRAIN ms | AUDIT ms | Raw log "
                   "bytes (compile/TRAIN/AUDIT) | Executable bytes |\n"
                   "| --- | --- | --- | --- | --- | --- |\n",
                   e->attempted, C_CODE_ACTIONS, e->verified, C_CODE_ACTIONS,
                   accepted ? "yes" : "no",
                   (unsigned long long)(c_monotonic_ms() - e->started_ms),
                   e->input_length, checkpoint_bytes, checkpoint_digest);
  if (n < 0 || (size_t)n >= capacity - *length)
    return C_LIMIT;
  *length += (size_t)n;
  for (unsigned i = 0; i < C_CODE_ACTIONS; ++i) {
    n = snprintf(report + *length, capacity - *length,
                 "| %u | %llu | %llu | %llu | %zu / %zu / %zu | %zu |\n", i,
                 (unsigned long long)e->compile_ms[i],
                 (unsigned long long)e->train_ms[i],
                 (unsigned long long)e->audit_ms[i], e->compile_bytes[i],
                 e->train_bytes[i], e->audit_bytes[i], e->executable_bytes[i]);
    if (n < 0 || (size_t)n >= capacity - *length)
      return C_LIMIT;
    *length += (size_t)n;
  }
  for (unsigned i = 0; i < C_CODE_ACTIONS; ++i) {
    n = snprintf(report + *length, capacity - *length,
                 "\nAction %u executable SHA256: `%s`.\n\n", i,
                 e->executable_digest[i]);
    if (n < 0 || (size_t)n >= capacity - *length)
      return C_LIMIT;
    *length += (size_t)n;
  }
  return C_OK;
}

static c_status publish(c_trainer *trainer, experiment *e) {
  char report[8192], path[EXP_PATH];
  c_status status = artifact(e->directory, "winner.c", candidates[e->winner],
                             strlen(candidates[e->winner]));
  if (status == C_OK)
    status = path_join(path, e->directory, "memory.centroid");
  if (status == C_OK)
    status = c_trainer_save(trainer, path);
  unsigned char *checkpoint = NULL;
  size_t checkpoint_bytes = 0;
  if (status == C_OK)
    status = c_read_file(path, &checkpoint, &checkpoint_bytes);
  if (status != C_OK) {
    free(checkpoint);
    return status;
  }
  char checkpoint_digest[C_DIGEST_HEX];
  c_hash(checkpoint, checkpoint_bytes, checkpoint_digest);
  free(checkpoint);
  int accepted = e->train_failures[e->winner] < e->train_failures[0] &&
                 e->audit_failures[e->winner] == 0;
  double before_loss = -log(e->before[e->winner]),
         after_loss = -log(e->after[e->winner]);
  int n = snprintf(
      report, sizeof(report),
      "# Finite native C range-predicate experiment\n\n"
      "This improvement is scoped to an authored integer-range fixture and "
      "four declared C candidates. "
      "It does not establish broad coding improvement or edit project "
      "source.\n\n"
      "Parent SHA256: `%s`\n\nIndependent evaluator SHA256: `%s`\n\nFrozen "
      "input SHA256: `%s`\n\n"
      "Frozen pretrial model-parent checkpoint SHA256: `%s`. Physical contact "
      "proof SHA256: `%s`; generation=%llu eligible=%u participants=%u "
      "contact-graph=%u. Both immutable artifacts precede every tool trial and "
      "are bound into every TRAIN receipt.\n\n"
      "Compiler: `%s`\n\nTRAIN families: symmetric, singleton, reversed (13 "
      "authored cases). "
      "AUDIT families: negative, wide, extremes (17 separately authored "
      "cases). "
      "Every raw case, expected value, actual value and failure is retained in "
      "each candidate's logs.\n\n"
      "| Action | Candidate | TRAIN failures / 13 | AUDIT failures / 17 |\n| "
      "--- | --- | --- | --- |\n"
      "| 0 | OR baseline | %u | %u |\n| 1 | Lower only | %u | %u |\n| 2 | "
      "Upper only | %u | %u |\n| 3 | Both inclusive | %u | %u |\n\n"
      "Winner action: %u. Selection uses TRAIN failures only; ties use the "
      "lowest action ID. "
      "AUDIT targets and outcomes stay in HOLDOUT records and never select a "
      "training target.\n\n"
      "Frozen model choice before targets were revealed: %u. Measured TRAIN "
      "winner probability: %.12g before, %.12g after. "
      "Measured-label cross entropy: %.12g before, %.12g after.\n\n"
      "Life generations: %llu to %llu; genuine contacts: %llu; model updates: "
      "%llu; completed tasks: %llu. "
      "Three independently measured TRAIN-family receipts are enqueued; only "
      "Life contact participants can update.\n\n"
      "Review `winner.c`, `input.bin`, `evaluator.c`, each "
      "`candidate-N/candidate.c`, `compile.log`, "
      "`train.log`, `audit.log`, and `memory.centroid`. No candidate is "
      "applied to the codebase.\n",
      e->parent, e->evaluator, e->input_digest, e->contact.checkpoint_digest,
      e->contact.proof_digest, (unsigned long long)e->contact.generation,
      e->contact.eligible, e->contact.participants, e->contact.graph,
      e->compiler, e->train_failures[0], e->audit_failures[0],
      e->train_failures[1], e->audit_failures[1], e->train_failures[2],
      e->audit_failures[2], e->train_failures[3], e->audit_failures[3],
      e->winner, e->predicted, e->before[e->winner], e->after[e->winner],
      before_loss, after_loss, (unsigned long long)e->start.generation,
      (unsigned long long)e->finish.generation,
      (unsigned long long)(e->finish.contacts - e->start.contacts),
      (unsigned long long)(e->finish.updates - e->start.updates),
      (unsigned long long)(e->finish.completed - e->start.completed));
  if (n < 0 || (size_t)n >= sizeof(report))
    return C_LIMIT;
  size_t length = (size_t)n;
  status = resources(e, report, sizeof(report), &length, checkpoint_bytes,
                     checkpoint_digest, accepted);
  if (status == C_OK)
    status = artifact(e->directory, "report.md", report, length);
  char digest[C_DIGEST_HEX];
  c_hash(report, length, digest);
  if (status == C_OK && accepted)
    status = artifact(e->directory, "accepted.sha256", digest, strlen(digest));
  return status == C_OK && !accepted ? C_DEFERRED : status;
}

static c_status failed_report(const experiment *e, c_status failure) {
  char report[4096];
  int n = snprintf(
      report, sizeof(report),
      "# Incomplete native C fixture experiment\n\n"
      "Infrastructure status: %s (%u). Attempted candidates: %u / %u. "
      "Verified native evaluations: %u / %u. Elapsed time: %llu ms.\n\n"
      "Retained pretrial model-parent checkpoint SHA256: `%s`. Physical "
      "contact "
      "proof SHA256: `%s`. Any attempted tool trial requires both "
      "artifacts.\n\n"
      "Candidate status codes: 0=%u; 1=%u; 2=%u; 3=%u. "
      "Unattempted entries use DEFERRED. Inspect each materialized candidate "
      "source and raw compiler/evaluator logs. "
      "A failed build or malformed measurement cannot provide a target. No "
      "accepted result is published.\n",
      c_status_string(failure), (unsigned)failure, e->attempted, C_CODE_ACTIONS,
      e->verified, C_CODE_ACTIONS,
      (unsigned long long)(c_monotonic_ms() - e->started_ms),
      e->contact.checkpoint_digest, e->contact.proof_digest,
      (unsigned)e->trial_status[0], (unsigned)e->trial_status[1],
      (unsigned)e->trial_status[2], (unsigned)e->trial_status[3]);
  if (n < 0 || (size_t)n >= sizeof(report))
    return C_LIMIT;
  return artifact(e->directory, "report.md", report, (size_t)n);
}

c_status c_experiment_run(c_trainer *trainer, const char *compiler,
                          const char *directory) {
  if (!trainer || !compiler || !*compiler || strlen(compiler) >= EXP_PATH ||
      !directory || !*directory || strlen(directory) >= EXP_PATH - 64)
    return C_INVALID;
  c_experiment_contact contact;
  const unsigned groups = c_model_group_count(c_trainer_model(trainer));
  if (groups < 2u || groups > C_MAX_GROUPS)
    return C_INVALID;
  c_status status =
      c_experiment_contact_preflight(trainer, (1u << groups) - 1u, &contact);
  if (status != C_OK)
    return status;
  experiment *e = calloc(1, sizeof(*e));
  if (!e)
    return C_NOMEM;
  memcpy(e->directory, directory, strlen(directory) + 1);
  memcpy(e->compiler, compiler, strlen(compiler) + 1);
  e->started_ms = c_monotonic_ms();
  e->contact = contact;
  for (unsigned i = 0; i < C_CODE_ACTIONS; ++i)
    e->trial_status[i] = C_DEFERRED;
  status = new_directory(directory);
  int created = status == C_OK;
  if (status == C_OK)
    status = absolute_directory(directory, e->directory);
  if (status == C_OK)
    status = compiler_path(compiler, e->compiler);
  if (status == C_OK)
    status = prepare(trainer, e);
  if (status == C_OK) {
    for (unsigned i = 0; i < C_CODE_ACTIONS; ++i) {
      ++e->attempted;
      e->trial_status[i] = trial(e, i);
      if (e->trial_status[i] == C_OK)
        ++e->verified;
      else if (status == C_OK)
        status = e->trial_status[i];
    }
  }
  if (status == C_OK) {
    for (unsigned i = 1; i < C_CODE_ACTIONS; ++i)
      if (e->train_failures[i] < e->train_failures[e->winner])
        e->winner = i;
    status = admit_evidence(trainer, e);
  }
  if (status == C_OK)
    status = enqueue_and_train(trainer, e);
  if (status == C_OK)
    status = publish(trainer, e);
  if (created && status != C_OK && status != C_DEFERRED) {
    c_status saved = failed_report(e, status);
    if (saved != C_OK)
      status = saved;
  }
  free(e);
  return status;
}
