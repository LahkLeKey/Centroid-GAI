#include "centroid_context.h"
#include <stdint.h>
#include <string.h>

/* Matches the retained legacy role-v1 wire contract. Records supply only
 * immutable input metadata/bytes; causal prefix excludes any future answer. */
static const char formatter[] =
    "centroid-dialogue/"
    "1:le-lengths:request-evidence-assistant:immutable-source-byte";

typedef struct {
  unsigned char *bytes;
  size_t length;
  cr_status status;
} frame_writer;

static void emit(frame_writer *writer, const void *bytes, size_t length) {
  if (writer->status != CR_OK)
    return;
  if (length > SIZE_MAX - writer->length) {
    writer->status = CR_LIMIT;
    return;
  }
  if (writer->bytes && length)
    memcpy(writer->bytes + writer->length, bytes, length);
  writer->length += length;
}

static void u32(frame_writer *writer, uint32_t value) {
  unsigned char bytes[4];
  for (unsigned i = 0; i < 4; ++i)
    bytes[i] = (unsigned char)(value >> (8u * i));
  emit(writer, bytes, sizeof(bytes));
}

static void u64(frame_writer *writer, uint64_t value) {
  unsigned char bytes[8];
  for (unsigned i = 0; i < 8; ++i)
    bytes[i] = (unsigned char)(value >> (8u * i));
  emit(writer, bytes, sizeof(bytes));
}

static void string(frame_writer *writer, const char *value) {
  size_t length = strlen(value);
  u64(writer, (uint64_t)length);
  emit(writer, value, length);
}

static void record(frame_writer *writer, uint32_t role,
                   const cr_record_view *value) {
  u32(writer, role);
  u64(writer, value->id);
  u64(writer, value->version);
  u32(writer, value->kind);
  u32(writer, value->split);
  string(writer, value->path);
  string(writer, value->attribution);
  emit(writer, value->digest, CR_DIGEST_HEX - 1u);
  u64(writer, (uint64_t)value->length);
  emit(writer, value->bytes, value->length);
}

static void frame(frame_writer *writer, const cr_record_view *request,
                  const cr_record_view *evidence, size_t count,
                  const unsigned char *prefix, size_t prefix_length) {
  emit(writer, formatter, sizeof(formatter) - 1u);
  record(writer, 1u, request);
  u32(writer, (uint32_t)count);
  for (size_t i = 0; i < count; ++i)
    record(writer, 2u, evidence + i);
  u32(writer, 3u);
  u64(writer, (uint64_t)prefix_length);
  emit(writer, prefix, prefix_length);
}

static cr_status find(const cr_context *context, uint64_t id,
                      cr_record_view *out) {
  if (!id)
    return CR_INVALID;
  for (size_t i = 0; i < cr_context_count(context); ++i) {
    cr_status status = cr_context_record(context, i, out);
    if (status != CR_OK)
      return status;
    if (out->id == id)
      return CR_OK;
  }
  return CR_NOT_FOUND;
}

/* Integer address ranges avoid ordering unrelated C pointers. All arguments
 * still denote valid caller/SDK objects under the public C storage contract. */
static int overlaps(const void *a, size_t alen, const void *b, size_t blen) {
  uintptr_t aa = (uintptr_t)a, bb = (uintptr_t)b;
  if (!alen || !blen)
    return 0;
  if (alen > UINTPTR_MAX - aa || blen > UINTPTR_MAX - bb)
    return 1;
  return aa < bb + blen && bb < aa + alen;
}

static int record_overlap(const void *out, size_t length,
                          const cr_record_view *value) {
  return overlaps(out, length, value->bytes, value->length) ||
         overlaps(out, length, value->path, strlen(value->path) + 1u) ||
         overlaps(out, length, value->attribution,
                  strlen(value->attribution) + 1u);
}

cr_status CR_CALL
cr_text_frame_context(const cr_context *context, uint64_t request_id,
                      const uint64_t *evidence_ids, size_t evidence_count,
                      const unsigned char *prefix, size_t prefix_length,
                      unsigned char *out, size_t capacity, size_t *required) {
  cr_record_view request, evidence[CR_TEXT_MAX_EVIDENCE];
  frame_writer writer = {NULL, 0, CR_OK};
  if (!context || !required || evidence_count > CR_TEXT_MAX_EVIDENCE ||
      (evidence_count && !evidence_ids) || (prefix_length && !prefix) ||
      (!out && capacity))
    return CR_INVALID;
  if (prefix_length > CR_TEXT_MAX_FRAME_BYTES)
    return CR_LIMIT;
  cr_status status = find(context, request_id, &request);
  if (status != CR_OK)
    return status;
  if (request.split != CR_TRAIN ||
      (request.kind != CR_SOURCE && request.kind != CR_ACTIVITY))
    return CR_INVALID;
  for (size_t i = 0; i < evidence_count; ++i) {
    for (size_t earlier = 0; earlier < i; ++earlier)
      if (evidence_ids[i] == evidence_ids[earlier])
        return CR_INVALID;
    status = find(context, evidence_ids[i], evidence + i);
    if (status != CR_OK)
      return status;
    if (evidence[i].split != CR_TRAIN || evidence[i].kind > CR_ACTIVITY)
      return CR_INVALID;
  }
  frame(&writer, &request, evidence, evidence_count, prefix, prefix_length);
  if (writer.status != CR_OK)
    return writer.status;
  const size_t length = writer.length;
  const size_t output_span = capacity < length ? capacity : length;
  if (overlaps(required, sizeof(*required), evidence_ids,
               evidence_count * sizeof(*evidence_ids)) ||
      overlaps(required, sizeof(*required), prefix, prefix_length) ||
      record_overlap(required, sizeof(*required), &request))
    return CR_INVALID;
  for (size_t i = 0; i < evidence_count; ++i)
    if (record_overlap(required, sizeof(*required), evidence + i))
      return CR_INVALID;
  if (out && (overlaps(out, output_span, required, sizeof(*required)) ||
              overlaps(out, output_span, evidence_ids,
                       evidence_count * sizeof(*evidence_ids)) ||
              overlaps(out, output_span, prefix, prefix_length) ||
              record_overlap(out, output_span, &request)))
    return CR_INVALID;
  for (size_t i = 0; out && i < evidence_count; ++i)
    if (record_overlap(out, output_span, evidence + i))
      return CR_INVALID;
  *required = length;
  if (length > CR_TEXT_MAX_FRAME_BYTES || capacity < length)
    return CR_LIMIT;
  writer.bytes = out;
  writer.length = 0;
  frame(&writer, &request, evidence, evidence_count, prefix, prefix_length);
  return writer.status;
}
