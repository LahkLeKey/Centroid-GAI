/* Holdout constructors belong in the scanner-excluded native evaluator tree. */
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "centroid_algorithms.h"
#include "centroid_code_helper.h"
#include "centroid_training.h"
#include "internal.h"
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CASES 40u
#define EPOCHS 12u
#define SEEDS 3u
#define CONDITIONS 4u
#define PATH_BYTES 4096u
_Static_assert(INT_MAX == INT32_MAX, "native lower_bound requires int32 int");
static const uint64_t seeds[SEEDS] = {1409u, 3251u, 7907u};
static const char *const conditions[CONDITIONS] = {
    "life", "frozen-physical", "deterministic", "frozen-model"};
static const char *const splits[3] = {"TRAIN", "DEV", "AUDIT"};
typedef struct {
  int32_t values[CR_CODE_MAX_VALUES], key;
  size_t count, oracle, result[4];
  uint64_t cost[4], oracle_cost;
  unsigned region, target, heuristic;
  cr_code_observation observation;
  char digest[C_DIGEST_HEX];
} workload;
typedef struct {
  uint64_t comparisons, binary, heuristic, frozen, oracle, semantic_failures;
  uint64_t inference_ns;
} evaluation;
typedef struct {
  char root[PATH_BYTES], directory[PATH_BYTES];
  char source_digest[C_DIGEST_HEX], evaluator_digest[C_DIGEST_HEX];
  char protocol_digest[C_DIGEST_HEX], checkpoint_digest[C_DIGEST_HEX];
  char selected_digest[C_DIGEST_HEX], parent_digest[C_DIGEST_HEX];
  unsigned char *source;
  size_t source_length;
  uint64_t started_ns, encoding_ns, measurement_ns, fitting_ns;
  uint64_t deployment_ns, deployment_calls;
  evaluation dev[SEEDS][CONDITIONS], audit[SEEDS][CONDITIONS];
  double retention[SEEDS][CONDITIONS];
  uint64_t generations[SEEDS][CONDITIONS], contacts[SEEDS][CONDITIONS];
  unsigned retained_ok[SEEDS][CONDITIONS], physical_ok[SEEDS][CONDITIONS];
  c_trainer *selected;
  unsigned audit_opened, accepted;
} trial;

static c_status path(char out[PATH_BYTES], const char *directory,
                     const char *name) {
  int n = snprintf(out, PATH_BYTES, "%s/%s", directory, name);
  return n < 0 || (size_t)n >= PATH_BYTES ? C_LIMIT : C_OK;
}
static c_status write_artifact(const char *directory, const char *name,
                               const void *bytes, size_t length) {
  char filename[PATH_BYTES];
  c_status s = path(filename, directory, name);
  return s == C_OK ? c_write_atomic(filename, bytes, length) : s;
}
static c_status digest_file(const char *filename, char digest[C_DIGEST_HEX]) {
  unsigned char *bytes = NULL;
  size_t length = 0;
  c_status s = c_read_file(filename, &bytes, &length);
  if (s == C_OK && length > C_MAX_FILE_BYTES)
    s = C_LIMIT;
  if (s == C_OK)
    c_hash(bytes, length, digest);
  free(bytes);
  return s;
}
static FILE *open_log(const char *directory, const char *name) {
  char filename[PATH_BYTES];
  return path(filename, directory, name) == C_OK ? fopen(filename, "wb") : NULL;
}
static c_status close_log(FILE *file) {
  int failed = ferror(file);
  return fclose(file) || failed ? C_IO : C_OK;
}
static cr_export_options options(const char *parent, const char *checkpoint,
                                 const char *qualification) {
  cr_export_options out = {0};
  out.struct_size = sizeof(out);
  out.api_version = CR_API_VERSION;
  out.qualification = qualification ? CR_QUALIFIED : CR_EXPERIMENTAL;
  out.parent_digest = parent;
  out.checkpoint_digest = checkpoint;
  out.qualification_digest = qualification;
  out.task_profile = CR_CODE_HELPER_PROFILE;
  out.observation_schema = CR_CODE_OBSERVATION_SCHEMA;
  out.action_catalog = CR_CODE_ACTION_CATALOG;
  out.provenance = "registered native M5 lower-bound strategy trial";
  out.qualification_reference = qualification ? "qualification.md" : NULL;
  return out;
}
static c_status runtime_identity(const c_trainer *trainer,
                                 const cr_export_options *opts,
                                 char digest[C_DIGEST_HEX],
                                 const char *filename) {
  cr_model *model = NULL;
  cr_model_info info = {0};
  c_status s = c_trainer_export_model(trainer, opts, &model);
  info.struct_size = sizeof(info);
  info.api_version = CR_API_VERSION;
  if (s == C_OK && cr_model_info_get(model, &info) != CR_OK)
    s = C_CORRUPT;
  if (s == C_OK)
    memcpy(digest, info.metadata.model_digest, C_DIGEST_HEX);
  if (s == C_OK && filename && cr_model_save_file(model, filename) != CR_OK)
    s = C_IO;
  cr_model_destroy(model);
  return s;
}

/* Complete linear correctness oracle, separate from all candidate dispatch. */
static size_t oracle(const int32_t *values, size_t count, int32_t key,
                     uint64_t *cost) {
  size_t answer = count;
  *cost = 0u;
  for (size_t i = 0; i < count; ++i) {
    ++*cost;
    if (values[i] >= key) {
      answer = i;
      break;
    }
  }
  return answer;
}
static c_status construct(workload *out, unsigned split, unsigned index) {
  uint64_t rng = UINT64_C(0x47ed25b82316f091) ^
                 ((uint64_t)(split + 1u) << 48u) ^ (uint64_t)index;
  memset(out, 0, sizeof(*out));
  out->region = index / 5u;
  out->count = out->region ? 2048u + (size_t)(c_random(&rng) % 2049u) : 0u;
  const unsigned family = index % 2u;
  for (size_t i = 0; i < out->count; ++i) {
    int64_t value;
    if (split == 0u)
      value = family ? (int64_t)(i / 2u) * 8 : (int64_t)i * 5;
    else if (split == 1u)
      value = family ? (int64_t)(i / 3u) * 12
                     : (int64_t)i * 5 + (int64_t)(i / 11u) * 2;
    else
      value = family ? (int64_t)(i - i / 7u) * 4
                     : (int64_t)i * 4 + (int64_t)(i / 5u) * 3;
    out->values[i] = (int32_t)(value - 20000);
  }
  size_t rank = 0;
  if (out->region == 1u)
    out->key = out->values[0] - 1;
  else if (out->region == 2u)
    out->key = out->values[out->count - 1u] + 1;
  else if (out->region) {
    if (out->region == 3u)
      rank = 1u + index % 3u;
    else if (out->region == 4u)
      rank = 8u + index % 5u;
    else if (out->region == 5u)
      rank = out->count / 2u + index % 17u;
    else if (out->region == 6u)
      rank = out->count - 2u - index % 4u;
    else
      rank = 64u + index % 193u;
    out->key = out->values[rank];
  }
  out->observation.struct_size = sizeof(out->observation);
  out->observation.api_version = CR_API_VERSION;
  if (cr_code_observe(out->values, out->count, out->key, &out->observation) !=
      CR_OK)
    return C_CORRUPT;
  out->oracle = oracle(out->values, out->count, out->key, &out->oracle_cost);
  out->target = 0u;
  for (unsigned a = 0; a < 4u; ++a) {
    if (cr_code_execute(a, out->values, out->count, out->key, &out->result[a],
                        &out->cost[a]) != CR_OK ||
        out->result[a] != out->oracle)
      return C_CORRUPT;
    if (out->cost[a] < out->cost[out->target])
      out->target = a;
  }
  size_t production_cost = 0u;
  if (c_lower_bound((const int *)out->values, out->count, (int)out->key,
                    &production_cost) != out->oracle ||
      production_cost != out->cost[0])
    return C_CORRUPT;
  out->heuristic = 0u;
  if (out->count) {
    if (out->key <= out->values[0] || out->observation.estimated_rank <= 2u)
      out->heuristic = 1u;
    else if (out->key > out->values[out->count - 1u] ||
             out->count - out->observation.estimated_rank <= 2u)
      out->heuristic = 2u;
  }
  c_writer bytes = {0};
  c_put_u64(&bytes, out->count);
  c_put_u32(&bytes, (uint32_t)out->key);
  for (size_t i = 0; i < out->count; ++i)
    c_put_u32(&bytes, (uint32_t)out->values[i]);
  c_status s = bytes.status;
  if (s == C_OK)
    c_hash(bytes.data, bytes.length, out->digest);
  free(bytes.data);
  return s;
}
static c_status retain_workloads(trial *t, workload *cases, unsigned split) {
  char name[64];
  snprintf(name, sizeof(name), "%s-measurements.tsv", splits[split]);
  FILE *log = open_log(t->directory, name);
  if (!log)
    return C_IO;
  fprintf(
      log,
      "case\tregion\tcount\tkey\tband\testimate\toracle\toracle_"
      "comparisons\ttarget\theuristic\tdigest\taction\tresult\tcomparisons\n");
  c_writer bytes = {0};
  c_put_u32(&bytes, CASES);
  for (unsigned i = 0; i < CASES; ++i) {
    uint64_t started = c_monotonic_ns();
    c_status s = construct(&cases[i], split, i);
    t->measurement_ns += c_monotonic_ns() - started;
    if (s != C_OK) {
      fclose(log);
      free(bytes.data);
      return s;
    }
    const workload *w = &cases[i];
    c_put_u64(&bytes, w->count);
    c_put_u32(&bytes, (uint32_t)w->key);
    for (size_t j = 0; j < w->count; ++j)
      c_put_u32(&bytes, (uint32_t)w->values[j]);
    for (unsigned a = 0; a < 4u; ++a)
      fprintf(log,
              "%u\t%u\t%zu\t%d\t%u\t%" PRIu64 "\t%zu\t%" PRIu64
              "\t%u\t%u\t%s\t%u\t%zu\t%" PRIu64 "\n",
              i, w->region, w->count, (int)w->key, w->observation.band,
              w->observation.estimated_rank, w->oracle, w->oracle_cost,
              w->target, w->heuristic, w->digest, a, w->result[a], w->cost[a]);
  }
  c_status s = close_log(log);
  snprintf(name, sizeof(name), "%s-workloads.bin", splits[split]);
  if (s == C_OK)
    s = bytes.status;
  if (s == C_OK)
    s = write_artifact(t->directory, name, bytes.data, bytes.length);
  free(bytes.data);
  return s;
}
static unsigned choose(const double scores[4]) {
  unsigned best = 0u;
  for (unsigned i = 1u; i < 4u; ++i)
    if (scores[i] > scores[best])
      best = i;
  return best;
}
static c_status evaluate(trial *t, const c_model *model, const c_model *frozen,
                         const workload *cases, unsigned seed,
                         unsigned condition, unsigned split, evaluation *out) {
  char name[96];
  snprintf(name, sizeof(name), "%" PRIu64 "-%s-%s-predictions.tsv", seeds[seed],
           conditions[condition], splits[split]);
  FILE *log = open_log(t->directory, name);
  if (!log)
    return C_IO;
  memset(out, 0, sizeof(*out));
  fprintf(log, "case\tband\taction\tresult\tcomparisons\tp0\tp1\tp2\tp3\tfrozen"
               "_action\tinference_ns\n");
  double mass[4] = {1, 1, 1, 1};
  c_status s = C_OK;
  for (unsigned i = 0; i < CASES && s == C_OK; ++i) {
    const workload *w = &cases[i];
    double scores[4], base[4];
    uint64_t started = c_monotonic_ns();
    cr_code_observation observation = {0};
    observation.struct_size = sizeof(observation);
    observation.api_version = CR_API_VERSION;
    if (cr_code_observe(w->values, w->count, w->key, &observation) != CR_OK)
      s = C_CORRUPT;
    uint64_t encoded = c_monotonic_ns();
    t->encoding_ns += encoded - started;
    if (s == C_OK)
      s = c_model_predict(model, C_CODE, observation.features, 3u, mass, scores,
                          4u);
    uint64_t elapsed = c_monotonic_ns() - started;
    if (s == C_OK)
      s = c_model_predict(frozen, C_CODE, observation.features, 3u, mass, base,
                          4u);
    if (s != C_OK)
      break;
    unsigned action = choose(scores), frozen_action = choose(base);
    out->inference_ns += elapsed;
    out->comparisons += w->cost[action];
    out->binary += w->cost[0];
    out->heuristic += w->cost[w->heuristic];
    out->frozen += w->cost[frozen_action];
    out->oracle += w->cost[w->target];
    out->semantic_failures += w->result[action] != w->oracle;
    fprintf(log,
            "%u\t%u\t%u\t%zu\t%" PRIu64
            "\t%.17g\t%.17g\t%.17g\t%.17g\t%u\t%" PRIu64 "\n",
            i, observation.band, action, w->result[action], w->cost[action],
            scores[0], scores[1], scores[2], scores[3], frozen_action, elapsed);
  }
  c_status closed = close_log(log);
  return s == C_OK ? closed : s;
}
/* Numerical deployment conformance is not an additional quality experiment. */
static c_status deployment_check(trial *t, const c_trainer *trainer,
                                 const workload *cases) {
  cr_model *model = NULL;
  cr_export_options opts = options(NULL, NULL, NULL);
  c_status s = c_trainer_export_model(trainer, &opts, &model);
  double mass[4] = {1, 1, 1, 1};
  for (unsigned i = 0; i < CASES && s == C_OK; ++i) {
    double actual[4], reference[4];
    cr_model_info info = {0};
    cr_code_observation observation = {0};
    info.struct_size = sizeof(info);
    info.api_version = CR_API_VERSION;
    observation.struct_size = sizeof(observation);
    observation.api_version = CR_API_VERSION;
    uint64_t started = c_monotonic_ns();
    if (cr_model_info_get(model, &info) != CR_OK ||
        strcmp(info.metadata.task_profile, CR_CODE_HELPER_PROFILE) ||
        cr_code_observe(cases[i].values, cases[i].count, cases[i].key,
                        &observation) != CR_OK ||
        cr_model_predict(model, CR_CODE, observation.features, 3u, mass, actual,
                         4u) != CR_OK)
      s = C_CORRUPT;
    t->deployment_ns += c_monotonic_ns() - started;
    ++t->deployment_calls;
    if (s == C_OK)
      s = c_model_predict(trainer->model, C_CODE, observation.features, 3u,
                          mass, reference, 4u);
    for (unsigned a = 0; a < 4u && s == C_OK; ++a)
      if (fabs(actual[a] - reference[a]) > 1e-12)
        s = C_CORRUPT;
  }
  cr_model_destroy(model);
  return s;
}
static c_status retention_loss(const trial *t, const c_model *model,
                               double *out) {
  double sum = 0.0, mass[4] = {1, 1, 1, 1};
  if (t->source_length < 8u)
    return C_INVALID;
  for (unsigned i = 0; i < 8u; ++i) {
    size_t at = i * (t->source_length - 1u) / 7u;
    double input[32], scores[257];
    c_status s = c_encode(t->source, at, input);
    if (s == C_OK)
      s = c_model_predict(model, C_TEXT, input, 3u, mass, scores, 257u);
    if (s != C_OK)
      return s;
    sum -= log(fmax(scores[t->source[at]], 1e-300));
  }
  *out = sum / 8.0;
  return C_OK;
}
static int unrelated_equal(const c_model *before, const c_trainer *after) {
  if (after->model->shared_clock || after->policy.clock ||
      after->model->shared_enabled || after->policy.enabled ||
      memcmp(before->shared_scale, after->model->shared_scale,
             sizeof(before->shared_scale)))
    return 0;
  for (unsigned g = 0; g < 2u; ++g)
    if (memcmp(before->expert[g].text, after->model->expert[g].text,
               sizeof(before->expert[g].text)))
      return 0;
  return 1;
}
static c_status fit(trial *t, c_trainer *trainer, const workload *cases,
                    unsigned seed, unsigned condition, const char *parent) {
  char name[96];
  snprintf(name, sizeof(name), "%" PRIu64 "-%s-training.tsv", seeds[seed],
           conditions[condition]);
  FILE *log = open_log(t->directory, name);
  if (!log)
    return C_IO;
  fprintf(log, "kind\tepoch\tcase_or_"
               "generation\tgraph\tupdates\tqueued\treceipt_or_world\n");
  c_status s = C_OK;
  t->physical_ok[seed][condition] = 1u;
  uint64_t started = c_monotonic_ns();
  for (unsigned epoch = 0; epoch < EPOCHS && s == C_OK; ++epoch) {
    for (unsigned i = 0; i < CASES && s == C_OK; ++i) {
      const workload *w = &cases[i];
      char record[768], receipt[65], record_path[96];
      int n = snprintf(
          record, sizeof(record),
          "source=%s evaluator=%s parent=%s seed=%" PRIu64
          " condition=%s epoch=%u case=%u workload=%s input=01%02x "
          "target=%u oracle=%zu costs=%" PRIu64 ",%" PRIu64 ",%" PRIu64
          ",%" PRIu64,
          t->source_digest, t->evaluator_digest, parent, seeds[seed],
          conditions[condition], epoch, i, w->digest, w->observation.band,
          w->target, w->oracle, w->cost[0], w->cost[1], w->cost[2], w->cost[3]);
      if (n < 0 || (size_t)n >= sizeof(record)) {
        s = C_LIMIT;
        break;
      }
      c_hash(record, (size_t)n, receipt);
      snprintf(record_path, sizeof(record_path), "measured/train/%u/%u", epoch,
               i);
      s = c_context_admit(trainer->context, C_TRAIN_MEASUREMENT, C_TRAIN,
                          record_path, "independent native verifier",
                          (const unsigned char *)record, (size_t)n, NULL);
      if (s == C_OK)
        s = c_trainer_enqueue_measurement(trainer, w->observation.bytes,
                                          CR_CODE_INPUT_BYTES, w->target, 3u,
                                          parent, t->evaluator_digest, receipt);
      fprintf(log, "receipt\t%u\t%u\t0\t0\t0\t%s %s\n", epoch, i, receipt,
              record);
    }
    unsigned generations = 0u;
    while (s == C_OK && trainer->report.queued && generations < 2048u) {
      unsigned char after[C_WORLD_CELLS];
      unsigned graph = 0;
      c_life_evolve(trainer->cells, after, &graph);
      uint64_t before = trainer->report.updates;
      unsigned char world[C_WORLD_CELLS];
      memcpy(world, trainer->cells, sizeof(world));
      c_training_report report;
      s = condition == 0u
              ? c_trainer_step(trainer, 1u, &report)
              : c_trainer_research_step(trainer, 1u, condition, &report);
      ++generations;
      if (s != C_OK)
        break;
      if (report.updates != before && !(graph & 2u))
        t->physical_ok[seed][condition] = 0u;
      fprintf(log,
              "generation\t%u\t%" PRIu64 "\t%u\t%" PRIu64 "\t%" PRIu64 "\t",
              epoch, report.generation, graph, report.updates, report.queued);
      if (report.updates != before)
        for (size_t cell = 0; cell < C_WORLD_CELLS; ++cell)
          fprintf(log, "%02x", (unsigned)world[cell]);
      fputc('\n', log);
    }
    if (s == C_OK && trainer->report.queued)
      s = C_DEFERRED;
  }
  t->fitting_ns += c_monotonic_ns() - started;
  t->generations[seed][condition] = trainer->generation;
  t->contacts[seed][condition] = trainer->report.contacts;
  if (s == C_OK && (trainer->report.updates != CASES * EPOCHS ||
                    trainer->model->expert[0].clock != CASES * EPOCHS ||
                    trainer->model->expert[1].clock != CASES * EPOCHS ||
                    trainer->receipt_count != CASES * EPOCHS))
    s = C_CORRUPT;
  for (size_t i = 0; s == C_OK && i < trainer->receipt_count; ++i)
    if (!trainer->receipt_consumed[i])
      s = C_CORRUPT;
  c_status closed = close_log(log);
  return s == C_OK ? closed : s;
}
static int quality(const evaluation *e) {
  return !e->semantic_failures && e->comparisons * 100u <= e->binary * 80u &&
         e->comparisons * 100u <= e->heuristic * 90u &&
         e->comparisons * 100u <= e->frozen * 90u &&
         e->inference_ns / CASES <= UINT64_C(10000000);
}
static c_status retain_candidate(trial *t, const c_trainer *trainer,
                                 unsigned seed, unsigned condition) {
  char filename[PATH_BYTES], name[96];
  snprintf(name, sizeof(name), "%" PRIu64 "-%s-model.bin", seeds[seed],
           conditions[condition]);
  c_writer writer = {0};
  c_model_write(&writer, trainer->model, 1);
  c_status s = writer.status;
  if (s == C_OK)
    s = write_artifact(t->directory, name, writer.data, writer.length);
  free(writer.data);
  if (condition == 0u && s == C_OK) {
    snprintf(name, sizeof(name), "%" PRIu64 "-life-checkpoint.centroid",
             seeds[seed]);
    s = path(filename, t->directory, name);
    if (s == C_OK)
      s = c_trainer_save(trainer, filename);
    if (s == C_OK && seed == 0u)
      s = digest_file(filename, t->checkpoint_digest);
  }
  return s;
}
static c_status run_seed(trial *t, const workload *train, const workload *dev,
                         unsigned seed, c_trainer **candidates,
                         c_model **initial) {
  c_status s = c_trainer_create(2u, seeds[seed], &candidates[0]);
  for (unsigned c = 1u; c < CONDITIONS && s == C_OK; ++c)
    s = c_trainer_research_create(2u, seeds[seed], &candidates[c]);
  if (s != C_OK)
    return s;
  *initial = malloc(sizeof(**initial));
  if (!*initial)
    return C_NOMEM;
  memcpy(*initial, candidates[0]->model, sizeof(**initial));
  char parent[65], filename[PATH_BYTES], name[80];
  snprintf(name, sizeof(name), "%" PRIu64 "-parent.crmodel", seeds[seed]);
  s = path(filename, t->directory, name);
  cr_export_options opts = options(NULL, NULL, NULL);
  if (s == C_OK)
    s = runtime_identity(candidates[0], &opts, parent, filename);
  if (s == C_OK && seed == 0u)
    memcpy(t->parent_digest, parent, sizeof(parent));
  snprintf(name, sizeof(name), "%" PRIu64 "-parent.centroid", seeds[seed]);
  if (s == C_OK)
    s = path(filename, t->directory, name);
  if (s == C_OK)
    s = c_trainer_save(candidates[0], filename);
  double initial_loss = 0.0;
  if (s == C_OK)
    s = retention_loss(t, *initial, &initial_loss);
  for (unsigned c = 0; c < CONDITIONS && s == C_OK; ++c) {
    s = c_context_admit(candidates[c]->context, C_SOURCE, C_TRAIN,
                        "src/domain/algorithms.c", "current production source",
                        t->source, t->source_length, NULL);
    if (s == C_OK && c < 3u)
      s = fit(t, candidates[c], train, seed, c, parent);
    c_status retained = retain_candidate(t, candidates[c], seed, c);
    if (s == C_OK)
      s = retained;
    double loss = 0.0;
    if (s == C_OK)
      s = retention_loss(t, candidates[c]->model, &loss);
    t->retention[seed][c] = loss - initial_loss;
    t->retained_ok[seed][c] = unrelated_equal(*initial, candidates[c]);
    if (s == C_OK)
      s = evaluate(t, candidates[c]->model, *initial, dev, seed, c, 1u,
                   &t->dev[seed][c]);
    if (s == C_OK && c == 0u)
      s = deployment_check(t, candidates[c], dev);
  }
  return s;
}
static c_status report(trial *t, c_status status) {
  FILE *file = open_log(t->directory, "report.md");
  if (!file)
    return C_IO;
  uint64_t elapsed = c_monotonic_ns() - t->started_ns;
  fprintf(file,
          "# Native SDK strategy qualification\n\nStatus: %s. Accepted: %u. "
          "AUDIT opened: %u.\n\n",
          c_status_string(status), t->accepted, t->audit_opened);
  fprintf(file,
          "Protocol SHA256: `%s`. Production source SHA256: `%s`. Native "
          "verifier executable SHA256: `%s`.\n\n",
          t->protocol_digest, t->source_digest, t->evaluator_digest);
  fprintf(
      file,
      "| Seed | Condition | DEV comparisons | AUDIT comparisons | Binary "
      "DEV/AUDIT | Heuristic DEV/AUDIT | Frozen DEV/AUDIT | Oracle DEV/AUDIT | "
      "Retention CE change | Generations | Physical contacts |\n| --- | --- | "
      "--- | --- | --- | --- | --- | --- | --- | --- | --- |\n");
  for (unsigned seed = 0; seed < SEEDS; ++seed)
    for (unsigned c = 0; c < CONDITIONS; ++c) {
      const evaluation *d = &t->dev[seed][c], *a = &t->audit[seed][c];
      fprintf(file,
              "| %" PRIu64 " | %s | %" PRIu64 " | %" PRIu64 " | %" PRIu64
              "/%" PRIu64 " | %" PRIu64 "/%" PRIu64 " | %" PRIu64 "/%" PRIu64
              " | %" PRIu64 "/%" PRIu64 " | %.17g | %" PRIu64 " | %" PRIu64
              " |\n",
              seeds[seed], conditions[c], d->comparisons, a->comparisons,
              d->binary, a->binary, d->heuristic, a->heuristic, d->frozen,
              a->frozen, d->oracle, a->oracle, t->retention[seed][c],
              t->generations[seed][c], t->contacts[seed][c]);
    }
  fprintf(file,
          "\nWhole run: %.6f s; encoding: %.6f s; candidate "
          "construction/measurement: %.6f s; fitting/scheduling: %.6f s; peak "
          "process memory: %" PRIu64 " bytes.\n\n",
          (double)elapsed / 1e9, (double)t->encoding_ns / 1e9,
          (double)t->measurement_ns / 1e9, (double)t->fitting_ns / 1e9,
          c_process_peak_memory_bytes());
  fprintf(
      file,
      "Portable runtime validation/observation/inference mean: %.3f "
      "microseconds over %" PRIu64
      " observations; each portable CODE score agrees with the fitted trainer "
      "within 1e-12. This timed path includes full model information "
      "validation and endpoint observation before inference.\n\n",
      t->deployment_calls
          ? (double)t->deployment_ns / (double)t->deployment_calls / 1000.0
          : 0.0,
      t->deployment_calls);
  fprintf(file,
          "Each trained condition used 480 joint updates and 960 owner "
          "updates; frozen model used zero. Model has 16,800 parameter values; "
          "complete parent, all attempted model states, Life checkpoints, "
          "complete arrays, probabilities, independent candidate outcomes, "
          "receipt bindings and contact worlds are retained beside this "
          "report. TEXT readout/moments and shared values are checked bitwise; "
          "TEXT routing retention uses eight source-bound probes.\n\n");
  fprintf(file,
          "Comparison savings measure bounded algorithm work, not net "
          "wall-clock search speed. Life, frozen and deterministic schedules "
          "receive identical examples, update order and fitting budget; tied "
          "results establish no Life superiority. This is finite strategy "
          "recommendation within this profile, not source generation, LLM "
          "coding improvement or general code understanding. Private context "
          "and receipt ledgers are absent from deployment bundles.\n");
  return close_log(file);
}
static c_status manifest_add(trial *t, c_writer *writer, const char *name) {
  char filename[PATH_BYTES], digest[65], line[256];
  unsigned char *bytes = NULL;
  size_t length = 0;
  c_status s = path(filename, t->directory, name);
  if (s == C_OK)
    s = c_read_file(filename, &bytes, &length);
  if (s == C_OK && length > C_MAX_FILE_BYTES)
    s = C_LIMIT;
  if (s == C_OK) {
    c_hash(bytes, length, digest);
    int n = snprintf(line, sizeof(line), "%s\t%zu\t%s\n", digest, length, name);
    if (n < 0 || (size_t)n >= sizeof(line))
      s = C_LIMIT;
    else {
      c_put_bytes(writer, line, (size_t)n);
      s = writer->status;
    }
  }
  free(bytes);
  return s;
}
static c_status evidence_manifest(trial *t, char digest[65]) {
  c_writer writer = {0};
  c_status s = C_OK;
  static const char *const fixed[] = {"registered-protocol.md",
                                      "production-source.c",
                                      "native-verifier.bin", "report.md"};
  for (unsigned i = 0; i < sizeof(fixed) / sizeof(fixed[0]) && s == C_OK; ++i)
    s = manifest_add(t, &writer, fixed[i]);
  char name[96];
  for (unsigned split = 0; split < 3u && s == C_OK; ++split) {
    snprintf(name, sizeof(name), "%s-workloads.bin", splits[split]);
    s = manifest_add(t, &writer, name);
    snprintf(name, sizeof(name), "%s-measurements.tsv", splits[split]);
    if (s == C_OK)
      s = manifest_add(t, &writer, name);
  }
  for (unsigned seed = 0; seed < SEEDS && s == C_OK; ++seed) {
    const char *const suffix[] = {"parent.crmodel", "parent.centroid",
                                  "life-checkpoint.centroid"};
    for (unsigned i = 0; i < 3u && s == C_OK; ++i) {
      snprintf(name, sizeof(name), "%" PRIu64 "-%s", seeds[seed], suffix[i]);
      s = manifest_add(t, &writer, name);
    }
    for (unsigned c = 0; c < CONDITIONS && s == C_OK; ++c) {
      snprintf(name, sizeof(name), "%" PRIu64 "-%s-model.bin", seeds[seed],
               conditions[c]);
      s = manifest_add(t, &writer, name);
      if (c < 3u && s == C_OK) {
        snprintf(name, sizeof(name), "%" PRIu64 "-%s-training.tsv", seeds[seed],
                 conditions[c]);
        s = manifest_add(t, &writer, name);
      }
      for (unsigned split = 1; split < 3u && s == C_OK; ++split) {
        snprintf(name, sizeof(name), "%" PRIu64 "-%s-%s-predictions.tsv",
                 seeds[seed], conditions[c], splits[split]);
        s = manifest_add(t, &writer, name);
      }
    }
  }
  if (s == C_OK) {
    c_hash(writer.data, writer.length, digest);
    s = write_artifact(t->directory, "evidence.sha256.tsv", writer.data,
                       writer.length);
  }
  free(writer.data);
  return s;
}
static c_status qualify(trial *t) {
  char filename[PATH_BYTES];
  cr_export_options opts =
      options(t->parent_digest, t->checkpoint_digest, NULL);
  c_status s = runtime_identity(t->selected, &opts, t->selected_digest, NULL);
  char manifest[65];
  if (s == C_OK)
    s = evidence_manifest(t, manifest);
  if (s != C_OK)
    return s;
  char record[4096];
  int n = snprintf(
      record, sizeof(record),
      "# Accepted native lower-bound strategy model\n\n"
      "Profile: `%s`. Observation: `%s`. Catalog: `%s`.\n\n"
      "Exact value-model SHA256: `%s`. Parent: `%s`. Training checkpoint: "
      "`%s`.\n\n"
      "Protocol: `%s`. Production source: `%s`. Independent native verifier "
      "executable: `%s`.\n\n"
      "Complete retained evidence manifest SHA256: `%s` (`evidence.sha256.tsv` "
      "gives the exact digest and length of every required raw artifact and "
      "report).\n\n"
      "Registered seeds 1409, 3251, 7907 all pass fixed DEV and fresh AUDIT "
      "comparison, semantic, physical contact, head-retention and resource "
      "gates. Fixed release seed 1409 receives 480 Life-authorized CODE "
      "updates for two owners; shared and policy clocks remain zero. Every "
      "attempt is retained in this directory. Raw detailed evidence is in "
      "report.md and the named complete "
      "workload/measurement/prediction/training records.\n\n"
      "Qualified for bounded sorted int32 lower-bound strategy recommendation "
      "only. Host must validate this record and the referenced identities, "
      "retains rollback parent, and chooses use outside its search hot path. "
      "No Life superiority, LLM coding or general generation claim.\n",
      CR_CODE_HELPER_PROFILE, CR_CODE_OBSERVATION_SCHEMA,
      CR_CODE_ACTION_CATALOG, t->selected_digest, t->parent_digest,
      t->checkpoint_digest, t->protocol_digest, t->source_digest,
      t->evaluator_digest, manifest);
  if (n < 0 || (size_t)n >= sizeof(record))
    return C_LIMIT;
  char qualification[65];
  c_hash(record, (size_t)n, qualification);
  if (s == C_OK)
    s = write_artifact(t->directory, "qualification.md", record, (size_t)n);
  opts = options(t->parent_digest, t->checkpoint_digest, qualification);
  if (s == C_OK)
    s = path(filename, t->directory, "qualified.crmodel");
  char actual[65];
  if (s == C_OK)
    s = runtime_identity(t->selected, &opts, actual, filename);
  if (s == C_OK && strcmp(actual, t->selected_digest))
    s = C_CORRUPT;
  return s;
}
int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr,
            "usage: centroid-sdk-qualify PROJECT_ROOT NEW_OUTPUT_DIRECTORY\n");
    return 2;
  }
  trial *t = calloc(1, sizeof(*t));
  workload *train = calloc(CASES, sizeof(*train)),
           *dev = calloc(CASES, sizeof(*dev));
  workload *audit = NULL;
  c_trainer *candidates[SEEDS][CONDITIONS] = {{0}};
  c_model *initial[SEEDS] = {0};
  if (!t || !train || !dev) {
    free(t);
    free(train);
    free(dev);
    return 1;
  }
  t->started_ns = c_monotonic_ns();
  c_status s = strlen(argv[1]) >= PATH_BYTES || strlen(argv[2]) >= PATH_BYTES
                   ? C_LIMIT
                   : C_OK;
  if (s == C_OK) {
    memcpy(t->root, argv[1], strlen(argv[1]) + 1u);
    memcpy(t->directory, argv[2], strlen(argv[2]) + 1u);
    s = c_new_directory(t->directory);
  }
  if (s != C_OK) {
    fprintf(stderr, "qualification: output must be a new directory: %s\n",
            c_status_string(s));
    free(t);
    free(train);
    free(dev);
    return 1;
  }
  char filename[PATH_BYTES];
  unsigned char *protocol = NULL;
  size_t protocol_length = 0;
  if (s == C_OK)
    s = path(filename, t->root, "research/sdk-release/M5_PROTOCOL.md");
  if (s == C_OK)
    s = c_read_file(filename, &protocol, &protocol_length);
  if (s == C_OK && protocol_length > C_MAX_FILE_BYTES)
    s = C_LIMIT;
  if (s == C_OK) {
    c_hash(protocol, protocol_length, t->protocol_digest);
    s = write_artifact(t->directory, "registered-protocol.md", protocol,
                       protocol_length);
  }
  free(protocol);
  unsigned char *verifier = NULL;
  size_t verifier_length = 0;
  if (s == C_OK)
    s = c_read_file(argv[0], &verifier, &verifier_length);
  if (s == C_OK && verifier_length > C_MAX_FILE_BYTES)
    s = C_LIMIT;
  if (s == C_OK) {
    c_hash(verifier, verifier_length, t->evaluator_digest);
    s = write_artifact(t->directory, "native-verifier.bin", verifier,
                       verifier_length);
  }
  free(verifier);
  if (s == C_OK)
    s = path(filename, t->root, "src/domain/algorithms.c");
  if (s == C_OK)
    s = c_read_file(filename, &t->source, &t->source_length);
  if (s == C_OK && t->source_length > C_MAX_FILE_BYTES)
    s = C_LIMIT;
  if (s == C_OK) {
    c_hash(t->source, t->source_length, t->source_digest);
    s = write_artifact(t->directory, "production-source.c", t->source,
                       t->source_length);
  }
  if (s == C_OK)
    s = retain_workloads(t, train, 0u);
  if (s == C_OK)
    s = retain_workloads(t, dev, 1u);
  for (unsigned seed = 0; seed < SEEDS && s == C_OK; ++seed)
    s = run_seed(t, train, dev, seed, candidates[seed], &initial[seed]);
  for (unsigned seed = 0; seed < SEEDS && s == C_OK; ++seed)
    if (!quality(&t->dev[seed][0]) || t->retention[seed][0] > .05 ||
        !t->retained_ok[seed][0] || !t->physical_ok[seed][0])
      s = C_DEFERRED;
  if (s == C_OK) {
    audit = calloc(CASES, sizeof(*audit));
    if (!audit)
      s = C_NOMEM;
  }
  if (s == C_OK) {
    t->audit_opened = 1u;
    s = retain_workloads(t, audit, 2u);
  }
  for (unsigned seed = 0; seed < SEEDS && s == C_OK; ++seed)
    for (unsigned c = 0; c < CONDITIONS && s == C_OK; ++c)
      s = evaluate(t, candidates[seed][c]->model, initial[seed], audit, seed, c,
                   2u, &t->audit[seed][c]);
  for (unsigned seed = 0; seed < SEEDS && s == C_OK; ++seed)
    s = deployment_check(t, candidates[seed][0], audit);
  for (unsigned seed = 0; seed < SEEDS && s == C_OK; ++seed)
    if (!quality(&t->audit[seed][0]))
      s = C_DEFERRED;
  if (s == C_OK &&
      (c_monotonic_ns() - t->started_ns > UINT64_C(120000000000) ||
       !c_process_peak_memory_bytes() || !t->deployment_calls ||
       t->deployment_ns / t->deployment_calls > UINT64_C(10000000) ||
       c_process_peak_memory_bytes() > UINT64_C(128) * 1024u * 1024u))
    s = C_LIMIT;
  if (s == C_OK) {
    t->selected = candidates[0][0];
    t->accepted = 1u;
    s = report(t, s);
    if (s == C_OK)
      s = qualify(t);
    if (s != C_OK)
      t->accepted = 0u;
  }
  if (s != C_OK) {
    c_status reported = report(t, s);
    if (reported != C_OK)
      fprintf(stderr, "failed to retain qualification report: %s\n",
              c_status_string(reported));
  }
  printf("qualification=%s accepted=%u audit_opened=%u artifacts=%s\n",
         c_status_string(s), t->accepted, t->audit_opened, t->directory);
  for (unsigned seed = 0; seed < SEEDS; ++seed) {
    for (unsigned c = 0; c < CONDITIONS; ++c)
      c_trainer_destroy(candidates[seed][c]);
    free(initial[seed]);
  }
  free(t->source);
  free(t);
  free(train);
  free(dev);
  free(audit);
  return s == C_OK ? 0 : 1;
}
