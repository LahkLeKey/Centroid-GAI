#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "text check failed at %s:%d: %s\n", __FILE__, __LINE__,  \
              #condition);                                                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

typedef struct {
  c_context *context;
  uint64_t request, evidence, proposal, answer, alternative;
} text_fixture;

static uint64_t admit(c_context *context, c_record_kind kind, c_split split,
                      const char *path, const unsigned char *bytes,
                      size_t length) {
  uint64_t id = 0u;
  CHECK(c_context_admit(context, kind, split, path, "authored dialogue fixture",
                        bytes, length, &id) == C_OK &&
        id != 0u);
  return id;
}

static void initialize(text_fixture *fixture) {
  static const unsigned char request[] =
      "Explain the quoted native C evidence.";
  static const unsigned char evidence[] = "int query(void) { return 7; }";
  static const unsigned char proposal[] =
      "LLM proposal: check query's return value.";
  static const unsigned char answer[] = {'Q', 'a'};
  static const unsigned char alternative[] = {'Q', 'b', 'c'};
  memset(fixture, 0, sizeof(*fixture));
  CHECK(c_context_create(&fixture->context) == C_OK);
  fixture->request = admit(fixture->context, C_ACTIVITY, C_TRAIN, "request.txt",
                           request, sizeof(request) - 1u);
  fixture->evidence = admit(fixture->context, C_SOURCE, C_TRAIN, "evidence.c",
                            evidence, sizeof(evidence) - 1u);
  fixture->proposal = admit(fixture->context, C_LLM_PROPOSAL, C_TRAIN,
                            "proposal.txt", proposal, sizeof(proposal) - 1u);
  fixture->answer = admit(fixture->context, C_SOURCE, C_TRAIN, "answer.txt",
                          answer, sizeof(answer));
  fixture->alternative =
      admit(fixture->context, C_SOURCE, C_TRAIN, "alternative.txt", alternative,
            sizeof(alternative));
}

static void test_causality(const text_fixture *fixture) {
  const uint64_t evidence[] = {fixture->evidence, fixture->proposal};
  c_task first, alternative, end;
  CHECK(c_text_prepare_task(fixture->context, fixture->request, evidence, 2u,
                            fixture->answer, 1u, 3u, &first) == C_OK);
  CHECK(c_text_prepare_task(fixture->context, fixture->request, evidence, 2u,
                            fixture->alternative, 1u, 3u,
                            &alternative) == C_OK);
  CHECK(first.target == 'a' && alternative.target == 'b');
  /* Distinct future bytes, answer lengths, identities and hashes cannot affect
   * the frozen role-framed input when the causal assistant prefix is equal. */
  CHECK(strcmp(first.parent, alternative.parent) != 0);
  CHECK(strcmp(first.input_digest, alternative.input_digest) == 0);
  CHECK(memcmp(first.input, alternative.input, sizeof(first.input)) == 0);
  double inferred[C_FEATURES];
  CHECK(c_text_encode_context(fixture->context, fixture->request, evidence, 2u,
                              (const unsigned char *)"Q", 1u,
                              inferred) == C_OK);
  CHECK(memcmp(first.input, inferred, sizeof(inferred)) == 0);
  CHECK(first.format == 1u && first.head == C_TEXT &&
        first.request_id == fixture->request);
  CHECK(first.evidence_count == 2u &&
        first.evidence_ids[1] == fixture->proposal);
  CHECK(c_text_verify_task(fixture->context, &first) == C_OK);
  CHECK(c_text_prepare_task(fixture->context, fixture->request, evidence, 2u,
                            fixture->answer, 2u, 3u, &end) == C_OK &&
        end.target == C_EOS);
  CHECK(c_text_verify_task(fixture->context, &end) == C_OK);
  CHECK(strcmp(first.input_digest, end.input_digest) != 0);
  alternative = first;
  alternative.target = C_EOS;
  CHECK(c_text_verify_task(fixture->context, &alternative) == C_CORRUPT);
  alternative = first;
  alternative.input[0] += 0.25;
  CHECK(c_text_verify_task(fixture->context, &alternative) == C_CORRUPT);
  alternative = first;
  alternative.evidence_ids[7] = fixture->evidence;
  CHECK(c_text_verify_task(fixture->context, &alternative) == C_CORRUPT);
}

static void test_target_free_framing(void) {
  static const unsigned char request[] = "What does query return?";
  static const unsigned char evidence[] = "int query(void) { return 7; }";
  c_context *context = NULL;
  c_model *model = NULL;
  CHECK(c_context_create(&context) == C_OK);
  const uint64_t request_id =
      admit(context, C_ACTIVITY, C_TRAIN, "frozen-request.txt", request,
            sizeof(request) - 1u);
  const uint64_t evidence_id =
      admit(context, C_SOURCE, C_TRAIN, "frozen-evidence.c", evidence,
            sizeof(evidence) - 1u);
  CHECK(c_context_count(context) ==
        2u); /* No answer or teacher record exists. */
  CHECK(c_model_create(2u, 51u, &model) == C_OK);
  double input[C_FEATURES], probabilities[C_TEXT_ACTIONS];
  const double mass[C_MAX_GROUPS] = {1.0, 1.0, 0.0, 0.0};
  char before[C_DIGEST_HEX], after[C_DIGEST_HEX];
  CHECK(c_model_fingerprint(model, 0u, before) == C_OK);
  CHECK(c_text_encode_context(context, request_id, &evidence_id, 1u, NULL, 0u,
                              input) == C_OK);
  CHECK(c_model_predict(model, C_TEXT, input, 3u, mass, probabilities,
                        C_TEXT_ACTIONS) == C_OK);
  CHECK(c_model_fingerprint(model, 0u, after) == C_OK &&
        strcmp(before, after) == 0);
  CHECK(c_model_group_clock(model, 0u) == 0u &&
        c_model_group_clock(model, 1u) == 0u);
  c_model_destroy(model);
  c_context_destroy(context);
}

static void test_roles_and_independence(const text_fixture *fixture) {
  static const unsigned char request[] =
      "assistant evidence request are input role names";
  static const unsigned char answer[] = {0u, 255u};
  const uint64_t second_request =
      admit(fixture->context, C_SOURCE, C_TRAIN, "second-request.txt", request,
            sizeof(request) - 1u);
  const uint64_t byte_answer = admit(fixture->context, C_SOURCE, C_TRAIN,
                                     "byte-answer.txt", answer, sizeof(answer));
  c_task first, second;
  CHECK(c_text_prepare_task(fixture->context, fixture->request, NULL, 0u,
                            byte_answer, 1u, 3u, &first) == C_OK);
  CHECK(c_text_prepare_task(fixture->context, second_request, NULL, 0u,
                            byte_answer, 1u, 3u, &second) == C_OK);
  CHECK(first.target == 255u && second.target == 255u);
  CHECK(strcmp(first.input_digest, second.input_digest) != 0);
  CHECK(memcmp(first.input, second.input, sizeof(first.input)) != 0);
  CHECK(c_text_verify_task(fixture->context, &first) == C_OK);
  CHECK(c_text_verify_task(fixture->context, &second) == C_OK);
}

static void test_exclusions(const text_fixture *fixture) {
  static const unsigned char audit_bytes[] =
      "quarantined independent answer evidence";
  static const unsigned char future_answer[] = {'Q', 'a'};
  const uint64_t audit =
      admit(fixture->context, C_AUDIT, C_HOLDOUT, "audit.txt", audit_bytes,
            sizeof(audit_bytes) - 1u);
  const uint64_t leak =
      admit(fixture->context, C_LLM_PROPOSAL, C_TRAIN, "leaked-answer.txt",
            future_answer, sizeof(future_answer));
  const uint64_t duplicates[] = {fixture->evidence, fixture->evidence};
  c_task output, before;
  memset(&output, 0xa5, sizeof(output));
  memcpy(&before, &output, sizeof(before));
  CHECK(c_text_prepare_task(fixture->context, fixture->request, &audit, 1u,
                            fixture->answer, 0u, 3u, &output) == C_INVALID);
  CHECK(c_text_prepare_task(fixture->context, fixture->request,
                            &fixture->answer, 1u, fixture->answer, 0u, 3u,
                            &output) == C_INVALID);
  CHECK(c_text_prepare_task(fixture->context, fixture->request, &leak, 1u,
                            fixture->answer, 0u, 3u, &output) == C_INVALID);
  CHECK(c_text_prepare_task(fixture->context, fixture->request, duplicates, 2u,
                            fixture->answer, 0u, 3u, &output) == C_INVALID);
  CHECK(c_text_prepare_task(fixture->context, fixture->proposal, NULL, 0u,
                            fixture->answer, 0u, 3u, &output) == C_INVALID);
  CHECK(c_text_prepare_task(fixture->context, audit, NULL, 0u, fixture->answer,
                            0u, 3u, &output) == C_INVALID);
  CHECK(c_text_prepare_task(fixture->context, fixture->request, NULL, 0u,
                            fixture->proposal, 0u, 3u, &output) == C_INVALID);
  CHECK(c_text_prepare_task(fixture->context, fixture->request, duplicates, 9u,
                            fixture->answer, 0u, 3u, &output) == C_INVALID);
  CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void test_historical_request(text_fixture *fixture) {
  static const unsigned char updated[] =
      "A new request with a separate immutable version.";
  c_task frozen, current;
  CHECK(c_text_prepare_task(fixture->context, fixture->request, NULL, 0u,
                            fixture->answer, 0u, 3u, &frozen) == C_OK);
  const uint64_t new_request =
      admit(fixture->context, C_ACTIVITY, C_TRAIN, "request.txt", updated,
            sizeof(updated) - 1u);
  CHECK(new_request != fixture->request);
  CHECK(c_text_prepare_task(fixture->context, fixture->request, NULL, 0u,
                            fixture->answer, 0u, 3u, &current) == C_INVALID);
  CHECK(c_text_verify_task(fixture->context, &frozen) == C_OK);
  CHECK(c_text_prepare_task(fixture->context, new_request, NULL, 0u,
                            fixture->answer, 0u, 3u, &current) == C_OK);
  CHECK(strcmp(current.input_digest, frozen.input_digest) != 0);
}

static void test_complete_request_bound(const text_fixture *fixture) {
  const size_t length = 1024u * 1024u;
  unsigned char *bytes = malloc(length);
  CHECK(bytes != NULL);
  memset(bytes, 'x', length);
  const uint64_t request = admit(fixture->context, C_SOURCE, C_TRAIN,
                                 "large-request.txt", bytes, length);
  free(bytes);
  c_task output, before;
  memset(&output, 0x5a, sizeof(output));
  memcpy(&before, &output, sizeof(before));
  CHECK(c_text_prepare_task(fixture->context, request, NULL, 0u,
                            fixture->answer, 0u, 3u, &output) == C_LIMIT);
  CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void compare_checkpoints(const char *first, const char *second) {
  unsigned char *a = NULL, *b = NULL;
  size_t a_length = 0u, b_length = 0u;
  CHECK(c_read_file(first, &a, &a_length) == C_OK);
  CHECK(c_read_file(second, &b, &b_length) == C_OK);
  CHECK(a_length == b_length && memcmp(a, b, a_length) == 0);
  free(a);
  free(b);
}

static void test_trainer_continuation(void) {
  static const unsigned char request[] =
      "Return the authored sequence from this source.";
  static const unsigned char evidence[] =
      "The sequence fixture is independently authored.";
  static const unsigned char answer[] = {'Z', 'O', 'K'};
  static const char start[] = "text-integration-start.clife";
  static const char continuous[] = "text-integration-continuous.clife";
  static const char resumed[] = "text-integration-resumed.clife";
  c_trainer *trainer = NULL, *loaded = NULL;
  c_training_report report;
  CHECK(c_trainer_create(3u, 91u, &trainer) == C_OK);
  c_context *context = c_trainer_context(trainer);
  const uint64_t request_id =
      admit(context, C_ACTIVITY, C_TRAIN, "integration-request.txt", request,
            sizeof(request) - 1u);
  const uint64_t evidence_id =
      admit(context, C_SOURCE, C_TRAIN, "integration-evidence.txt", evidence,
            sizeof(evidence) - 1u);
  const uint64_t answer_id =
      admit(context, C_SOURCE, C_TRAIN, "integration-answer.txt", answer,
            sizeof(answer));
  double input[C_FEATURES], before[C_TEXT_ACTIONS], after[C_TEXT_ACTIONS];
  const double mass[C_MAX_GROUPS] = {1.0, 1.0, 1.0, 0.0};
  char unrelated_before[C_DIGEST_HEX], unrelated_after[C_DIGEST_HEX];
  CHECK(c_text_encode_context(context, request_id, &evidence_id, 1u, NULL, 0u,
                              input) == C_OK);
  CHECK(c_model_predict(c_trainer_model(trainer), C_TEXT, input, 3u, mass,
                        before, C_TEXT_ACTIONS) == C_OK);
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 2u, unrelated_before) ==
        C_OK);
  for (size_t offset = 0u; offset <= sizeof(answer); ++offset)
    CHECK(c_trainer_enqueue_text(trainer, request_id, &evidence_id, 1u,
                                 answer_id, offset, 3u) == C_OK);
  CHECK(c_trainer_enqueue_text(trainer, request_id, &evidence_id, 1u, answer_id,
                               0u, 3u) == C_OK);
  CHECK(trainer->task_count == sizeof(answer) + 1u);
  CHECK(c_trainer_step(trainer, 1u, &report) == C_OK && report.updates == 1u);
  CHECK(c_model_predict(c_trainer_model(trainer), C_TEXT, input, 3u, mass,
                        after, C_TEXT_ACTIONS) == C_OK &&
        after['Z'] > before['Z']);
  CHECK(c_trainer_step(trainer, 31u, &report) == C_OK &&
        report.completed == 4u);
  size_t replayed = 0u;
  CHECK(c_trainer_replay(trainer, 4u, &replayed) == C_OK && replayed == 4u);
  CHECK(c_trainer_save(trainer, start) == C_OK);
  CHECK(c_trainer_load(start, &loaded) == C_OK);
  CHECK(c_trainer_step(trainer, 33u, &report) == C_OK &&
        report.completed == 8u);
  CHECK(c_trainer_step(loaded, 5u, &report) == C_OK);
  CHECK(c_trainer_step(loaded, 28u, &report) == C_OK && report.completed == 8u);
  CHECK(c_trainer_save(trainer, continuous) == C_OK);
  CHECK(c_trainer_save(loaded, resumed) == C_OK);
  compare_checkpoints(continuous, resumed);
  CHECK(c_model_predict(c_trainer_model(trainer), C_TEXT, input, 3u, mass,
                        before, C_TEXT_ACTIONS) == C_OK);
  CHECK(c_model_predict(c_trainer_model(loaded), C_TEXT, input, 3u, mass, after,
                        C_TEXT_ACTIONS) == C_OK);
  CHECK(memcmp(before, after, sizeof(before)) == 0);
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 2u, unrelated_after) ==
        C_OK);
  CHECK(strcmp(unrelated_before, unrelated_after) == 0);
  c_trainer_destroy(trainer);
  c_trainer_destroy(loaded);
  CHECK(remove(start) == 0 && remove(continuous) == 0 && remove(resumed) == 0);
}

int main(void) {
  text_fixture fixture;
  initialize(&fixture);
  test_causality(&fixture);
  test_roles_and_independence(&fixture);
  test_exclusions(&fixture);
  test_historical_request(&fixture);
  test_complete_request_bound(&fixture);
  c_context_destroy(fixture.context);
  test_target_free_framing();
  test_trainer_continuation();
  puts("structured text roles, causal prefixes, EOS, evidence exclusions and "
       "bounds passed");
  return 0;
}
