#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define TEXT_INPUT_BYTES (1024u * 1024u)
#define TEXT_EVIDENCE_LIMIT 8u

/* Role tags and framing are input-only metadata. Output IDs remain original
 * answer bytes 0..255 and EOS 256. No future-answer length/hash enters input.
 */
static const char formatter[] =
    "centroid-dialogue/"
    "1:le-lengths:request-evidence-assistant:immutable-source-byte";
enum text_role { ROLE_REQUEST = 1u, ROLE_EVIDENCE = 2u, ROLE_ASSISTANT = 3u };

static c_status find_record(const c_context *context, uint64_t id,
                            c_record *record) {
  if (id == 0u)
    return C_INVALID;
  for (size_t i = 0u; i < c_context_count(context); ++i) {
    const c_status status = c_context_record(context, i, record);
    if (status != C_OK)
      return status;
    if (record->id == id)
      return C_OK;
  }
  return C_NOT_FOUND;
}

static void bounded_bytes(c_writer *writer, const void *bytes, size_t length) {
  if (writer->status != C_OK)
    return;
  if (length > TEXT_INPUT_BYTES || writer->length > TEXT_INPUT_BYTES - length) {
    writer->status = C_LIMIT;
    return;
  }
  c_put_bytes(writer, bytes, length);
}

static void framed_u32(c_writer *writer, uint32_t value) {
  unsigned char bytes[4];
  for (unsigned i = 0u; i < 4u; ++i)
    bytes[i] = (unsigned char)(value >> (8u * i));
  bounded_bytes(writer, bytes, sizeof(bytes));
}

static void framed_u64(c_writer *writer, uint64_t value) {
  unsigned char bytes[8];
  for (unsigned i = 0u; i < 8u; ++i)
    bytes[i] = (unsigned char)(value >> (8u * i));
  bounded_bytes(writer, bytes, sizeof(bytes));
}

static void framed_string(c_writer *writer, const char *value) {
  const size_t length = strlen(value);
  framed_u64(writer, (uint64_t)length);
  bounded_bytes(writer, value, length);
}

static void framed_record(c_writer *writer, enum text_role role,
                          const c_record *record) {
  framed_u32(writer, (uint32_t)role);
  framed_u64(writer, record->id);
  framed_u64(writer, record->version);
  framed_u32(writer, (uint32_t)record->kind);
  framed_u32(writer, (uint32_t)record->split);
  framed_string(writer, record->path);
  framed_string(writer, record->attribution);
  bounded_bytes(writer, record->digest, C_DIGEST_HEX - 1u);
  framed_u64(writer, (uint64_t)record->length);
  bounded_bytes(writer, record->bytes, record->length);
}

static int request_valid(const c_record *record, int historical) {
  return record->split == C_TRAIN && (historical || record->current) &&
         (record->kind == C_SOURCE || record->kind == C_ACTIVITY);
}

static int evidence_valid(const c_record *record) {
  return record->split == C_TRAIN &&
         (record->kind == C_SOURCE || record->kind == C_ACTIVITY ||
          record->kind == C_LLM_PROPOSAL);
}

static c_status append_evidence(const c_context *context, const uint64_t *ids,
                                size_t count, const c_record *answer,
                                c_writer *writer) {
  for (size_t i = 0u; i < count; ++i) {
    c_record record;
    for (size_t earlier = 0u; earlier < i; ++earlier)
      if (ids[i] == ids[earlier])
        return C_INVALID;
    c_status status = find_record(context, ids[i], &record);
    if (status != C_OK)
      return status;
    if (!evidence_valid(&record) ||
        (answer != NULL && (record.id == answer->id ||
                            strcmp(record.digest, answer->digest) == 0)))
      return C_INVALID;
    framed_record(writer, ROLE_EVIDENCE, &record);
    if (writer->status != C_OK)
      return writer->status;
  }
  return C_OK;
}

static c_status encode_context(const c_context *context, uint64_t request_id,
                               const uint64_t *evidence_ids,
                               size_t evidence_count,
                               const unsigned char *prefix,
                               size_t prefix_length, int historical,
                               const c_record *answer, double out[C_FEATURES],
                               char digest[C_DIGEST_HEX]) {
  c_record request = {0};
  c_writer writer = {0};
  double encoded[C_FEATURES];
  if (context == NULL || out == NULL || digest == NULL ||
      evidence_count > TEXT_EVIDENCE_LIMIT ||
      (evidence_count != 0u && evidence_ids == NULL) ||
      (prefix_length != 0u && prefix == NULL))
    return C_INVALID;
  if (prefix_length > TEXT_INPUT_BYTES)
    return C_LIMIT;
  c_status status = find_record(context, request_id, &request);
  if (status != C_OK)
    return status;
  if (!request_valid(&request, historical))
    return C_INVALID;
  bounded_bytes(&writer, formatter, sizeof(formatter) - 1u);
  framed_record(&writer, ROLE_REQUEST, &request);
  framed_u32(&writer, (uint32_t)evidence_count);
  status = writer.status;
  if (status == C_OK)
    status =
        append_evidence(context, evidence_ids, evidence_count, answer, &writer);
  if (status == C_OK) {
    framed_u32(&writer, ROLE_ASSISTANT);
    framed_u64(&writer, (uint64_t)prefix_length);
    bounded_bytes(&writer, prefix, prefix_length);
    status = writer.status;
  }
  if (status == C_OK)
    status = c_encode(writer.data, writer.length, encoded);
  if (status == C_OK) {
    c_hash(writer.data, writer.length, digest);
    memcpy(out, encoded, sizeof(encoded));
  }
  free(writer.data);
  return status;
}

c_status c_text_encode_context(const c_context *context, uint64_t request_id,
                               const uint64_t *evidence_ids,
                               size_t evidence_count,
                               const unsigned char *prefix,
                               size_t prefix_length, double out[C_FEATURES]) {
  char digest[C_DIGEST_HEX];
  return encode_context(context, request_id, evidence_ids, evidence_count,
                        prefix, prefix_length, 1, NULL, out, digest);
}

static c_status prepare_task(const c_context *context, uint64_t request_id,
                             const uint64_t *evidence_ids,
                             size_t evidence_count, uint64_t answer_id,
                             size_t offset, unsigned eligible, int historical,
                             c_task *out) {
  c_record request = {0}, answer = {0};
  c_task task = {0};
  if (context == NULL || out == NULL || evidence_count > TEXT_EVIDENCE_LIMIT ||
      (evidence_count != 0u && evidence_ids == NULL) || eligible == 0u ||
      (eligible & (eligible - 1u)) == 0u || (eligible >> C_MAX_GROUPS) != 0u)
    return C_INVALID;
  c_status status = find_record(context, request_id, &request);
  if (status == C_OK)
    status = find_record(context, answer_id, &answer);
  if (status != C_OK)
    return status;
  if (!request_valid(&request, historical) || answer.kind != C_SOURCE ||
      answer.split != C_TRAIN || request.id == answer.id ||
      offset > answer.length)
    return C_INVALID;
  status = encode_context(context, request_id, evidence_ids, evidence_count,
                          answer.bytes, offset, historical, &answer, task.input,
                          task.input_digest);
  if (status == C_OK) {
    task.format = 1u;
    task.head = C_TEXT;
    task.source_id = answer_id;
    task.offset = (uint64_t)offset;
    task.request_id = request_id;
    task.evidence_count = (unsigned)evidence_count;
    for (size_t i = 0u; i < evidence_count; ++i)
      task.evidence_ids[i] = evidence_ids[i];
    task.eligible = eligible;
    task.target = offset == answer.length ? C_EOS : answer.bytes[offset];
    memcpy(task.parent, answer.digest, sizeof(task.parent));
    c_hash(formatter, sizeof(formatter) - 1u, task.evaluator);
  }
  if (status == C_OK)
    *out = task;
  return status;
}

c_status c_text_prepare_task(const c_context *context, uint64_t request_id,
                             const uint64_t *evidence_ids,
                             size_t evidence_count, uint64_t answer_id,
                             size_t offset, unsigned eligible, c_task *out) {
  return prepare_task(context, request_id, evidence_ids, evidence_count,
                      answer_id, offset, eligible, 0, out);
}

c_status c_text_verify_task(const c_context *context, const c_task *task) {
  c_task expected;
  if (context == NULL || task == NULL)
    return C_INVALID;
  if (task->format != 1u || task->head != C_TEXT || task->done > 1u ||
      task->evidence_count > TEXT_EVIDENCE_LIMIT ||
      (uint64_t)(size_t)task->offset != task->offset)
    return C_CORRUPT;
  for (unsigned i = task->evidence_count; i < TEXT_EVIDENCE_LIMIT; ++i)
    if (task->evidence_ids[i] != 0u)
      return C_CORRUPT;
  const c_status status = prepare_task(
      context, task->request_id, task->evidence_ids, task->evidence_count,
      task->source_id, (size_t)task->offset, task->eligible, 1, &expected);
  if (status != C_OK)
    return status == C_NOMEM ? C_NOMEM : C_CORRUPT;
  if (task->target != expected.target ||
      memcmp(task->input, expected.input, sizeof(task->input)) ||
      memcmp(task->parent, expected.parent, sizeof(task->parent)) ||
      memcmp(task->evaluator, expected.evaluator, sizeof(task->evaluator)) ||
      memcmp(task->input_digest, expected.input_digest,
             sizeof(task->input_digest)) ||
      memcmp(task->receipt, expected.receipt, sizeof(task->receipt)))
    return C_CORRUPT;
  return C_OK;
}
