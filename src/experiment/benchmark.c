/* A fixed native byte-prediction experiment. This measures the current
 * mechanism; it does not compare alternative schedulers or establish arbitrary
 * coding skill. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../../data/audit/benchmark_fixtures.h"
#include "internal.h"
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define B_PATH 4096u
#define B_SEED UINT64_C(20031)
#define B_GROUPS 2u
#define B_ELIGIBLE 3u
#define B_EPOCHS 4u
#define B_MAX_GENERATIONS 1024u
#define B_OFFSETS 8u
#define B_TEXT_CASES 32u
#define B_CODE_CASES 2u
#define B_CASES (B_TEXT_CASES + B_CODE_CASES)
#define B_CONDITIONS 3u

static const unsigned char train_range[] =
    "int in_range(int x, int lo, int hi) { return x >= lo && x <= hi; }\n";
static const unsigned char train_clamp[] =
    "int clamp_value(int x, int lo, int hi) { if(x < lo) return lo; if(x > hi) "
    "return hi; return x; }\n";
static const unsigned char retention_range[] = "retention range lower upper";
static const unsigned char retention_clamp[] = "retention clamp bounded value";

typedef struct {
  const char *family, *file, *split;
  const unsigned char *bytes;
  size_t length;
  size_t offsets[B_OFFSETS];
} benchmark_family;

static const benchmark_family families[] = {
    {"range",
     "train/range.c",
     "TRAIN",
     train_range,
     sizeof(train_range) - 1,
     {0, 4, 8, 16, 24, 40, 53, sizeof(train_range) - 1}},
    {"clamp",
     "train/clamp.c",
     "TRAIN",
     train_clamp,
     sizeof(train_clamp) - 1,
     {0, 4, 16, 31, 47, 62, 79, sizeof(train_clamp) - 1}},
    C_BENCHMARK_AUDIT_FAMILIES};

typedef struct {
  double probabilities[C_TEXT_ACTIONS], loss;
  unsigned target, predicted;
} observation;

typedef struct {
  size_t count, correct;
  double loss;
} summary;

typedef struct {
  c_trainer *trainer;
  c_model *frozen;
  uint64_t source_ids[2], training_ms, total_ms;
  size_t checkpoint_bytes, changed_train, context_records;
  int code_weights_unchanged;
  observation rows[B_CONDITIONS][B_CASES];
  summary train[B_CONDITIONS], audit[B_CONDITIONS], code[B_CONDITIONS];
  c_training_report report;
} benchmark;

static c_status path_join(char out[B_PATH], const char *directory,
                          const char *name) {
  int length = snprintf(out, B_PATH, "%s/%s", directory, name);
  return length > 0 && (size_t)length < B_PATH ? C_OK : C_LIMIT;
}

static c_status artifact(const char *directory, const char *name,
                         const void *bytes, size_t length) {
  char path[B_PATH];
  c_status status = path_join(path, directory, name);
  return status == C_OK ? c_write_atomic(path, bytes, length) : status;
}

static void format(c_writer *writer, const char *text, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, text);
  int length = vsnprintf(buffer, sizeof(buffer), text, args);
  va_end(args);
  if (length < 0 || (size_t)length >= sizeof(buffer))
    writer->status = C_LIMIT;
  else
    c_put_bytes(writer, buffer, (size_t)length);
}

static c_status new_directory(const char *directory) {
#ifdef _WIN32
  return _mkdir(directory) == 0 ? C_OK : errno == EEXIST ? C_INVALID : C_IO;
#else
  return mkdir(directory, 0700) == 0 ? C_OK
         : errno == EEXIST           ? C_INVALID
                                     : C_IO;
#endif
}

static c_status write_protocol(const char *directory) {
  c_writer writer = {0};
  format(&writer,
         "# Frozen source-quality protocol\n\nRecipe: `%s`. Seed: %llu; "
         "groups: %u; eligibility: %u.\n\n",
         C_RECIPE, (unsigned long long)B_SEED, B_GROUPS, B_ELIGIBLE);
  format(&writer,
         "This protocol is published before any AUDIT prediction. Families and "
         "byte offsets below are authored constants, selected before observing "
         "model outputs. Each target is the immutable next byte at its offset, "
         "or EOS=256 at file length. Input encoding consumes only the causal "
         "prefix bytes[0:offset].\n\n");
  format(
      &writer,
      "TRAIN: range and clamp, 8 offsets each, 16 cases. AUDIT: counted-loop "
      "and array-lookup, 8 offsets each, 16 separate cases. AUDIT bytes remain "
      "in local frozen fixtures and audit/ artifacts; they are never admitted "
      "to context, enqueued, retrieved, or fitted.\n\n");
  format(&writer,
         "Budget: four epochs of all 16 TRAIN offsets, exactly 64 source task "
         "updates; at most 1024 generations total. Production Life contacts "
         "are the only update authority. No code-head training occurs. If the "
         "encounter budget is exhausted, preserve observed results and report "
         "deferral.\n\n");
  format(&writer,
         "Conditions: INITIAL before learning; LIFE_TRAINED after the fixed "
         "budget; FROZEN after the run with the same initial seed/model and no "
         "updates. FROZEN is a no-learning comparator, not an alternative "
         "scheduler. All cases contribute to denominators, including failures "
         "and EOS. Report mean next-byte negative-log-probability and top-1 "
         "accuracy; do not select on AUDIT results.\n\n");
  format(&writer,
         "Code retention: two fixed inference-only anchors, range target "
         "action=3 and clamp target action=1, use the complete anchor bytes as "
         "input. They are arbitrary predeclared probability-retention probes, "
         "not verified code-utility supervision. Track mean loss before/after "
         "and exact code readout preservation.\n\n");
  for (size_t i = 0; i < sizeof(families) / sizeof(families[0]); ++i) {
    char digest[C_DIGEST_HEX];
    c_hash(families[i].bytes, families[i].length, digest);
    format(&writer,
           "- %s %s: SHA256 `%s`, bytes=%zu, offsets=", families[i].split,
           families[i].family, digest, families[i].length);
    for (size_t j = 0; j < B_OFFSETS; ++j)
      format(&writer, "%s%zu", j == 0 ? "" : ",", families[i].offsets[j]);
    format(&writer, ".\n");
  }
  format(
      &writer,
      "\nReport loss and accuracy even if AUDIT quality deteriorates. Gains on "
      "these small authored source families do not establish general coding "
      "ability, transfer, or Life optimality. A deterministic alternative "
      "scheduler, matched compute comparisons, multiple seeds, longer "
      "sequences, and broader independent families remain pending.\n");
  c_status status = writer.status == C_OK ? artifact(directory, "protocol.md",
                                                     writer.data, writer.length)
                                          : writer.status;
  free(writer.data);
  return status;
}

static c_status freeze_fixtures(const char *directory) {
  char path[B_PATH];
  c_status status = path_join(path, directory, "train");
  if (status == C_OK)
    status = c_make_directory(path);
  if (status == C_OK)
    status = path_join(path, directory, "audit");
  if (status == C_OK)
    status = c_make_directory(path);
  for (size_t i = 0;
       status == C_OK && i < sizeof(families) / sizeof(families[0]); ++i)
    status = artifact(directory, families[i].file, families[i].bytes,
                      families[i].length);
  return status;
}

static c_status create_owners(benchmark *run) {
  c_status status = c_trainer_create(B_GROUPS, B_SEED, &run->trainer);
  if (status == C_OK)
    status = c_model_create(B_GROUPS, B_SEED, &run->frozen);
  for (unsigned group = 0; status == C_OK && group < B_GROUPS; ++group) {
    char trainer_digest[C_DIGEST_HEX], frozen_digest[C_DIGEST_HEX];
    status = c_model_fingerprint(c_trainer_model(run->trainer), group,
                                 trainer_digest);
    if (status == C_OK)
      status = c_model_fingerprint(run->frozen, group, frozen_digest);
    if (status == C_OK && strcmp(trainer_digest, frozen_digest) != 0)
      status = C_CORRUPT;
  }
  return status;
}

static c_status admit_training(benchmark *run) {
  for (size_t i = 0; i < 2; ++i) {
    c_status status = c_context_admit(
        c_trainer_context(run->trainer), C_SOURCE, C_TRAIN, families[i].file,
        "frozen-source-benchmark/v1", families[i].bytes, families[i].length,
        &run->source_ids[i]);
    if (status != C_OK)
      return status;
  }
  run->context_records = c_context_count(c_trainer_context(run->trainer));
  return run->context_records == 2 ? C_OK : C_CORRUPT;
}

static c_status observe(const c_model *model, c_head head,
                        const unsigned char *bytes, size_t length,
                        unsigned target, observation *row) {
  double input[C_FEATURES];
  const double mass[C_MAX_GROUPS] = {1, 1, 0, 0};
  c_status status = c_encode(bytes, length, input);
  if (status == C_OK)
    status = c_model_predict(model, head, input, B_ELIGIBLE, mass,
                             row->probabilities, C_TEXT_ACTIONS);
  if (status != C_OK)
    return status;
  unsigned actions = head == C_TEXT ? C_TEXT_ACTIONS : C_CODE_ACTIONS;
  row->target = target;
  for (unsigned action = 1; action < actions; ++action)
    if (row->probabilities[action] > row->probabilities[row->predicted])
      row->predicted = action;
  if (target >= actions || !isfinite(row->probabilities[target]) ||
      row->probabilities[target] <= 0)
    return C_CORRUPT;
  row->loss = -log(row->probabilities[target]);
  return C_OK;
}

static void summarize(summary *total, const observation *row) {
  ++total->count;
  total->correct += row->target == row->predicted ? 1u : 0u;
  total->loss += row->loss;
}

static c_status evaluate(benchmark *run, unsigned condition,
                         const c_model *model) {
  for (size_t family = 0; family < 4; ++family) {
    for (size_t i = 0; i < B_OFFSETS; ++i) {
      size_t offset = families[family].offsets[i],
             index = family * B_OFFSETS + i;
      unsigned target = offset == families[family].length
                            ? C_EOS
                            : families[family].bytes[offset];
      c_status status = observe(model, C_TEXT, families[family].bytes, offset,
                                target, &run->rows[condition][index]);
      if (status != C_OK)
        return status;
      summarize(family < 2 ? &run->train[condition] : &run->audit[condition],
                &run->rows[condition][index]);
    }
  }
  c_status status =
      observe(model, C_CODE, retention_range, sizeof(retention_range) - 1, 3,
              &run->rows[condition][B_TEXT_CASES]);
  if (status == C_OK)
    status =
        observe(model, C_CODE, retention_clamp, sizeof(retention_clamp) - 1, 1,
                &run->rows[condition][B_TEXT_CASES + 1]);
  if (status == C_OK)
    for (size_t i = B_TEXT_CASES; i < B_CASES; ++i)
      summarize(&run->code[condition], &run->rows[condition][i]);
  return status;
}

static c_status train_budget(benchmark *run) {
  uint64_t start = c_monotonic_ms();
  c_status status = C_OK;
  for (unsigned epoch = 0; epoch < B_EPOCHS; ++epoch) {
    for (size_t family = 0; family < 2; ++family)
      for (size_t offset = 0; offset < B_OFFSETS; ++offset) {
        status = c_trainer_enqueue_source(run->trainer, run->source_ids[family],
                                          families[family].offsets[offset],
                                          B_ELIGIBLE);
        if (status != C_OK)
          return status;
      }
    size_t target_updates = (size_t)(epoch + 1) * 2 * B_OFFSETS;
    while (run->report.completed < target_updates &&
           run->report.generation < B_MAX_GENERATIONS) {
      status = c_trainer_step(run->trainer, 1, &run->report);
      if (status != C_OK)
        return status;
    }
    if (run->report.completed < target_updates) {
      status = C_DEFERRED;
      break;
    }
  }
  run->training_ms = c_monotonic_ms() - start;
  return status;
}

static c_status verify_comparator(benchmark *run) {
  for (size_t i = 0; i < B_CASES; ++i)
    if (memcmp(run->rows[0][i].probabilities, run->rows[2][i].probabilities,
               sizeof(run->rows[0][i].probabilities)) != 0)
      return C_CORRUPT;
  for (size_t i = 0; i < 16; ++i)
    if (memcmp(run->rows[0][i].probabilities, run->rows[1][i].probabilities,
               sizeof(run->rows[0][i].probabilities)) != 0)
      ++run->changed_train;
  run->code_weights_unchanged = 1;
  const c_model *trained = c_trainer_model(run->trainer);
  for (unsigned group = 0; group < B_GROUPS; ++group) {
    if (c_model_group_clock(run->frozen, group) != 0)
      return C_CORRUPT;
    if (memcmp(trained->expert[group].code, run->frozen->expert[group].code,
               sizeof(trained->expert[group].code)) != 0)
      run->code_weights_unchanged = 0;
  }
  return c_context_count(c_trainer_context(run->trainer)) == 2 ? C_OK
                                                               : C_CORRUPT;
}

static c_status write_predictions(const benchmark *run, const char *directory) {
  static const char *const conditions[] = {"INITIAL", "LIFE_TRAINED", "FROZEN"};
  char path[B_PATH];
  c_status status = path_join(path, directory, "predictions.tsv");
  if (status != C_OK)
    return status;
  FILE *file = fopen(path, "wb");
  if (file == NULL)
    return C_IO;
  fprintf(file, "condition\tsplit\tfamily\thead\toffset\ttarget\tpredicted\tp_"
                "target\tloss");
  for (unsigned action = 0; action < C_TEXT_ACTIONS; ++action)
    fprintf(file, "\tp%u", action);
  fputc('\n', file);
  for (unsigned condition = 0; condition < B_CONDITIONS; ++condition)
    for (size_t i = 0; i < B_CASES; ++i) {
      const observation *row = &run->rows[condition][i];
      int text = i < B_TEXT_CASES;
      size_t family = text ? i / B_OFFSETS : 0;
      const char *name = text                ? families[family].family
                         : i == B_TEXT_CASES ? "retention-range"
                                             : "retention-clamp";
      fprintf(file, "%s\t%s\t%s\t%s\t%zu\t%u\t%u\t%.17g\t%.17g",
              conditions[condition],
              text ? families[family].split : "RETENTION", name,
              text ? "TEXT" : "CODE",
              text ? families[family].offsets[i % B_OFFSETS] : 0, row->target,
              row->predicted, row->probabilities[row->target], row->loss);
      for (unsigned action = 0; action < C_TEXT_ACTIONS; ++action) {
        if (text || action < C_CODE_ACTIONS)
          fprintf(file, "\t%.17g", row->probabilities[action]);
        else
          fprintf(file, "\tNA");
      }
      fputc('\n', file);
    }
  if (ferror(file))
    status = C_IO;
  if (fclose(file) != 0)
    status = C_IO;
  return status;
}

static double mean_loss(const summary *value) {
  return value->count != 0 ? value->loss / (double)value->count : 0;
}

static double accuracy(const summary *value) {
  return value->count != 0 ? (double)value->correct / (double)value->count : 0;
}

static c_status save_checkpoint(benchmark *run, const char *directory) {
  char path[B_PATH];
  c_status status = path_join(path, directory, "trained.checkpoint");
  if (status == C_OK)
    status = c_trainer_save(run->trainer, path);
  unsigned char *bytes = NULL;
  if (status == C_OK)
    status = c_read_file(path, &bytes, &run->checkpoint_bytes);
  free(bytes);
  return status;
}

static c_status write_metrics(const benchmark *run, const char *directory) {
  c_writer writer = {0};
  format(&writer,
         "metric\tvalue\ntrain_cases\t16\naudit_cases\t16\nconditions\t3\ntext_"
         "rows\t96\ncode_retention_cases\t2\ncode_rows\t6\nsource_context_"
         "records\t%zu\naudit_context_records\t0\n",
         run->context_records);
  format(&writer,
         "source_updates_budget\t64\nsource_updates\t%llu\ngenerations\t%"
         "llu\ncontacts\t%llu\ndeferred_generations\t%llu\nreseeds\t%"
         "llu\ngroup_0_updates\t%llu\ngroup_1_updates\t%llu\n",
         (unsigned long long)run->report.updates,
         (unsigned long long)run->report.generation,
         (unsigned long long)run->report.contacts,
         (unsigned long long)run->report.deferred,
         (unsigned long long)run->report.reseeds,
         (unsigned long long)run->report.group_updates[0],
         (unsigned long long)run->report.group_updates[1]);
  format(&writer,
         "changed_train_outputs\t%zu\ncode_head_weights_unchanged\t%"
         "d\ntraining_ms\t%llu\ntotal_ms\t%llu\ncheckpoint_bytes\t%zu\nmodel_"
         "fixed_bound_bytes\t%zu\nresident_model_pair_bytes\t%zu\n",
         run->changed_train, run->code_weights_unchanged,
         (unsigned long long)run->training_ms,
         (unsigned long long)run->total_ms, run->checkpoint_bytes,
         sizeof(c_model), 2 * sizeof(c_model));
  static const char *const names[] = {"initial", "trained", "frozen"};
  for (unsigned i = 0; i < B_CONDITIONS; ++i) {
    format(&writer,
           "train_%s_loss\t%.17g\ntrain_%s_accuracy\t%.17g\ntrain_%s_correct\t%"
           "zu\n",
           names[i], mean_loss(&run->train[i]), names[i],
           accuracy(&run->train[i]), names[i], run->train[i].correct);
    format(&writer,
           "audit_%s_loss\t%.17g\naudit_%s_accuracy\t%.17g\naudit_%s_correct\t%"
           "zu\n",
           names[i], mean_loss(&run->audit[i]), names[i],
           accuracy(&run->audit[i]), names[i], run->audit[i].correct);
    format(&writer, "code_%s_loss\t%.17g\ncode_%s_accuracy\t%.17g\n", names[i],
           mean_loss(&run->code[i]), names[i], accuracy(&run->code[i]));
  }
  format(&writer,
         "train_loss_gain\t%.17g\naudit_loss_gain\t%.17g\ncode_retention_loss_"
         "delta\t%.17g\n",
         mean_loss(&run->train[0]) - mean_loss(&run->train[1]),
         mean_loss(&run->audit[0]) - mean_loss(&run->audit[1]),
         mean_loss(&run->code[1]) - mean_loss(&run->code[0]));
  c_status status = writer.status == C_OK ? artifact(directory, "metrics.tsv",
                                                     writer.data, writer.length)
                                          : writer.status;
  free(writer.data);
  return status;
}

static c_status write_report(const benchmark *run, const char *directory) {
  c_writer writer = {0};
  format(&writer,
         "# Native source-quality measurement\n\nThe protocol and immutable "
         "fixtures were published before AUDIT prediction. Raw "
         "`predictions.tsv` retains every target, top-1 prediction, loss and "
         "complete action distribution for all 102 inference attempts. "
         "`metrics.tsv` retains exact denominators and costs. No held-out "
         "bytes entered the two-record training context.\n\n");
  format(
      &writer,
      "| Set | Cases per condition | Initial loss | Life-trained loss | Frozen "
      "loss | Initial accuracy | Life-trained accuracy | Frozen accuracy |\n| "
      "--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  const summary *sets[] = {run->train, run->audit, run->code};
  const char *labels[] = {"TRAIN range + clamp",
                          "AUDIT counted-loop + array-lookup",
                          "CODE retention probes"};
  for (size_t i = 0; i < 3; ++i)
    format(&writer, "| %s | %zu | %.9g | %.9g | %.9g | %.6g | %.6g | %.6g |\n",
           labels[i], sets[i][0].count, mean_loss(&sets[i][0]),
           mean_loss(&sets[i][1]), mean_loss(&sets[i][2]),
           accuracy(&sets[i][0]), accuracy(&sets[i][1]), accuracy(&sets[i][2]));
  format(&writer,
         "\nCompleted %llu / 64 planned source updates through %llu contact "
         "generations, with %llu total generations, %llu deferrals and %llu "
         "reseeds. Both owned group clocks: %llu, %llu. Changed TRAIN "
         "probability distributions: %zu / 16. Code readout values and "
         "optimizer moments stayed exact: %s; shared routing changes can still "
         "move code probabilities.\n\n",
         (unsigned long long)run->report.updates,
         (unsigned long long)run->report.contacts,
         (unsigned long long)run->report.generation,
         (unsigned long long)run->report.deferred,
         (unsigned long long)run->report.reseeds,
         (unsigned long long)run->report.group_updates[0],
         (unsigned long long)run->report.group_updates[1], run->changed_train,
         run->code_weights_unchanged ? "yes" : "no");
  format(&writer,
         "Source loss gain: %.9g. AUDIT loss gain: %.9g (negative means "
         "deterioration). Code retention loss delta: %.9g (positive means "
         "deterioration). These results are retained regardless of sign.\n\n",
         mean_loss(&run->train[0]) - mean_loss(&run->train[1]),
         mean_loss(&run->audit[0]) - mean_loss(&run->audit[1]),
         mean_loss(&run->code[1]) - mean_loss(&run->code[0]));
  format(&writer,
         "Measured training/scheduler wall time: %llu ms; complete measured "
         "run before report publication: %llu ms. Trained checkpoint: %zu "
         "bytes. Fixed allocated model bound: %zu bytes per model, %zu bytes "
         "for trained plus frozen comparator. Model bytes are sizeof-based "
         "allocation bounds, not process RSS or peak-memory measurements.\n\n",
         (unsigned long long)run->training_ms,
         (unsigned long long)run->total_ms, run->checkpoint_bytes,
         sizeof(c_model), 2 * sizeof(c_model));
  format(&writer,
         "This is a fixed-seed, tiny, causal next-byte benchmark on authored C "
         "fixtures. TRAIN gains establish scoped learning on selected "
         "prefixes; separate AUDIT families measure only these prefixes and "
         "may improve or deteriorate. CODE anchors measure probability "
         "retention rather than independently verified code utility. The "
         "frozen comparator has identical initial parameters and zero updates; "
         "it is not compute-matched and does not isolate scheduler value. Life "
         "optimality, a deterministic alternative scheduler, matched compute "
         "comparisons, multiple seeds, broader independent families, and "
         "longer-sequence quality remain pending.\n");
  c_status status = writer.status == C_OK ? artifact(directory, "report.md",
                                                     writer.data, writer.length)
                                          : writer.status;
  free(writer.data);
  return status;
}

c_status c_benchmark_run(const char *directory) {
  if (directory == NULL || directory[0] == '\0')
    return C_INVALID;
  if (strlen(directory) > 3800)
    return C_LIMIT;
  c_status status = new_directory(directory);
  if (status != C_OK)
    return status;
  uint64_t start = c_monotonic_ms();
  benchmark *run = calloc(1, sizeof(*run));
  if (run == NULL)
    return C_NOMEM;
  status = write_protocol(directory);
  if (status == C_OK)
    status = freeze_fixtures(directory);
  if (status == C_OK)
    status = create_owners(run);
  if (status == C_OK)
    status = admit_training(run);
  char initial_path[B_PATH];
  if (status == C_OK)
    status = path_join(initial_path, directory, "initial.checkpoint");
  if (status == C_OK)
    status = c_trainer_save(run->trainer, initial_path);
  if (status == C_OK)
    status = evaluate(run, 0, c_trainer_model(run->trainer));
  c_status training_status = status == C_OK ? train_budget(run) : status;
  if (status == C_OK && training_status != C_OK &&
      training_status != C_DEFERRED)
    status = training_status;
  if (status == C_OK)
    status = evaluate(run, 1, c_trainer_model(run->trainer));
  if (status == C_OK)
    status = evaluate(run, 2, run->frozen);
  if (status == C_OK)
    status = verify_comparator(run);
  if (status == C_OK)
    status = write_predictions(run, directory);
  if (status == C_OK)
    status = save_checkpoint(run, directory);
  run->total_ms = c_monotonic_ms() - start;
  if (status == C_OK)
    status = write_metrics(run, directory);
  if (status == C_OK)
    status = write_report(run, directory);
  c_trainer_destroy(run->trainer);
  c_model_destroy(run->frozen);
  free(run);
  return status == C_OK ? training_status : status;
}
