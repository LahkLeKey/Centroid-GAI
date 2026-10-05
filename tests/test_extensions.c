#include "centroid_extensions.h"
#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "extension check failed at %s:%d: %s\n", __FILE__,       \
              __LINE__, #condition);                                           \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static uint64_t source(c_trainer *trainer, const char *path,
                       const unsigned char *bytes, size_t length) {
  uint64_t id = 0;
  CHECK(c_context_admit(trainer->context, C_SOURCE, C_TRAIN, path,
                        "native extension contract fixture", bytes, length,
                        &id) == C_OK);
  return id;
}

static double loss(const c_model *model, c_head head, const double *input,
                   unsigned target, const double *mass) {
  double probabilities[C_TEXT_ACTIONS];
  CHECK(c_model_predict(model, head, input, (1u << model->groups) - 1u, mass,
                        probabilities, C_TEXT_ACTIONS) == C_OK);
  CHECK(probabilities[target] > 0 && isfinite(probabilities[target]));
  return -log(probabilities[target]);
}

static void test_shared_math(void) {
  c_model *model = NULL, *before;
  c_gradient *gradients = calloc(C_MAX_GROUPS, sizeof(*gradients));
  const double mass[C_MAX_GROUPS] = {2.0, 0.7, 1.1, 0.0};
  double input[C_FEATURES], derivative[C_FEATURES], ignored_loss;
  CHECK(gradients && c_model_create(3u, 233u, &model) == C_OK);
  CHECK(c_encode((const unsigned char *)"owned representation derivative", 31u,
                 input) == C_OK);
  model->shared_enabled = 1u;
  for (unsigned f = 0; f < C_FEATURES; ++f)
    model->shared_scale[f].value = 0.7 + (double)f / 37.0;
  for (unsigned head = C_TEXT; head <= C_CODE; ++head) {
    const unsigned target = head == C_TEXT ? C_EOS : 2u;
    CHECK(c_model_shared_gradient(model, (c_head)head, input, 7u, mass, target,
                                  derivative) == C_OK);
    for (unsigned f = 0; f < C_FEATURES; ++f) {
      const double original = model->shared_scale[f].value;
      const double epsilon = 1e-6;
      model->shared_scale[f].value = original + epsilon;
      const double positive = loss(model, (c_head)head, input, target, mass);
      model->shared_scale[f].value = original - epsilon;
      const double negative = loss(model, (c_head)head, input, target, mass);
      model->shared_scale[f].value = original;
      CHECK(fabs((positive - negative) / (2.0 * epsilon) - derivative[f]) <
            3e-7 * (1.0 + fabs(derivative[f])));
    }
  }
  CHECK(c_model_gradient(model, C_TEXT, input, 7u, mass, C_EOS, gradients,
                         &ignored_loss) == C_OK);
  CHECK(c_model_shared_gradient(model, C_TEXT, input, 7u, mass, C_EOS,
                                derivative) == C_OK);
  before = malloc(sizeof(*before));
  CHECK(before != NULL);
  memcpy(before, model, sizeof(*before));
  derivative[17] = NAN;
  CHECK(c_model_apply_shared(model, C_TEXT, 7u, gradients, derivative, 0.03) ==
        C_INVALID);
  CHECK(!memcmp(before, model, sizeof(*model)));
  derivative[17] = 0.0;
  CHECK(c_model_apply_shared(model, C_TEXT, 3u, gradients, derivative, 0.03) ==
        C_INVALID);
  CHECK(!memcmp(before, model, sizeof(*model)));
  CHECK(c_model_apply_shared(model, C_TEXT, 7u, gradients, derivative, 0.03) ==
        C_OK);
  CHECK(model->shared_clock == 1u);
  for (unsigned g = 0; g < model->groups; ++g)
    CHECK(model->expert[g].clock == 1u &&
          !memcmp(model->expert[g].code, before->expert[g].code,
                  sizeof(model->expert[g].code)));
  free(before);
  free(gradients);
  c_model_destroy(model);
}

static void test_default_and_contacts(void) {
  static const unsigned char bytes[] = "int root(void){return 7;}\n";
  c_trainer *base = NULL, *shared = NULL, *policy = NULL;
  c_training_report report;
  CHECK(c_trainer_create(2u, 41u, &base) == C_OK);
  uint64_t id = source(base, "contacts.c", bytes, sizeof(bytes) - 1u);
  c_scalar frozen[C_FEATURES];
  memcpy(frozen, base->model->shared_scale, sizeof(frozen));
  CHECK(c_trainer_enqueue_source(base, id, 0u, 3u) == C_OK);
  CHECK(c_trainer_step(base, 1u, &report) == C_OK && report.updates == 1u);
  CHECK(!base->model->shared_enabled && !base->model->shared_clock &&
        !memcmp(frozen, base->model->shared_scale, sizeof(frozen)));
  CHECK(c_trainer_shared_candidate(base, &shared) == C_OK);
  CHECK(c_trainer_policy_candidate(shared, &policy) == C_OK);
  c_trainer_destroy(shared);
  CHECK(c_trainer_enqueue_source(policy, id, 1u, 3u) == C_OK);
  memset(policy->cells, 0, sizeof(policy->cells));
  c_model *before = malloc(sizeof(*before));
  CHECK(before != NULL);
  memcpy(before, policy->model, sizeof(*before));
  c_policy prior_policy = policy->policy;
  CHECK(c_trainer_step(policy, 1u, &report) == C_OK &&
        report.updates == base->report.updates);
  CHECK(!memcmp(before, policy->model, sizeof(*before)) &&
        policy->policy.clock == prior_policy.clock &&
        !memcmp(prior_policy.readout, policy->policy.readout,
                sizeof(prior_policy.readout)));
  c_life_seed(policy);
  CHECK(c_trainer_step(policy, 1u, &report) == C_OK && report.updates == 2u);
  CHECK(policy->model->shared_clock == 1u && policy->policy.clock == 1u);
  CHECK(!memcmp(base->model->expert[0].code, policy->model->expert[0].code,
                sizeof(base->model->expert[0].code)));
  CHECK(base->report.updates == 1u && base->model->shared_clock == 0u &&
        base->policy.clock == 0u);
  free(before);
  c_trainer_destroy(policy);
  c_trainer_destroy(base);
}

static void test_world_teacher_independence(void) {
  c_trainer *first = NULL, *second = NULL, *a = NULL, *b = NULL;
  c_training_report report;
  CHECK(c_trainer_create(4u, 41u, &first) == C_OK);
  CHECK(c_trainer_create(4u, 41u, &second) == C_OK);
  uint64_t first_id = source(first, "first.c", (const unsigned char *)"A", 1u);
  uint64_t second_id =
      source(second, "second.c", (const unsigned char *)"Z", 1u);
  CHECK(c_trainer_enqueue_source(first, first_id, 0u, 15u) == C_OK);
  CHECK(c_trainer_enqueue_source(second, second_id, 0u, 15u) == C_OK);
  CHECK(c_trainer_policy_candidate(first, &a) == C_OK);
  CHECK(c_trainer_policy_candidate(second, &b) == C_OK);
  CHECK(c_trainer_step(a, 256u, &report) == C_OK);
  CHECK(c_trainer_step(b, 256u, &report) == C_OK);
  CHECK(!memcmp(a->cells, b->cells, sizeof(a->cells)) &&
        !memcmp(&a->policy, &b->policy, sizeof(a->policy)) && a->rng == b->rng);
  CHECK(a->policy.clock > 0u && a->policy.interventions > 0u &&
        a->policy.contact_changes > 0u);
  c_trainer_destroy(first);
  c_trainer_destroy(second);
  c_trainer_destroy(a);
  c_trainer_destroy(b);
}

static void test_edit_ablation(void) {
  c_trainer *parent = NULL, *enabled = NULL, *disabled = NULL, *loaded = NULL;
  c_training_report report;
  CHECK(c_trainer_create(4u, 41u, &parent) == C_OK);
  CHECK(c_trainer_policy_candidate(parent, &enabled) == C_OK);
  CHECK(c_trainer_policy_candidate(parent, &disabled) == C_OK);
  disabled->policy.editing_enabled = 0u;
  /* The same frozen readout chooses renewal; only publishing edits differs. */
  enabled->policy.readout[3][0].value = 100.0;
  disabled->policy.readout[3][0].value = 100.0;
  CHECK(c_trainer_step(enabled, 1u, &report) == C_OK);
  CHECK(c_trainer_step(disabled, 1u, &report) == C_OK);
  CHECK(enabled->policy.clock == 1u && disabled->policy.clock == 1u &&
        !memcmp(enabled->policy.readout, disabled->policy.readout,
                sizeof(enabled->policy.readout)));
  CHECK(enabled->policy.interventions == 1u &&
        disabled->policy.interventions == 0u &&
        disabled->policy.contact_changes == 0u &&
        memcmp(enabled->cells, disabled->cells, sizeof(enabled->cells)) != 0);
  CHECK(c_trainer_save(disabled, "extension-edits-disabled.clife") == C_OK);
  CHECK(c_trainer_load("extension-edits-disabled.clife", &loaded) == C_OK);
  CHECK(loaded->policy.enabled && !loaded->policy.editing_enabled &&
        loaded->policy.clock == disabled->policy.clock);
  disabled->policy.editing_enabled = 2u;
  CHECK(c_trainer_save(disabled, "extension-edits-disabled.clife") ==
        C_CORRUPT);
  disabled->policy.editing_enabled = 0u;
  c_trainer_destroy(parent);
  c_trainer_destroy(enabled);
  c_trainer_destroy(disabled);
  c_trainer_destroy(loaded);
  CHECK(remove("extension-edits-disabled.clife") == 0);
}

static void compare_files(const char *first, const char *second) {
  unsigned char *a = NULL, *b = NULL;
  size_t na = 0, nb = 0;
  CHECK(c_read_file(first, &a, &na) == C_OK);
  CHECK(c_read_file(second, &b, &nb) == C_OK);
  CHECK(na == nb && !memcmp(a, b, na));
  free(a);
  free(b);
}

static void test_continuation(const char *program) {
  static const unsigned char bytes[] =
      "unsigned grid_value(unsigned x){return x^3;}\n";
  c_trainer *base = NULL, *shared = NULL, *continuous = NULL, *resumed = NULL;
  c_training_report report;
  CHECK(c_trainer_create(4u, 73u, &base) == C_OK);
  uint64_t id = source(base, "grid-value.c", bytes, sizeof(bytes) - 1u);
  CHECK(c_trainer_shared_candidate(base, &shared) == C_OK);
  CHECK(c_trainer_policy_candidate(shared, &continuous) == C_OK);
  for (unsigned i = 0; i < 12u; ++i)
    CHECK(c_trainer_enqueue_source(
              continuous, id, (sizeof(bytes) - 1u) * i / 11u, 15u) == C_OK);
  CHECK(c_trainer_step(continuous, 17u, &report) == C_OK);
  CHECK(c_trainer_save(continuous, "extension-split.clife") == C_OK);
  CHECK(c_trainer_load("extension-split.clife", &resumed) == C_OK);
  CHECK(c_trainer_step(continuous, 47u, &report) == C_OK);
  CHECK(c_trainer_step(resumed, 19u, &report) == C_OK);
  CHECK(c_trainer_step(resumed, 28u, &report) == C_OK);
  CHECK(continuous->model->shared_clock > 0u && continuous->policy.clock > 0u);
  CHECK(c_trainer_save(continuous, "extension-continuous.clife") == C_OK);
  CHECK(c_trainer_save(resumed, "extension-resumed.clife") == C_OK);
  compare_files("extension-continuous.clife", "extension-resumed.clife");
  const char *arguments[] = {
      program, "--resume", "extension-split.clife", "extension-process.clife",
      "47",    NULL};
  c_process_options options = {program, arguments, NULL, 30000u, 4096u};
  c_process_result result = {0};
  CHECK(c_process_run(&options, &result) == C_OK);
  if (result.exit_code)
    fwrite(result.output, 1u, result.length, stderr);
  CHECK(result.exit_code == 0 && !result.timed_out);
  c_process_dispose(&result);
  compare_files("extension-continuous.clife", "extension-process.clife");
  const uint64_t saved_clock = continuous->policy.clock;
  continuous->policy.clock = continuous->report.contacts + 1u;
  CHECK(c_trainer_save(continuous, "extension-continuous.clife") == C_CORRUPT);
  compare_files("extension-continuous.clife", "extension-resumed.clife");
  continuous->policy.clock = saved_clock;
  const double saved_scale = continuous->model->shared_scale[7].value;
  continuous->model->shared_scale[7].value = NAN;
  CHECK(c_trainer_save(continuous, "extension-continuous.clife") == C_CORRUPT);
  compare_files("extension-continuous.clife", "extension-resumed.clife");
  continuous->model->shared_scale[7].value = saved_scale;
  CHECK(remove("extension-split.clife") == 0);
  CHECK(remove("extension-continuous.clife") == 0);
  CHECK(remove("extension-resumed.clife") == 0);
  CHECK(remove("extension-process.clife") == 0);
  c_trainer_destroy(base);
  c_trainer_destroy(shared);
  c_trainer_destroy(continuous);
  c_trainer_destroy(resumed);
}

static void legacy_report(c_writer *writer, const c_training_report *report) {
  c_put_u64(writer, report->generation);
  c_put_u64(writer, report->updates);
  c_put_u64(writer, report->contacts);
  c_put_u64(writer, report->queued);
  c_put_u64(writer, report->completed);
  c_put_u64(writer, report->deferred);
  c_put_u64(writer, report->reseeds);
  for (unsigned g = 0; g < C_MAX_GROUPS; ++g)
    c_put_u64(writer, report->group_updates[g]);
  c_put_double(writer, report->last_loss);
}

static void write_schema_two_without_archive(const c_trainer *trainer,
                                             const char *path) {
  unsigned char *payload = NULL, *context = NULL;
  size_t payload_length = 0, context_length = 0;
  c_writer writer = {0};
  CHECK(trainer->task_count == 0u && trainer->retired_count > 0u);
  CHECK(c_trainer_save(trainer, "extension-archive-schema-three.clife") ==
        C_OK);
  CHECK(c_envelope_read("extension-archive-schema-three.clife", "CLIFE001",
                        &payload, &payload_length) == C_OK);
  c_reader reader = {payload, payload_length, 0u, C_OK};
  CHECK(c_get_u32(&reader) == 3u);
  for (unsigned i = 0; i < 2u; ++i) {
    const uint32_t length = c_get_u32(&reader);
    CHECK(reader.status == C_OK && length <= reader.length - reader.offset);
    reader.offset += length;
  }
  c_put_bytes(&writer, payload, reader.offset);
  writer.data[0] = 2u;
  c_model_write(&writer, trainer->model, 1);
  c_put_bytes(&writer, trainer->cells, sizeof(trainer->cells));
  c_put_u64(&writer, trainer->generation);
  c_put_u64(&writer, trainer->rng);
  c_put_u64(&writer, trainer->next_task);
  legacy_report(&writer, &trainer->report);
  c_put_u32(&writer, 0u);
  c_put_u32(&writer, (uint32_t)trainer->receipt_count);
  for (size_t i = 0; i < trainer->receipt_count; ++i) {
    c_put_bytes(&writer, trainer->receipts[i], C_DIGEST_HEX);
    c_put_bytes(&writer, trainer->receipt_bindings[i], C_DIGEST_HEX);
    c_put_u32(&writer, trainer->receipt_consumed[i]);
  }
  c_put_u32(&writer, trainer->policy.enabled);
  c_put_u64(&writer, trainer->policy.clock);
  c_put_u64(&writer, trainer->policy.interventions);
  c_put_u64(&writer, trainer->policy.contact_changes);
  for (unsigned a = 0; a < 4u; ++a)
    for (unsigned f = 0; f < C_FEATURES; ++f) {
      const c_scalar *scalar = &trainer->policy.readout[a][f];
      c_put_double(&writer, scalar->value);
      c_put_double(&writer, scalar->first);
      c_put_double(&writer, scalar->second);
    }
  c_put_u32(&writer, trainer->merge_count);
  c_put_u64(&writer, trainer->retired_clock);
  for (unsigned event = 0; event < 2u; ++event) {
    c_put_u64(&writer, trainer->merge_parents[event][0]);
    c_put_u64(&writer, trainer->merge_parents[event][1]);
    c_put_u64(&writer, trainer->merge_children[event]);
    c_put_u64(&writer, trainer->merge_generations[event]);
    c_put_bytes(&writer, trainer->merge_worlds[event], C_WORLD_CELLS);
  }
  CHECK(c_context_pack(trainer->context, &context, &context_length) == C_OK);
  c_put_u64(&writer, context_length);
  c_put_bytes(&writer, context, context_length);
  CHECK(writer.status == C_OK);
  /* A newly hashed, valid historical envelope omits archives by definition. */
  CHECK(c_envelope_write(path, "CLIFE001", writer.data, writer.length) == C_OK);
  free(writer.data);
  free(payload);
  free(context);
  CHECK(remove("extension-archive-schema-three.clife") == 0);
}

static void test_schema_one_compatibility(void) {
  c_trainer *original = NULL, *loaded = NULL;
  unsigned char *payload = NULL, *context = NULL;
  size_t payload_length = 0, context_length = 0;
  c_writer writer = {0};
  CHECK(c_trainer_create(3u, 19u, &original) == C_OK);
  source(original, "schema-one.c", (const unsigned char *)"int old(void);",
         14u);
  CHECK(c_trainer_save(original, "extension-schema-two.clife") == C_OK);
  CHECK(c_envelope_read("extension-schema-two.clife", "CLIFE001", &payload,
                        &payload_length) == C_OK);
  c_reader reader = {payload, payload_length, 0u, C_OK};
  CHECK(c_get_u32(&reader) == 3u);
  for (unsigned i = 0; i < 2u; ++i) {
    const uint32_t length = c_get_u32(&reader);
    CHECK(reader.status == C_OK && length <= reader.length - reader.offset);
    reader.offset += length;
  }
  /* Build/recipe bytes are taken from this build's current envelope. The rest
   * is independently authored canonical schema1, with no extension fields. */
  c_put_bytes(&writer, payload, reader.offset);
  writer.data[0] = 1u;
  c_model_write(&writer, original->model, 0);
  c_put_bytes(&writer, original->cells, sizeof(original->cells));
  c_put_u64(&writer, original->generation);
  c_put_u64(&writer, original->rng);
  c_put_u64(&writer, original->next_task);
  legacy_report(&writer, &original->report);
  c_put_u32(&writer, 0u); /* No tasks. */
  c_put_u32(&writer, 0u); /* No receipts. */
  CHECK(c_context_pack(original->context, &context, &context_length) == C_OK);
  c_put_u64(&writer, context_length);
  c_put_bytes(&writer, context, context_length);
  CHECK(writer.status == C_OK);
  CHECK(c_envelope_write("extension-schema-one.clife", "CLIFE001", writer.data,
                         writer.length) == C_OK);
  CHECK(c_trainer_load("extension-schema-one.clife", &loaded) == C_OK);
  CHECK(!loaded->model->shared_enabled && !loaded->model->shared_clock &&
        !loaded->policy.enabled && !loaded->merge_count);
  CHECK(c_trainer_save(loaded, "extension-converted.clife") == C_OK);
  compare_files("extension-schema-two.clife", "extension-converted.clife");
  free(writer.data);
  memset(&writer, 0, sizeof(writer));
  c_trainer_destroy(loaded);
  loaded = NULL;
  original->policy.enabled = 1u;
  original->policy.editing_enabled = 1u;
  CHECK(c_trainer_save(original, "extension-schema-two.clife") == C_OK);
  c_put_bytes(&writer, payload, reader.offset);
  writer.data[0] = 2u;
  c_model_write(&writer, original->model, 1);
  c_put_bytes(&writer, original->cells, sizeof(original->cells));
  c_put_u64(&writer, original->generation);
  c_put_u64(&writer, original->rng);
  c_put_u64(&writer, original->next_task);
  legacy_report(&writer, &original->report);
  c_put_u32(&writer, 0u);
  c_put_u32(&writer, 0u);
  c_put_u32(&writer, original->policy.enabled);
  c_put_u64(&writer, original->policy.clock);
  c_put_u64(&writer, original->policy.interventions);
  c_put_u64(&writer, original->policy.contact_changes);
  for (unsigned a = 0; a < 4u; ++a)
    for (unsigned f = 0; f < C_FEATURES; ++f) {
      const c_scalar *scalar = &original->policy.readout[a][f];
      c_put_double(&writer, scalar->value);
      c_put_double(&writer, scalar->first);
      c_put_double(&writer, scalar->second);
    }
  c_put_u32(&writer, 0u);
  c_put_u64(&writer, 0u);
  for (unsigned event = 0; event < 2u; ++event) {
    for (unsigned field = 0; field < 4u; ++field)
      c_put_u64(&writer, 0u);
    c_put_bytes(&writer, original->merge_worlds[event], C_WORLD_CELLS);
  }
  c_put_u64(&writer, context_length);
  c_put_bytes(&writer, context, context_length);
  CHECK(writer.status == C_OK);
  CHECK(c_envelope_write("extension-schema-legacy-two.clife", "CLIFE001",
                         writer.data, writer.length) == C_OK);
  CHECK(c_trainer_load("extension-schema-legacy-two.clife", &loaded) == C_OK);
  CHECK(loaded->policy.enabled && loaded->policy.editing_enabled &&
        loaded->retired_count == 0u);
  CHECK(c_trainer_save(loaded, "extension-converted.clife") == C_OK);
  compare_files("extension-schema-two.clife", "extension-converted.clife");
  free(writer.data);
  free(payload);
  free(context);
  c_trainer_destroy(original);
  c_trainer_destroy(loaded);
  CHECK(remove("extension-schema-two.clife") == 0);
  CHECK(remove("extension-schema-one.clife") == 0);
  CHECK(remove("extension-schema-legacy-two.clife") == 0);
  CHECK(remove("extension-converted.clife") == 0);
}

static void test_merge_lineage(void) {
  c_trainer *base = NULL, *merged = NULL, *loaded = NULL;
  c_training_report report;
  const unsigned char bytes[] = "int mergeable(unsigned x){return x;}\n";
  char parent[C_DIGEST_HEX], evaluator[C_DIGEST_HEX], receipt[C_DIGEST_HEX];
  CHECK(c_trainer_create(4u, 41u, &base) == C_OK);
  uint64_t id = source(base, "mergeable.c", bytes, sizeof(bytes) - 1u);
  c_hash("parent", 6u, parent);
  c_hash("independent native evaluator", 28u, evaluator);
  c_hash("unique measured receipt", 23u, receipt);
  CHECK(c_trainer_enqueue_measurement(base, bytes, sizeof(bytes) - 1u, 2u, 15u,
                                      parent, evaluator, receipt) == C_OK);
  CHECK(c_trainer_enqueue_source(base, id, 0u, 3u) == C_OK);
  uint64_t request_id;
  static const unsigned char request[] = "quote mergeable";
  CHECK(c_context_admit(base->context, C_ACTIVITY, C_TRAIN, "merge-request.txt",
                        "original retired typed request", request,
                        sizeof(request) - 1u, &request_id) == C_OK);
  CHECK(c_trainer_enqueue_text(base, request_id, NULL, 0u, id, 1u, 15u) ==
        C_OK);
  CHECK(c_trainer_merge_candidate(base, 0u, 1u, &merged) == C_INVALID &&
        !merged);
  CHECK(c_trainer_step(base, 128u, &report) == C_OK && !report.queued);
  CHECK(base->receipt_count == 1u && base->receipt_consumed[0] == 1u);
  CHECK(base->task_count == 3u && report.updates == 3u);
  c_task originals[3];
  memcpy(originals, base->tasks, sizeof(originals));
  unsigned char contact_world[C_WORLD_CELLS];
  memcpy(contact_world, base->cells, sizeof(contact_world));
  memset(base->cells, 0, sizeof(base->cells));
  CHECK(c_trainer_merge_candidate(base, 0u, 1u, &merged) == C_DEFERRED &&
        !merged);
  memcpy(base->cells, contact_world, sizeof(contact_world));
  c_model *before = malloc(sizeof(*before));
  CHECK(before != NULL);
  memcpy(before, base->model, sizeof(*before));
  CHECK(c_trainer_merge_candidate(base, 0u, 1u, &merged) == C_OK);
  CHECK(!memcmp(before, base->model, sizeof(*before)) &&
        merged->model->groups == 3u && merged->merge_count == 1u &&
        merged->merge_parents[0][0] == 1u &&
        merged->merge_parents[0][1] == 2u && merged->merge_children[0] == 5u &&
        merged->model->expert[0].uid == 5u);
  CHECK(merged->retired_clock == 3u && merged->model->expert[0].clock == 3u &&
        merged->receipt_count == 1u && merged->receipt_consumed[0] == 1u &&
        !strcmp(merged->receipts[0], receipt) && merged->task_count == 0u);
  CHECK(c_trainer_retired_count(merged) == 3u &&
        c_trainer_retention_count(merged) == 3u);
  for (size_t i = 0; i < 3u; ++i) {
    CHECK(!memcmp(&merged->retired[i].task, &originals[i], sizeof(c_task)) &&
          merged->retired[i].groups == 4u &&
          merged->retired[i].merge_index == 0u);
    for (unsigned group = 0; group < C_MAX_GROUPS; ++group)
      CHECK(merged->retired[i].owner_uids[group] == (uint64_t)group + 1u);
  }
  unsigned scope = 99u;
  CHECK(c_trainer_task_scope(base, merged, &base->tasks[1], &scope) == C_OK &&
        scope == 1u);
  CHECK(c_trainer_task_scope(merged, merged, &merged->retired[1].task,
                             &scope) == C_OK &&
        scope == 1u);
  const c_task *retained = NULL;
  CHECK(c_trainer_retention_task(merged, 1u, &retained) == C_OK &&
        retained == &merged->retired[1].task);
  CHECK(!memcmp(&merged->model->expert[1], &base->model->expert[2],
                sizeof(c_expert)));
  for (unsigned f = 0; f < C_FEATURES; ++f) {
    const c_scalar *a = &base->model->expert[0].centroid[f];
    const c_scalar *b = &base->model->expert[1].centroid[f];
    const c_scalar *c = &merged->model->expert[0].centroid[f];
    CHECK(c->value == 0.5 * a->value + 0.5 * b->value &&
          c->first == 0.5 * a->first + 0.5 * b->first &&
          c->second == 0.5 * a->second + 0.5 * b->second);
  }
  CHECK(c_trainer_save(merged, "extension-merged.clife") == C_OK);
  CHECK(c_trainer_load("extension-merged.clife", &loaded) == C_OK);
  CHECK(c_trainer_save(loaded, "extension-merge-copy.clife") == C_OK);
  compare_files("extension-merged.clife", "extension-merge-copy.clife");
  merged->merge_children[0] = 999u;
  CHECK(c_trainer_save(merged, "extension-merged.clife") == C_CORRUPT);
  compare_files("extension-merged.clife", "extension-merge-copy.clife");
  merged->merge_children[0] = 5u;
  merged->merge_children[1] = 77u;
  CHECK(c_trainer_save(merged, "extension-merged.clife") == C_CORRUPT);
  compare_files("extension-merged.clife", "extension-merge-copy.clife");
  merged->merge_children[1] = 0u;
  unsigned char merge_world[C_WORLD_CELLS];
  memcpy(merge_world, merged->merge_worlds[0], sizeof(merge_world));
  memset(merged->merge_worlds[0], 0, sizeof(merge_world));
  CHECK(c_trainer_save(merged, "extension-merged.clife") == C_CORRUPT);
  compare_files("extension-merged.clife", "extension-merge-copy.clife");
  memcpy(merged->merge_worlds[0], merge_world, sizeof(merge_world));
  merged->retired[1].task.target ^= 1u;
  CHECK(c_trainer_save(merged, "extension-merged.clife") == C_CORRUPT);
  compare_files("extension-merged.clife", "extension-merge-copy.clife");
  merged->retired[1].task.target ^= 1u;
  merged->retired[2].owner_uids[0] = 99u;
  CHECK(c_trainer_save(merged, "extension-merged.clife") == C_CORRUPT);
  compare_files("extension-merged.clife", "extension-merge-copy.clife");
  merged->retired[2].owner_uids[0] = 1u;
  {
    unsigned char *payload = NULL;
    size_t length = 0u, last = 0u, matches = 0u;
    CHECK(c_envelope_read("extension-merged.clife", "CLIFE001", &payload,
                          &length) == C_OK);
    for (size_t offset = 0; offset + C_DIGEST_HEX <= length; ++offset)
      if (!memcmp(payload + offset, receipt, C_DIGEST_HEX)) {
        last = offset;
        ++matches;
      }
    CHECK(matches == 2u); /* Ledger receipt plus original archived CODE task. */
    payload[last] = payload[last] == 'a' ? 'b' : 'a';
    CHECK(c_envelope_write("extension-archive-bad.clife", "CLIFE001", payload,
                           length) == C_OK);
    c_trainer *incumbent = loaded;
    CHECK(c_trainer_load("extension-archive-bad.clife", &loaded) == C_CORRUPT &&
          loaded == incumbent);
    free(payload);
    CHECK(remove("extension-archive-bad.clife") == 0);
  }
  CHECK(c_trainer_enqueue_measurement(merged, bytes, sizeof(bytes) - 1u, 2u, 7u,
                                      parent, evaluator, receipt) == C_INVALID);
  size_t requeued = 777u;
  CHECK(c_trainer_replay_retired(merged, 0u, 3u, 1u, &requeued) == C_INVALID &&
        requeued == 777u && merged->task_count == 0u);
  CHECK(c_trainer_replay_retired(merged, 0u, C_MAX_TASKS + 1u, 7u, &requeued) ==
            C_LIMIT &&
        requeued == 777u && merged->task_count == 0u);
  CHECK(c_trainer_replay_retired(merged, 0u, 3u, 7u, &requeued) == C_OK &&
        requeued == 2u && merged->task_count == 2u &&
        merged->tasks[0].id > originals[2].id &&
        merged->tasks[0].eligible == 7u);
  CHECK(merged->receipt_consumed[0] == 1u);
  for (size_t i = 0; i < 3u; ++i)
    CHECK(!memcmp(&merged->retired[i].task, &originals[i], sizeof(c_task)));
  CHECK(c_trainer_replay_retired(merged, 0u, 3u, 7u, &requeued) == C_OK &&
        requeued == 0u && merged->task_count == 2u);
  c_trainer *second = NULL;
  CHECK(c_trainer_merge_candidate(merged, 0u, 1u, &second) == C_INVALID &&
        !second);
  CHECK(c_trainer_save(merged, "extension-replay.clife") == C_OK);
  c_trainer_destroy(loaded);
  loaded = NULL;
  CHECK(c_trainer_load("extension-replay.clife", &loaded) == C_OK);
  CHECK(c_trainer_step(merged, 128u, &report) == C_OK && !report.queued);
  CHECK(c_trainer_step(loaded, 31u, &report) == C_OK);
  CHECK(c_trainer_step(loaded, 97u, &report) == C_OK && !report.queued);
  CHECK(c_trainer_save(merged, "extension-replay-continuous.clife") == C_OK);
  CHECK(c_trainer_save(loaded, "extension-replay-resumed.clife") == C_OK);
  compare_files("extension-replay-continuous.clife",
                "extension-replay-resumed.clife");
  CHECK(c_trainer_merge_candidate(merged, 0u, 1u, &second) == C_OK);
  CHECK(second->model->groups == 2u && second->retired_count == 5u &&
        second->retired[3].groups == 3u &&
        second->retired[3].merge_index == 1u &&
        second->retired[3].owner_uids[0] == 5u &&
        second->retired[3].owner_uids[1] == 3u &&
        second->retired[3].owner_uids[2] == 4u &&
        second->retired[3].owner_uids[3] == 0u);
  CHECK(c_trainer_task_scope(second, second, &second->retired[1].task,
                             &scope) == C_OK &&
        scope == 1u);
  CHECK(c_trainer_save(second, "extension-two-merges.clife") == C_OK);
  c_trainer_destroy(second);
  free(before);
  c_trainer_destroy(base);
  c_trainer_destroy(merged);
  c_trainer_destroy(loaded);
  CHECK(remove("extension-merged.clife") == 0);
  CHECK(remove("extension-merge-copy.clife") == 0);
  CHECK(remove("extension-replay.clife") == 0);
  CHECK(remove("extension-replay-continuous.clife") == 0);
  CHECK(remove("extension-replay-resumed.clife") == 0);
  CHECK(remove("extension-two-merges.clife") == 0);
}

static void gate_fixture(c_trainer **parent, c_source_probe probes[24]) {
  static const unsigned char train[] = "abcdefghijk";
  static const unsigned char development[] = "mnopqrstuvw";
  static const unsigned char audit[] = "xyz01234567";
  const unsigned char *bytes[] = {train, development, audit};
  const c_record_kind kinds[] = {C_SOURCE, C_DEVELOPMENT, C_AUDIT};
  const c_split splits[] = {C_TRAIN, C_DEV, C_HOLDOUT};
  const char *paths[] = {"gate/train.c", "gate/dev.c", "gate/audit.c"};
  CHECK(c_trainer_create(4u, 81u, parent) == C_OK);
  for (unsigned split = 0; split < 3u; ++split) {
    uint64_t id;
    CHECK(c_context_admit((*parent)->context, kinds[split], splits[split],
                          paths[split], "frozen independent gate", bytes[split],
                          11u, &id) == C_OK);
    for (unsigned offset = 0; offset < 8u; ++offset) {
      probes[split * 8u + offset].record_id = id;
      probes[split * 8u + offset].offset = offset;
    }
  }
}

static void test_gates(void) {
  c_trainer *parent = NULL, *candidate = NULL, *previous = NULL;
  c_source_probe probes[24];
  c_extension_report report, before;
  gate_fixture(&parent, probes);
  CHECK(c_trainer_policy_candidate(parent, &candidate) == C_OK);
  memset(&report, 0xa5, sizeof(report));
  memcpy(&before, &report, sizeof(before));
  CHECK(c_extension_evaluate(parent, candidate, probes, 23u, 0.0, 0, &report) ==
            C_INVALID &&
        !memcmp(&before, &report, sizeof(report)));
  c_source_probe repeated[24];
  memcpy(repeated, probes, sizeof(repeated));
  repeated[1] = repeated[0];
  CHECK(c_extension_evaluate(parent, candidate, repeated, 24u, 0.0, 0,
                             &report) == C_INVALID);
  CHECK(c_extension_evaluate(parent, candidate, probes, 24u, 0.0, 1, &report) ==
            C_OK &&
        !report.accepted);
  CHECK(c_extension_evaluate(parent, candidate, probes, 24u, 0.0, 0, &report) ==
            C_OK &&
        report.accepted && report.cases[0] == 8u && report.cases[1] == 8u &&
        report.cases[2] == 8u);
  {
    /* A separate model cannot launder the parent's known DEV or AUDIT bytes by
     * declaring the same IDs/digests to be TRAIN SOURCE. */
    static const unsigned char relabeled[][12] = {"abcdefghijk", "mnopqrstuvw",
                                                  "xyz01234567"};
    const char *paths[] = {"gate/train.c", "gate/dev.c", "gate/audit.c"};
    c_trainer *violator = NULL;
    CHECK(c_trainer_create(4u, 81u, &violator) == C_OK);
    for (unsigned i = 0; i < 3u; ++i)
      CHECK(c_context_admit(violator->context, C_SOURCE, C_TRAIN, paths[i],
                            "frozen independent gate", relabeled[i], 11u,
                            NULL) == C_OK);
    const c_status status =
        c_extension_evaluate(parent, violator, probes, 24u, 0.0, 0, &report);
    CHECK(status == C_CORRUPT || status == C_INVALID);
    c_trainer_destroy(violator);
  }
  c_trainer *original = parent, *accepted = candidate;
  CHECK(c_trainer_promote(&parent, &candidate, probes, 24u, 0.0, 0, &report,
                          &previous) == C_OK);
  CHECK(parent == accepted && !candidate && previous == original);
  c_trainer_destroy(previous);
  previous = NULL;
  CHECK(c_trainer_shared_candidate(parent, &candidate) == C_OK);
  /* Deliberately degraded isolated candidate: zero is absent from all teachers.
   */
  for (unsigned g = 0; g < candidate->model->groups; ++g) {
    for (unsigned a = 0; a < C_TEXT_ACTIONS; ++a)
      for (unsigned f = 0; f < C_FEATURES; ++f)
        candidate->model->expert[g].text[a][f].value = 0.0;
    candidate->model->expert[g].text[0][0].value = 100.0;
  }
  original = parent;
  accepted = candidate;
  char fingerprint[C_DIGEST_HEX], after[C_DIGEST_HEX];
  CHECK(c_model_fingerprint(parent->model, 0u, fingerprint) == C_OK);
  CHECK(c_trainer_promote(&parent, &candidate, probes, 24u, 0.02, 0, &report,
                          &previous) == C_DEFERRED);
  CHECK(parent == original && candidate == accepted && !previous &&
        !report.accepted);
  CHECK(c_model_fingerprint(parent->model, 0u, after) == C_OK &&
        !strcmp(fingerprint, after));
  uint64_t proposal;
  static const unsigned char hypothesis[] = "proposal cannot supply labels";
  CHECK(c_context_admit(parent->context, C_LLM_PROPOSAL, C_TRAIN,
                        "gate/llm.txt", "unverified", hypothesis,
                        sizeof(hypothesis) - 1u, &proposal) == C_OK);
  CHECK(c_context_admit(candidate->context, C_LLM_PROPOSAL, C_TRAIN,
                        "gate/llm.txt", "unverified", hypothesis,
                        sizeof(hypothesis) - 1u, NULL) == C_OK);
  memcpy(repeated, probes, sizeof(repeated));
  repeated[0].record_id = proposal;
  CHECK(c_extension_evaluate(parent, candidate, repeated, 24u, 0.02, 0,
                             &report) == C_INVALID);
  c_trainer_destroy(candidate);
  c_trainer_destroy(parent);
}

static void test_scoped_retention(void) {
  static const unsigned char input[] =
      "stable independently measured code task";
  c_trainer *parent = NULL, *candidate = NULL;
  c_source_probe probes[24];
  c_training_report training;
  c_extension_report report;
  char parent_digest[C_DIGEST_HEX], evaluator[C_DIGEST_HEX],
      receipt[C_DIGEST_HEX];
  gate_fixture(&parent, probes);
  c_hash("fixed code catalog", 18u, parent_digest);
  c_hash("independent code oracle", 23u, evaluator);
  c_hash("scoped retention receipt", 24u, receipt);
  CHECK(c_trainer_enqueue_measurement(parent, input, sizeof(input) - 1u, 0u, 3u,
                                      parent_digest, evaluator,
                                      receipt) == C_OK);
  CHECK(c_trainer_step(parent, 128u, &training) == C_OK &&
        training.updates == 1u && !training.queued);
  /* Original task owners predict action0. Unrelated experts predict action1
   * and dominate all-owner inference. A gate that discards the frozen task's
   * eligibility cannot see the subsequent action0 -> action2 regression. */
  for (unsigned g = 0; g < parent->model->groups; ++g) {
    for (unsigned f = 0; f < C_FEATURES; ++f)
      parent->model->expert[g].centroid[f].value = g < 2u ? 100.0 : 0.0;
    for (unsigned a = 0; a < C_CODE_ACTIONS; ++a)
      for (unsigned f = 0; f < C_FEATURES; ++f)
        parent->model->expert[g].code[a][f].value = 0.0;
    parent->model->expert[g].code[g < 2u ? 0u : 1u][0].value = 100.0;
  }
  CHECK(c_trainer_policy_candidate(parent, &candidate) == C_OK);
  for (unsigned g = 0; g < 2u; ++g) {
    candidate->model->expert[g].code[0][0].value = 0.0;
    candidate->model->expert[g].code[2][0].value = 100.0;
  }
  CHECK(c_extension_evaluate(parent, candidate, probes, 24u, 0.0, 0, &report) ==
        C_OK);
  CHECK(report.retained_tasks == 1u && !report.accepted &&
        report.retention_candidate > report.retention_parent);
  c_trainer_destroy(candidate);
  c_trainer_destroy(parent);
}

static void test_archive_prefix(void) {
  c_trainer *base = NULL, *parent = NULL, *candidate = NULL, *legacy = NULL,
            *grown = NULL, *previous = NULL;
  c_source_probe probes[24];
  c_extension_report report, unchanged;
  c_training_report training;
  char parent_digest[C_DIGEST_HEX], evaluator[C_DIGEST_HEX],
      receipt[C_DIGEST_HEX];
  static const unsigned char measured[] = "archived original CODE input";
  gate_fixture(&base, probes);
  c_hash("archive catalog", 15u, parent_digest);
  c_hash("archive oracle", 14u, evaluator);
  c_hash("archive consumed receipt", 24u, receipt);
  CHECK(c_trainer_enqueue_measurement(base, measured, sizeof(measured) - 1u, 2u,
                                      15u, parent_digest, evaluator,
                                      receipt) == C_OK);
  CHECK(c_trainer_enqueue_source(base, probes[0].record_id, 0u, 3u) == C_OK);
  CHECK(c_trainer_enqueue_source(base, probes[0].record_id, 1u, 3u) == C_OK);
  CHECK(c_trainer_step(base, 128u, &training) == C_OK &&
        training.updates == 3u && !training.queued);
  c_life_seed(base);
  CHECK(c_trainer_merge_candidate(base, 0u, 1u, &parent) == C_OK);
  CHECK(parent->retired_count == 3u && parent->receipt_consumed[0] == 1u);
  CHECK(c_trainer_policy_candidate(parent, &candidate) == C_OK);
  CHECK(c_extension_evaluate(parent, candidate, probes, 24u, 0.0, 0, &report) ==
            C_OK &&
        report.accepted && report.retained_tasks == 3u);
  /* This altered eligibility still passes source provenance validation. The
   * promotion boundary must preserve the original task field itself. */
  candidate->retired[1].task.eligible = 15u;
  CHECK(c_trainer_validate(candidate) == C_OK);
  memset(&report, 0xa5, sizeof(report));
  memcpy(&unchanged, &report, sizeof(report));
  CHECK(c_extension_evaluate(parent, candidate, probes, 24u, 0.0, 0, &report) ==
            C_CORRUPT &&
        !memcmp(&report, &unchanged, sizeof(report)));
  candidate->retired[1].task.eligible = 3u;
  c_retired_task swap = candidate->retired[1];
  candidate->retired[1] = candidate->retired[2];
  candidate->retired[2] = swap;
  CHECK(c_trainer_validate(candidate) == C_OK);
  CHECK(c_extension_evaluate(parent, candidate, probes, 24u, 0.0, 0, &report) ==
        C_CORRUPT);
  swap = candidate->retired[1];
  candidate->retired[1] = candidate->retired[2];
  candidate->retired[2] = swap;
  write_schema_two_without_archive(parent, "extension-archive-legacy.clife");
  CHECK(c_trainer_load("extension-archive-legacy.clife", &legacy) == C_OK &&
        legacy->retired_count == 0u && legacy->receipt_count == 1u &&
        legacy->receipt_consumed[0] == 1u);
  c_trainer *incumbent = parent, *omitted = legacy;
  CHECK(c_trainer_promote(&parent, &legacy, probes, 24u, 0.0, 0, &report,
                          &previous) == C_CORRUPT &&
        parent == incumbent && legacy == omitted && !previous &&
        !memcmp(&report, &unchanged, sizeof(report)));
  /* A second real merge legitimately appends newly completed supervision. */
  CHECK(c_trainer_enqueue_source(candidate, probes[0].record_id, 2u, 7u) ==
        C_OK);
  CHECK(c_trainer_step(candidate, 128u, &training) == C_OK && !training.queued);
  c_life_seed(candidate);
  CHECK(c_trainer_merge_candidate(candidate, 0u, 1u, &grown) == C_OK &&
        grown->retired_count == 4u);
  CHECK(c_extension_evaluate(parent, grown, probes, 24u, 0.1, 0, &report) ==
            C_OK &&
        report.retained_tasks == 3u);
  c_trainer_destroy(base);
  c_trainer_destroy(parent);
  c_trainer_destroy(candidate);
  c_trainer_destroy(legacy);
  c_trainer_destroy(grown);
  CHECK(remove("extension-archive-legacy.clife") == 0);
}

static void test_existing_artifacts(void) {
  static const unsigned char marker[] = {0,   255, 'i', 'm', 'm', 'u',
                                         't', 'a', 'b', 'l', 'e'};
  const char *path = "test-extension-immutable.marker";
  unsigned char *loaded = NULL;
  size_t length = 0;
  CHECK(c_write_atomic(path, marker, sizeof(marker)) == C_OK);
  CHECK(c_extensions_run(".") == C_INVALID);
  CHECK(c_read_file(path, &loaded, &length) == C_OK);
  CHECK(length == sizeof(marker) && !memcmp(loaded, marker, length));
  free(loaded);
  CHECK(remove(path) == 0);
}

int main(int argc, char **argv) {
  if (argc == 5 && !strcmp(argv[1], "--resume")) {
    c_trainer *trainer = NULL;
    c_training_report report;
    CHECK(c_trainer_load(argv[2], &trainer) == C_OK);
    CHECK(c_trainer_step(trainer, (size_t)strtoul(argv[4], NULL, 10),
                         &report) == C_OK);
    CHECK(c_trainer_save(trainer, argv[3]) == C_OK);
    c_trainer_destroy(trainer);
    return 0;
  }
  CHECK(argc == 1);
  test_shared_math();
  test_default_and_contacts();
  test_world_teacher_independence();
  test_edit_ablation();
  test_continuation(argv[0]);
  test_schema_one_compatibility();
  test_merge_lineage();
  test_gates();
  test_scoped_retention();
  test_existing_artifacts();
  test_archive_prefix();
  puts("shared derivatives/contact ownership, independent cellular teacher, "
       "extension restart, legacy schema, merge lineage and promotion gates "
       "passed");
  return 0;
}
