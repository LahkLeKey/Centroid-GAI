#include "centroid_extensions.h"
#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned group_mask(const c_trainer *t) {
  return (1u << t->model->groups) - 1u;
}
static int valid_digest(const char *text) {
  size_t i;
  if (!text || strlen(text) != 64)
    return 0;
  for (i = 0; i < 64; i++)
    if (!((text[i] >= '0' && text[i] <= '9') ||
          (text[i] >= 'a' && text[i] <= 'f')))
      return 0;
  return 1;
}
static void refresh_report(c_trainer *t) {
  size_t i;
  t->report.generation = t->generation;
  t->report.queued = 0;
  for (i = 0; i < t->task_count; i++)
    if (!t->tasks[i].done)
      t->report.queued++;
  for (i = 0; i < C_MAX_GROUPS; i++)
    t->report.group_updates[i] =
        i < t->model->groups ? t->model->expert[i].clock : 0;
}
static c_status source_record(const c_trainer *t, uint64_t id,
                              c_record *record) {
  size_t i;
  for (i = 0; i < c_context_count(t->context); i++) {
    c_status s = c_context_record(t->context, i, record);
    if (s != C_OK)
      return s;
    if (record->id == id)
      return record->kind == C_SOURCE && record->split == C_TRAIN ? C_OK
                                                                  : C_INVALID;
  }
  return C_NOT_FOUND;
}
static void compact_tasks(c_trainer *t) {
  size_t i, j = 0;
  for (i = 0; i < t->task_count; i++)
    if (!t->tasks[i].done)
      t->tasks[j++] = t->tasks[i];
  memset(t->tasks + j, 0, (t->task_count - j) * sizeof(t->tasks[0]));
  t->task_count = j;
}
static c_status trainer_create(unsigned groups, uint64_t seed,
                               unsigned research, c_trainer **out) {
  c_trainer *t;
  c_status s;
  if (!out || *out || groups < (research ? 1u : 2u) || groups > C_MAX_GROUPS)
    return C_INVALID;
  t = (c_trainer *)calloc(1, sizeof(*t));
  if (!t)
    return C_NOMEM;
  s = c_model_create(groups, seed, &t->model);
  if (s == C_OK)
    s = c_context_create(&t->context);
  if (s != C_OK) {
    c_trainer_destroy(t);
    return s;
  }
  t->rng = seed;
  t->next_task = 1;
  t->research_mode = research;
  c_life_seed(t);
  refresh_report(t);
  *out = t;
  return C_OK;
}
c_status c_trainer_create(unsigned groups, uint64_t seed, c_trainer **out) {
  return trainer_create(groups, seed, 0, out);
}
c_status c_trainer_research_create(unsigned groups, uint64_t seed,
                                   c_trainer **out) {
  return trainer_create(groups, seed, 1, out);
}
void c_trainer_destroy(c_trainer *t) {
  if (t) {
    c_model_destroy(t->model);
    c_context_destroy(t->context);
    free(t);
  }
}
c_context *c_trainer_context(c_trainer *t) { return t ? t->context : NULL; }
const c_model *c_trainer_model(const c_trainer *t) {
  return t ? t->model : NULL;
}
c_status c_trainer_enqueue_source(c_trainer *t, uint64_t record_id,
                                  size_t offset, unsigned eligible) {
  c_record record;
  c_task task;
  size_t i;
  c_status s;
  if (!t || !eligible || !(eligible & (eligible - 1)) ||
      (eligible & ~group_mask(t)))
    return C_INVALID;
  s = source_record(t, record_id, &record);
  if (s != C_OK)
    return s;
  if (offset > record.length)
    return C_INVALID;
  for (i = 0; i < t->task_count; i++)
    if (!t->tasks[i].done && t->tasks[i].head == C_TEXT &&
        t->tasks[i].format == 0 && t->tasks[i].source_id == record_id &&
        t->tasks[i].offset == offset && t->tasks[i].eligible == eligible)
      return C_OK;
  memset(&task, 0, sizeof(task));
  task.head = C_TEXT;
  task.source_id = record_id;
  task.offset = offset;
  task.target = offset == record.length ? C_EOS : record.bytes[offset];
  task.eligible = eligible;
  s = c_encode(record.bytes, offset, task.input);
  if (s != C_OK)
    return s;
  memcpy(task.parent, record.digest, C_DIGEST_HEX);
  c_hash("immutable-source-byte/v1", sizeof("immutable-source-byte/v1") - 1,
         task.evaluator);
  c_hash(record.bytes, offset, task.input_digest);
  if (t->next_task == UINT64_MAX)
    return C_LIMIT;
  if (t->task_count == C_MAX_TASKS)
    compact_tasks(t);
  if (t->task_count == C_MAX_TASKS)
    return C_LIMIT;
  task.id = t->next_task++;
  t->tasks[t->task_count++] = task;
  refresh_report(t);
  return C_OK;
}
c_status c_task_binding(const c_task *task, char digest[C_DIGEST_HEX]) {
  c_writer w = {0};
  unsigned i;
  c_status s;
  if (!task || !digest)
    return C_INVALID;
  c_put_u32(&w, (uint32_t)task->head);
  c_put_u32(&w, task->target);
  c_put_u32(&w, task->eligible);
  c_put_bytes(&w, task->input_digest, 64);
  for (i = 0; i < C_FEATURES; i++)
    c_put_double(&w, task->input[i]);
  c_put_bytes(&w, task->parent, 64);
  c_put_bytes(&w, task->evaluator, 64);
  s = w.status;
  if (s == C_OK)
    c_hash(w.data, w.length, digest);
  free(w.data);
  return s;
}
c_status c_trainer_enqueue_text(c_trainer *t, uint64_t request_id,
                                const uint64_t *evidence_ids,
                                size_t evidence_count, uint64_t answer_id,
                                size_t offset, unsigned eligible) {
  c_task task;
  size_t i;
  c_status s;
  if (!t || !eligible || !(eligible & (eligible - 1)) ||
      (eligible & ~group_mask(t)))
    return C_INVALID;
  s = c_text_prepare_task(t->context, request_id, evidence_ids, evidence_count,
                          answer_id, offset, eligible, &task);
  if (s != C_OK)
    return s;
  for (i = 0; i < t->task_count; i++)
    if (!t->tasks[i].done && t->tasks[i].head == C_TEXT &&
        t->tasks[i].format == 1 && t->tasks[i].source_id == answer_id &&
        t->tasks[i].offset == offset && t->tasks[i].eligible == eligible &&
        !strcmp(t->tasks[i].input_digest, task.input_digest))
      return C_OK;
  if (t->next_task == UINT64_MAX)
    return C_LIMIT;
  if (t->task_count == C_MAX_TASKS)
    compact_tasks(t);
  if (t->task_count == C_MAX_TASKS)
    return C_LIMIT;
  task.id = t->next_task++;
  t->tasks[t->task_count++] = task;
  refresh_report(t);
  return C_OK;
}
c_status c_trainer_replay(c_trainer *t, size_t maximum, size_t *requeued) {
  size_t i, count = 0;
  if (!t || !requeued || maximum > C_MAX_TASKS)
    return C_INVALID;
  for (i = 0; i < t->task_count && count < maximum; i++)
    if (t->tasks[i].head == C_TEXT && t->tasks[i].done) {
      t->tasks[i].done = 0;
      count++;
    }
  *requeued = count;
  refresh_report(t);
  return C_OK;
}
c_status c_trainer_enqueue_measurement(c_trainer *t, const unsigned char *input,
                                       size_t length, unsigned target,
                                       unsigned eligible, const char *parent,
                                       const char *evaluator,
                                       const char *receipt) {
  c_task task;
  char binding[C_DIGEST_HEX];
  size_t i;
  c_status s;
  if (!t || (!input && length) || target >= C_CODE_ACTIONS || !eligible ||
      !(eligible & (eligible - 1)) || (eligible & ~group_mask(t)) ||
      !valid_digest(parent) || !valid_digest(evaluator) ||
      !valid_digest(receipt))
    return C_INVALID;
  memset(&task, 0, sizeof(task));
  task.head = C_CODE;
  task.target = target;
  task.eligible = eligible;
  s = c_encode(input, length, task.input);
  if (s != C_OK)
    return s;
  memcpy(task.parent, parent, C_DIGEST_HEX);
  memcpy(task.evaluator, evaluator, C_DIGEST_HEX);
  memcpy(task.receipt, receipt, C_DIGEST_HEX);
  c_hash(input, length, task.input_digest);
  s = c_task_binding(&task, binding);
  if (s != C_OK)
    return s;
  for (i = 0; i < t->receipt_count; i++)
    if (!strcmp(t->receipts[i], receipt))
      return strcmp(t->receipt_bindings[i], binding) ? C_INVALID : C_OK;
  if (t->receipt_count == C_MAX_RECEIPTS || t->next_task == UINT64_MAX)
    return C_LIMIT;
  if (t->task_count == C_MAX_TASKS)
    compact_tasks(t);
  if (t->task_count == C_MAX_TASKS)
    return C_LIMIT;
  memcpy(task.parent, parent, C_DIGEST_HEX);
  memcpy(task.evaluator, evaluator, C_DIGEST_HEX);
  memcpy(task.receipt, receipt, C_DIGEST_HEX);
  task.id = t->next_task++;
  t->tasks[t->task_count++] = task;
  memcpy(t->receipts[t->receipt_count], receipt, C_DIGEST_HEX);
  memcpy(t->receipt_bindings[t->receipt_count++], binding, C_DIGEST_HEX);
  refresh_report(t);
  return C_OK;
}
static unsigned participants_for(unsigned contact_graph, unsigned eligible,
                                 unsigned groups) {
  unsigned i, j, participants = 0;
  for (i = 0; i < groups; i++)
    for (j = i + 1; j < groups; j++)
      if ((contact_graph & (1u << (i * C_MAX_GROUPS + j))) &&
          (eligible & (1u << i)) && (eligible & (1u << j)))
        participants |= (1u << i) | (1u << j);
  return participants;
}
static c_status generation_bounds(const c_trainer *t, size_t selected,
                                  unsigned participants, unsigned contacts,
                                  unsigned scheduler) {
  if ((contacts && t->report.contacts == UINT64_MAX) ||
      (!scheduler && (t->generation + 1u) % 8u == 0u &&
       t->report.reseeds == UINT64_MAX))
    return C_LIMIT;
  if (selected == SIZE_MAX)
    return t->report.queued && t->report.deferred == UINT64_MAX ? C_LIMIT
                                                                : C_OK;
  if (t->report.updates == UINT64_MAX || t->report.completed == UINT64_MAX ||
      (t->model->shared_enabled && t->model->shared_clock == UINT64_MAX))
    return C_LIMIT;
  unsigned admitted = 0;
  for (unsigned g = 0; g < t->model->groups; ++g)
    if (participants & (1u << g)) {
      if (t->model->expert[g].clock == UINT64_MAX)
        return C_LIMIT;
      ++admitted;
    }
  if (!t->research_mode) {
    /* The canonical production audit includes retired clocks. Per-owner clocks
     * may each fit while their complete next-generation sum does not. Check
     * this before applying any numerical, task, receipt or coverage changes. */
    uint64_t clocks = t->retired_clock;
    if (t->report.updates >= UINT64_MAX / 2u)
      return C_LIMIT;
    for (unsigned g = 0; g < t->model->groups; ++g) {
      if (t->model->expert[g].clock > UINT64_MAX - clocks)
        return C_LIMIT;
      clocks += t->model->expert[g].clock;
    }
    if ((uint64_t)admitted > UINT64_MAX - clocks)
      return C_LIMIT;
  }
  return C_OK;
}
static c_status generation(c_trainer *t, unsigned scheduler) {
  unsigned char after[C_WORLD_CELLS];
  unsigned contacts, participants = 0;
  size_t i, selected = SIZE_MAX;
  c_gradient *gradients;
  double mass[C_MAX_GROUPS] = {1, 1, 1, 1}, loss = 0;
  c_status s;
  c_policy policy_candidate = t->policy;
  if (t->generation == UINT64_MAX)
    return C_LIMIT;
  c_life_evolve(t->cells, after, &contacts);
  if (t->policy.enabled && !scheduler) {
    unsigned char edited[C_WORLD_CELLS];
    s = c_policy_prepare(t, after, contacts, &policy_candidate, edited);
    if (s != C_OK)
      return s;
    memcpy(after, edited, sizeof(after));
  }
  if (scheduler == 1)
    memcpy(after, t->cells, sizeof(after));
  for (i = 0; i < t->task_count; i++)
    if (!t->tasks[i].done) {
      unsigned matching =
          participants_for(contacts, t->tasks[i].eligible, t->model->groups);
      if (scheduler == 2)
        matching = t->tasks[i].eligible;
      if (t->model->shared_enabled && matching != group_mask(t))
        matching = 0;
      if (matching) {
        selected = i;
        participants = matching;
        break;
      }
    }
  s = generation_bounds(t, selected, participants, contacts, scheduler);
  if (s != C_OK)
    return s;
  if (selected != SIZE_MAX) {
    c_task *task = &t->tasks[selected];
    size_t receipt_index = SIZE_MAX;
    if (task->head == C_CODE) {
      for (i = 0; i < t->receipt_count; i++)
        if (!strcmp(task->receipt, t->receipts[i])) {
          receipt_index = i;
          break;
        }
      if (receipt_index == SIZE_MAX || t->receipt_consumed[receipt_index])
        return C_CORRUPT;
    }
    gradients = (c_gradient *)calloc(C_MAX_GROUPS, sizeof(*gradients));
    if (!gradients)
      return C_NOMEM;
    /* All participant proposals and derivatives observe one frozen model.
     * No mutation occurs until the complete candidate has been validated. */
    s = c_model_gradient(t->model, task->head, task->input, participants, mass,
                         task->target, gradients, &loss);
    if (s == C_OK && t->model->shared_enabled) {
      double shared[C_FEATURES];
      s = c_model_shared_gradient(t->model, task->head, task->input,
                                  participants, mass, task->target, shared);
      if (s == C_OK)
        s = c_model_apply_shared(t->model, task->head, participants, gradients,
                                 shared, 0.03);
    } else if (s == C_OK)
      s = c_model_apply(t->model, task->head, participants, gradients, 0.03);
    free(gradients);
    if (s != C_OK)
      return s;
    task->done = 1;
    if (receipt_index != SIZE_MAX)
      t->receipt_consumed[receipt_index] = 1;
    t->report.updates++;
    t->report.completed++;
    t->report.last_loss = loss;
  } else if (t->report.queued)
    t->report.deferred++;
  memcpy(t->cells, after, sizeof(after));
  t->policy = policy_candidate;
  t->generation++;
  if (contacts)
    t->report.contacts++;
  /* A declared fixed interval renews encounters without inspecting labels,
   * task outcomes or owner utility. Extinction never touches model owners. */
  if (!scheduler && t->generation % 8 == 0) {
    c_life_seed(t);
    t->report.reseeds++;
  }
  refresh_report(t);
  return C_OK;
}
c_status c_trainer_step(c_trainer *t, size_t generations,
                        c_training_report *report) {
  size_t i;
  c_status s;
  if (!t || t->research_mode || !report || generations > 1000000)
    return C_INVALID;
  for (i = 0; i < generations; i++) {
    s = generation(t, 0);
    if (s != C_OK) {
      refresh_report(t);
      *report = t->report;
      return s;
    }
  }
  refresh_report(t);
  *report = t->report;
  return C_OK;
}
c_status c_trainer_research_step(c_trainer *t, size_t generations,
                                 unsigned scheduler,
                                 c_training_report *report) {
  if (!t || !t->research_mode || !report || scheduler > 2 ||
      generations > 1000000)
    return C_INVALID;
  c_status s = C_OK;
  for (size_t i = 0; i < generations && s == C_OK; ++i)
    s = generation(t, scheduler);
  refresh_report(t);
  *report = t->report;
  return s;
}
c_status c_trainer_research_enqueue(c_trainer *t, uint64_t id, size_t offset,
                                    unsigned eligible,
                                    const double input[C_FEATURES]) {
  c_record record;
  if (!t || !t->research_mode || !input || !eligible ||
      (eligible & ~group_mask(t)))
    return C_INVALID;
  c_status s = source_record(t, id, &record);
  if (s != C_OK)
    return s;
  if (offset > record.length || t->next_task == UINT64_MAX)
    return C_INVALID;
  for (size_t j = 0; j < C_FEATURES; ++j)
    if (!isfinite(input[j]))
      return C_INVALID;
  if (t->task_count == C_MAX_TASKS)
    compact_tasks(t);
  if (t->task_count == C_MAX_TASKS)
    return C_LIMIT;
  c_task task = {0};
  task.id = t->next_task++;
  task.source_id = id;
  task.offset = offset;
  task.head = C_TEXT;
  task.eligible = eligible;
  task.format = 2;
  task.target = offset == record.length ? C_EOS : record.bytes[offset];
  memcpy(task.input, input, sizeof(task.input));
  memcpy(task.parent, record.digest, sizeof(task.parent));
  t->tasks[t->task_count++] = task;
  refresh_report(t);
  return C_OK;
}
c_status c_trainer_report(const c_trainer *t, c_training_report *report) {
  if (!t || !report)
    return C_INVALID;
  *report = t->report;
  return C_OK;
}

size_t c_trainer_retired_count(const c_trainer *t) {
  return t ? t->retired_count : 0u;
}

size_t c_trainer_retention_count(const c_trainer *t) {
  size_t count;
  if (!t)
    return 0u;
  count = t->retired_count;
  for (size_t i = 0; i < t->task_count; ++i)
    if (t->tasks[i].done)
      ++count;
  return count;
}

c_status c_trainer_retention_task(const c_trainer *t, size_t index,
                                  const c_task **out) {
  if (!t || !out)
    return C_INVALID;
  for (size_t i = 0; i < t->task_count; ++i)
    if (t->tasks[i].done) {
      if (!index) {
        *out = &t->tasks[i];
        return C_OK;
      }
      --index;
    }
  if (index >= t->retired_count)
    return C_NOT_FOUND;
  *out = &t->retired[index].task;
  return C_OK;
}

c_status c_trainer_task_scope(const c_trainer *parent,
                              const c_trainer *candidate, const c_task *task,
                              unsigned *mask) {
  uint64_t owners[C_MAX_GROUPS] = {0};
  unsigned groups = 0, mapped = 0;
  if (!parent || !candidate || !task || !mask || !parent->model ||
      !candidate->model)
    return C_INVALID;
  if (parent->merge_count > 2u || candidate->merge_count > 2u ||
      parent->merge_count > candidate->merge_count ||
      parent->model->groups + parent->merge_count !=
          candidate->model->groups + candidate->merge_count)
    return C_CORRUPT;
  for (unsigned i = 0; i < parent->merge_count; ++i)
    if (memcmp(parent->merge_parents[i], candidate->merge_parents[i],
               sizeof(parent->merge_parents[i])) ||
        parent->merge_children[i] != candidate->merge_children[i] ||
        parent->merge_generations[i] != candidate->merge_generations[i] ||
        memcmp(parent->merge_worlds[i], candidate->merge_worlds[i],
               C_WORLD_CELLS))
      return C_CORRUPT;
  for (size_t i = 0; i < parent->task_count; ++i)
    if (task == &parent->tasks[i]) {
      groups = parent->model->groups;
      for (unsigned g = 0; g < groups; ++g)
        owners[g] = parent->model->expert[g].uid;
      break;
    }
  if (!groups)
    for (size_t i = 0; i < parent->retired_count; ++i)
      if (task == &parent->retired[i].task) {
        groups = parent->retired[i].groups;
        memcpy(owners, parent->retired[i].owner_uids, sizeof(owners));
        break;
      }
  if (!groups || groups > C_MAX_GROUPS || !task->eligible ||
      (task->eligible >> groups))
    return C_INVALID;
  for (unsigned g = 0; g < groups; ++g)
    if (task->eligible & (1u << g)) {
      uint64_t uid = owners[g];
      for (unsigned event = 0; event < candidate->merge_count; ++event)
        if (uid == candidate->merge_parents[event][0] ||
            uid == candidate->merge_parents[event][1])
          uid = candidate->merge_children[event];
      unsigned found = 0;
      for (unsigned slot = 0; slot < candidate->model->groups; ++slot)
        if (candidate->model->expert[slot].uid == uid) {
          mapped |= 1u << slot;
          found = 1;
          break;
        }
      if (!found)
        return C_CORRUPT;
    }
  if (!mapped)
    return C_CORRUPT;
  *mask = mapped;
  return C_OK;
}

c_status c_trainer_replay_retired(c_trainer *t, size_t first, size_t maximum,
                                  unsigned eligible, size_t *requeued) {
  c_trainer *staged;
  size_t inserted = 0, end;
  c_status status;
  if (!t || !requeued || !eligible || !(eligible & (eligible - 1u)) ||
      (eligible & ~group_mask(t)) || first > t->retired_count)
    return C_INVALID;
  if (maximum > C_MAX_TASKS)
    return C_LIMIT;
  status = c_trainer_validate(t);
  if (status != C_OK)
    return status;
  if (!maximum) {
    *requeued = 0u;
    return C_OK;
  }
  staged = malloc(sizeof(*staged));
  if (!staged)
    return C_NOMEM;
  memcpy(staged, t, sizeof(*staged));
  end = t->retired_count - first < maximum ? t->retired_count : first + maximum;
  for (size_t i = first; i < end; ++i) {
    const c_task *original = &t->retired[i].task;
    if (original->head != C_TEXT)
      continue;
    int duplicate = 0;
    for (size_t j = 0; j < staged->task_count; ++j) {
      const c_task *pending = &staged->tasks[j];
      if (!pending->done && pending->head == C_TEXT &&
          pending->source_id == original->source_id &&
          pending->offset == original->offset &&
          pending->format == original->format &&
          pending->eligible == eligible &&
          !strcmp(pending->input_digest, original->input_digest)) {
        duplicate = 1;
        break;
      }
    }
    if (duplicate)
      continue;
    if (staged->next_task == UINT64_MAX) {
      status = C_LIMIT;
      break;
    }
    if (staged->task_count == C_MAX_TASKS)
      compact_tasks(staged);
    if (staged->task_count == C_MAX_TASKS) {
      status = C_LIMIT;
      break;
    }
    c_task replay = *original;
    replay.eligible = eligible;
    replay.done = 0u;
    replay.id = staged->next_task++;
    staged->tasks[staged->task_count++] = replay;
    ++inserted;
  }
  if (status == C_OK) {
    refresh_report(staged);
    status = c_trainer_validate(staged);
  }
  if (status == C_OK) {
    memcpy(t->tasks, staged->tasks, sizeof(t->tasks));
    t->task_count = staged->task_count;
    t->next_task = staged->next_task;
    t->report = staged->report;
    *requeued = inserted;
  }
  free(staged); /* model/context are borrowed from the unchanged incumbent. */
  return status;
}

static c_status fork_trainer(const c_trainer *parent, c_trainer **out) {
  if (!parent || !out || *out || parent->research_mode)
    return C_INVALID;
  c_status valid = c_trainer_validate(parent);
  if (valid != C_OK)
    return valid;
  c_trainer *copy = malloc(sizeof(*copy));
  if (!copy)
    return C_NOMEM;
  memcpy(copy, parent, sizeof(*copy));
  copy->model = NULL;
  copy->context = NULL;
  unsigned char *bytes = NULL;
  size_t length = 0;
  c_status s = c_context_pack(parent->context, &bytes, &length);
  if (s == C_OK)
    s = c_context_unpack(bytes, length, &copy->context);
  free(bytes);
  if (s == C_OK) {
    copy->model = malloc(sizeof(*copy->model));
    if (!copy->model)
      s = C_NOMEM;
    else
      memcpy(copy->model, parent->model, sizeof(*copy->model));
  }
  if (s != C_OK) {
    c_trainer_destroy(copy);
    return s;
  }
  *out = copy;
  return C_OK;
}
c_status c_trainer_shared_candidate(const c_trainer *parent, c_trainer **out) {
  if (!parent || parent->model->shared_enabled)
    return C_INVALID;
  c_status s = fork_trainer(parent, out);
  if (s == C_OK)
    (*out)->model->shared_enabled = 1;
  return s;
}
c_status c_trainer_policy_candidate(const c_trainer *parent, c_trainer **out) {
  if (!parent || parent->policy.enabled)
    return C_INVALID;
  c_status s = fork_trainer(parent, out);
  if (s == C_OK) {
    (*out)->policy.enabled = 1;
    (*out)->policy.editing_enabled = 1;
  }
  return s;
}
static void average_scalar(c_scalar *out, const c_scalar *a,
                           const c_scalar *b) {
  out->value = 0.5 * a->value + 0.5 * b->value;
  out->first = 0.5 * a->first + 0.5 * b->first;
  out->second = 0.5 * a->second + 0.5 * b->second;
}
c_status c_trainer_merge_candidate(const c_trainer *parent, unsigned first,
                                   unsigned second, c_trainer **out) {
  if (!parent || first >= second || second >= parent->model->groups ||
      parent->model->groups < 3 || parent->merge_count >= 2 ||
      parent->report.queued)
    return C_INVALID;
  if (parent->retired_count > C_MAX_RETIRED_TASKS ||
      parent->task_count > C_MAX_RETIRED_TASKS - parent->retired_count)
    return C_LIMIT;
  unsigned char after[C_WORLD_CELLS];
  unsigned contacts;
  c_life_evolve(parent->cells, after, &contacts);
  if (!(contacts & (1u << (first * C_MAX_GROUPS + second))))
    return C_DEFERRED;
  c_status s = fork_trainer(parent, out);
  if (s != C_OK)
    return s;
  c_trainer *t = *out;
  c_expert *a = &t->model->expert[first], *b = &t->model->expert[second];
  uint64_t uid = 0;
  for (unsigned g = 0; g < t->model->groups; g++)
    if (t->model->expert[g].uid > uid)
      uid = t->model->expert[g].uid;
  if (uid == UINT64_MAX) {
    c_trainer_destroy(t);
    *out = NULL;
    return C_LIMIT;
  }
  unsigned index = t->merge_count++;
  t->merge_generations[index] = parent->generation;
  memcpy(t->merge_worlds[index], parent->cells, C_WORLD_CELLS);
  for (size_t task_index = 0; task_index < parent->task_count; ++task_index) {
    c_retired_task *retired = &t->retired[t->retired_count++];
    retired->task = parent->tasks[task_index];
    retired->groups = parent->model->groups;
    retired->merge_index = index;
    for (unsigned group = 0; group < retired->groups; ++group)
      retired->owner_uids[group] = parent->model->expert[group].uid;
  }
  uint64_t retired = a->clock < b->clock ? a->clock : b->clock;
  if (retired > UINT64_MAX - t->retired_clock) {
    c_trainer_destroy(t);
    *out = NULL;
    return C_LIMIT;
  }
  t->retired_clock += retired;
  t->merge_parents[index][0] = a->uid;
  t->merge_parents[index][1] = b->uid;
  t->merge_children[index] = uid + 1;
  a->uid = uid + 1;
  if (b->clock > a->clock)
    a->clock = b->clock;
  for (size_t j = 0; j < C_FEATURES; j++)
    average_scalar(&a->centroid[j], &a->centroid[j], &b->centroid[j]);
  for (unsigned k = 0; k < C_CODE_ACTIONS; k++)
    for (size_t j = 0; j < C_FEATURES; j++)
      average_scalar(&a->code[k][j], &a->code[k][j], &b->code[k][j]);
  for (unsigned k = 0; k < C_TEXT_ACTIONS; k++)
    for (size_t j = 0; j < C_FEATURES; j++)
      average_scalar(&a->text[k][j], &a->text[k][j], &b->text[k][j]);
  for (unsigned g = second + 1; g < t->model->groups; g++)
    t->model->expert[g - 1] = t->model->expert[g];
  t->model->groups--;
  memset(&t->model->expert[t->model->groups], 0, sizeof(c_expert));
  /* The archive retains complete original bindings and owner UIDs. Pending work
   * is forbidden, so no live task is rewritten to another owner. */
  t->task_count = 0;
  memset(t->tasks, 0, sizeof(t->tasks));
  for (size_t i = 0; i < C_WORLD_CELLS; i++) {
    unsigned claims = 0;
    for (unsigned g = 0; g < parent->model->groups; g++)
      if (parent->cells[i] & (1u << g)) {
        unsigned mapped = g == second ? first : g > second ? g - 1 : g;
        claims |= 1u << mapped;
      }
    t->cells[i] = (unsigned char)claims;
  }
  refresh_report(t);
  return C_OK;
}
