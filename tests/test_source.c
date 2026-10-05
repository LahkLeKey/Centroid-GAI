#include "centroid_source.h"
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "source line %d: %s\n", __LINE__, #x);                   \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static uint64_t admit(c_trainer *trainer, c_record_kind kind, c_split split,
                      const char *path, const unsigned char *bytes,
                      size_t length) {
  uint64_t id = 0;
  CHECK(c_context_admit(c_trainer_context(trainer), kind, split, path,
                        "native source workflow fixture", bytes, length,
                        &id) == C_OK);
  return id;
}

static void same_file(const char *first, const char *second) {
  unsigned char *a = NULL, *b = NULL;
  size_t na = 0, nb = 0;
  CHECK(c_read_file(first, &a, &na) == C_OK);
  CHECK(c_read_file(second, &b, &nb) == C_OK);
  CHECK(na == nb && !memcmp(a, b, na));
  free(a);
  free(b);
}

static void test_filter_and_byte_targets(void) {
  c_trainer *trainer = NULL;
  c_source_train_report report;
  static const unsigned char old[] = "stale previous source";
  static const unsigned char bytes[] = {0,   'a', 'b', 'X', 'y',
                                        'z', 255, 'q', 0};
  static const unsigned char dev[] = "reserved development source";
  static const unsigned char audit[] = "reserved audit source";
  static const unsigned char proposal[] = "unverified proposal";
  static const unsigned char activity[] = "observed work";
  CHECK(c_trainer_create(2, 42, &trainer) == C_OK);
  const uint64_t stale =
      admit(trainer, C_SOURCE, C_TRAIN, "raw.c", old, sizeof(old) - 1u);
  admit(trainer, C_SOURCE, C_DEV, "dev.c", dev, sizeof(dev) - 1u);
  admit(trainer, C_SOURCE, C_HOLDOUT, "audit.c", audit, sizeof(audit) - 1u);
  admit(trainer, C_LLM_PROPOSAL, C_TRAIN, "proposal.txt", proposal,
        sizeof(proposal) - 1u);
  admit(trainer, C_ACTIVITY, C_TRAIN, "activity.txt", activity,
        sizeof(activity) - 1u);
  const uint64_t source =
      admit(trainer, C_SOURCE, C_TRAIN, "raw.c", bytes, sizeof(bytes));
  const uint64_t empty = admit(trainer, C_SOURCE, C_TRAIN, "empty.c", NULL, 0u);
  CHECK(source != stale);
  CHECK(c_source_train(trainer, 1u, 0u, &report) == C_OK);
  CHECK(report.epochs_completed == 1u && report.available_sources == 2u &&
        report.selected_sources == 2u && report.omitted_sources == 0u &&
        report.enqueue_attempts == 8u && report.training.generation == 0u &&
        report.training.queued == 5u && report.training.updates == 0u);
  const size_t offsets[] = {0u, 3u, 6u, 9u};
  const unsigned targets[] = {0u, 'X', 255u, C_EOS};
  for (size_t i = 0; i < 4u; ++i) {
    const c_task *task = &trainer->tasks[i];
    double input[C_FEATURES];
    CHECK(task->source_id == source && task->head == C_TEXT &&
          task->offset == offsets[i] && task->target == targets[i] &&
          task->eligible == 3u);
    CHECK(c_encode(bytes, offsets[i], input) == C_OK);
    CHECK(!memcmp(input, task->input, sizeof(input)));
  }
  CHECK(trainer->tasks[4].source_id == empty &&
        trainer->tasks[4].target == C_EOS);
  c_trainer_destroy(trainer);
}

static void test_physical_authority(void) {
  c_trainer *trainer = NULL;
  c_source_train_report report;
  static const unsigned char bytes[] = "physical encounter";
  CHECK(c_trainer_create(2, 42, &trainer) == C_OK);
  admit(trainer, C_SOURCE, C_TRAIN, "contact.c", bytes, sizeof(bytes) - 1u);
  memset(trainer->cells, 0, sizeof(trainer->cells));
  char before[C_DIGEST_HEX], after[C_DIGEST_HEX];
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0u, before) == C_OK);
  CHECK(c_source_train(trainer, 1u, 7u, &report) == C_OK);
  CHECK(report.training.generation == 7u && report.training.updates == 0u &&
        report.training.contacts == 0u && report.training.deferred == 7u &&
        report.training.queued == 4u);
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0u, after) == C_OK &&
        !strcmp(before, after));
  CHECK(c_source_train(trainer, 1u, 2u, &report) == C_OK);
  CHECK(report.training.reseeds == 1u && report.training.contacts > 0u &&
        report.training.updates == 1u && report.training.queued == 3u &&
        report.training.group_updates[0] == 1u &&
        report.training.group_updates[1] == 1u);
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0u, after) == C_OK &&
        strcmp(before, after));
  c_trainer_destroy(trainer);
}

static void test_coverage(void) {
  c_trainer *trainer = NULL;
  c_source_train_report report;
  uint64_t ids[35];
  CHECK(c_trainer_create(2, 17, &trainer) == C_OK);
  for (size_t i = 0; i < 35u; ++i) {
    char path[64];
    const unsigned char bytes[] = {0u, (unsigned char)i, 255u, 0u};
    int n = snprintf(path, sizeof(path), "coverage/%zu.c", i);
    CHECK(n > 0 && (size_t)n < sizeof(path));
    ids[i] = admit(trainer, C_SOURCE, C_TRAIN, path, bytes, sizeof(bytes));
  }
  CHECK(c_source_train(trainer, 2u, 0u, &report) == C_OK);
  CHECK(report.epochs_completed == 2u && report.available_sources == 35u &&
        report.selected_sources == 32u && report.omitted_sources == 3u &&
        report.enqueue_attempts == 256u && report.training.queued == 128u);
  for (size_t i = 0; i < trainer->task_count; ++i)
    CHECK(trainer->tasks[i].source_id == ids[i / 4u]);
  c_trainer_destroy(trainer);
}

static void test_failures(void) {
  c_trainer *trainer = NULL, *research = NULL;
  c_source_train_report report, before;
  static const unsigned char source[] = "native bounded source";
  CHECK(c_trainer_create(2, 9, &trainer) == C_OK);
  memset(&report, 0xa5, sizeof(report));
  memcpy(&before, &report, sizeof(before));
  CHECK(c_source_train(NULL, 1u, 0u, &report) == C_INVALID);
  CHECK(c_source_train(trainer, 1u, 0u, NULL) == C_INVALID);
  CHECK(c_source_train(trainer, 0u, 0u, &report) == C_INVALID);
  CHECK(c_source_train(trainer, C_SOURCE_TRAIN_EPOCHS + 1u, 0u, &report) ==
        C_INVALID);
  CHECK(c_source_train(trainer, 1u, C_SOURCE_TRAIN_GENERATIONS + 1u, &report) ==
        C_INVALID);
  CHECK(!memcmp(&before, &report, sizeof(report)) && !trainer->task_count &&
        !trainer->generation);
  CHECK(c_trainer_research_create(2u, 9u, &research) == C_OK);
  admit(research, C_SOURCE, C_TRAIN, "research.c", source, sizeof(source) - 1u);
  CHECK(c_source_train(research, 1u, 0u, &report) == C_INVALID &&
        !memcmp(&before, &report, sizeof(report)) && !research->task_count &&
        !research->generation);
  c_trainer_destroy(research);
  CHECK(c_source_train(trainer, 1u, 0u, &report) == C_NOT_FOUND &&
        report.available_sources == 0u && report.enqueue_attempts == 0u &&
        report.epochs_completed == 0u && !trainer->task_count &&
        !trainer->generation);
  /* An admitted task can precede a runtime capacity failure. The report must
   * expose that partial queue progress, without claiming a completed epoch. */
  admit(trainer, C_SOURCE, C_TRAIN, "pending.c", source, sizeof(source) - 1u);
  trainer->next_task = UINT64_MAX - 1u;
  CHECK(c_source_train(trainer, 1u, 4u, &report) == C_LIMIT &&
        report.epochs_completed == 0u && report.enqueue_attempts == 2u &&
        report.training.queued == 1u && report.training.generation == 0u &&
        report.training.updates == 0u);
  c_trainer_destroy(trainer);
  trainer = NULL;
  CHECK(c_trainer_create(2u, 9u, &trainer) == C_OK);
  unsigned char pending[300];
  memset(pending, 'x', sizeof(pending));
  const uint64_t id =
      admit(trainer, C_SOURCE, C_TRAIN, "full.c", pending, sizeof(pending));
  for (size_t i = 0; i < C_MAX_TASKS; ++i)
    CHECK(c_trainer_enqueue_source(trainer, id, i, 3u) == C_OK);
  CHECK(c_source_train(trainer, 1u, 1u, &report) == C_LIMIT &&
        report.enqueue_attempts == 4u && report.epochs_completed == 0u &&
        report.training.queued == C_MAX_TASKS &&
        report.training.generation == 0u && !report.training.updates);
  c_trainer_destroy(trainer);
}

static void test_continuation(void) {
  c_trainer *continuous = NULL, *resumed = NULL, *manual = NULL;
  c_source_train_report report;
  static const unsigned char bytes[] = "native source continuation";
  CHECK(c_trainer_create(3u, 71u, &continuous) == C_OK);
  const uint64_t id = admit(continuous, C_SOURCE, C_TRAIN, "continuation.c",
                            bytes, sizeof(bytes) - 1u);
  CHECK(c_trainer_save(continuous, "source-start.clife") == C_OK);
  CHECK(c_source_train(continuous, 2u, 32u, &report) == C_OK &&
        report.epochs_completed == 2u && report.enqueue_attempts == 8u);
  CHECK(c_trainer_load("source-start.clife", &resumed) == C_OK);
  CHECK(c_source_train(resumed, 1u, 32u, &report) == C_OK);
  CHECK(c_trainer_save(resumed, "source-split.clife") == C_OK);
  c_trainer_destroy(resumed);
  resumed = NULL;
  CHECK(c_trainer_load("source-split.clife", &resumed) == C_OK);
  CHECK(c_source_train(resumed, 1u, 32u, &report) == C_OK);
  CHECK(c_trainer_save(continuous, "source-continuous.clife") == C_OK);
  CHECK(c_trainer_save(resumed, "source-resumed.clife") == C_OK);
  same_file("source-continuous.clife", "source-resumed.clife");
  /* Known recipe locations provide a direct comparison with explicit library
   * enqueue/step calls, including EOS. Workflow refactoring cannot change it.
   */
  const size_t positions[] = {0u, 8u, 17u, 26u};
  CHECK(sizeof(bytes) - 1u == positions[3]);
  CHECK(c_trainer_load("source-start.clife", &manual) == C_OK);
  c_training_report training;
  for (size_t epoch = 0; epoch < 2u; ++epoch) {
    for (size_t i = 0; i < 4u; ++i)
      CHECK(c_trainer_enqueue_source(manual, id, positions[i], 7u) == C_OK);
    CHECK(c_trainer_step(manual, 32u, &training) == C_OK);
  }
  CHECK(c_trainer_save(manual, "source-manual.clife") == C_OK);
  same_file("source-continuous.clife", "source-manual.clife");
  CHECK(remove("source-start.clife") == 0);
  CHECK(remove("source-split.clife") == 0);
  CHECK(remove("source-continuous.clife") == 0);
  CHECK(remove("source-resumed.clife") == 0);
  CHECK(remove("source-manual.clife") == 0);
  c_trainer_destroy(continuous);
  c_trainer_destroy(resumed);
  c_trainer_destroy(manual);
}

int main(void) {
  test_filter_and_byte_targets();
  test_physical_authority();
  test_coverage();
  test_failures();
  test_continuation();
  puts("native source workflow contracts passed");
  return 0;
}
