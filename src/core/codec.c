#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(double) == 8, "canonical codec requires binary64 double");

#define C_MAX_BUNDLE (128u * 1024u * 1024u)
void c_put_bytes(c_writer *w, const void *data, size_t length) {
  size_t needed, capacity;
  unsigned char *grown;
  if (!w || w->status != C_OK)
    return;
  if ((!data && length) || w->length > w->capacity ||
      w->capacity > C_MAX_BUNDLE || (w->capacity && !w->data) ||
      length > C_MAX_BUNDLE - w->length) {
    w->status = C_LIMIT;
    return;
  }
  needed = w->length + length;
  if (needed > w->capacity) {
    capacity = w->capacity ? w->capacity : 1024;
    while (capacity < needed)
      capacity *= 2;
    grown = (unsigned char *)realloc(w->data, capacity);
    if (!grown) {
      w->status = C_NOMEM;
      return;
    }
    w->data = grown;
    w->capacity = capacity;
  }
  if (length)
    memcpy(w->data + w->length, data, length);
  w->length = needed;
}
void c_put_u32(c_writer *w, uint32_t v) {
  unsigned char b[4];
  unsigned i;
  for (i = 0; i < 4; i++)
    b[i] = (unsigned char)(v >> (8 * i));
  c_put_bytes(w, b, 4);
}
void c_put_u64(c_writer *w, uint64_t v) {
  unsigned char b[8];
  unsigned i;
  for (i = 0; i < 8; i++)
    b[i] = (unsigned char)(v >> (8 * i));
  c_put_bytes(w, b, 8);
}
void c_put_double(c_writer *w, double v) {
  uint64_t bits = 0;
  if (sizeof(v) != 8 || !isfinite(v)) {
    w->status = C_INVALID;
    return;
  }
  memcpy(&bits, &v, 8);
  c_put_u64(w, bits);
}
void c_get_bytes(c_reader *r, void *out, size_t length) {
  if (!r || r->status != C_OK)
    return;
  if ((!out && length) || r->offset > r->length ||
      length > r->length - r->offset) {
    r->status = C_CORRUPT;
    return;
  }
  if (length)
    memcpy(out, r->data + r->offset, length);
  r->offset += length;
}
uint32_t c_get_u32(c_reader *r) {
  unsigned char b[4] = {0};
  uint32_t v = 0;
  unsigned i;
  c_get_bytes(r, b, 4);
  for (i = 0; i < 4; i++)
    v |= (uint32_t)b[i] << (8 * i);
  return v;
}
uint64_t c_get_u64(c_reader *r) {
  unsigned char b[8] = {0};
  uint64_t v = 0;
  unsigned i;
  c_get_bytes(r, b, 8);
  for (i = 0; i < 8; i++)
    v |= (uint64_t)b[i] << (8 * i);
  return v;
}
double c_get_double(c_reader *r) {
  uint64_t bits = c_get_u64(r);
  double v = 0;
  memcpy(&v, &bits, 8);
  if (!isfinite(v))
    r->status = C_CORRUPT;
  return v;
}
c_status c_envelope_write(const char *path, const char magic[8],
                          const unsigned char *data, size_t length) {
  c_writer w = {0};
  char digest[C_DIGEST_HEX];
  c_status s;
  if (!path || !magic || (!data && length))
    return C_INVALID;
  c_hash(data, length, digest);
  c_put_bytes(&w, magic, 8);
  c_put_u64(&w, length);
  c_put_bytes(&w, digest, 64);
  c_put_bytes(&w, data, length);
  s = w.status == C_OK ? c_write_atomic(path, w.data, w.length) : w.status;
  free(w.data);
  return s;
}
c_status c_envelope_read(const char *path, const char magic[8],
                         unsigned char **data, size_t *length) {
  unsigned char *raw = NULL, *copy;
  size_t size = 0;
  uint64_t payload;
  c_reader r;
  char found[8], expected[65] = {0}, actual[65];
  c_status s;
  if (!path || !magic || !data || !length)
    return C_INVALID;
  s = c_read_file(path, &raw, &size);
  if (s != C_OK)
    return s;
  r.data = raw;
  r.length = size;
  r.offset = 0;
  r.status = C_OK;
  c_get_bytes(&r, found, 8);
  payload = c_get_u64(&r);
  c_get_bytes(&r, expected, 64);
  if (r.status != C_OK || memcmp(found, magic, 8) || payload > C_MAX_BUNDLE ||
      payload != size - r.offset) {
    free(raw);
    return C_CORRUPT;
  }
  c_hash(raw + r.offset, (size_t)payload, actual);
  if (memcmp(actual, expected, 64)) {
    free(raw);
    return C_CORRUPT;
  }
  copy = (unsigned char *)malloc(payload ? (size_t)payload : 1);
  if (!copy) {
    free(raw);
    return C_NOMEM;
  }
  if (payload)
    memcpy(copy, raw + r.offset, (size_t)payload);
  free(raw);
  *data = copy;
  *length = (size_t)payload;
  return C_OK;
}
