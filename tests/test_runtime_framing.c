#include "centroid_context.h"
#ifdef CENTROID_TEST_TRAINING
#include "centroid.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "framing check failed %s:%d: %s\n", __FILE__, __LINE__,  \
              #x);                                                             \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static void test_frame(void) {
  static const unsigned char request[] = {'r', 0, 255, 'q'};
  static const unsigned char source[] = "int frame_source = 7;\n";
  static const unsigned char proposal[] = {'p', 128, 0, 'x'};
  static const unsigned char prefix[] = {'a', 0, 254};
  cr_context *context = NULL;
  cr_context_options options;
  uint64_t request_id, ids[3], dev_id, audit_id;
  unsigned char frame[8192], previous[8192];
  size_t needed = 0, written = 0;
  double encoded[CR_FEATURES];
  cr_context_options_init(&options);
  CHECK(cr_context_create(&options, &context) == CR_OK);
  CHECK(cr_context_admit(context, CR_ACTIVITY, CR_TRAIN, "request", "host",
                         request, sizeof(request), &request_id) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "source.c", "editor",
                         source, sizeof(source) - 1, ids) == CR_OK);
  CHECK(cr_context_admit(context, CR_LLM_PROPOSAL, CR_TRAIN, "proposal",
                         "LLM input", proposal, sizeof(proposal),
                         ids + 1) == CR_OK);
  CHECK(cr_context_admit(context, CR_ACTIVITY, CR_TRAIN, "work", "host", NULL,
                         0, ids + 2) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_DEV, "dev", "excluded", source,
                         sizeof(source) - 1, &dev_id) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_AUDIT, "audit", "excluded",
                         source, sizeof(source) - 1, &audit_id) == CR_OK);
  CHECK(cr_text_frame_context(context, request_id, ids, 3, prefix,
                              sizeof(prefix), NULL, 0, &needed) == CR_LIMIT);
  CHECK(needed > sizeof(source) && needed < sizeof(frame));
  memset(frame, 0xa7, sizeof(frame));
  memcpy(previous, frame, sizeof(frame));
  CHECK(cr_text_frame_context(context, request_id, ids, 3, prefix,
                              sizeof(prefix), frame, needed - 1,
                              &written) == CR_LIMIT);
  CHECK(written == needed && !memcmp(frame, previous, sizeof(frame)));
  CHECK(cr_text_frame_context(context, request_id, ids, 3, prefix,
                              sizeof(prefix), frame, needed,
                              &written) == CR_OK);
  CHECK(written == needed && frame[written] == 0xa7);
  CHECK(!memcmp(frame, "centroid-dialogue/1:le-lengths:", 30));
  CHECK(!memcmp(frame + written - sizeof(prefix), prefix, sizeof(prefix)));
  CHECK(cr_encode(frame, written, encoded) == CR_OK);
#ifdef CENTROID_TEST_TRAINING
  c_context *legacy = NULL;
  uint64_t legacy_request, legacy_ids[3];
  double expected[C_FEATURES];
  CHECK(c_context_create(&legacy) == C_OK);
  CHECK(c_context_admit(legacy, C_ACTIVITY, C_TRAIN, "request", "host", request,
                        sizeof(request), &legacy_request) == C_OK);
  CHECK(c_context_admit(legacy, C_SOURCE, C_TRAIN, "source.c", "editor", source,
                        sizeof(source) - 1, legacy_ids) == C_OK);
  CHECK(c_context_admit(legacy, C_LLM_PROPOSAL, C_TRAIN, "proposal",
                        "LLM input", proposal, sizeof(proposal),
                        legacy_ids + 1) == C_OK);
  CHECK(c_context_admit(legacy, C_ACTIVITY, C_TRAIN, "work", "host", NULL, 0,
                        legacy_ids + 2) == C_OK);
  CHECK(c_text_encode_context(legacy, legacy_request, legacy_ids, 3, prefix,
                              sizeof(prefix), expected) == C_OK);
  CHECK(!memcmp(encoded, expected, sizeof(encoded)));
  c_context_destroy(legacy);
#endif
  memcpy(previous, frame, sizeof(frame));
  written = 12345;
  CHECK(cr_text_frame_context(context, request_id, &dev_id, 1, NULL, 0, frame,
                              sizeof(frame), &written) == CR_INVALID);
  CHECK(written == 12345 && !memcmp(previous, frame, sizeof(frame)));
  CHECK(cr_text_frame_context(context, audit_id, NULL, 0, NULL, 0, frame,
                              sizeof(frame), &written) == CR_INVALID);
  CHECK(cr_text_frame_context(context, request_id, &audit_id, 1, NULL, 0, frame,
                              sizeof(frame), &written) == CR_INVALID);
  CHECK(cr_text_frame_context(context, ids[1], NULL, 0, NULL, 0, frame,
                              sizeof(frame), &written) == CR_INVALID);
  uint64_t duplicates[] = {ids[0], ids[0]};
  CHECK(cr_text_frame_context(context, request_id, duplicates, 2, NULL, 0,
                              frame, sizeof(frame), &written) == CR_INVALID);
  CHECK(cr_text_frame_context(context, request_id, NULL,
                              CR_TEXT_MAX_EVIDENCE + 1, NULL, 0, frame,
                              sizeof(frame), &written) == CR_INVALID);
  CHECK(cr_text_frame_context(context, 999, NULL, 0, NULL, 0, frame,
                              sizeof(frame), &written) == CR_NOT_FOUND);
  cr_record_view view;
  CHECK(cr_context_record(context, 0, &view) == CR_OK);
  CHECK(cr_text_frame_context(context, request_id, NULL, 0, NULL, 0,
                              (unsigned char *)view.bytes, view.length,
                              &written) == CR_INVALID);
  CHECK(!memcmp(view.bytes, request, sizeof(request)));
  /* Explicit historical input remains valid when an editor submits a refresh.
   */
  uint64_t refreshed;
  CHECK(cr_context_admit(context, CR_ACTIVITY, CR_TRAIN, "request", "host",
                         source, sizeof(source) - 1, &refreshed) == CR_OK);
  CHECK(refreshed != request_id);
  CHECK(cr_text_frame_context(context, request_id, ids, 3, prefix,
                              sizeof(prefix), frame, sizeof(frame),
                              &written) == CR_OK);
  CHECK(written == needed && !memcmp(frame, previous, written));
  cr_context_destroy(context);
}

static void test_frame_bound(void) {
  cr_context *context = NULL;
  cr_context_options options;
  uint64_t request;
  size_t overhead = 0, length = 0;
  cr_context_options_init(&options);
  options.max_records = 3;
  CHECK(cr_context_create(&options, &context) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "x", "host", NULL, 0,
                         &request) == CR_OK);
  CHECK(cr_text_frame_context(context, request, NULL, 0, NULL, 0, NULL, 0,
                              &overhead) == CR_LIMIT);
  unsigned char *input = malloc(CR_TEXT_MAX_FRAME_BYTES);
  unsigned char *output = malloc(CR_TEXT_MAX_FRAME_BYTES + 1u);
  CHECK(input && output);
  memset(input, 0xff, CR_TEXT_MAX_FRAME_BYTES);
  size_t payload = CR_TEXT_MAX_FRAME_BYTES - overhead;
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "x", "host", input,
                         payload, &request) == CR_OK);
  memset(output, 0xa7, CR_TEXT_MAX_FRAME_BYTES + 1u);
  CHECK(cr_text_frame_context(context, request, NULL, 0, NULL, 0, output,
                              CR_TEXT_MAX_FRAME_BYTES, &length) == CR_OK);
  CHECK(length == CR_TEXT_MAX_FRAME_BYTES && output[length] == 0xa7);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "x", "host", input,
                         payload + 1u, &request) == CR_OK);
  memset(output, 0xa7, CR_TEXT_MAX_FRAME_BYTES + 1u);
  CHECK(cr_text_frame_context(context, request, NULL, 0, NULL, 0, output,
                              CR_TEXT_MAX_FRAME_BYTES + 1u,
                              &length) == CR_LIMIT);
  CHECK(length == CR_TEXT_MAX_FRAME_BYTES + 1u);
  for (size_t i = 0; i <= CR_TEXT_MAX_FRAME_BYTES; ++i)
    CHECK(output[i] == 0xa7);
  free(input);
  free(output);
  cr_context_destroy(context);
}

int main(void) {
  test_frame();
  test_frame_bound();
  puts(
      "Runtime role-v1 framing, quarantine, bounds and historical inputs pass");
  return 0;
}
