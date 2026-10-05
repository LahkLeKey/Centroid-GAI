#include "../../data/audit/extension_fixtures.h"
#include "../life/policy_internal.h"
#include "centroid_extensions.h"
#include "internal.h"
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static c_status find(const c_context *context, uint64_t id, c_record *record) {
  for (size_t i = 0; i < c_context_count(context); i++) {
    c_status s = c_context_record(context, i, record);
    if (s != C_OK)
      return s;
    if (record->id == id)
      return C_OK;
  }
  return C_NOT_FOUND;
}
static c_status scoped_loss(const c_model *model, c_head head,
                            const double *input, unsigned target,
                            unsigned eligible, double *out, int *correct) {
  double p[C_TEXT_ACTIONS], mass[C_MAX_GROUPS] = {1, 1, 1, 1};
  c_status s =
      c_model_predict(model, head, input, eligible, mass, p, C_TEXT_ACTIONS);
  if (s != C_OK)
    return s;
  unsigned actions = head == C_TEXT ? C_TEXT_ACTIONS : C_CODE_ACTIONS, best = 0;
  if (target >= actions || p[target] <= 0 || !isfinite(p[target]))
    return C_CORRUPT;
  for (unsigned a = 1; a < actions; a++)
    if (p[a] > p[best])
      best = a;
  *out = -log(p[target]);
  *correct = best == target;
  return C_OK;
}
static c_status loss(const c_model *model, c_head head, const double *input,
                     unsigned target, double *out, int *correct) {
  return scoped_loss(model, head, input, target, (1u << model->groups) - 1, out,
                     correct);
}
static int same_task(const c_task *a, const c_task *b) {
  if (a->id != b->id || a->source_id != b->source_id ||
      a->offset != b->offset || a->head != b->head || a->target != b->target ||
      a->eligible != b->eligible || a->done != b->done ||
      a->format != b->format || a->request_id != b->request_id ||
      a->evidence_count != b->evidence_count ||
      memcmp(a->input, b->input, sizeof(a->input)) ||
      memcmp(a->parent, b->parent, C_DIGEST_HEX) ||
      memcmp(a->evaluator, b->evaluator, C_DIGEST_HEX) ||
      memcmp(a->receipt, b->receipt, C_DIGEST_HEX) ||
      memcmp(a->input_digest, b->input_digest, C_DIGEST_HEX))
    return 0;
  for (unsigned i = 0; i < 8u; ++i)
    if (a->evidence_ids[i] != b->evidence_ids[i])
      return 0;
  return 1;
}
static int preserves_archive(const c_trainer *parent,
                             const c_trainer *candidate) {
  if (candidate->retired_count < parent->retired_count)
    return 0;
  for (size_t i = 0; i < parent->retired_count; ++i) {
    const c_retired_task *a = &parent->retired[i];
    const c_retired_task *b = &candidate->retired[i];
    if (a->groups != b->groups || a->merge_index != b->merge_index ||
        !same_task(&a->task, &b->task))
      return 0;
    for (unsigned group = 0; group < C_MAX_GROUPS; ++group)
      if (a->owner_uids[group] != b->owner_uids[group])
        return 0;
  }
  return 1;
}
c_status c_extension_evaluate(const c_trainer *parent,
                              const c_trainer *candidate,
                              const c_source_probe *probes, size_t count,
                              double margin, int require_gain,
                              c_extension_report *out) {
  if (!parent || !candidate || !probes || !out || count > 256 ||
      !isfinite(margin) || margin < 0 || margin > 0.1 ||
      parent->research_mode || candidate->research_mode ||
      candidate->report.queued)
    return C_INVALID;
  c_extension_report report = {0};
  if (c_trainer_validate(parent) != C_OK ||
      c_trainer_validate(candidate) != C_OK)
    return C_CORRUPT;
  if (!preserves_archive(parent, candidate))
    return C_CORRUPT;
  if (c_context_count(candidate->context) < c_context_count(parent->context))
    return C_CORRUPT;
  if (candidate->receipt_count < parent->receipt_count)
    return C_CORRUPT;
  for (size_t i = 0; i < parent->receipt_count; i++)
    if (strcmp(parent->receipts[i], candidate->receipts[i]) ||
        strcmp(parent->receipt_bindings[i], candidate->receipt_bindings[i]) ||
        (parent->receipt_consumed[i] && !candidate->receipt_consumed[i]))
      return C_CORRUPT;
  for (size_t i = 0; i < c_context_count(parent->context); i++) {
    c_record a = {0}, b = {0};
    c_status s = c_context_record(parent->context, i, &a);
    if (s == C_OK)
      s = find(candidate->context, a.id, &b);
    if (s != C_OK || a.version != b.version || a.kind != b.kind ||
        a.split != b.split || a.length != b.length || strcmp(a.path, b.path) ||
        strcmp(a.attribution, b.attribution) || strcmp(a.digest, b.digest) ||
        (a.length && memcmp(a.bytes, b.bytes, a.length)))
      return C_CORRUPT;
  }
  for (size_t i = 0; i < count; i++) {
    c_record r, c;
    double input[C_FEATURES], before = 0, after = 0;
    int ca = 0, cb = 0;
    for (size_t j = 0; j < i; j++)
      if (probes[i].record_id == probes[j].record_id &&
          probes[i].offset == probes[j].offset)
        return C_INVALID;
    c_status s = find(parent->context, probes[i].record_id, &r);
    if (s != C_OK)
      return s;
    s = find(candidate->context, probes[i].record_id, &c);
    if (s != C_OK || strcmp(r.digest, c.digest) || r.length != c.length)
      return C_CORRUPT;
    unsigned split = (unsigned)r.split;
    if (split > 2 || probes[i].offset > r.length ||
        !(r.kind == C_SOURCE || (r.kind == C_DEVELOPMENT && r.split == C_DEV) ||
          (r.kind == C_AUDIT && r.split == C_HOLDOUT)))
      return C_INVALID;
    unsigned target =
        probes[i].offset == r.length ? C_EOS : r.bytes[probes[i].offset];
    s = c_encode(r.bytes, probes[i].offset, input);
    if (s == C_OK)
      s = loss(parent->model, C_TEXT, input, target, &before, &ca);
    if (s == C_OK)
      s = loss(candidate->model, C_TEXT, input, target, &after, &cb);
    if (s != C_OK)
      return s;
    report.cases[split]++;
    report.loss_parent[split] += before;
    report.loss_candidate[split] += after;
    report.correct_parent[split] += (size_t)ca;
    report.correct_candidate[split] += (size_t)cb;
  }
  report.accepted = 1;
  for (unsigned split = 0; split < 3; split++) {
    if (report.cases[split] < 8)
      return C_INVALID;
    report.loss_parent[split] /= (double)report.cases[split];
    report.loss_candidate[split] /= (double)report.cases[split];
    if (report.loss_candidate[split] > report.loss_parent[split] + margin ||
        report.correct_candidate[split] < report.correct_parent[split])
      report.accepted = 0;
  }
  if (require_gain && report.loss_candidate[0] >= report.loss_parent[0] - 1e-6)
    report.accepted = 0;
  for (size_t i = 0; i < c_trainer_retention_count(parent); i++) {
    const c_task *task = NULL;
    unsigned parent_scope = 0, candidate_scope = 0;
    c_status s = c_trainer_retention_task(parent, i, &task);
    if (s == C_OK)
      s = c_trainer_task_scope(parent, parent, task, &parent_scope);
    if (s == C_OK)
      s = c_trainer_task_scope(parent, candidate, task, &candidate_scope);
    if (s != C_OK)
      return s;
    double before = 0, after = 0;
    int ca = 0, cb = 0;
    s = scoped_loss(parent->model, task->head, task->input, task->target,
                    parent_scope, &before, &ca);
    if (s == C_OK)
      s = scoped_loss(candidate->model, task->head, task->input, task->target,
                      candidate_scope, &after, &cb);
    if (s != C_OK)
      return s;
    report.retained_tasks++;
    report.retention_parent += before;
    report.retention_candidate += after;
    if (cb < ca)
      report.accepted = 0;
  }
  if (report.retained_tasks) {
    report.retention_parent /= (double)report.retained_tasks;
    report.retention_candidate /= (double)report.retained_tasks;
    if (report.retention_candidate > report.retention_parent + margin)
      report.accepted = 0;
  }
  *out = report;
  return C_OK;
}
c_status c_trainer_promote(c_trainer **incumbent, c_trainer **candidate,
                           const c_source_probe *probes, size_t count,
                           double margin, int gain, c_extension_report *report,
                           c_trainer **previous) {
  if (!incumbent || !candidate || incumbent == candidate || !previous ||
      *previous || !*incumbent || !*candidate || *incumbent == *candidate)
    return C_INVALID;
  c_status s = c_extension_evaluate(*incumbent, *candidate, probes, count,
                                    margin, gain, report);
  if (s != C_OK)
    return s;
  if (!report->accepted)
    return C_DEFERRED;
  *previous = *incumbent;
  *incumbent = *candidate;
  *candidate = NULL;
  return C_OK;
}
static c_status artifact(const char *dir, const char *name, const void *data,
                         size_t length) {
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/%s", dir, name);
  return n < 0 || (size_t)n >= sizeof(path)
             ? C_LIMIT
             : c_write_atomic(path, data, length);
}
typedef struct {
  uint64_t elapsed_ns, step_ns, inspection_ns, lookahead_ns, policy_adam_ns;
  c_training_report before, after;
} phase_cost;
static c_status budget(c_trainer *trainer, uint64_t id, size_t length,
                       unsigned rounds, unsigned seed, const char *arm,
                       FILE *trace_file, phase_cost *cost) {
  memset(cost, 0, sizeof(*cost));
  cost->before = trainer->report;
  uint64_t start = c_monotonic_ns();
  unsigned mask = (1u << trainer->model->groups) - 1;
  c_status s = C_OK;
  for (unsigned epoch = 0; epoch < rounds && s == C_OK; epoch++) {
    for (unsigned i = 0; i < 16; i++) {
      s = c_trainer_enqueue_source(trainer, id, length * i / 15, mask);
      if (s != C_OK)
        break;
    }
    for (unsigned generation = 0; generation < 128 && s == C_OK; generation++) {
      c_policy_trace trace = {0};
      uint64_t policy_clock = trainer->policy.clock;
      uint64_t edits = trainer->policy.interventions;
      uint64_t changes = trainer->policy.contact_changes;
      uint64_t before_generation = trainer->generation;
      char before[C_DIGEST_HEX], evolved[C_DIGEST_HEX], proposed[C_DIGEST_HEX],
          published[C_DIGEST_HEX];
      if (trainer->policy.enabled) {
        c_hash(trainer->cells, C_WORLD_CELLS, before);
        uint64_t inspection_start = c_monotonic_ns();
        s = c_policy_inspect(trainer, &trace);
        cost->inspection_ns += c_monotonic_ns() - inspection_start;
        cost->lookahead_ns += trace.lookahead_ns;
        cost->policy_adam_ns += trace.optimizer_ns;
        if (s != C_OK)
          break;
      }
      uint64_t step_start = c_monotonic_ns();
      s = c_trainer_step(trainer, 1, &cost->after);
      cost->step_ns += c_monotonic_ns() - step_start;
      if (s == C_OK && trainer->policy.enabled) {
        c_hash(trace.evolved, C_WORLD_CELLS, evolved);
        c_hash(trace.edited, C_WORLD_CELLS, proposed);
        c_hash(trainer->cells, C_WORLD_CELLS, published);
        fprintf(trace_file,
                "%u\t%s\t%" PRIu64 "\t%u\t%u\t%u\t%u\t%.17g\t"
                "%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%d\t%s\t%s\t%s\t%s",
                seed, arm, before_generation, trace.x, trace.y,
                trace.prediction, trace.teacher, trace.loss,
                trainer->policy.clock - policy_clock,
                trainer->policy.interventions - edits,
                trainer->policy.contact_changes - changes,
                trainer->generation % 8u == 0, before, evolved, proposed,
                published);
        for (unsigned a = 0; a < 4; a++)
          fprintf(trace_file, "\t%.17g", trace.probabilities[a]);
        for (unsigned a = 0; a < 4; a++)
          fprintf(trace_file, "\t%.17g", trace.utility[a]);
        fprintf(trace_file, "\t%" PRIu64 "\t%" PRIu64 "\n", trace.lookahead_ns,
                trace.optimizer_ns);
      }
    }
    if (s == C_OK && cost->after.queued)
      s = C_DEFERRED;
  }
  cost->elapsed_ns = c_monotonic_ns() - start;
  cost->after = trainer->report;
  if (s == C_OK &&
      cost->after.updates - cost->before.updates != (uint64_t)rounds * 16u)
    s = C_CORRUPT;
  return s;
}
static c_status model_size(const c_model *model, size_t *length) {
  c_writer writer = {0};
  c_model_write(&writer, model, 1);
  c_status s = writer.status;
  if (s == C_OK)
    *length = writer.length;
  free(writer.data);
  return s;
}
static c_status retain_state(const char *directory, unsigned seed,
                             const char *name, const c_trainer *trainer,
                             size_t *length) {
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/seed-%u-%s.checkpoint", directory,
                   seed, name);
  if (n < 0 || (size_t)n >= sizeof(path))
    return C_LIMIT;
  c_status s = c_trainer_save(trainer, path);
  unsigned char *data = NULL;
  if (s == C_OK)
    s = c_read_file(path, &data, length);
  free(data);
  return s;
}
static void cost_row(FILE *file, unsigned seed, const char *name,
                     const phase_cost *cost, const c_trainer *trainer,
                     size_t model_bytes, size_t checkpoint_bytes) {
  fprintf(file,
          "%u\t%s\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
          "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
          "\t%" PRIu64 "\t%" PRIu64 "\t%u\t%zu\t%zu\t%zu\t%zu\t%" PRIu64
          "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64,
          seed, name, cost->before.generation, cost->after.generation,
          cost->after.updates - cost->before.updates, cost->after.updates,
          cost->after.contacts - cost->before.contacts,
          cost->after.deferred - cost->before.deferred,
          cost->after.reseeds - cost->before.reseeds, cost->elapsed_ns,
          cost->step_ns, cost->inspection_ns, cost->lookahead_ns,
          cost->policy_adam_ns, trainer->model->groups, model_bytes,
          checkpoint_bytes, trainer->task_count, trainer->retired_count,
          trainer->retired_clock, trainer->model->shared_clock,
          trainer->policy.clock, trainer->policy.interventions,
          trainer->policy.contact_changes);
  for (unsigned g = 0; g < C_MAX_GROUPS; g++)
    fprintf(file, "\t%" PRIu64, trainer->report.group_updates[g]);
  fputc('\n', file);
}
static c_status prediction_row(FILE *file, unsigned seed, const char *candidate,
                               const char *state, const char *split,
                               uint64_t identity, size_t offset, c_head head,
                               unsigned target, unsigned eligible,
                               const double input[C_FEATURES],
                               const c_model *model) {
  double probabilities[C_TEXT_ACTIONS], mass[C_MAX_GROUPS] = {1, 1, 1, 1};
  c_status s = c_model_predict(model, head, input, eligible, mass,
                               probabilities, C_TEXT_ACTIONS);
  if (s != C_OK)
    return s;
  unsigned actions = head == C_TEXT ? C_TEXT_ACTIONS : C_CODE_ACTIONS, best = 0;
  for (unsigned a = 1; a < actions; a++)
    if (probabilities[a] > probabilities[best])
      best = a;
  fprintf(file, "%u\t%s\t%s\t%s\t%" PRIu64 "\t%zu\t%u\t%u\t%u\t%u\t%.17g", seed,
          candidate, state, split, identity, offset, (unsigned)head, target,
          eligible, best, -log(probabilities[target]));
  for (unsigned j = 0; j < C_FEATURES; j++)
    fprintf(file, "\t%.17g", input[j]);
  for (unsigned a = 0; a < C_TEXT_ACTIONS; a++) {
    if (a < actions)
      fprintf(file, "\t%.17g", probabilities[a]);
    else
      fputs("\tNA", file);
  }
  fputc('\n', file);
  return ferror(file) ? C_IO : C_OK;
}
static c_status retain_predictions(FILE *file, unsigned seed, const char *name,
                                   const c_trainer *parent,
                                   const c_trainer *candidate,
                                   const c_source_probe *probes, size_t count) {
  const char *splits[] = {"TRAIN", "DEV", "AUDIT"};
  c_status s = C_OK;
  for (size_t i = 0; i < count && s == C_OK; i++) {
    c_record record;
    double input[C_FEATURES];
    s = find(parent->context, probes[i].record_id, &record);
    if (s == C_OK)
      s = c_encode(record.bytes, probes[i].offset, input);
    if (s != C_OK)
      break;
    unsigned target = probes[i].offset == record.length
                          ? C_EOS
                          : record.bytes[probes[i].offset];
    s = prediction_row(file, seed, name, "reference", splits[record.split],
                       record.id, probes[i].offset, C_TEXT, target,
                       (1u << parent->model->groups) - 1, input, parent->model);
    if (s == C_OK)
      s = prediction_row(file, seed, name, "candidate", splits[record.split],
                         record.id, probes[i].offset, C_TEXT, target,
                         (1u << candidate->model->groups) - 1, input,
                         candidate->model);
  }
  for (size_t i = 0; i < c_trainer_retention_count(parent) && s == C_OK; i++) {
    const c_task *task = NULL;
    unsigned old_mask = 0, new_mask = 0;
    s = c_trainer_retention_task(parent, i, &task);
    if (s == C_OK)
      s = c_trainer_task_scope(parent, parent, task, &old_mask);
    if (s == C_OK)
      s = c_trainer_task_scope(parent, candidate, task, &new_mask);
    if (s == C_OK)
      s = prediction_row(file, seed, name, "reference", "RETENTION", task->id,
                         (size_t)task->offset, task->head, task->target,
                         old_mask, task->input, parent->model);
    if (s == C_OK)
      s = prediction_row(file, seed, name, "candidate", "RETENTION", task->id,
                         (size_t)task->offset, task->head, task->target,
                         new_mask, task->input, candidate->model);
  }
  return s;
}
static FILE *open_artifact(const char *directory, const char *name) {
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/%s", directory, name);
  return n < 0 || (size_t)n >= sizeof(path) ? NULL : fopen(path, "wb");
}
c_status c_extensions_run(const char *directory) {
  static const unsigned char training[] =
      "unsigned parity_bits(unsigned x) { unsigned p = 0; while(x) { p ^= x & "
      "1; x >>= 1; } return p; }\n";
  static const char protocol[] =
      "# Frozen extension protocol, instrumentation revision2\n\n"
      "This rerun uses the same already-opened seeds/families as revision1. "
      "It adds durable schema3 archives, raw predictions and measured costs; "
      "it is not a fresh independent quality trial.\n\n"
      "Seeds41,73,109; four owners;32 parent updates,32 additional matched "
      "reference/candidate updates for shared and policy arms, zero additional "
      "updates for merging.16 distinct causal prefixes including EOS per "
      "TRAIN/DEV/AUDIT family. Identity-initialized shared diagonal scaling "
      "requires all physical participants. World-only policy teacher uses "
      "three-generation contact/lineage lookahead, at most eight edits; "
      "edit-disabled control still fits the same policy learner. Fixed "
      "encountered merge pair0,1 chosen before evaluation. Merge keeps "
      "completed supervision in the durable retired archive and retires "
      "lineage clocks without resetting cumulative update accounting.\n\n"
      "Fixed gate: mean loss regression<=0.02 per split; no top1 accuracy "
      "loss; no completed-task top1 or mean-loss retention regression>0.02. "
      "Shared additionally needs TRAIN gain against plain source updates. "
      "Policy additionally must edit cells and change subsequent physical "
      "encounter masks against its edit-disabled fitting control. Merge "
      "must reduce canonical serialized model bytes. No held-out fitting or "
      "global production recipe promotion. Passed gates cover this fixture "
      "only; rejected candidates remain isolated.\n\n"
      "costs.tsv separates actual training-step time from duplicated read-only "
      "policy inspection. Inspection separately times teacher lookahead and "
      "hypothetical policy Adam; these do not enter checkpoint state. Elapsed "
      "phase time also includes enqueueing, inspection and trace output. "
      "Policy-trace interventions/contactchanges describe proposed post-Life "
      "edits; every eighth generation fixed reseeding may override them, "
      "explicitly flagged with final published-world SHA. Models have fixed "
      "in-memory maximum-owner capacity; wire-size reduction is not an "
      "in-memory allocation reduction. Whole checkpoints also retain archives. "
      "Process peak includes the harness, clones and serialization buffers.\n";
  if (!directory || !*directory || strlen(directory) > 3700)
    return C_INVALID;
  c_status s = c_new_directory(directory);
  if (s != C_OK)
    return s;
  s = artifact(directory, "protocol.md", protocol, sizeof(protocol) - 1);
  if (s == C_OK)
    s = artifact(directory, "train.c", training, sizeof(training) - 1);
  if (s == C_OK)
    s = artifact(directory, "development.c", extension_development,
                 sizeof(extension_development) - 1);
  if (s == C_OK)
    s = artifact(directory, "audit.c", extension_audit,
                 sizeof(extension_audit) - 1);
  if (s != C_OK)
    return s;
  FILE *report_file = open_artifact(directory, "report.md");
  FILE *cost_file = open_artifact(directory, "costs.tsv");
  FILE *raw_file = open_artifact(directory, "predictions.tsv");
  FILE *trace_file = open_artifact(directory, "policy-trace.tsv");
  if (!report_file || !cost_file || !raw_file || !trace_file) {
    if (report_file)
      fclose(report_file);
    if (cost_file)
      fclose(cost_file);
    if (raw_file)
      fclose(raw_file);
    if (trace_file)
      fclose(trace_file);
    return C_IO;
  }
  fputs("# Gated native extension results, instrumentation revision2\n\n"
        "Same previously opened seeds/families; see protocol.md. No global "
        "recipe is promoted from these AUDIT results.\n\n"
        "| Seed | Candidate | TRAIN loss; correct/16 reference -> candidate | "
        "DEV loss; correct/16 | AUDIT loss; correct/16 | "
        "Retention loss (tasks) | Gate | Model wire bytes | Checkpoint bytes | "
        "Contacts changed |\n"
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n",
        report_file);
  fputs("seed\tarm\tgeneration_before\tgeneration_after\tupdates_delta\t"
        "updates_total\tcontact_generations_delta\tdeferrals_delta\t"
        "reseeds_delta\telapsed_ns\tstep_ns\tinspection_ns\tlookahead_ns\t"
        "policy_adam_ns\tgroups\tmodel_wire_bytes\tcheckpoint_bytes\t"
        "active_tasks\tretired_tasks\tretired_clock\tshared_clock\t"
        "policy_clock\tinterventions\tcontact_changes\tclock0\tclock1\t"
        "clock2\tclock3\n",
        cost_file);
  fputs(
      "seed\tarm\tgeneration_before\tx\ty\tprediction\tteacher\tloss\t"
      "policy_updates_delta\tinterventions_delta\tcontact_changes_delta\t"
      "reseed_overrides\tbefore_sha\tevolved_sha\tproposed_sha\tpublished_sha\t"
      "p_keep\tp_insert\tp_remove\tp_renew\tutility_keep\tutility_insert\t"
      "utility_remove\tutility_renew\tlookahead_ns\tpolicy_adam_ns\n",
      trace_file);
  fputs("seed\tcandidate\tstate\tsplit\tidentity\toffset\thead\ttarget\t"
        "eligible\ttop1\tloss",
        raw_file);
  for (unsigned j = 0; j < C_FEATURES; j++)
    fprintf(raw_file, "\tx%u", j);
  for (unsigned a = 0; a < C_TEXT_ACTIONS; a++)
    fprintf(raw_file, "\tp%u", a);
  fputc('\n', raw_file);
  uint64_t start = c_monotonic_ns();
  const unsigned seeds[3] = {41, 73, 109};
  const char *names[] = {"shared-scale", "world-policy", "merge-0-1"};
  for (unsigned k = 0; k < 3 && s == C_OK; k++) {
    c_trainer *parent = NULL, *reference = NULL, *ablation = NULL,
              *candidates[3] = {NULL, NULL, NULL};
    uint64_t ids[3] = {0};
    c_source_probe probes[48];
    phase_cost parent_cost = {0}, reference_cost = {0}, ablation_cost = {0},
               candidate_cost[3] = {{0}, {0}, {0}};
    size_t parent_wire = 0, reference_wire = 0, ablation_wire = 0;
    size_t parent_bytes = 0, reference_bytes = 0, ablation_bytes = 0;
    s = c_trainer_create(4, seeds[k], &parent);
    if (s == C_OK)
      s = c_context_admit(parent->context, C_SOURCE, C_TRAIN, "train/parity.c",
                          "extension/v2 same frozen fixture", training,
                          sizeof(training) - 1, &ids[0]);
    if (s == C_OK)
      s = c_context_admit(parent->context, C_DEVELOPMENT, C_DEV,
                          "development/bitmask.c", "extension/v2 same fixture",
                          extension_development,
                          sizeof(extension_development) - 1, &ids[1]);
    if (s == C_OK)
      s = c_context_admit(parent->context, C_AUDIT, C_HOLDOUT,
                          "audit/intersection.c", "extension/v2 same fixture",
                          extension_audit, sizeof(extension_audit) - 1,
                          &ids[2]);
    const size_t lengths[3] = {sizeof(training) - 1,
                               sizeof(extension_development) - 1,
                               sizeof(extension_audit) - 1};
    for (unsigned split = 0; split < 3; split++)
      for (unsigned i = 0; i < 16; i++) {
        probes[split * 16 + i].record_id = ids[split];
        probes[split * 16 + i].offset = lengths[split] * i / 15;
      }
    if (s == C_OK)
      s = budget(parent, ids[0], lengths[0], 2, seeds[k], "parent", trace_file,
                 &parent_cost);
    if (s == C_OK)
      s = model_size(parent->model, &parent_wire);
    if (s == C_OK)
      s = retain_state(directory, seeds[k], "parent", parent, &parent_bytes);
    if (s == C_OK)
      cost_row(cost_file, seeds[k], "parent", &parent_cost, parent, parent_wire,
               parent_bytes);
    if (s == C_OK)
      s = c_trainer_shared_candidate(parent, &candidates[0]);
    if (s == C_OK)
      s = c_trainer_policy_candidate(parent, &candidates[1]);
    if (s == C_OK)
      s = c_trainer_merge_candidate(parent, 0, 1, &candidates[2]);
    if (s == C_OK)
      s = c_trainer_policy_candidate(parent, &reference);
    if (s == C_OK) {
      reference->policy.enabled = 0;
      reference->policy.editing_enabled = 0;
      s = c_trainer_policy_candidate(parent, &ablation);
    }
    if (s == C_OK)
      ablation->policy.editing_enabled = 0;
    if (s == C_OK)
      s = budget(reference, ids[0], lengths[0], 2, seeds[k], "source-reference",
                 trace_file, &reference_cost);
    if (s == C_OK)
      s = budget(ablation, ids[0], lengths[0], 2, seeds[k], "policy-no-edits",
                 trace_file, &ablation_cost);
    if (s == C_OK)
      s = model_size(reference->model, &reference_wire);
    if (s == C_OK)
      s = model_size(ablation->model, &ablation_wire);
    if (s == C_OK)
      s = retain_state(directory, seeds[k], "source-reference", reference,
                       &reference_bytes);
    if (s == C_OK)
      s = retain_state(directory, seeds[k], "policy-no-edits", ablation,
                       &ablation_bytes);
    if (s == C_OK) {
      cost_row(cost_file, seeds[k], "source-reference", &reference_cost,
               reference, reference_wire, reference_bytes);
      cost_row(cost_file, seeds[k], "policy-no-edits", &ablation_cost, ablation,
               ablation_wire, ablation_bytes);
    }
    for (unsigned kind = 0; kind < 3 && s == C_OK; kind++) {
      if (kind < 2)
        s = budget(candidates[kind], ids[0], lengths[0], 2, seeds[k],
                   names[kind], trace_file, &candidate_cost[kind]);
      else {
        candidate_cost[kind].before = parent->report;
        candidate_cost[kind].after = candidates[kind]->report;
      }
      const c_trainer *baseline = kind == 0   ? reference
                                  : kind == 1 ? ablation
                                              : parent;
      c_extension_report report = {0};
      size_t candidate_wire = 0, candidate_bytes = 0, baseline_wire = 0,
             baseline_bytes = kind == 0   ? reference_bytes
                              : kind == 1 ? ablation_bytes
                                          : parent_bytes;
      if (s == C_OK)
        s = c_extension_evaluate(baseline, candidates[kind], probes, 48, 0.02,
                                 kind == 0, &report);
      if (s == C_OK)
        s = model_size(baseline->model, &baseline_wire);
      if (s == C_OK)
        s = model_size(candidates[kind]->model, &candidate_wire);
      if (s != C_OK)
        break;
      if (kind == 1 && (!candidates[kind]->policy.interventions ||
                        !candidates[kind]->policy.contact_changes))
        report.accepted = 0;
      if (kind == 2 && candidate_wire >= baseline_wire)
        report.accepted = 0;
      s = retain_state(directory, seeds[k], names[kind], candidates[kind],
                       &candidate_bytes);
      if (s == C_OK)
        s = retain_predictions(raw_file, seeds[k], names[kind], baseline,
                               candidates[kind], probes, 48);
      if (s != C_OK)
        break;
      cost_row(cost_file, seeds[k], names[kind], &candidate_cost[kind],
               candidates[kind], candidate_wire, candidate_bytes);
      fprintf(report_file, "| %u | %s |", seeds[k], names[kind]);
      for (unsigned split = 0; split < 3; split++)
        fprintf(report_file, " %.9g; %zu/%zu -> %.9g; %zu/%zu |",
                report.loss_parent[split], report.correct_parent[split],
                report.cases[split], report.loss_candidate[split],
                report.correct_candidate[split], report.cases[split]);
      fprintf(report_file,
              " %.9g -> %.9g (%zu) | %s | %zu -> %zu | %zu -> %zu | "
              "%" PRIu64 " |\n",
              report.retention_parent, report.retention_candidate,
              report.retained_tasks, report.accepted ? "passed" : "rejected",
              baseline_wire, candidate_wire, baseline_bytes, candidate_bytes,
              candidates[kind]->policy.contact_changes);
    }
    c_trainer_destroy(parent);
    c_trainer_destroy(reference);
    c_trainer_destroy(ablation);
    for (unsigned i = 0; i < 3; i++)
      c_trainer_destroy(candidates[i]);
  }
  fprintf(report_file,
          "\nElapsed: %" PRIu64 " ns. Process peak resident/working-set: "
          "%" PRIu64 " bytes. Model allocation capacity is %zu bytes per "
          "snapshot, irrespective of live owners. Raw probabilities and exact "
          "features are in predictions.tsv; clocks, update deltas, physical "
          "contacts/deferrals/reseeds, wire sizes and phase costs are in "
          "costs.tsv. Policy trace includes unmodified observations and "
          "proposed/published world identities. Parent and matched-reference "
          "checkpoints are retained for replay and rollback. Accepted gates "
          "do not establish fluent generation or general coding quality.\n",
          c_monotonic_ns() - start, c_process_peak_memory_bytes(),
          sizeof(c_model));
  FILE *files[] = {report_file, cost_file, raw_file, trace_file};
  for (unsigned i = 0; i < 4; i++) {
    if (ferror(files[i]) && s == C_OK)
      s = C_IO;
    if (fclose(files[i]) && s == C_OK)
      s = C_IO;
  }
  return s;
}
