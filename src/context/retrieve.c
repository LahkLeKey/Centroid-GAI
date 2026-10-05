#include "context_internal.h"
#include <math.h>
#include <string.h>

static int word_byte(unsigned char byte) {
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') || byte == '_';
}

static int next_word(const unsigned char *bytes, size_t length,
                     size_t *position, size_t *begin, size_t *size) {
  while (*position < length && !word_byte(bytes[*position]))
    ++*position;
  *begin = *position;
  while (*position < length && word_byte(bytes[*position]))
    ++*position;
  *size = *position - *begin;
  return *size != 0;
}

static int supported_word(const unsigned char *bytes, size_t size) {
  if (size < 3)
    return 0;
  for (size_t i = 0; i < size; ++i)
    if ((bytes[i] >= 'a' && bytes[i] <= 'z') ||
        (bytes[i] >= 'A' && bytes[i] <= 'Z'))
      return 1;
  return 0;
}

static int has_word(const unsigned char *bytes, size_t length,
                    const unsigned char *word, size_t size) {
  size_t position = 0, begin = 0, found_size = 0;
  while (next_word(bytes, length, &position, &begin, &found_size))
    if (size == found_size && memcmp(word, bytes + begin, size) == 0)
      return 1;
  return 0;
}

static int overlap(const unsigned char *query, size_t length,
                   const c_record *record) {
  size_t position = 0, begin = 0, size = 0;
  while (next_word(query, length, &position, &begin, &size))
    if (supported_word(query + begin, size) &&
        (has_word(record->bytes, record->length, query + begin, size) ||
         has_word((const unsigned char *)record->path, strlen(record->path),
                  query + begin, size)))
      return 1;
  return 0;
}

static double similarity(const double a[C_FEATURES],
                         const double b[C_FEATURES]) {
  double product = 0, first = 0, second = 0;
  for (size_t i = 0; i < C_FEATURES; ++i) {
    product += a[i] * b[i];
    first += a[i] * a[i];
    second += b[i] * b[i];
  }
  if (first == 0 || second == 0)
    return 0;
  double cosine = product / sqrt(first * second);
  return fmax(0, fmin(1, (cosine + 1) * 0.5));
}

static c_status retrieve_filtered(const c_context *context,
                                  const unsigned char *query, size_t length,
                                  c_record *out, double *score, int kind) {
  double features[C_FEATURES], best_score = -1;
  const c_owned_record *best = NULL;
  if (context == NULL || out == NULL || score == NULL ||
      (query == NULL && length != 0))
    return C_INVALID;
  if (length > C_CONTEXT_RECORD_BYTES)
    return C_LIMIT;
  if (length == 0)
    return C_NOT_FOUND;
  c_status status = c_encode(query, length, features);
  if (status != C_OK)
    return status;
  for (size_t i = 0; i < context->count; ++i) {
    const c_owned_record *record = context->records[i];
    if (!record->view.current || record->view.split != C_TRAIN ||
        record->view.kind > C_ACTIVITY || record->view.length == 0 ||
        (kind >= 0 && (int)record->view.kind != kind) ||
        !overlap(query, length, &record->view))
      continue;
    double candidate = similarity(features, record->centroid);
    if (candidate > best_score) {
      best = record;
      best_score = candidate;
    }
  }
  if (best == NULL)
    return C_NOT_FOUND;
  *out = best->view;
  *score = best_score;
  return C_OK;
}

c_status c_context_retrieve(const c_context *context,
                            const unsigned char *query, size_t length,
                            c_record *out, double *score) {
  return retrieve_filtered(context, query, length, out, score, -1);
}

c_status c_context_retrieve_kind(const c_context *context, c_record_kind kind,
                                 const unsigned char *query, size_t length,
                                 c_record *out, double *score) {
  if ((unsigned)kind > (unsigned)C_ACTIVITY)
    return C_INVALID;
  return retrieve_filtered(context, query, length, out, score, (int)kind);
}
