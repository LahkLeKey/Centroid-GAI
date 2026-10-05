#include "centroid_session.h"
#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef C_BUILD_ID
#define C_BUILD_ID "unknown-build"
#endif

#define SESSION_INPUT_BYTES (2u * 1024u * 1024u)
static const char session_recipe[] =
    "centroid-session/1:roles-current-source-span:greedy-byte-eos";
static const unsigned char abstention[] =
    "No supported current source excerpt matches this request.";

/* The model codec belongs to the common numerical checkpoint layer. */
void c_model_write(c_writer *writer, const c_model *model, int extensions);
c_status c_model_read(c_reader *reader, c_model **out, int extensions);

typedef struct {
  c_session_turn view;
  unsigned char *request, *answer;
} session_turn;

struct c_session {
  c_model *model;
  c_context *context;
  session_turn turns[C_SESSION_TURNS];
  size_t count, history_bytes;
};

static c_status copy_context(const c_context *context, c_context **out) {
  unsigned char *packed = NULL;
  size_t length = 0;
  c_status status = c_context_pack(context, &packed, &length);
  if (status == C_OK)
    status = c_context_unpack(packed, length, out);
  free(packed);
  return status;
}

c_status c_session_create(const c_model *model, const c_context *context,
                          c_session **out) {
  double input[C_FEATURES] = {1.0}, probabilities[C_TEXT_ACTIONS];
  const double mass[C_MAX_GROUPS] = {1.0, 1.0, 1.0, 1.0};
  c_session *session;
  c_status status;
  unsigned groups;
  if (!model || !context || !out || *out)
    return C_INVALID;
  groups = c_model_group_count(model);
  if (!groups || groups > C_MAX_GROUPS)
    return C_INVALID;
  status = c_model_predict(model, C_TEXT, input, (1u << groups) - 1u, mass,
                           probabilities, C_TEXT_ACTIONS);
  if (status != C_OK)
    return status;
  session = calloc(1, sizeof(*session));
  if (!session)
    return C_NOMEM;
  session->model = malloc(sizeof(*session->model));
  if (!session->model) {
    c_session_destroy(session);
    return C_NOMEM;
  }
  memcpy(session->model, model, sizeof(*model));
  status = copy_context(context, &session->context);
  if (status != C_OK) {
    c_session_destroy(session);
    return status;
  }
  *out = session;
  return C_OK;
}

void c_session_destroy(c_session *session) {
  if (!session)
    return;
  for (size_t i = 0; i < session->count; ++i) {
    free(session->turns[i].request);
    free(session->turns[i].answer);
  }
  c_model_destroy(session->model);
  c_context_destroy(session->context);
  free(session);
}

static c_status request_valid(const c_session *session,
                              const unsigned char *request, size_t length) {
  if (!session || (!request && length))
    return C_INVALID;
  if (length > C_SESSION_REQUEST_BYTES || session->count == C_SESSION_TURNS ||
      length > C_SESSION_HISTORY_BYTES - session->history_bytes)
    return C_LIMIT;
  return C_OK;
}

static int word_byte(unsigned char byte) {
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') || byte == '_';
}

static size_t matching_position(const c_record *record,
                                const unsigned char *request, size_t length) {
  size_t chosen = 0, longest = 0, q = 0;
  while (q < length) {
    while (q < length && !word_byte(request[q]))
      ++q;
    size_t begin = q;
    while (q < length && word_byte(request[q]))
      ++q;
    const size_t size = q - begin;
    if (size < 3 || size <= longest)
      continue;
    size_t p = 0;
    while (p < record->length) {
      while (p < record->length && !word_byte(record->bytes[p]))
        ++p;
      const size_t start = p;
      while (p < record->length && word_byte(record->bytes[p]))
        ++p;
      if (size == p - start &&
          memcmp(request + begin, record->bytes + start, size) == 0) {
        longest = size;
        chosen = start;
        break;
      }
    }
  }
  return chosen;
}

static void source_span(const c_record *record, const unsigned char *request,
                        size_t length, size_t *begin, size_t *size) {
  const size_t match = matching_position(record, request, length);
  *begin = record->length > C_SESSION_EXCERPT_BYTES && match > 128u
               ? match - 128u
               : 0u;
  *size = record->length - *begin;
  if (*size > C_SESSION_EXCERPT_BYTES)
    *size = C_SESSION_EXCERPT_BYTES;
}

static c_status retrieve_source(const c_session *session,
                                const unsigned char *request, size_t length,
                                c_session_answer *answer) {
  c_status status =
      c_context_retrieve_kind(session->context, C_SOURCE, request, length,
                              &answer->evidence, &answer->lexical_score);
  if (status == C_NOT_FOUND) {
    answer->kind = C_SESSION_ABSTAINED;
    answer->bytes = abstention;
    answer->length = sizeof(abstention) - 1u;
    return C_OK;
  }
  if (status != C_OK)
    return status;
  answer->kind = C_SESSION_SUPPORTED;
  source_span(&answer->evidence, request, length, &answer->span_begin,
              &answer->span_length);
  answer->bytes = answer->evidence.bytes + answer->span_begin;
  answer->length = answer->span_length;
  return C_OK;
}

static c_status commit_turn(c_session *session, const unsigned char *request,
                            size_t length, const c_session_answer *answer,
                            c_session_answer *out) {
  session_turn candidate = {0};
  size_t bytes;
  if (!c_checked_add(length, answer->length, &bytes) ||
      bytes > C_SESSION_HISTORY_BYTES - session->history_bytes)
    return C_LIMIT;
  candidate.request = malloc(length ? length : 1u);
  if (!candidate.request)
    return C_NOMEM;
  if (length)
    memcpy(candidate.request, request, length);
  candidate.view.id = session->count + 1u;
  candidate.view.user_bytes = candidate.request;
  candidate.view.user_length = length;
  candidate.view.assistant = *answer;
  if (answer->kind != C_SESSION_SUPPORTED) {
    candidate.answer = malloc(answer->length ? answer->length : 1u);
    if (!candidate.answer) {
      free(candidate.request);
      return C_NOMEM;
    }
    if (answer->length)
      memcpy(candidate.answer, answer->bytes, answer->length);
    candidate.view.assistant.bytes = candidate.answer;
  }
  session->turns[session->count++] = candidate;
  session->history_bytes += bytes;
  *out = candidate.view.assistant;
  return C_OK;
}

c_status c_session_ask(c_session *session, const unsigned char *request,
                       size_t length, c_session_answer *out) {
  c_session_answer answer = {0};
  c_status status;
  if (!out)
    return C_INVALID;
  status = request_valid(session, request, length);
  if (status == C_OK)
    status = retrieve_source(session, request, length, &answer);
  if (status == C_OK)
    status = commit_turn(session, request, length, &answer, out);
  return status;
}

static void frame_bytes(c_writer *writer, const void *bytes, size_t length) {
  if (writer->status != C_OK)
    return;
  if (length > SESSION_INPUT_BYTES ||
      writer->length > SESSION_INPUT_BYTES - length) {
    writer->status = C_LIMIT;
    return;
  }
  c_put_bytes(writer, bytes, length);
}

static void frame_message(c_writer *writer, unsigned role,
                          const unsigned char *bytes, size_t length) {
  c_put_u32(writer, role);
  c_put_u64(writer, length);
  frame_bytes(writer, bytes, length);
}

static void frame_string(c_writer *writer, const char *text) {
  const size_t length = strlen(text);
  c_put_u64(writer, length);
  frame_bytes(writer, text, length);
}

static void frame_evidence(c_writer *writer, const c_record *record,
                           size_t begin, size_t length) {
  c_put_u32(writer, 10u + (unsigned)record->kind);
  c_put_u64(writer, record->id);
  c_put_u64(writer, record->version);
  c_put_u32(writer, (unsigned)record->kind);
  c_put_u32(writer, (unsigned)record->split);
  frame_string(writer, record->path);
  frame_string(writer, record->attribution);
  frame_bytes(writer, record->digest, C_DIGEST_HEX - 1u);
  c_put_u64(writer, record->length);
  c_put_u64(writer, begin);
  c_put_u64(writer, length);
  frame_bytes(writer, record->bytes + begin, length);
}

static c_status generation_frame(const c_session *session,
                                 const unsigned char *request, size_t length,
                                 c_writer *writer) {
  writer->data = malloc(SESSION_INPUT_BYTES);
  if (!writer->data)
    return C_NOMEM;
  writer->capacity = SESSION_INPUT_BYTES;
  frame_bytes(writer, session_recipe, sizeof(session_recipe) - 1u);
  c_put_u32(writer, (uint32_t)session->count);
  for (size_t i = 0; i < session->count; ++i) {
    const c_session_turn *turn = &session->turns[i].view;
    frame_message(writer, 1u, turn->user_bytes, turn->user_length);
    c_put_u32(writer, (unsigned)turn->assistant.kind);
    frame_message(writer, 2u, turn->assistant.bytes, turn->assistant.length);
    c_put_u64(writer, turn->assistant.evidence.id);
    c_put_u64(writer, turn->assistant.span_begin);
    c_put_u64(writer, turn->assistant.span_length);
  }
  /* Current request is always complete, after historical role boundaries. */
  frame_message(writer, 3u, request, length);
  for (unsigned kind = C_SOURCE; kind <= C_ACTIVITY; ++kind) {
    c_record evidence = {0};
    double similarity;
    size_t begin, size;
    c_status status =
        c_context_retrieve_kind(session->context, (c_record_kind)kind, request,
                                length, &evidence, &similarity);
    c_put_u32(writer, (unsigned)(status == C_OK));
    if (status == C_NOT_FOUND)
      continue;
    if (status != C_OK)
      return status;
    source_span(&evidence, request, length, &begin, &size);
    frame_evidence(writer, &evidence, begin, size);
  }
  c_put_u32(writer, 4u); /* Current causal assistant prefix follows. */
  return writer->status;
}

c_status c_session_encode_context(const c_session *session,
                                  const unsigned char *request, size_t length,
                                  const unsigned char *prefix,
                                  size_t prefix_length,
                                  double out[C_FEATURES]) {
  c_writer writer = {0};
  double encoded[C_FEATURES];
  c_status status;
  if (!out || (!prefix && prefix_length))
    return C_INVALID;
  if (prefix_length > C_SESSION_GENERATED_BYTES)
    return C_LIMIT;
  status = request_valid(session, request, length);
  if (status == C_OK)
    status = generation_frame(session, request, length, &writer);
  if (status == C_OK) {
    c_put_u64(&writer, prefix_length);
    frame_bytes(&writer, prefix, prefix_length);
    status = writer.status;
  }
  if (status == C_OK)
    status = c_encode(writer.data, writer.length, encoded);
  if (status == C_OK)
    memcpy(out, encoded, sizeof(encoded));
  free(writer.data);
  return status;
}

c_status c_session_generate(c_session *session, const unsigned char *request,
                            size_t length, const unsigned char *prefix,
                            size_t prefix_length, size_t maximum_new_bytes,
                            c_session_answer *out) {
  c_session_answer answer = {0};
  c_writer writer = {0};
  unsigned char bytes[C_SESSION_GENERATED_BYTES];
  const double mass[C_MAX_GROUPS] = {1.0, 1.0, 1.0, 1.0};
  size_t base_length, produced = 0;
  c_status status;
  if (!out || (!prefix && prefix_length))
    return C_INVALID;
  if (prefix_length > C_SESSION_GENERATED_BYTES || !maximum_new_bytes ||
      maximum_new_bytes > C_SESSION_GENERATED_BYTES - prefix_length)
    return C_LIMIT;
  status = request_valid(session, request, length);
  if (status != C_OK)
    return status;
  status = retrieve_source(session, request, length, &answer);
  if (status != C_OK)
    return status;
  if (prefix_length)
    memcpy(bytes, prefix, prefix_length);
  status = generation_frame(session, request, length, &writer);
  base_length = writer.length;
  while (status == C_OK && produced < maximum_new_bytes) {
    double input[C_FEATURES], probabilities[C_TEXT_ACTIONS];
    unsigned best = 0;
    writer.length = base_length;
    c_put_u64(&writer, prefix_length + produced);
    frame_bytes(&writer, bytes, prefix_length + produced);
    status = writer.status;
    if (status == C_OK)
      status = c_encode(writer.data, writer.length, input);
    if (status == C_OK)
      status = c_model_predict(session->model, C_TEXT, input,
                               (1u << session->model->groups) - 1u, mass,
                               probabilities, C_TEXT_ACTIONS);
    if (status != C_OK)
      break;
    for (unsigned i = 1; i < C_TEXT_ACTIONS; ++i)
      if (probabilities[i] > probabilities[best])
        best = i;
    if (best == C_EOS) {
      answer.eos = 1;
      break;
    }
    bytes[prefix_length + produced++] = (unsigned char)best;
  }
  free(writer.data);
  if (status != C_OK)
    return status;
  answer.kind = C_SESSION_GENERATED;
  answer.bytes = bytes;
  answer.length = prefix_length + produced;
  answer.prefix_length = prefix_length;
  answer.generated_bytes = produced;
  answer.generation_budget = maximum_new_bytes;
  answer.byte_limit_reached = !answer.eos && produced == maximum_new_bytes;
  return commit_turn(session, request, length, &answer, out);
}

size_t c_session_count(const c_session *session) {
  return session ? session->count : 0u;
}

c_status c_session_history(const c_session *session, size_t index,
                           c_session_turn *out) {
  if (!session || !out)
    return C_INVALID;
  if (index >= session->count)
    return C_NOT_FOUND;
  *out = session->turns[index].view;
  return C_OK;
}

static void put_answer(c_writer *writer, const c_session_answer *answer) {
  c_put_u32(writer, (unsigned)answer->kind);
  c_put_u64(writer, answer->length);
  c_put_bytes(writer, answer->bytes, answer->length);
  c_put_u64(writer, answer->evidence.id);
  c_put_u64(writer, answer->span_begin);
  c_put_u64(writer, answer->span_length);
  c_put_double(writer, answer->lexical_score);
  c_put_u32(writer, (unsigned)answer->eos);
  c_put_u32(writer, (unsigned)answer->byte_limit_reached);
  c_put_u64(writer, answer->prefix_length);
  c_put_u64(writer, answer->generated_bytes);
  c_put_u64(writer, answer->generation_budget);
}

c_status c_session_save(const c_session *session, const char *path) {
  c_writer writer = {0};
  unsigned char *context = NULL;
  size_t context_length = 0;
  c_status status;
  if (!session || !path || !path[0])
    return C_INVALID;
  status = c_context_pack(session->context, &context, &context_length);
  if (status != C_OK)
    return status;
  c_put_u32(&writer, 1u);
  c_put_u32(&writer, (uint32_t)sizeof(session_recipe));
  c_put_bytes(&writer, session_recipe, sizeof(session_recipe));
  c_put_u32(&writer, (uint32_t)sizeof(C_RECIPE));
  c_put_bytes(&writer, C_RECIPE, sizeof(C_RECIPE));
  c_put_u32(&writer, (uint32_t)sizeof(C_BUILD_ID));
  c_put_bytes(&writer, C_BUILD_ID, sizeof(C_BUILD_ID));
  c_model_write(&writer, session->model, 1);
  c_put_u64(&writer, context_length);
  c_put_bytes(&writer, context, context_length);
  c_put_u32(&writer, (uint32_t)session->count);
  c_put_u64(&writer, session->history_bytes);
  for (size_t i = 0; i < session->count; ++i) {
    const c_session_turn *turn = &session->turns[i].view;
    c_put_u64(&writer, turn->id);
    c_put_u64(&writer, turn->user_length);
    c_put_bytes(&writer, turn->user_bytes, turn->user_length);
    put_answer(&writer, &turn->assistant);
  }
  free(context);
  status = writer.status == C_OK
               ? c_envelope_write(path, "CSESS001", writer.data, writer.length)
               : writer.status;
  free(writer.data);
  return status;
}

static int get_identity(c_reader *reader, const char *expected, size_t length) {
  if (c_get_u32(reader) != length || reader->status != C_OK ||
      reader->offset > reader->length ||
      length > reader->length - reader->offset ||
      memcmp(reader->data + reader->offset, expected, length)) {
    reader->status = C_CORRUPT;
    return 0;
  }
  reader->offset += length;
  return 1;
}

static const unsigned char *get_content(c_reader *reader, size_t bound,
                                        size_t *length) {
  const uint64_t size = c_get_u64(reader);
  if (reader->status != C_OK || size > bound ||
      reader->offset > reader->length ||
      size > reader->length - reader->offset) {
    reader->status = C_CORRUPT;
    return NULL;
  }
  const unsigned char *bytes = reader->data + reader->offset;
  reader->offset += (size_t)size;
  *length = (size_t)size;
  return bytes;
}

static int same_answer(const c_session_answer *a, const c_session_answer *b) {
  return a->kind == b->kind && a->length == b->length &&
         (!a->length || memcmp(a->bytes, b->bytes, a->length) == 0) &&
         a->evidence.id == b->evidence.id && a->span_begin == b->span_begin &&
         a->span_length == b->span_length &&
         a->lexical_score == b->lexical_score && a->eos == b->eos &&
         a->byte_limit_reached == b->byte_limit_reached &&
         a->prefix_length == b->prefix_length &&
         a->generated_bytes == b->generated_bytes &&
         a->generation_budget == b->generation_budget;
}

static c_status load_turn(c_reader *reader, c_session *session) {
  const uint64_t id = c_get_u64(reader);
  size_t request_length = 0;
  const unsigned char *request =
      get_content(reader, C_SESSION_REQUEST_BYTES, &request_length);
  c_session_answer stored = {0}, actual = {0};
  stored.kind = (c_session_answer_kind)c_get_u32(reader);
  stored.bytes = get_content(reader, C_SESSION_EXCERPT_BYTES, &stored.length);
  stored.evidence.id = c_get_u64(reader);
  const uint64_t begin = c_get_u64(reader), span = c_get_u64(reader);
  stored.lexical_score = c_get_double(reader);
  const uint32_t eos = c_get_u32(reader), capped = c_get_u32(reader);
  const uint64_t prefix = c_get_u64(reader), produced = c_get_u64(reader),
                 budget = c_get_u64(reader);
  if (reader->status != C_OK || id != session->count + 1u ||
      (unsigned)stored.kind > C_SESSION_GENERATED || begin > SIZE_MAX ||
      span > C_SESSION_EXCERPT_BYTES || stored.lexical_score < 0.0 ||
      stored.lexical_score > 1.0 || eos > 1u || capped > 1u ||
      prefix > stored.length || produced > C_SESSION_GENERATED_BYTES ||
      budget > C_SESSION_GENERATED_BYTES) {
    return C_CORRUPT;
  }
  stored.span_begin = (size_t)begin;
  stored.span_length = (size_t)span;
  stored.eos = (int)eos;
  stored.byte_limit_reached = (int)capped;
  stored.prefix_length = (size_t)prefix;
  stored.generated_bytes = (size_t)produced;
  stored.generation_budget = (size_t)budget;
  c_status status;
  if (stored.kind == C_SESSION_GENERATED) {
    if (prefix + produced != stored.length || !budget ||
        prefix > C_SESSION_GENERATED_BYTES ||
        budget > C_SESSION_GENERATED_BYTES - prefix)
      return C_CORRUPT;
    status = c_session_generate(session, request, request_length, stored.bytes,
                                (size_t)prefix, (size_t)budget, &actual);
  } else {
    status = c_session_ask(session, request, request_length, &actual);
  }
  if (status != C_OK)
    return status == C_NOMEM ? status : C_CORRUPT;
  return same_answer(&stored, &actual) ? C_OK : C_CORRUPT;
}

c_status c_session_load(const char *path, c_session **out) {
  unsigned char *data = NULL;
  size_t length = 0;
  c_reader reader;
  c_session *session = NULL;
  c_status status;
  if (!path || !path[0] || !out)
    return C_INVALID;
  status = c_envelope_read(path, "CSESS001", &data, &length);
  if (status != C_OK)
    return status;
  reader.data = data;
  reader.length = length;
  reader.offset = 0;
  reader.status = C_OK;
  if (c_get_u32(&reader) != 1u ||
      !get_identity(&reader, session_recipe, sizeof(session_recipe)) ||
      !get_identity(&reader, C_RECIPE, sizeof(C_RECIPE)) ||
      !get_identity(&reader, C_BUILD_ID, sizeof(C_BUILD_ID))) {
    status = C_CORRUPT;
    goto failed;
  }
  session = calloc(1, sizeof(*session));
  if (!session) {
    status = C_NOMEM;
    goto failed;
  }
  status = c_model_read(&reader, &session->model, 1);
  if (status != C_OK)
    goto failed;
  const uint64_t context_length = c_get_u64(&reader);
  if (reader.status != C_OK || reader.offset > reader.length ||
      context_length > reader.length - reader.offset) {
    status = C_CORRUPT;
    goto failed;
  }
  status = c_context_unpack(reader.data + reader.offset, (size_t)context_length,
                            &session->context);
  if (status != C_OK)
    goto failed;
  reader.offset += (size_t)context_length;
  const uint32_t count = c_get_u32(&reader);
  const uint64_t history_bytes = c_get_u64(&reader);
  if (reader.status != C_OK || count > C_SESSION_TURNS ||
      history_bytes > C_SESSION_HISTORY_BYTES) {
    status = C_CORRUPT;
    goto failed;
  }
  for (uint32_t i = 0; i < count; ++i) {
    status = load_turn(&reader, session);
    if (status != C_OK)
      goto failed;
  }
  if (reader.status != C_OK || reader.offset != reader.length ||
      history_bytes != session->history_bytes) {
    status = C_CORRUPT;
    goto failed;
  }
  if (*out) {
    status = C_INVALID;
    goto failed;
  }
  free(data);
  *out = session;
  return C_OK;
failed:
  free(data);
  c_session_destroy(session);
  return status;
}
