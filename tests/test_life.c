#include "centroid_extensions.h"
#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "life line %d: %s\n", __LINE__, #x);                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static size_t at(unsigned x, unsigned y) { return y * C_WORLD_SIDE + x; }
static void boundary_counters(c_trainer *t, uint64_t updates) {
  t->generation = updates;
  t->report.generation = updates;
  t->report.updates = updates;
  t->report.completed = updates;
  t->report.contacts = updates;
  t->report.deferred = 0;
  t->report.reseeds = updates / 8u;
  for (unsigned g = 0; g < t->model->groups; ++g) {
    t->model->expert[g].clock = updates;
    t->report.group_updates[g] = updates;
  }
}
static int refuse_boundary_generation(c_trainer *t) {
  c_trainer *before = malloc(sizeof(*before));
  c_model *model = malloc(sizeof(*model));
  c_training_report result;
  unsigned char *saved = NULL, *resaved = NULL;
  size_t length = 0, relength = 0;
  CHECK(before && model);
  CHECK(c_trainer_validate(t) == C_OK);
  CHECK(c_trainer_save(t, "life-boundary-before.clife") == C_OK);
  memcpy(before, t, sizeof(*before));
  memcpy(model, t->model, sizeof(*model));
  CHECK(c_trainer_step(t, 1u, &result) == C_LIMIT);
  CHECK(!memcmp(before, t, sizeof(*before)) &&
        !memcmp(model, t->model, sizeof(*model)) &&
        !memcmp(&result, &t->report, sizeof(result)));
  CHECK(c_trainer_validate(t) == C_OK);
  CHECK(c_trainer_save(t, "life-boundary-after.clife") == C_OK);
  CHECK(c_read_file("life-boundary-before.clife", &saved, &length) == C_OK);
  CHECK(c_read_file("life-boundary-after.clife", &resaved, &relength) == C_OK);
  CHECK(length == relength && !memcmp(saved, resaved, length));
  free(saved);
  free(resaved);
  free(before);
  free(model);
  CHECK(remove("life-boundary-before.clife") == 0);
  CHECK(remove("life-boundary-after.clife") == 0);
  return 0;
}
static int test_generation_boundaries(void) {
  static const unsigned char source[] = {'a', 'b'};
  static const unsigned char measured[] = "independently measured TRAIN input";
  c_trainer *t = NULL, *candidate = NULL;
  uint64_t id;
  char parent[C_DIGEST_HEX], evaluator[C_DIGEST_HEX], receipt[C_DIGEST_HEX];
  c_training_report report;
  c_hash("fixed parent catalog", 20u, parent);
  c_hash("fixed independent evaluator", 27u, evaluator);
  c_hash("fixed complete measured receipt", 31u, receipt);

  CHECK(c_trainer_create(4u, 123u, &t) == C_OK);
  CHECK(c_context_admit(t->context, C_SOURCE, C_TRAIN, "boundary.c", "test",
                        source, sizeof(source), &id) == C_OK);
  CHECK(c_trainer_enqueue_source(t, id, 0u, 15u) == C_OK);
  CHECK(c_trainer_enqueue_measurement(t, measured, sizeof(measured) - 1u, 2u,
                                      3u, parent, evaluator, receipt) == C_OK);
  boundary_counters(t, UINT64_MAX / 4u);
  CHECK(c_trainer_policy_candidate(t, &candidate) == C_OK);
  c_trainer_destroy(t);
  t = candidate;
  candidate = NULL;
  /* Every individual clock fits, but four increments exceed the complete
   * owned-clock audit. Policy preparation and a due reseed must also roll back.
   */
  CHECK(refuse_boundary_generation(t) == 0);
  c_trainer_destroy(t);
  t = NULL;

  CHECK(c_trainer_create(4u, 123u, &t) == C_OK);
  CHECK(c_context_admit(t->context, C_SOURCE, C_TRAIN, "boundary.c", "test",
                        source, sizeof(source), &id) == C_OK);
  CHECK(c_trainer_enqueue_source(t, id, 0u, 3u) == C_OK);
  CHECK(c_trainer_enqueue_measurement(t, measured, sizeof(measured) - 1u, 2u,
                                      3u, parent, evaluator, receipt) == C_OK);
  const uint64_t four = UINT64_MAX / 4u;
  boundary_counters(t, four);
  c_expert *unrelated = malloc(2u * sizeof(*unrelated));
  CHECK(unrelated);
  memcpy(unrelated, &t->model->expert[2], 2u * sizeof(*unrelated));
  /* Two increments still fit beside large inactive-owner clocks. */
  CHECK(c_trainer_validate(t) == C_OK);
  CHECK(c_trainer_step(t, 1u, &report) == C_OK && report.updates == four + 1u &&
        t->model->expert[0].clock == four + 1u &&
        t->model->expert[1].clock == four + 1u &&
        !memcmp(unrelated, &t->model->expert[2], 2u * sizeof(*unrelated)) &&
        !t->receipt_consumed[0] && c_trainer_validate(t) == C_OK);
  free(unrelated);
  c_life_seed(t);
  CHECK(refuse_boundary_generation(t) == 0);
  c_trainer_destroy(t);
  t = NULL;

  CHECK(c_trainer_create(2u, 123u, &t) == C_OK);
  CHECK(c_trainer_enqueue_measurement(t, measured, sizeof(measured) - 1u, 2u,
                                      3u, parent, evaluator, receipt) == C_OK);
  boundary_counters(t, UINT64_MAX / 2u);
  CHECK(refuse_boundary_generation(t) == 0 && !t->receipt_consumed[0]);
  c_trainer_destroy(t);
  t = NULL;

  CHECK(c_trainer_create(4u, 123u, &t) == C_OK);
  CHECK(c_context_admit(t->context, C_SOURCE, C_TRAIN, "boundary.c", "test",
                        source, sizeof(source), &id) == C_OK);
  CHECK(c_trainer_enqueue_source(t, id, 0u, 15u) == C_OK);
  CHECK(c_trainer_enqueue_measurement(t, measured, sizeof(measured) - 1u, 2u,
                                      15u, parent, evaluator, receipt) == C_OK);
  CHECK(c_trainer_step(t, 128u, &report) == C_OK && report.updates == 2u &&
        !report.queued);
  c_life_seed(t);
  CHECK(c_trainer_merge_candidate(t, 0u, 1u, &candidate) == C_OK &&
        candidate->retired_clock == 2u && candidate->retired_count == 2u);
  c_trainer_destroy(t);
  t = candidate;
  candidate = NULL;
  CHECK(c_trainer_enqueue_source(t, id, 1u, 3u) == C_OK);
  boundary_counters(t, UINT64_MAX / 3u - 1u);
  /* The live-owner sum alone has room. Durable retired clocks consume it;
   * refusing the update must preserve archived teachers and consumed receipts.
   */
  CHECK(refuse_boundary_generation(t) == 0 && t->receipt_consumed[0] == 1u);
  c_trainer_destroy(t);
  return 0;
}
int main(void) {
  unsigned char cells[C_WORLD_CELLS] = {0}, next[C_WORLD_CELLS];
  unsigned contact;
  c_trainer *t = NULL;
  uint64_t id;
  char before[65], after[65], parent[65], teacher[65], receipt[65];
  c_training_report report;
  double x[C_FEATURES], p[C_TEXT_ACTIONS], mass[4] = {1, 1, 1, 1}, initial;
  static const unsigned char bytes[] = {0, 'A', '\n', 255};
  CHECK(test_generation_boundaries() == 0);
  cells[at(15, 4)] = 1;
  cells[at(0, 4)] = 2;
  cells[at(1, 4)] = 2;
  c_life_evolve(cells, next, &contact);
  CHECK(next[at(0, 3)] == 3 && next[at(0, 4)] == 2 && next[at(0, 5)] == 3);
  CHECK(contact & (1u << 1));
  CHECK(!next[at(15, 4)] && !next[at(1, 4)]);
  memset(cells, 0, sizeof(cells));
  cells[at(4, 4)] = 1;
  cells[at(5, 4)] = 2;
  cells[at(6, 4)] = 4;
  c_life_evolve(cells, next, &contact);
  CHECK((contact & (1u << 1)) && (contact & (1u << 6)) &&
        !(contact & (1u << 2)));
  CHECK(c_trainer_create(3, 42, &t) == C_OK);
  CHECK(c_context_admit(t->context, C_SOURCE, C_TRAIN, "source.c", "test",
                        bytes, sizeof(bytes), &id) == C_OK);
  CHECK(c_trainer_enqueue_source(t, id, 1, 3) == C_OK);
  CHECK(c_trainer_enqueue_source(t, id, 1, 3) == C_OK);
  CHECK(t->task_count == 1 && t->tasks[0].target == 'A');
  CHECK(c_encode(bytes, 1, x) == C_OK &&
        !memcmp(x, t->tasks[0].input, sizeof(x)));
  CHECK(c_model_predict(t->model, C_TEXT, x, 3, mass, p, C_TEXT_ACTIONS) ==
        C_OK);
  initial = p['A'];
  CHECK(c_model_fingerprint(t->model, 2, before) == C_OK);
  memset(t->cells, 0, sizeof(t->cells));
  CHECK(c_trainer_step(t, 7, &report) == C_OK && report.updates == 0 &&
        report.deferred == 7);
  CHECK(c_model_group_clock(t->model, 0) == 0);
  CHECK(c_model_group_clock(t->model, 1) == 0);
  CHECK(c_trainer_step(t, 2, &report) == C_OK && report.updates == 1 &&
        report.reseeds == 1);
  CHECK(c_model_group_clock(t->model, 0) == 1 &&
        c_model_group_clock(t->model, 1) == 1);
  CHECK(c_model_fingerprint(t->model, 2, after) == C_OK &&
        !strcmp(before, after));
  CHECK(c_model_predict(t->model, C_TEXT, x, 3, mass, p, C_TEXT_ACTIONS) ==
            C_OK &&
        p['A'] > initial);
  CHECK(c_trainer_step(t, 30, &report) == C_OK && report.updates == 1);
  CHECK(c_trainer_enqueue_source(t, id, sizeof(bytes), 3) == C_OK &&
        t->tasks[t->task_count - 1].target == C_EOS);
  c_hash("parent", 6, parent);
  c_hash("evaluator", 9, teacher);
  c_hash("receipt", 7, receipt);
  CHECK(c_trainer_enqueue_measurement(t, (const unsigned char *)"task", 4, 3, 3,
                                      parent, teacher, receipt) == C_OK);
  {
    size_t count = t->task_count;
    CHECK(c_trainer_enqueue_measurement(t, (const unsigned char *)"task", 4, 3,
                                        3, parent, teacher, receipt) == C_OK &&
          t->task_count == count);
  }
  CHECK(c_trainer_enqueue_measurement(t, (const unsigned char *)"task", 4, 2, 3,
                                      parent, teacher, receipt) == C_INVALID);
  CHECK(c_trainer_step(t, 64, &report) == C_OK && report.updates == 3);
  CHECK(c_trainer_step(t, 64, &report) == C_OK && report.updates == 3);
  CHECK(c_trainer_enqueue_source(t, id, 99, 3) == C_INVALID);
  /* Invalid numerical state preserves a whole uncommitted generation. */
  CHECK(c_trainer_enqueue_source(t, id, 0, 3) == C_OK);
  c_life_seed(t);
  {
    unsigned char world[C_WORLD_CELLS];
    uint64_t generation = t->generation;
    memcpy(world, t->cells, sizeof(world));
    t->model->expert[0].code[0][0].value = NAN;
    CHECK(c_trainer_step(t, 1, &report) == C_INVALID &&
          t->generation == generation &&
          !memcmp(world, t->cells, sizeof(world)));
  }
  c_trainer_destroy(t);
  puts("Life authority contracts passed");
  return 0;
}
