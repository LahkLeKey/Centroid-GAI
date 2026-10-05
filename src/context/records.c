#include "context_internal.h"
#include <stdlib.h>
#include <string.h>

int c_context_metadata(c_record_kind kind, c_split split) {
  if (kind < C_SOURCE || kind > C_AUDIT || split < C_TRAIN || split > C_HOLDOUT)
    return 0;
  return (kind != C_DEVELOPMENT || split == C_DEV) &&
         (kind != C_AUDIT || split == C_HOLDOUT) &&
         (kind != C_TRAIN_MEASUREMENT || split == C_TRAIN);
}

static size_t provenance_length(const char *text) {
  size_t length = 0;
  if (text == NULL)
    return 0;
  while (length <= C_CONTEXT_PROVENANCE_BYTES && text[length] != '\0')
    ++length;
  return length <= C_CONTEXT_PROVENANCE_BYTES ? length : 0;
}

static c_status provenance_status(const char *text) {
  size_t length = 0;
  if (text == NULL || text[0] == '\0')
    return C_INVALID;
  while (length <= C_CONTEXT_PROVENANCE_BYTES && text[length] != '\0')
    ++length;
  return length > C_CONTEXT_PROVENANCE_BYTES ? C_LIMIT : C_OK;
}

int c_context_family(const c_record *a, const c_record *b) {
  return a->kind == b->kind && a->split == b->split &&
         strcmp(a->path, b->path) == 0 &&
         strcmp(a->attribution, b->attribution) == 0;
}

static int split_conflict(const c_record *a, const c_record *b) {
  if (a->split == b->split)
    return 0;
  /* Retained historical bytes also remain quarantined across split boundaries.
   */
  return (strcmp(a->path, b->path) == 0 &&
          strcmp(a->attribution, b->attribution) == 0) ||
         (a->length != 0 && a->length == b->length &&
          memcmp(a->bytes, b->bytes, a->length) == 0);
}

static void record_destroy(c_owned_record *record) {
  if (record != NULL) {
    free((void *)record->view.path);
    free((void *)record->view.attribution);
    free((void *)record->view.bytes);
    free(record);
  }
}

c_status c_context_create(c_context **out) {
  c_context *context;
  if (out == NULL || *out != NULL)
    return C_INVALID;
  context = calloc(1, sizeof(*context));
  if (context == NULL)
    return C_NOMEM;
  context->next_id = 1;
  *out = context;
  return C_OK;
}

void c_context_destroy(c_context *context) {
  if (context != NULL) {
    for (size_t i = 0; i < context->count; ++i)
      record_destroy(context->records[i]);
    free(context->records);
    free(context);
  }
}

static char *copy_string(const char *text, size_t length) {
  char *copy = malloc(length + 1);
  if (copy != NULL)
    memcpy(copy, text, length + 1);
  return copy;
}

static c_status prepare_record(const c_record *input, c_owned_record **out) {
  c_owned_record *record = calloc(1, sizeof(*record));
  if (record == NULL)
    return C_NOMEM;
  record->view = *input;
  record->view.path = copy_string(input->path, strlen(input->path));
  record->view.attribution =
      copy_string(input->attribution, strlen(input->attribution));
  record->view.bytes = malloc(input->length != 0 ? input->length : 1);
  if (record->view.path == NULL || record->view.attribution == NULL ||
      record->view.bytes == NULL) {
    record_destroy(record);
    return C_NOMEM;
  }
  if (input->length != 0)
    memcpy((void *)record->view.bytes, input->bytes, input->length);
  c_hash(input->bytes, input->length, record->view.digest);
  c_status status = c_encode(input->bytes, input->length, record->centroid);
  if (status != C_OK) {
    record_destroy(record);
    return status;
  }
  *out = record;
  return C_OK;
}

static c_status find_current(const c_context *context, const c_record *input,
                             size_t *current) {
  *current = context->count;
  for (size_t i = 0; i < context->count; ++i) {
    const c_record *record = &context->records[i]->view;
    if (split_conflict(record, input))
      return C_INVALID;
    if (record->current && c_context_family(record, input))
      *current = i;
  }
  return C_OK;
}

static c_status append_record(c_context *context, c_record *input,
                              size_t previous, uint64_t *id) {
  c_owned_record *record = NULL;
  c_status status = prepare_record(input, &record);
  if (status != C_OK)
    return status;
  c_owned_record **records =
      realloc(context->records, (context->count + 1) * sizeof(*records));
  if (records == NULL) {
    record_destroy(record);
    return C_NOMEM;
  }
  context->records = records;
  if (previous != context->count)
    context->records[previous]->view.current = 0;
  context->records[context->count++] = record;
  context->byte_count += input->length;
  ++context->next_id;
  *id = input->id;
  return C_OK;
}

c_status c_context_admit(c_context *context, c_record_kind kind, c_split split,
                         const char *path, const char *attribution,
                         const unsigned char *bytes, size_t length,
                         uint64_t *id) {
  size_t previous;
  uint64_t discarded_id;
  c_record input = {0};
  if (context == NULL || !c_context_metadata(kind, split) ||
      (bytes == NULL && length != 0))
    return C_INVALID;
  if (id == NULL)
    id = &discarded_id;
  c_status status = provenance_status(path);
  if (status != C_OK || (status = provenance_status(attribution)) != C_OK)
    return status;
  if (length > C_CONTEXT_RECORD_BYTES)
    return C_LIMIT;
  input = (c_record){context->next_id, 1,     kind,   split, path,
                     attribution,      bytes, length, {0},   1};
  status = find_current(context, &input, &previous);
  if (status != C_OK)
    return status;
  if (previous != context->count) {
    const c_record *current = &context->records[previous]->view;
    if (current->length == length &&
        (length == 0 || memcmp(current->bytes, bytes, length) == 0)) {
      *id = current->id;
      return C_OK;
    }
    input.version = current->version + 1;
  }
  if (context->count == C_CONTEXT_RECORD_LIMIT ||
      length > C_CONTEXT_TOTAL_BYTES - context->byte_count)
    return C_LIMIT;
  return append_record(context, &input, previous, id);
}

size_t c_context_count(const c_context *context) {
  return context == NULL ? 0 : context->count;
}

c_status c_context_record(const c_context *context, size_t index,
                          c_record *out) {
  if (context == NULL || out == NULL)
    return C_INVALID;
  if (index >= context->count)
    return C_NOT_FOUND;
  *out = context->records[index]->view;
  return C_OK;
}

c_status c_context_admit_file(c_context *context, c_record_kind kind,
                              c_split split, const char *path,
                              const char *attribution, uint64_t *id) {
  unsigned char *bytes = NULL;
  size_t length = 0;
  if (context == NULL || !c_context_metadata(kind, split))
    return C_INVALID;
  c_status status = provenance_status(path);
  if (status != C_OK || (status = provenance_status(attribution)) != C_OK)
    return status;
  char absolute[C_CONTEXT_PROVENANCE_BYTES + 1u];
  status = c_context_absolute(path, absolute);
  if (status != C_OK)
    return status;
  if (split == C_TRAIN && c_context_reserved_training_path(absolute))
    return C_INVALID;
  status = c_context_read_regular(absolute, &bytes, &length);
  if (status == C_OK)
    status = c_context_admit(context, kind, split, path, attribution, bytes,
                             length, id);
  free(bytes);
  return status;
}

static int record_history_valid(const c_context *context, size_t index) {
  const c_record *record = &context->records[index]->view, *previous = NULL;
  int current = 1;
  for (size_t i = 0; i < context->count; ++i) {
    const c_record *other = &context->records[i]->view;
    if (i < index && split_conflict(record, other))
      return 0;
    if (!c_context_family(record, other))
      continue;
    if (i < index)
      previous = other;
    else if (i > index)
      current = 0;
  }
  if (record->current != current ||
      record->version != (previous == NULL ? 1 : previous->version + 1))
    return 0;
  return previous == NULL || previous->length != record->length ||
         (record->length != 0 &&
          memcmp(previous->bytes, record->bytes, record->length) != 0);
}

c_status c_context_valid(const c_context *context) {
  size_t total = 0;
  if (context == NULL || context->count > C_CONTEXT_RECORD_LIMIT ||
      context->next_id != context->count + 1 ||
      (context->count != 0 && context->records == NULL))
    return C_CORRUPT;
  for (size_t i = 0; i < context->count; ++i) {
    const c_owned_record *owned = context->records[i];
    if (owned == NULL)
      return C_CORRUPT;
    const c_record *record = &owned->view;
    char digest[C_DIGEST_HEX];
    double features[C_FEATURES];
    if (record->id != i + 1 ||
        !c_context_metadata(record->kind, record->split) ||
        provenance_length(record->path) == 0 ||
        provenance_length(record->attribution) == 0 || record->bytes == NULL ||
        record->length > C_CONTEXT_RECORD_BYTES ||
        record->length > C_CONTEXT_TOTAL_BYTES - total ||
        !record_history_valid(context, i))
      return C_CORRUPT;
    c_hash(record->bytes, record->length, digest);
    if (strcmp(digest, record->digest) != 0 ||
        c_encode(record->bytes, record->length, features) != C_OK ||
        memcmp(features, owned->centroid, sizeof(features)) != 0)
      return C_CORRUPT;
    total += record->length;
  }
  return total == context->byte_count ? C_OK : C_CORRUPT;
}
