#include "centroid_session.h"
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "session check failed at %s:%d: %s\n", __FILE__,         \
              __LINE__, #condition);                                           \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

typedef struct {
  const char *query;
  unsigned family;
  int supported;
} query_case;

/* The complete query set is frozen by SESSION_PROTOCOL.md before evaluation.
 * These expected family identities remain independent of searchable records. */
static const query_case cases[] = {{"ring_push", 0u, 1},
                                   {"ring_pop", 0u, 1},
                                   {"ring_capacity", 0u, 1},
                                   {"align_up", 1u, 1},
                                   {"alignment_mask", 1u, 1},
                                   {"alignment_power", 1u, 1},
                                   {"decimal_parse", 2u, 1},
                                   {"decimal_digit", 2u, 1},
                                   {"decimal_overflow", 2u, 1},
                                   {"binary_marker", 3u, 1},
                                   {"orphan_zebra_code", 0u, 0},
                                   {"phantom_exec_code", 0u, 0},
                                   {"audit_answer_marker", 0u, 0},
                                   {"stale_ring_only", 0u, 0},
                                   {"unknownmodule_ocean", 0u, 0},
                                   {"123 !?!", 0u, 0},
                                   {"", 0u, 0}};

static uint64_t admit(c_context *context, c_record_kind kind, c_split split,
                      const char *path, const unsigned char *bytes,
                      size_t length) {
  uint64_t id = 0;
  CHECK(c_context_admit(context, kind, split, path,
                        "independently authored frozen session fixture", bytes,
                        length, &id) == C_OK);
  return id;
}

static void fixture(c_trainer **trainer, uint64_t ids[4]) {
  static const unsigned char old_ring[] =
      "int stale_ring_only(void){return 9;}";
  static const unsigned char ring[] =
      "/* ring_capacity is fixed. */\n"
      "int ring_push(int *q,int i,int value){q[i]=value;return i+1;}\n"
      "int ring_pop(const int *q,int i){return q[i];}\n";
  static const unsigned char alignment[] =
      "/* alignment_power assumes a power of two. */\n"
      "unsigned align_up(unsigned x,unsigned alignment_mask) {\n"
      " return (x+alignment_mask)&~alignment_mask;\n}\n";
  static const unsigned char decimal[] =
      "/* decimal_overflow is checked before updating. */\n"
      "unsigned decimal_parse(const unsigned char *p) {\n"
      " unsigned x=0;while(*p){unsigned decimal_digit=*p++-'0';\n"
      " if(x>429496729u)return 0;x=x*10+decimal_digit;}return x;\n}\n";
  static const unsigned char binary[] = {'b', 'i', 'n',  'a',  'r', 'y',
                                         '_', 'm', 'a',  'r',  'k', 'e',
                                         'r', 0u,  255u, 128u, '\n'};
  static const unsigned char proposal[] =
      "orphan_zebra_code: unverified proposal to invent an implementation";
  static const unsigned char activity[] =
      "phantom_exec_code: a recorded hypothetical request, not source evidence";
  static const unsigned char audit[] = "audit_answer_marker reserved fixture";
  CHECK(c_trainer_create(2u, 81023u, trainer) == C_OK);
  c_context *context = c_trainer_context(*trainer);
  admit(context, C_SOURCE, C_TRAIN, "session-fixtures/ring.c", old_ring,
        sizeof(old_ring) - 1u);
  ids[0] = admit(context, C_SOURCE, C_TRAIN, "session-fixtures/ring.c", ring,
                 sizeof(ring) - 1u);
  ids[1] = admit(context, C_SOURCE, C_TRAIN, "session-fixtures/align.c",
                 alignment, sizeof(alignment) - 1u);
  ids[2] = admit(context, C_SOURCE, C_TRAIN, "session-fixtures/decimal.c",
                 decimal, sizeof(decimal) - 1u);
  ids[3] = admit(context, C_SOURCE, C_TRAIN, "session-fixtures/binary.c",
                 binary, sizeof(binary));
  admit(context, C_LLM_PROPOSAL, C_TRAIN, "session-fixtures/proposal.txt",
        proposal, sizeof(proposal) - 1u);
  admit(context, C_ACTIVITY, C_TRAIN, "session-fixtures/activity.txt", activity,
        sizeof(activity) - 1u);
  admit(context, C_AUDIT, C_HOLDOUT, "session-fixtures/audit.txt", audit,
        sizeof(audit) - 1u);
}

static void compare_files(const char *first, const char *second) {
  unsigned char *a = NULL, *b = NULL;
  size_t na = 0, nb = 0;
  CHECK(c_read_file(first, &a, &na) == C_OK);
  CHECK(c_read_file(second, &b, &nb) == C_OK);
  CHECK(na == nb && memcmp(a, b, na) == 0);
  free(a);
  free(b);
}

static void check_supported(const c_session_answer *answer, uint64_t id) {
  char digest[C_DIGEST_HEX];
  CHECK(answer->kind == C_SESSION_SUPPORTED && answer->evidence.id == id);
  CHECK(answer->evidence.kind == C_SOURCE &&
        answer->evidence.split == C_TRAIN && answer->evidence.current);
  CHECK(answer->span_length == answer->length &&
        answer->span_length <= C_SESSION_EXCERPT_BYTES &&
        answer->span_begin <= answer->evidence.length &&
        answer->span_length <= answer->evidence.length - answer->span_begin);
  CHECK(!memcmp(answer->bytes, answer->evidence.bytes + answer->span_begin,
                answer->length));
  c_hash(answer->evidence.bytes, answer->evidence.length, digest);
  CHECK(!strcmp(digest, answer->evidence.digest));
  CHECK(answer->evidence.path[0] && answer->evidence.attribution[0] &&
        answer->evidence.version >= 1u && answer->lexical_score >= 0.0 &&
        answer->lexical_score <= 1.0);
}

static void test_queries(const char *report_directory) {
  c_trainer *trainer = NULL;
  uint64_t ids[4];
  size_t positives = 0, negatives = 0, passed = 0;
  FILE *attempts = NULL;
  char path[4096];
  fixture(&trainer, ids);
  CHECK(c_trainer_save(trainer, "session-owner-before.clife") == C_OK);
  if (report_directory) {
    CHECK(c_make_directory(report_directory) == C_OK);
    CHECK(snprintf(path, sizeof(path), "%s/attempts.tsv", report_directory) >
          0);
    attempts = fopen(path, "wbx");
    CHECK(attempts != NULL);
    CHECK(fputs("query\texpected_supported\tobserved_kind\trecord\tversion\t"
                "path\tattribution\tspan_begin\tspan_length\tdigest\tpass\n",
                attempts) >= 0);
    CHECK(snprintf(path, sizeof(path), "%s/fixture-context.cctx",
                   report_directory) > 0);
    CHECK(c_context_save(c_trainer_context(trainer), path) == C_OK);
    c_session *snapshot = NULL;
    CHECK(c_session_create(c_trainer_model(trainer), c_trainer_context(trainer),
                           &snapshot) == C_OK);
    CHECK(snprintf(path, sizeof(path), "%s/initial.csession",
                   report_directory) > 0);
    CHECK(c_session_save(snapshot, path) == C_OK);
    c_session_destroy(snapshot);
  }
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    c_session *session = NULL;
    c_session_answer answer;
    CHECK(c_session_create(c_trainer_model(trainer), c_trainer_context(trainer),
                           &session) == C_OK);
    CHECK(c_session_ask(session, (const unsigned char *)cases[i].query,
                        strlen(cases[i].query), &answer) == C_OK);
    if (cases[i].supported) {
      ++positives;
      check_supported(&answer, ids[cases[i].family]);
      CHECK(answer.evidence.version == (cases[i].family == 0u ? 2u : 1u));
    } else {
      ++negatives;
      CHECK(answer.kind == C_SESSION_ABSTAINED && answer.evidence.id == 0u);
      CHECK(answer.length > 0u && !answer.eos);
    }
    ++passed;
    if (attempts)
      CHECK(fprintf(attempts,
                    "%s\t%d\t%d\t%llu\t%llu\t%s\t%s\t%zu\t%zu\t%s\t1\n",
                    cases[i].query, cases[i].supported, (int)answer.kind,
                    (unsigned long long)answer.evidence.id,
                    (unsigned long long)answer.evidence.version,
                    answer.evidence.id ? answer.evidence.path : "none",
                    answer.evidence.id ? answer.evidence.attribution : "none",
                    answer.span_begin, answer.span_length,
                    answer.evidence.id ? answer.evidence.digest : "none") > 0);
    c_session_destroy(session);
  }
  CHECK(c_trainer_save(trainer, "session-owner-after.clife") == C_OK);
  compare_files("session-owner-before.clife", "session-owner-after.clife");
  CHECK(remove("session-owner-before.clife") == 0);
  CHECK(remove("session-owner-after.clife") == 0);
  if (attempts) {
    CHECK(fclose(attempts) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/report.md", report_directory) > 0);
    FILE *report = fopen(path, "wbx");
    CHECK(report != NULL);
    CHECK(
        fprintf(
            report,
            "# Frozen local evidence session measurement\n\n"
            "Protocol: research/SESSION_PROTOCOL.md, registered before "
            "measurement.\n\n"
            "Positive supported queries: %zu/%zu. Negative abstentions: "
            "%zu/%zu. "
            "All attempts: %zu/%zu. Exact bytes, current versions, digests, "
            "paths, "
            "spans and attribution passed.\n\n"
            "Families: ring buffer, alignment, decimal parsing and binary "
            "source. "
            "Unverified proposal, activity, quarantined audit, stale source, "
            "missing "
            "identifier, numeric/punctuation and empty negatives all "
            "abstained.\n\n"
            "The original trainer checkpoints are byte-identical across every "
            "session inference. Session snapshots survive source changes, "
            "Life-owned "
            "training and original-owner destruction. Native tests also cover "
            "full "
            "binary requests, explicit roles, EOS/unknown output bytes, "
            "transactional "
            "bounds, canonical restart and tamper preservation.\n\n"
            "This establishes bounded source quoting and abstention. Retrieval "
            "is "
            "lexical-gated centroid ranking; no learned conversation, semantic "
            "answer correctness, LLM benefit or sequence fluency is claimed. "
            "Generated text is separately labeled unverified.\n",
            positives, positives, negatives, negatives, passed,
            sizeof(cases) / sizeof(cases[0])) > 0);
    CHECK(fclose(report) == 0);
  }
  c_trainer_destroy(trainer);
}

static void test_snapshot_retention(void) {
  c_trainer *trainer = NULL;
  c_session *session = NULL;
  c_session_answer answer;
  c_training_report report;
  uint64_t ids[4];
  static const unsigned char request[] = {'r', 'i', 'n', 'g', '_',  'p',
                                          'u', 's', 'h', 0u,  255u, 'x'};
  static const unsigned char changed[] =
      "int replacement_only(void){return 4;}";
  fixture(&trainer, ids);
  CHECK(c_session_create(c_trainer_model(trainer), c_trainer_context(trainer),
                         &session) == C_OK);
  char frozen_before[C_DIGEST_HEX], original_after[C_DIGEST_HEX];
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0u, frozen_before) ==
        C_OK);
  CHECK(c_trainer_enqueue_source(trainer, ids[0], 0u, 3u) == C_OK);
  CHECK(c_trainer_step(trainer, 16u, &report) == C_OK && report.updates > 0u);
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0u, original_after) ==
        C_OK);
  CHECK(strcmp(frozen_before, original_after) != 0);
  admit(c_trainer_context(trainer), C_SOURCE, C_TRAIN,
        "session-fixtures/ring.c", changed, sizeof(changed) - 1u);
  c_trainer_destroy(trainer);
  CHECK(c_session_ask(session, request, sizeof(request), &answer) == C_OK);
  check_supported(&answer, ids[0]);
  CHECK(answer.evidence.version == 2u);
  c_session_turn turn;
  CHECK(c_session_history(session, 0u, &turn) == C_OK && turn.id == 1u);
  CHECK(turn.user_length == sizeof(request) &&
        !memcmp(turn.user_bytes, request, sizeof(request)));
  c_session_destroy(session);
}

static void test_bounds_and_spans(void) {
  c_model *model = NULL;
  c_context *context = NULL;
  c_session *session = NULL;
  c_session_answer answer, before;
  const size_t source_length = 12000u;
  unsigned char *source = malloc(source_length);
  unsigned char *large_request = malloc(C_SESSION_REQUEST_BYTES + 1u);
  CHECK(source && large_request);
  memset(source, ' ', source_length);
  memcpy(source + 10000u, "tail_identifier", 15u);
  memset(large_request, 'x', C_SESSION_REQUEST_BYTES + 1u);
  CHECK(c_model_create(2u, 3u, &model) == C_OK);
  CHECK(c_context_create(&context) == C_OK);
  const uint64_t id =
      admit(context, C_SOURCE, C_TRAIN, "long.c", source, source_length);
  CHECK(c_session_create(model, context, &session) == C_OK);
  memset(&answer, 0xa5, sizeof(answer));
  memcpy(&before, &answer, sizeof(before));
  CHECK(c_session_ask(session, large_request, C_SESSION_REQUEST_BYTES + 1u,
                      &answer) == C_LIMIT);
  CHECK(!memcmp(&answer, &before, sizeof(answer)) &&
        c_session_count(session) == 0u);
  CHECK(c_session_ask(session, (const unsigned char *)"tail_identifier", 15u,
                      &answer) == C_OK);
  check_supported(&answer, id);
  CHECK(answer.span_begin == 10000u - 128u && answer.span_length == 2128u);
  /* A maximum-size request is retained whole, then history saturation fails
   * before publishing any partial user or assistant message. */
  CHECK(c_session_ask(session, large_request, C_SESSION_REQUEST_BYTES,
                      &answer) == C_OK);
  c_session_turn turn;
  CHECK(c_session_history(session, 1u, &turn) == C_OK &&
        turn.user_length == C_SESSION_REQUEST_BYTES &&
        !memcmp(turn.user_bytes, large_request, C_SESSION_REQUEST_BYTES));
  CHECK(c_session_ask(session, large_request, C_SESSION_REQUEST_BYTES,
                      &answer) == C_OK);
  CHECK(c_session_ask(session, large_request, C_SESSION_REQUEST_BYTES,
                      &answer) == C_OK);
  memcpy(&before, &answer, sizeof(before));
  const size_t count = c_session_count(session);
  CHECK(c_session_ask(session, large_request, C_SESSION_REQUEST_BYTES,
                      &answer) == C_LIMIT);
  CHECK(c_session_count(session) == count &&
        !memcmp(&answer, &before, sizeof(answer)));
  c_session_destroy(session);
  session = NULL;
  CHECK(c_session_create(model, context, &session) == C_OK);
  for (unsigned i = 0u; i < C_SESSION_TURNS; ++i)
    CHECK(c_session_ask(session, NULL, 0u, &answer) == C_OK);
  CHECK(c_session_ask(session, NULL, 0u, &answer) == C_LIMIT &&
        c_session_count(session) == C_SESSION_TURNS);
  c_session_destroy(session);
  c_context_destroy(context);
  c_model_destroy(model);
  free(source);
  free(large_request);
}

static c_model *forced_model(unsigned token) {
  c_model *model = NULL;
  CHECK(token < C_TEXT_ACTIONS && c_model_create(2u, 42u, &model) == C_OK);
  for (unsigned g = 0; g < model->groups; ++g) {
    for (unsigned a = 0; a < C_TEXT_ACTIONS; ++a)
      for (unsigned f = 0; f < C_FEATURES; ++f)
        model->expert[g].text[a][f].value = 0.0;
    model->expert[g].text[token][0].value = 100.0;
  }
  return model;
}

static void test_roles_eos_unknown(void) {
  static const unsigned char request[] = {'u', 's', 'e', 'r', 0u,
                                          'a', 's', 's', 'i', 's',
                                          't', 'a', 'n', 't', 255u};
  static const unsigned char prefix[] = {0u, 255u, 'r', 'o', 'l', 'e'};
  c_context *context = NULL;
  c_model *model = forced_model(C_EOS);
  c_session *session = NULL;
  c_session_answer answer;
  CHECK(c_context_create(&context) == C_OK);
  CHECK(c_session_create(model, context, &session) == C_OK);
  double empty[C_FEATURES], prefixed[C_FEATURES], altered[C_FEATURES],
      historical[C_FEATURES];
  CHECK(c_session_encode_context(session, request, sizeof(request), NULL, 0u,
                                 empty) == C_OK);
  CHECK(c_session_encode_context(session, request, sizeof(request), prefix,
                                 sizeof(prefix), prefixed) == C_OK);
  CHECK(memcmp(empty, prefixed, sizeof(empty)) != 0);
  unsigned char alternate[sizeof(request)];
  memcpy(alternate, request, sizeof(request));
  alternate[0] = 'U';
  CHECK(c_session_encode_context(session, alternate, sizeof(alternate), prefix,
                                 sizeof(prefix), altered) == C_OK);
  CHECK(memcmp(altered, prefixed, sizeof(altered)) != 0);
  CHECK(c_session_generate(session, request, sizeof(request), prefix,
                           sizeof(prefix), 8u, &answer) == C_OK);
  CHECK(answer.kind == C_SESSION_GENERATED && answer.eos &&
        !answer.byte_limit_reached && answer.generated_bytes == 0u &&
        answer.length == sizeof(prefix) &&
        !memcmp(answer.bytes, prefix, sizeof(prefix)));
  CHECK(c_session_encode_context(session, request, sizeof(request), prefix,
                                 sizeof(prefix), historical) == C_OK);
  CHECK(memcmp(historical, prefixed, sizeof(historical)) != 0);
  c_session_turn turn;
  CHECK(c_session_history(session, 0u, &turn) == C_OK &&
        turn.user_length == sizeof(request) &&
        !memcmp(turn.user_bytes, request, sizeof(request)));
  c_session_destroy(session);
  c_model_destroy(model);
  for (unsigned token = 0u; token <= 255u; token += 255u) {
    session = NULL;
    model = forced_model(token);
    CHECK(c_session_create(model, context, &session) == C_OK);
    char before[C_DIGEST_HEX], after[C_DIGEST_HEX];
    CHECK(c_model_fingerprint(model, 0u, before) == C_OK);
    CHECK(c_session_generate(session, request, sizeof(request), prefix,
                             sizeof(prefix), 3u, &answer) == C_OK);
    CHECK(!answer.eos && answer.byte_limit_reached &&
          answer.generated_bytes == 3u &&
          answer.length == sizeof(prefix) + 3u &&
          !memcmp(answer.bytes, prefix, sizeof(prefix)));
    for (size_t i = sizeof(prefix); i < answer.length; ++i)
      CHECK(answer.bytes[i] == token);
    CHECK(c_model_fingerprint(model, 0u, after) == C_OK &&
          !strcmp(before, after) && c_model_group_clock(model, 0u) == 0u);
    c_session_destroy(session);
    c_model_destroy(model);
  }
  c_context_destroy(context);
}

static size_t first_answer_offset(unsigned char *payload, size_t length) {
  c_reader reader = {payload, length, 0u, C_OK};
  CHECK(c_get_u32(&reader) == 1u);
  for (unsigned i = 0; i < 3u; ++i) {
    const uint32_t size = c_get_u32(&reader);
    CHECK(size <= reader.length - reader.offset);
    reader.offset += size;
  }
  c_model *model = NULL;
  CHECK(c_model_read(&reader, &model, 1) == C_OK);
  c_model_destroy(model);
  const uint64_t context_length = c_get_u64(&reader);
  CHECK(context_length <= reader.length - reader.offset);
  reader.offset += (size_t)context_length;
  CHECK(c_get_u32(&reader) >= 1u);
  (void)c_get_u64(&reader);
  CHECK(c_get_u64(&reader) == 1u);
  const uint64_t request_length = c_get_u64(&reader);
  CHECK(request_length <= reader.length - reader.offset);
  reader.offset += (size_t)request_length;
  (void)c_get_u32(&reader);
  CHECK(c_get_u64(&reader) > 0u && reader.status == C_OK);
  return reader.offset;
}

static void test_resume_and_corruption(const char *program) {
  c_trainer *trainer = NULL;
  c_session *session = NULL, *resumed = NULL;
  c_session_answer a, b;
  uint64_t ids[4];
  static const unsigned char prefix[] = {'P', 0u, 255u};
  fixture(&trainer, ids);
  CHECK(c_session_create(c_trainer_model(trainer), c_trainer_context(trainer),
                         &session) == C_OK);
  CHECK(c_session_ask(session, (const unsigned char *)"ring_push", 9u, &a) ==
        C_OK);
  CHECK(c_session_generate(session, (const unsigned char *)"decimal_parse", 13u,
                           prefix, sizeof(prefix), 7u, &a) == C_OK);
  CHECK(c_session_save(session, "session-start.csession") == C_OK);
  CHECK(c_session_load("session-start.csession", &resumed) == C_OK);
  CHECK(c_session_generate(session, (const unsigned char *)"align_up", 8u, NULL,
                           0u, 11u, &a) == C_OK);
  CHECK(c_session_generate(resumed, (const unsigned char *)"align_up", 8u, NULL,
                           0u, 11u, &b) == C_OK);
  CHECK(a.length == b.length && !memcmp(a.bytes, b.bytes, a.length) &&
        a.eos == b.eos && a.byte_limit_reached == b.byte_limit_reached);
  CHECK(c_session_save(session, "session-continuous.csession") == C_OK);
  CHECK(c_session_save(resumed, "session-resumed.csession") == C_OK);
  compare_files("session-continuous.csession", "session-resumed.csession");
  {
    const char *arguments[] = {program, "--resume", "session-start.csession",
                               "session-fresh-process.csession", NULL};
    c_process_options options = {program, arguments, NULL, 30000u, 4096u};
    c_process_result result = {0};
    CHECK(c_process_run(&options, &result) == C_OK);
    if (result.exit_code)
      fwrite(result.output, 1u, result.length, stderr);
    CHECK(result.exit_code == 0 && !result.timed_out);
    c_process_dispose(&result);
    compare_files("session-continuous.csession",
                  "session-fresh-process.csession");
  }
  c_session *incumbent = resumed;
  CHECK(c_session_load("session-start.csession", &resumed) == C_INVALID &&
        resumed == incumbent && c_session_count(resumed) == 3u);
  unsigned char *payload = NULL, *raw = NULL;
  size_t payload_length = 0, raw_length = 0;
  CHECK(c_envelope_read("session-start.csession", "CSESS001", &payload,
                        &payload_length) == C_OK);
  /* Rehashed altered source claims must still fail deterministic revalidation.
   */
  payload[first_answer_offset(payload, payload_length)] ^= 1u;
  CHECK(c_envelope_write("session-tampered.csession", "CSESS001", payload,
                         payload_length) == C_OK);
  CHECK(c_session_load("session-tampered.csession", &resumed) == C_CORRUPT &&
        resumed == incumbent && c_session_count(resumed) == 3u);
  free(payload);
  CHECK(c_read_file("session-start.csession", &raw, &raw_length) == C_OK);
  CHECK(c_write_atomic("session-tampered.csession", raw, raw_length - 1u) ==
        C_OK);
  CHECK(c_session_load("session-tampered.csession", &resumed) == C_CORRUPT &&
        resumed == incumbent);
  unsigned char *trailing = malloc(raw_length + 1u);
  CHECK(trailing != NULL);
  memcpy(trailing, raw, raw_length);
  trailing[raw_length] = 0u;
  CHECK(c_write_atomic("session-tampered.csession", trailing,
                       raw_length + 1u) == C_OK);
  CHECK(c_session_load("session-tampered.csession", &resumed) == C_CORRUPT &&
        resumed == incumbent);
  free(raw);
  free(trailing);
  /* Save failure does not publish a new file and leaves session history intact.
   */
  CHECK(c_session_save(session, "session-absent-directory/checkpoint") == C_IO);
  CHECK(c_session_count(session) == 3u);
  CHECK(remove("session-start.csession") == 0);
  CHECK(remove("session-continuous.csession") == 0);
  CHECK(remove("session-resumed.csession") == 0);
  CHECK(remove("session-fresh-process.csession") == 0);
  CHECK(remove("session-tampered.csession") == 0);
  c_session_destroy(session);
  c_session_destroy(resumed);
  c_trainer_destroy(trainer);
}

int main(int argc, char **argv) {
  if (argc == 4 && !strcmp(argv[1], "--resume")) {
    c_session *session = NULL;
    c_session_answer answer;
    CHECK(c_session_load(argv[2], &session) == C_OK);
    CHECK(c_session_generate(session, (const unsigned char *)"align_up", 8u,
                             NULL, 0u, 11u, &answer) == C_OK);
    CHECK(c_session_save(session, argv[3]) == C_OK);
    c_session_destroy(session);
    return 0;
  }
  CHECK(argc <= 2);
  test_snapshot_retention();
  test_bounds_and_spans();
  test_roles_eos_unknown();
  test_resume_and_corruption(argv[0]);
  test_queries(argc == 2 ? argv[1] : NULL);
  puts("frozen sessions: 10/10 supported excerpts, 7/7 abstentions; exact "
       "roles, "
       "binary bytes, EOS, bounds, ownership, retention and restart passed");
  return 0;
}
