#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "checkpoint line %d: %s\n", __LINE__, #x);               \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static int same_file(const char *a, const char *b) {
  unsigned char *x = NULL, *y = NULL;
  size_t nx = 0, ny = 0;
  int equal;
  if (c_read_file(a, &x, &nx) != C_OK || c_read_file(b, &y, &ny) != C_OK) {
    free(x);
    free(y);
    return 0;
  }
  equal = nx == ny && !memcmp(x, y, nx);
  free(x);
  free(y);
  return equal;
}
int main(int argc, char **argv) {
  c_trainer *a = NULL, *b = NULL, *loaded = NULL, *incumbent;
  uint64_t id;
  size_t i;
  unsigned char *raw = NULL, *payload = NULL;
  size_t length = 0, n = 0;
  c_training_report report;
  char parent[65], teacher[65], receipt[65];
  static const unsigned char source[] =
      "int clamp(int x) { return x < 0 ? 0 : x; }\n";
  if (argc == 5 && !strcmp(argv[1], "--resume")) {
    CHECK(c_trainer_load(argv[2], &a) == C_OK);
    CHECK(c_trainer_step(a, (size_t)strtoul(argv[4], NULL, 10), &report) ==
          C_OK);
    CHECK(c_trainer_save(a, argv[3]) == C_OK);
    c_trainer_destroy(a);
    return 0;
  }
  CHECK(c_trainer_create(3, 71, &a) == C_OK);
  CHECK(c_context_admit(a->context, C_SOURCE, C_TRAIN, "clamp.c", "fixture",
                        source, sizeof(source) - 1, &id) == C_OK);
  for (i = 0; i < 12; i++)
    CHECK(c_trainer_enqueue_source(a, id, i, 3) == C_OK);
  c_hash("parent", 6, parent);
  c_hash("verifier", 8, teacher);
  c_hash("measurement", 11, receipt);
  CHECK(c_trainer_enqueue_measurement(a, (const unsigned char *)"choose", 6, 3,
                                      3, parent, teacher, receipt) == C_OK);
  CHECK(c_trainer_save(a, "checkpoint-start.clife") == C_OK);
  CHECK(c_trainer_load("checkpoint-start.clife", &b) == C_OK);
  CHECK(c_trainer_step(a, 31, &report) == C_OK);
  CHECK(c_trainer_save(a, "checkpoint-continuous.clife") == C_OK);
  CHECK(c_trainer_step(b, 3, &report) == C_OK);
  CHECK(c_trainer_step(b, 6, &report) == C_OK);
  CHECK(c_trainer_save(b, "checkpoint-split.clife") == C_OK);
  CHECK(c_trainer_load("checkpoint-split.clife", &loaded) == C_OK);
  CHECK(c_trainer_step(loaded, 22, &report) == C_OK);
  CHECK(c_trainer_save(loaded, "checkpoint-resumed.clife") == C_OK);
  CHECK(same_file("checkpoint-continuous.clife", "checkpoint-resumed.clife"));
  {
    const char *args[] = {argv[0],
                          "--resume",
                          "checkpoint-split.clife",
                          "checkpoint-process.clife",
                          "22",
                          NULL};
    c_process_options options = {argv[0], args, NULL, 30000, 4096};
    c_process_result result = {0};
    CHECK(c_process_run(&options, &result) == C_OK);
    if (result.exit_code)
      fwrite(result.output, 1, result.length, stderr);
    CHECK(result.exit_code == 0 && !result.timed_out);
    c_process_dispose(&result);
    CHECK(same_file("checkpoint-continuous.clife", "checkpoint-process.clife"));
  }
  CHECK(c_read_file("checkpoint-split.clife", &raw, &length) == C_OK);
  incumbent = loaded;
  CHECK(c_trainer_load("checkpoint-continuous.clife", &loaded) == C_INVALID &&
        loaded == incumbent);
  raw[length / 2] ^= 1;
  CHECK(c_write_atomic("checkpoint-bad.clife", raw, length) == C_OK);
  CHECK(c_trainer_load("checkpoint-bad.clife", &loaded) == C_CORRUPT &&
        loaded == incumbent);
  raw[length / 2] ^= 1;
  CHECK(c_write_atomic("checkpoint-bad.clife", raw, length - 1) == C_OK);
  CHECK(c_trainer_load("checkpoint-bad.clife", &loaded) == C_CORRUPT &&
        loaded == incumbent);
  raw = (unsigned char *)realloc(raw, length + 1);
  CHECK(raw);
  raw[length] = 0;
  CHECK(c_write_atomic("checkpoint-bad.clife", raw, length + 1) == C_OK);
  CHECK(c_trainer_load("checkpoint-bad.clife", &loaded) == C_CORRUPT &&
        loaded == incumbent);
  free(raw);
  CHECK(c_envelope_read("checkpoint-split.clife", "CLIFE001", &payload, &n) ==
        C_OK);
  payload[8] ^= 1;
  CHECK(c_envelope_write("checkpoint-bad.clife", "CLIFE001", payload, n) ==
        C_OK);
  free(payload);
  CHECK(c_trainer_load("checkpoint-bad.clife", &loaded) == C_CORRUPT &&
        loaded == incumbent);
  CHECK(c_trainer_enqueue_measurement(a, (const unsigned char *)"choose", 6, 2,
                                      3, parent, teacher,
                                      receipt) == C_INVALID);
  CHECK(c_trainer_step(a, 128, &report) == C_OK);
  for (i = 0; i < a->task_count; i++)
    if (a->tasks[i].head == C_CODE)
      break;
  CHECK(i < a->task_count && a->tasks[i].done && a->receipt_consumed[0]);
  a->tasks[i].target = 2;
  CHECK(c_trainer_save(a, "checkpoint-continuous.clife") == C_CORRUPT);
  a->tasks[i].target = 3;
  a->tasks[i].done = 0;
  a->report.queued++;
  CHECK(c_trainer_save(a, "checkpoint-continuous.clife") == C_CORRUPT);
  a->tasks[i].done = 1;
  a->report.queued--;
  CHECK(same_file("checkpoint-continuous.clife", "checkpoint-process.clife"));
  c_trainer_destroy(a);
  c_trainer_destroy(b);
  c_trainer_destroy(loaded);
  remove("checkpoint-start.clife");
  remove("checkpoint-continuous.clife");
  remove("checkpoint-split.clife");
  remove("checkpoint-resumed.clife");
  remove("checkpoint-process.clife");
  remove("checkpoint-bad.clife");
  puts("canonical checkpoint and fresh-process continuation passed");
  return 0;
}
