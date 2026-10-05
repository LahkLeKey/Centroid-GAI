#include "internal.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef C_BUILD_ID
#define C_BUILD_ID "unknown-build"
#endif

static void put_scalar(c_writer *w, const c_scalar *s) {
  c_put_double(w, s->value);
  c_put_double(w, s->first);
  c_put_double(w, s->second);
}
static void get_scalar(c_reader *r, c_scalar *s) {
  s->value = c_get_double(r);
  s->first = c_get_double(r);
  s->second = c_get_double(r);
  if (s->second < 0)
    r->status = C_CORRUPT;
}
void c_model_write(c_writer *w, const c_model *model, int extensions) {
  unsigned g, a, f;
  c_put_u32(w, model->groups);
  c_put_u64(w, model->seed);
  for (g = 0; g < model->groups; g++) {
    const c_expert *e = &model->expert[g];
    c_put_u64(w, e->uid);
    c_put_u64(w, e->clock);
    for (f = 0; f < C_FEATURES; f++)
      put_scalar(w, &e->centroid[f]);
    for (a = 0; a < C_CODE_ACTIONS; a++)
      for (f = 0; f < C_FEATURES; f++)
        put_scalar(w, &e->code[a][f]);
    for (a = 0; a < C_TEXT_ACTIONS; a++)
      for (f = 0; f < C_FEATURES; f++)
        put_scalar(w, &e->text[a][f]);
  }
  if (extensions) {
    c_put_u32(w, model->shared_enabled);
    c_put_u64(w, model->shared_clock);
    for (f = 0; f < C_FEATURES; f++)
      put_scalar(w, &model->shared_scale[f]);
  }
}
c_status c_model_read(c_reader *r, c_model **out, int extensions) {
  unsigned groups = c_get_u32(r), g, a, f;
  uint64_t seed = c_get_u64(r);
  c_model *model = NULL;
  c_status s;
  if (!out || *out || r->status != C_OK || groups < 1 || groups > C_MAX_GROUPS)
    return C_CORRUPT;
  s = c_model_create(groups, seed, &model);
  if (s != C_OK)
    return s;
  for (g = 0; g < groups; g++) {
    c_expert *e = &model->expert[g];
    uint64_t expected = e->uid;
    e->uid = c_get_u64(r);
    e->clock = c_get_u64(r);
    if ((!extensions && e->uid != expected) || !e->uid)
      r->status = C_CORRUPT;
    for (unsigned earlier = 0; earlier < g; ++earlier)
      if (model->expert[earlier].uid == e->uid)
        r->status = C_CORRUPT;
    for (f = 0; f < C_FEATURES; f++)
      get_scalar(r, &e->centroid[f]);
    for (a = 0; a < C_CODE_ACTIONS; a++)
      for (f = 0; f < C_FEATURES; f++)
        get_scalar(r, &e->code[a][f]);
    for (a = 0; a < C_TEXT_ACTIONS; a++)
      for (f = 0; f < C_FEATURES; f++)
        get_scalar(r, &e->text[a][f]);
  }
  if (extensions) {
    model->shared_enabled = c_get_u32(r);
    model->shared_clock = c_get_u64(r);
    if (model->shared_enabled > 1 ||
        (!model->shared_enabled && model->shared_clock))
      r->status = C_CORRUPT;
    for (f = 0; f < C_FEATURES; f++) {
      get_scalar(r, &model->shared_scale[f]);
      if (!model->shared_enabled &&
          (model->shared_scale[f].value != 1.0 ||
           model->shared_scale[f].first || model->shared_scale[f].second))
        r->status = C_CORRUPT;
    }
  }
  if (r->status != C_OK) {
    c_model_destroy(model);
    return r->status;
  }
  *out = model;
  return C_OK;
}
static void put_report(c_writer *w, const c_training_report *report) {
  unsigned g;
  c_put_u64(w, report->generation);
  c_put_u64(w, report->updates);
  c_put_u64(w, report->contacts);
  c_put_u64(w, report->queued);
  c_put_u64(w, report->completed);
  c_put_u64(w, report->deferred);
  c_put_u64(w, report->reseeds);
  for (g = 0; g < C_MAX_GROUPS; g++)
    c_put_u64(w, report->group_updates[g]);
  c_put_double(w, report->last_loss);
}
static void get_report(c_reader *r, c_training_report *report) {
  unsigned g;
  report->generation = c_get_u64(r);
  report->updates = c_get_u64(r);
  report->contacts = c_get_u64(r);
  report->queued = c_get_u64(r);
  report->completed = c_get_u64(r);
  report->deferred = c_get_u64(r);
  report->reseeds = c_get_u64(r);
  for (g = 0; g < C_MAX_GROUPS; g++)
    report->group_updates[g] = c_get_u64(r);
  report->last_loss = c_get_double(r);
}
static void put_task(c_writer *w, const c_task *task) {
  unsigned f;
  c_put_u64(w, task->id);
  c_put_u64(w, task->source_id);
  c_put_u64(w, task->offset);
  c_put_u32(w, (uint32_t)task->head);
  c_put_u32(w, task->target);
  c_put_u32(w, task->eligible);
  c_put_u32(w, task->done);
  for (f = 0; f < C_FEATURES; f++)
    c_put_double(w, task->input[f]);
  c_put_bytes(w, task->parent, C_DIGEST_HEX);
  c_put_bytes(w, task->evaluator, C_DIGEST_HEX);
  c_put_bytes(w, task->receipt, C_DIGEST_HEX);
  c_put_bytes(w, task->input_digest, C_DIGEST_HEX);
  c_put_u32(w, task->format);
  c_put_u64(w, task->request_id);
  c_put_u32(w, task->evidence_count);
  for (f = 0; f < 8; f++)
    c_put_u64(w, task->evidence_ids[f]);
}
static void get_task(c_reader *r, c_task *task) {
  unsigned f;
  task->id = c_get_u64(r);
  task->source_id = c_get_u64(r);
  task->offset = c_get_u64(r);
  task->head = (c_head)c_get_u32(r);
  task->target = c_get_u32(r);
  task->eligible = c_get_u32(r);
  task->done = c_get_u32(r);
  for (f = 0; f < C_FEATURES; f++)
    task->input[f] = c_get_double(r);
  c_get_bytes(r, task->parent, C_DIGEST_HEX);
  c_get_bytes(r, task->evaluator, C_DIGEST_HEX);
  c_get_bytes(r, task->receipt, C_DIGEST_HEX);
  c_get_bytes(r, task->input_digest, C_DIGEST_HEX);
  task->format = c_get_u32(r);
  task->request_id = c_get_u64(r);
  task->evidence_count = c_get_u32(r);
  for (f = 0; f < 8; f++)
    task->evidence_ids[f] = c_get_u64(r);
}
static int digest_valid(const char *d) {
  size_t i;
  if (d[64])
    return 0;
  for (i = 0; i < 64; i++)
    if (!((d[i] >= 'a' && d[i] <= 'f') || (d[i] >= '0' && d[i] <= '9')))
      return 0;
  return 1;
}
static const c_task *stored_task(const c_trainer *t, size_t index) {
  return index < t->task_count ? &t->tasks[index]
                               : &t->retired[index - t->task_count].task;
}

static c_status retired_scope_valid(const c_trainer *t,
                                    const c_retired_task *retired) {
  const unsigned initial = t->model->groups + t->merge_count;
  uint64_t live[C_MAX_GROUPS] = {0};
  unsigned remaining = initial;
  if (retired->merge_index >= t->merge_count ||
      retired->groups != initial - retired->merge_index ||
      retired->groups < 3u || retired->groups > C_MAX_GROUPS ||
      retired->task.done != 1u)
    return C_CORRUPT;
  for (unsigned g = 0; g < initial; ++g)
    live[g] = (uint64_t)g + 1u;
  for (unsigned event = 0; event < retired->merge_index; ++event) {
    unsigned first = remaining, second = remaining;
    for (unsigned g = 0; g < remaining; ++g) {
      if (live[g] == t->merge_parents[event][0])
        first = g;
      if (live[g] == t->merge_parents[event][1])
        second = g;
    }
    if (first >= second || second >= remaining)
      return C_CORRUPT;
    live[first] = t->merge_children[event];
    for (unsigned g = second + 1u; g < remaining; ++g)
      live[g - 1u] = live[g];
    live[--remaining] = 0u;
  }
  return memcmp(live, retired->owner_uids, sizeof(live)) == 0 ? C_OK
                                                              : C_CORRUPT;
}

static c_status validate_tasks(const c_trainer *t) {
  size_t i, j, k, queued = 0, done = 0;
  const size_t total = t->task_count + t->retired_count;
  for (i = 0; i < t->receipt_count; i++) {
    if (!digest_valid(t->receipts[i]) ||
        !digest_valid(t->receipt_bindings[i]) || t->receipt_consumed[i] > 1)
      return C_CORRUPT;
    if (!t->receipt_consumed[i]) {
      size_t pending = 0;
      for (j = 0; j < t->task_count; j++)
        if (t->tasks[j].head == C_CODE && !t->tasks[j].done &&
            !memcmp(t->tasks[j].receipt, t->receipts[i], C_DIGEST_HEX))
          pending++;
      if (pending != 1)
        return C_CORRUPT;
    }
    for (j = 0; j < i; j++)
      if (!strcmp(t->receipts[i], t->receipts[j]))
        return C_CORRUPT;
  }
  for (i = 0; i < total; i++) {
    const c_task *task = stored_task(t, i);
    const unsigned groups = i < t->task_count
                                ? t->model->groups
                                : t->retired[i - t->task_count].groups;
    const unsigned mask = (1u << groups) - 1u;
    if (!task->id || task->id >= t->next_task || task->done > 1 ||
        !task->eligible || !(task->eligible & (task->eligible - 1)) ||
        (task->eligible & ~mask) || !digest_valid(task->parent) ||
        !digest_valid(task->evaluator) || !digest_valid(task->input_digest))
      return C_CORRUPT;
    for (j = 0; j < i; j++)
      if (task->id == stored_task(t, j)->id)
        return C_CORRUPT;
    if (!task->done)
      queued++;
    else
      done++;
    if (task->format > 1 || task->evidence_count > 8)
      return C_CORRUPT;
    if (task->format == 0) {
      if (task->request_id || task->evidence_count)
        return C_CORRUPT;
      for (k = 0; k < 8; k++)
        if (task->evidence_ids[k])
          return C_CORRUPT;
    }
    if (task->head == C_TEXT) {
      int found = 0;
      if (task->format == 1) {
        if (c_text_verify_task(t->context, task) != C_OK || task->receipt[0])
          return C_CORRUPT;
        continue;
      }
      for (j = 0; j < c_context_count(t->context); j++) {
        c_record record;
        double encoded[C_FEATURES];
        char input_digest[65], evaluator[65];
        if (c_context_record(t->context, j, &record) != C_OK)
          return C_CORRUPT;
        if (record.id != task->source_id)
          continue;
        if (record.kind != C_SOURCE || record.split != C_TRAIN ||
            task->offset > record.length || strcmp(record.digest, task->parent))
          return C_CORRUPT;
        if (task->target != (task->offset == record.length
                                 ? C_EOS
                                 : record.bytes[task->offset]))
          return C_CORRUPT;
        if (c_encode(record.bytes, (size_t)task->offset, encoded) != C_OK ||
            memcmp(encoded, task->input, sizeof(encoded)))
          return C_CORRUPT;
        c_hash(record.bytes, (size_t)task->offset, input_digest);
        c_hash("immutable-source-byte/v1",
               sizeof("immutable-source-byte/v1") - 1, evaluator);
        if (strcmp(input_digest, task->input_digest) ||
            strcmp(evaluator, task->evaluator))
          return C_CORRUPT;
        found = 1;
        break;
      }
      if (!found || task->receipt[0])
        return C_CORRUPT;
    } else if (task->head == C_CODE) {
      int found = 0;
      char binding[65];
      if (task->target >= C_CODE_ACTIONS || !digest_valid(task->receipt))
        return C_CORRUPT;
      for (k = 0; k < t->receipt_count; k++)
        if (!strcmp(task->receipt, t->receipts[k])) {
          found = 1;
          break;
        }
      if (!found || task->source_id || task->offset || task->format)
        return C_CORRUPT;
      if (task->done != t->receipt_consumed[k])
        return C_CORRUPT;
      if (c_task_binding(task, binding) != C_OK ||
          strcmp(binding, t->receipt_bindings[k]))
        return C_CORRUPT;
      for (j = 0; j < i; j++)
        if (stored_task(t, j)->head == C_CODE &&
            !strcmp(task->receipt, stored_task(t, j)->receipt))
          return C_CORRUPT;
    } else
      return C_CORRUPT;
  }
  return queued == t->report.queued && done <= t->report.completed ? C_OK
                                                                   : C_CORRUPT;
}
c_status c_trainer_validate(const c_trainer *t) {
  if (!t || !t->model || !t->context || t->model->groups < 2 ||
      t->model->groups > C_MAX_GROUPS || t->task_count > C_MAX_TASKS ||
      t->receipt_count > C_MAX_RECEIPTS ||
      t->retired_count > C_MAX_RETIRED_TASKS)
    return C_CORRUPT;
  unsigned g, mask = (1u << t->model->groups) - 1;
  size_t i;
  uint64_t clocks = 0;
  if (t->research_mode || t->model->groups < 2)
    return C_CORRUPT;
  if (t->policy.enabled > 1 || t->policy.editing_enabled > 1 ||
      (!t->policy.enabled && t->policy.editing_enabled) ||
      t->policy.clock > t->report.contacts ||
      t->policy.interventions > t->generation ||
      t->policy.contact_changes > t->policy.interventions ||
      t->merge_count > 2 || (t->merge_count == 0 && t->retired_clock))
    return C_CORRUPT;
  for (unsigned a = 0; a < 4; a++)
    for (unsigned f = 0; f < C_FEATURES; f++) {
      const c_scalar *p = &t->policy.readout[a][f];
      if (!isfinite(p->value) || !isfinite(p->first) || !isfinite(p->second) ||
          p->second < 0 ||
          (!t->policy.enabled && (p->value || p->first || p->second)))
        return C_CORRUPT;
    }
  if (!t->policy.enabled &&
      (t->policy.clock || t->policy.interventions || t->policy.contact_changes))
    return C_CORRUPT;
  for (unsigned j = 0; j < t->merge_count; j++)
    if (!t->merge_parents[j][0] || !t->merge_parents[j][1] ||
        t->merge_parents[j][0] == t->merge_parents[j][1] ||
        t->merge_children[j] <= t->merge_parents[j][0] ||
        t->merge_children[j] <= t->merge_parents[j][1])
      return C_CORRUPT;
  for (unsigned j = t->merge_count; j < 2; j++)
    if (t->merge_parents[j][0] || t->merge_parents[j][1] ||
        t->merge_children[j])
      return C_CORRUPT;
  unsigned initial_groups = t->model->groups + t->merge_count;
  uint64_t live[C_MAX_GROUPS];
  if (initial_groups > C_MAX_GROUPS)
    return C_CORRUPT;
  for (unsigned j = 0; j < initial_groups; j++)
    live[j] = (uint64_t)j + 1;
  unsigned remaining = initial_groups;
  for (unsigned j = 0; j < t->merge_count; j++) {
    unsigned first = remaining, second = remaining;
    for (unsigned slot = 0; slot < remaining; slot++) {
      if (live[slot] == t->merge_parents[j][0])
        first = slot;
      if (live[slot] == t->merge_parents[j][1])
        second = slot;
    }
    if (first >= second || second >= remaining ||
        t->merge_children[j] != (uint64_t)initial_groups + j + 1 ||
        t->merge_generations[j] > t->generation)
      return C_CORRUPT;
    unsigned char after[C_WORLD_CELLS];
    unsigned contacts;
    for (size_t cell = 0; cell < C_WORLD_CELLS; cell++)
      if (t->merge_worlds[j][cell] & ~((1u << remaining) - 1u))
        return C_CORRUPT;
    c_life_evolve(t->merge_worlds[j], after, &contacts);
    if (!(contacts & (1u << (first * C_MAX_GROUPS + second))))
      return C_CORRUPT;
    live[first] = t->merge_children[j];
    for (unsigned slot = second + 1; slot < remaining; slot++)
      live[slot - 1] = live[slot];
    remaining--;
  }
  for (unsigned j = 0; j < remaining; j++)
    if (live[j] != t->model->expert[j].uid)
      return C_CORRUPT;
  for (unsigned j = t->merge_count; j < 2; j++) {
    if (t->merge_generations[j])
      return C_CORRUPT;
    for (size_t cell = 0; cell < C_WORLD_CELLS; cell++)
      if (t->merge_worlds[j][cell])
        return C_CORRUPT;
  }
  if (!t->merge_count)
    for (unsigned j = 0; j < t->model->groups; j++)
      if (t->model->expert[j].uid != (uint64_t)j + 1)
        return C_CORRUPT;
  for (size_t retired = 0; retired < t->retired_count; ++retired)
    if (retired_scope_valid(t, &t->retired[retired]) != C_OK)
      return C_CORRUPT;
  char fingerprint[C_DIGEST_HEX];
  if (c_model_fingerprint(t->model, 0, fingerprint) != C_OK ||
      t->model->shared_clock > t->report.updates)
    return C_CORRUPT;
  if (!t->next_task || t->report.generation != t->generation ||
      t->report.completed != t->report.updates ||
      t->report.updates > t->report.contacts ||
      t->report.contacts > t->generation ||
      t->report.deferred > t->generation - t->report.updates ||
      t->report.reseeds != t->generation / 8 || t->report.last_loss < 0)
    return C_CORRUPT;
  for (i = 0; i < C_WORLD_CELLS; i++)
    if (t->cells[i] & ~mask)
      return C_CORRUPT;
  for (g = 0; g < C_MAX_GROUPS; g++) {
    uint64_t clock = g < t->model->groups ? t->model->expert[g].clock : 0;
    if (clock != t->report.group_updates[g] || clock > t->report.updates ||
        clock > UINT64_MAX - clocks)
      return C_CORRUPT;
    clocks += clock;
  }
  if (t->retired_clock > UINT64_MAX - clocks ||
      t->report.updates > UINT64_MAX / 2 ||
      clocks + t->retired_clock < 2 * t->report.updates)
    return C_CORRUPT;
  return validate_tasks(t);
}
c_status c_trainer_save(const c_trainer *t, const char *path) {
  c_writer w = {0};
  unsigned char *context = NULL;
  size_t length = 0, i;
  c_status s;
  static const char recipe[] = C_RECIPE;
  static const char build[] = C_BUILD_ID;
  if (!t || !path || DBL_MANT_DIG != 53 || sizeof(double) != 8)
    return C_INVALID;
  s = c_trainer_validate(t);
  if (s != C_OK)
    return s;
  s = c_context_pack(t->context, &context, &length);
  if (s != C_OK)
    return s;
  c_put_u32(&w, 3);
  c_put_u32(&w, (uint32_t)sizeof(recipe));
  c_put_bytes(&w, recipe, sizeof(recipe));
  c_put_u32(&w, (uint32_t)sizeof(build));
  c_put_bytes(&w, build, sizeof(build));
  c_model_write(&w, t->model, 1);
  c_put_bytes(&w, t->cells, sizeof(t->cells));
  c_put_u64(&w, t->generation);
  c_put_u64(&w, t->rng);
  c_put_u64(&w, t->next_task);
  put_report(&w, &t->report);
  c_put_u32(&w, (uint32_t)t->task_count);
  for (i = 0; i < t->task_count; i++)
    put_task(&w, &t->tasks[i]);
  c_put_u32(&w, (uint32_t)t->receipt_count);
  for (i = 0; i < t->receipt_count; i++) {
    c_put_bytes(&w, t->receipts[i], C_DIGEST_HEX);
    c_put_bytes(&w, t->receipt_bindings[i], C_DIGEST_HEX);
    c_put_u32(&w, t->receipt_consumed[i]);
  }
  c_put_u32(&w, t->policy.enabled);
  c_put_u32(&w, t->policy.editing_enabled);
  c_put_u64(&w, t->policy.clock);
  c_put_u64(&w, t->policy.interventions);
  c_put_u64(&w, t->policy.contact_changes);
  for (unsigned a = 0; a < 4; a++)
    for (unsigned f = 0; f < C_FEATURES; f++)
      put_scalar(&w, &t->policy.readout[a][f]);
  c_put_u32(&w, t->merge_count);
  c_put_u64(&w, t->retired_clock);
  for (unsigned j = 0; j < 2; j++) {
    c_put_u64(&w, t->merge_parents[j][0]);
    c_put_u64(&w, t->merge_parents[j][1]);
    c_put_u64(&w, t->merge_children[j]);
    c_put_u64(&w, t->merge_generations[j]);
    c_put_bytes(&w, t->merge_worlds[j], C_WORLD_CELLS);
  }
  c_put_u32(&w, (uint32_t)t->retired_count);
  for (size_t retired = 0; retired < t->retired_count; ++retired) {
    const c_retired_task *entry = &t->retired[retired];
    c_put_u32(&w, entry->groups);
    c_put_u32(&w, entry->merge_index);
    for (unsigned group = 0; group < C_MAX_GROUPS; ++group)
      c_put_u64(&w, entry->owner_uids[group]);
    put_task(&w, &entry->task);
  }
  c_put_u64(&w, length);
  c_put_bytes(&w, context, length);
  free(context);
  s = w.status == C_OK ? c_envelope_write(path, "CLIFE001", w.data, w.length)
                       : w.status;
  free(w.data);
  return s;
}
c_status c_trainer_load(const char *path, c_trainer **out) {
  unsigned char *data = NULL;
  size_t length = 0, i;
  uint64_t context_length;
  c_reader r;
  uint32_t version;
  c_trainer *t;
  c_status s;
  char recipe[sizeof(C_RECIPE)], build[sizeof(C_BUILD_ID)];
  if (!path || !out || DBL_MANT_DIG != 53 || sizeof(double) != 8)
    return C_INVALID;
  s = c_envelope_read(path, "CLIFE001", &data, &length);
  if (s != C_OK)
    return s;
  r.data = data;
  r.length = length;
  r.offset = 0;
  r.status = C_OK;
  version = c_get_u32(&r);
  if ((version != 1 && version != 2 && version != 3) ||
      c_get_u32(&r) != sizeof(recipe)) {
    free(data);
    return C_CORRUPT;
  }
  c_get_bytes(&r, recipe, sizeof(recipe));
  if (memcmp(recipe, C_RECIPE, sizeof(recipe)) ||
      c_get_u32(&r) != sizeof(build)) {
    free(data);
    return C_CORRUPT;
  }
  c_get_bytes(&r, build, sizeof(build));
  if (r.status != C_OK || memcmp(build, C_BUILD_ID, sizeof(build))) {
    free(data);
    return C_CORRUPT;
  }
  t = (c_trainer *)calloc(1, sizeof(*t));
  if (!t) {
    free(data);
    return C_NOMEM;
  }
  s = c_model_read(&r, &t->model, version >= 2);
  if (s != C_OK)
    goto failed;
  c_get_bytes(&r, t->cells, sizeof(t->cells));
  t->generation = c_get_u64(&r);
  t->rng = c_get_u64(&r);
  t->next_task = c_get_u64(&r);
  get_report(&r, &t->report);
  t->task_count = c_get_u32(&r);
  if (t->task_count > C_MAX_TASKS) {
    s = C_CORRUPT;
    goto failed;
  }
  for (i = 0; i < t->task_count; i++)
    get_task(&r, &t->tasks[i]);
  t->receipt_count = c_get_u32(&r);
  if (t->receipt_count > C_MAX_RECEIPTS) {
    s = C_CORRUPT;
    goto failed;
  }
  for (i = 0; i < t->receipt_count; i++) {
    c_get_bytes(&r, t->receipts[i], C_DIGEST_HEX);
    c_get_bytes(&r, t->receipt_bindings[i], C_DIGEST_HEX);
    {
      uint32_t consumed = c_get_u32(&r);
      if (consumed > 1)
        r.status = C_CORRUPT;
      t->receipt_consumed[i] = (unsigned char)consumed;
    }
  }
  if (version >= 2) {
    t->policy.enabled = c_get_u32(&r);
    t->policy.editing_enabled =
        version >= 3 ? c_get_u32(&r) : t->policy.enabled;
    t->policy.clock = c_get_u64(&r);
    t->policy.interventions = c_get_u64(&r);
    t->policy.contact_changes = c_get_u64(&r);
    for (unsigned a = 0; a < 4; a++)
      for (unsigned f = 0; f < C_FEATURES; f++)
        get_scalar(&r, &t->policy.readout[a][f]);
    t->merge_count = c_get_u32(&r);
    t->retired_clock = c_get_u64(&r);
    for (unsigned j = 0; j < 2; j++) {
      t->merge_parents[j][0] = c_get_u64(&r);
      t->merge_parents[j][1] = c_get_u64(&r);
      t->merge_children[j] = c_get_u64(&r);
      t->merge_generations[j] = c_get_u64(&r);
      c_get_bytes(&r, t->merge_worlds[j], C_WORLD_CELLS);
    }
  }
  if (version >= 3) {
    t->retired_count = c_get_u32(&r);
    if (r.status != C_OK || t->retired_count > C_MAX_RETIRED_TASKS) {
      s = C_CORRUPT;
      goto failed;
    }
    for (size_t retired = 0; retired < t->retired_count; ++retired) {
      c_retired_task *entry = &t->retired[retired];
      entry->groups = c_get_u32(&r);
      entry->merge_index = c_get_u32(&r);
      for (unsigned group = 0; group < C_MAX_GROUPS; ++group)
        entry->owner_uids[group] = c_get_u64(&r);
      get_task(&r, &entry->task);
    }
  }
  context_length = c_get_u64(&r);
  if (r.status != C_OK || r.offset > r.length ||
      context_length != r.length - r.offset) {
    s = C_CORRUPT;
    goto failed;
  }
  s = c_context_unpack(r.data + r.offset, (size_t)context_length, &t->context);
  if (s != C_OK)
    goto failed;
  s = c_trainer_validate(t);
  if (s != C_OK)
    goto failed;
  if (*out) {
    s = C_INVALID;
    goto failed;
  }
  free(data);
  *out = t;
  return C_OK;
failed:
  free(data);
  c_trainer_destroy(t);
  return s;
}
