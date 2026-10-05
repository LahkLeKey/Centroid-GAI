#include "context_internal.h"
#include <stdlib.h>
#include <string.h>

#define C_CONTEXT_CODEC_VERSION 1u

static void put_string(c_writer *writer, const char *text) {
  size_t length = strlen(text);
  c_put_u64(writer, (uint64_t)length);
  c_put_bytes(writer, text, length);
}

static void put_record(c_writer *writer, const c_record *record) {
  c_put_u64(writer, record->id);
  c_put_u64(writer, record->version);
  c_put_u32(writer, (uint32_t)record->kind);
  c_put_u32(writer, (uint32_t)record->split);
  c_put_u32(writer, (uint32_t)record->current);
  put_string(writer, record->path);
  put_string(writer, record->attribution);
  c_put_u64(writer, (uint64_t)record->length);
  c_put_bytes(writer, record->bytes, record->length);
  c_put_bytes(writer, record->digest, C_DIGEST_HEX - 1u);
}

c_status c_context_pack(const c_context *context, unsigned char **bytes,
                        size_t *length) {
  if (context == NULL || bytes == NULL || length == NULL || *bytes != NULL)
    return C_INVALID;
  c_status status = c_context_valid(context);
  if (status != C_OK)
    return status;
  c_writer writer = {0};
  c_put_u32(&writer, C_CONTEXT_CODEC_VERSION);
  c_put_u64(&writer, (uint64_t)context->count);
  c_put_u64(&writer, (uint64_t)context->byte_count);
  c_put_u64(&writer, context->next_id);
  for (size_t i = 0; i < context->count; ++i)
    put_record(&writer, &context->records[i]->view);
  if (writer.status != C_OK) {
    free(writer.data);
    return writer.status;
  }
  *bytes = writer.data;
  *length = writer.length;
  return C_OK;
}

static char *get_string(c_reader *reader) {
  uint64_t length = c_get_u64(reader);
  if (reader->status != C_OK)
    return NULL;
  if (length == 0 || length > C_CONTEXT_PROVENANCE_BYTES) {
    reader->status = C_CORRUPT;
    return NULL;
  }
  char *text = malloc((size_t)length + 1);
  if (text == NULL) {
    reader->status = C_NOMEM;
    return NULL;
  }
  c_get_bytes(reader, text, (size_t)length);
  text[length] = '\0';
  if (reader->status == C_OK && memchr(text, '\0', (size_t)length) != NULL)
    reader->status = C_CORRUPT;
  return text;
}

static unsigned char *get_content(c_reader *reader, size_t *length) {
  uint64_t size = c_get_u64(reader);
  if (reader->status != C_OK)
    return NULL;
  if (size > C_CONTEXT_RECORD_BYTES) {
    reader->status = C_CORRUPT;
    return NULL;
  }
  unsigned char *bytes = malloc(size != 0 ? (size_t)size : 1);
  if (bytes == NULL) {
    reader->status = C_NOMEM;
    return NULL;
  }
  *length = (size_t)size;
  c_get_bytes(reader, bytes, *length);
  return bytes;
}

static c_status admit_decoded(c_context *context, const c_record *record) {
  uint64_t id = 0;
  c_status status =
      c_context_admit(context, record->kind, record->split, record->path,
                      record->attribution, record->bytes, record->length, &id);
  if (status != C_OK)
    return status == C_NOMEM ? status : C_CORRUPT;
  const c_record *decoded = &context->records[context->count - 1]->view;
  return id == record->id && decoded->id == record->id &&
                 decoded->version == record->version &&
                 strcmp(decoded->digest, record->digest) == 0
             ? C_OK
             : C_CORRUPT;
}

static c_status get_record(c_reader *reader, c_context *context,
                           unsigned char *current) {
  c_record record = {0};
  record.id = c_get_u64(reader);
  record.version = c_get_u64(reader);
  uint32_t kind = c_get_u32(reader), split = c_get_u32(reader),
           live = c_get_u32(reader);
  if (reader->status != C_OK)
    return reader->status;
  if (kind > C_AUDIT || split > C_HOLDOUT || live > 1) {
    reader->status = C_CORRUPT;
    return C_CORRUPT;
  }
  record.kind = (c_record_kind)kind;
  record.split = (c_split)split;
  record.path = get_string(reader);
  record.attribution = get_string(reader);
  record.bytes = get_content(reader, &record.length);
  c_get_bytes(reader, record.digest, C_DIGEST_HEX - 1u);
  record.digest[C_DIGEST_HEX - 1u] = '\0';
  c_status status =
      reader->status == C_OK ? admit_decoded(context, &record) : reader->status;
  *current = (unsigned char)live;
  free((void *)record.path);
  free((void *)record.attribution);
  free((void *)record.bytes);
  return status;
}

static c_status decode_records(c_reader *reader, c_context *context,
                               size_t count) {
  /* Admissions reconstruct version chains; only then can final current flags
   * be compared with the serialized history without trusting those flags. */
  unsigned char current[C_CONTEXT_RECORD_LIMIT];
  for (size_t i = 0; i < count; ++i) {
    c_status status = get_record(reader, context, &current[i]);
    if (status != C_OK)
      return status;
  }
  if (context->count != count || reader->offset != reader->length)
    return C_CORRUPT;
  for (size_t i = 0; i < count; ++i)
    if (context->records[i]->view.current != current[i])
      return C_CORRUPT;
  return c_context_valid(context);
}

c_status c_context_unpack(const unsigned char *bytes, size_t length,
                          c_context **out) {
  if (bytes == NULL || out == NULL || *out != NULL)
    return C_INVALID;
  c_reader reader = {bytes, length, 0, C_OK};
  uint32_t version = c_get_u32(&reader);
  uint64_t count = c_get_u64(&reader), total = c_get_u64(&reader),
           next_id = c_get_u64(&reader);
  if (reader.status != C_OK || version != C_CONTEXT_CODEC_VERSION ||
      count > C_CONTEXT_RECORD_LIMIT || total > C_CONTEXT_TOTAL_BYTES ||
      next_id != count + 1)
    return C_CORRUPT;
  c_context *candidate = NULL;
  c_status status = c_context_create(&candidate);
  if (status == C_OK)
    status = decode_records(&reader, candidate, (size_t)count);
  if (status == C_OK && candidate->byte_count != total)
    status = C_CORRUPT;
  if (status != C_OK) {
    c_context_destroy(candidate);
    return status;
  }
  *out = candidate;
  return C_OK;
}

c_status c_context_save(const c_context *context, const char *path) {
  unsigned char *bytes = NULL;
  size_t length = 0;
  if (path == NULL || path[0] == '\0')
    return C_INVALID;
  c_status status = c_context_pack(context, &bytes, &length);
  if (status == C_OK)
    status = c_envelope_write(path, "CONTEXT1", bytes, length);
  free(bytes);
  return status;
}

c_status c_context_load(const char *path, c_context **out) {
  unsigned char *bytes = NULL;
  size_t length = 0;
  if (path == NULL || path[0] == '\0' || out == NULL || *out != NULL)
    return C_INVALID;
  c_status status = c_envelope_read(path, "CONTEXT1", &bytes, &length);
  if (status == C_OK)
    status = c_context_unpack(bytes, length, out);
  free(bytes);
  return status;
}
