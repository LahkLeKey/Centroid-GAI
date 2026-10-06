#include "runtime_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

uint32_t CR_CALL cr_version(void) { return CR_API_VERSION; }

const char *CR_CALL cr_status_string(cr_status status) {
  static const char *const names[] = {"ok",
                                      "invalid argument",
                                      "capacity limit",
                                      "I/O failure",
                                      "out of memory",
                                      "corrupt state",
                                      "no supported evidence",
                                      "deferred",
                                      "unsupported schema"};
  return status < sizeof(names) / sizeof(names[0]) ? names[status]
                                                   : "unknown status";
}

static uint64_t position_mix(uint64_t value) {
  value = (value ^ (value >> 30u)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27u)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31u);
}

cr_status CR_CALL cr_encode(const unsigned char *bytes, size_t length,
                            double out[CR_FEATURES]) {
  double features[CR_FEATURES] = {0};
  uint64_t sequence = UINT64_C(14695981039346656037);
  double squared = 0.0;
  if (!out || (!bytes && length))
    return CR_INVALID;
  if (length > CR_MAX_INPUT_BYTES)
    return CR_LIMIT;
  features[0] = 1.0;
  for (size_t i = 0u; i < length; ++i) {
    const uint64_t code =
        position_mix(((uint64_t)i << 8u) | (uint64_t)bytes[i]);
    const size_t bucket = 1u + (size_t)(code % (CR_FEATURES - 1u));
    const double amplitude = 1.0 + (double)bytes[i] / 255.0;
    features[bucket] += (code >> 63u) ? amplitude : -amplitude;
    sequence = (sequence ^ (uint64_t)bytes[i]) * UINT64_C(1099511628211);
  }
  if (length) {
    const uint64_t final = position_mix(sequence ^ (uint64_t)length);
    features[1] += (double)(final >> 11u) / 4503599627370496.0 - 1.0;
  }
  for (size_t i = 0u; i < CR_FEATURES; ++i)
    squared += features[i] * features[i];
  const double scale = 1.0 / sqrt(squared);
  for (size_t i = 0u; i < CR_FEATURES; ++i)
    out[i] = features[i] * scale;
  return CR_OK;
}

static int terminated(const char *text, size_t capacity) {
  return memchr(text, 0, capacity) != NULL;
}

static int digest_valid(const char text[CR_DIGEST_HEX], int empty_allowed) {
  if (empty_allowed && !text[0])
    return terminated(text, CR_DIGEST_HEX);
  if (text[64] != 0)
    return 0;
  for (size_t i = 0; i < 64; ++i)
    if (!((text[i] >= '0' && text[i] <= '9') ||
          (text[i] >= 'a' && text[i] <= 'f')))
      return 0;
  return 1;
}

int cr_metadata_valid(const cr_model_metadata *metadata) {
  if (!metadata || metadata->struct_size != sizeof(*metadata) ||
      metadata->api_version != CR_API_VERSION || metadata->reserved ||
      metadata->qualification > CR_QUALIFIED ||
      !digest_valid(metadata->model_digest, 0) ||
      !digest_valid(metadata->parent_digest, 1) ||
      !digest_valid(metadata->checkpoint_digest, 1) ||
      !digest_valid(metadata->qualification_digest, 1) ||
      !terminated(metadata->task_profile, CR_ID_BYTES) ||
      !metadata->task_profile[0] ||
      !terminated(metadata->observation_schema, CR_ID_BYTES) ||
      !metadata->observation_schema[0] ||
      !terminated(metadata->action_catalog, CR_ID_BYTES) ||
      !metadata->action_catalog[0] ||
      !terminated(metadata->provenance, CR_REFERENCE_BYTES) ||
      !metadata->provenance[0] ||
      !terminated(metadata->qualification_reference, CR_REFERENCE_BYTES))
    return 0;
  if (metadata->qualification == CR_QUALIFIED &&
      (!metadata->qualification_digest[0] ||
       !metadata->qualification_reference[0]))
    return 0;
  if (metadata->qualification == CR_EXPERIMENTAL &&
      (metadata->qualification_digest[0] ||
       metadata->qualification_reference[0]))
    return 0;
  return 1;
}

int cr_model_values_valid(const cr_model *model) {
  if (!model || !model->groups || model->groups > CR_MAX_GROUPS ||
      model->shared_enabled > 1u)
    return 0;
  for (size_t j = 0; j < CR_FEATURES; ++j)
    if (!isfinite(model->shared_scale[j]) ||
        (!model->shared_enabled && model->shared_scale[j] != 1.0))
      return 0;
  for (uint32_t g = 0; g < model->groups; ++g) {
    const cr_expert_values *expert = &model->experts[g];
    if (!expert->uid)
      return 0;
    for (uint32_t previous = 0; previous < g; ++previous)
      if (model->experts[previous].uid == expert->uid)
        return 0;
    for (size_t j = 0; j < CR_FEATURES; ++j)
      if (!isfinite(expert->centroid[j]))
        return 0;
    for (size_t a = 0; a < CR_CODE_ACTIONS; ++a)
      for (size_t j = 0; j < CR_FEATURES; ++j)
        if (!isfinite(expert->code[a][j]))
          return 0;
    for (size_t a = 0; a < CR_TEXT_ACTIONS; ++a)
      for (size_t j = 0; j < CR_FEATURES; ++j)
        if (!isfinite(expert->text[a][j]))
          return 0;
  }
  return 1;
}

cr_status CR_CALL cr_model_create_values(const cr_model_values *values,
                                         cr_model **out) {
  if (!values || !out || *out || values->struct_size != sizeof(*values) ||
      values->api_version != CR_API_VERSION || !values->groups ||
      values->groups > CR_MAX_GROUPS || values->shared_enabled > 1u ||
      !values->owner_uids || !values->shared_scale || !values->centroids ||
      !values->code || !values->text || !values->metadata)
    return CR_INVALID;
  cr_model *model = calloc(1, sizeof(*model));
  if (!model)
    return CR_NOMEM;
  model->groups = values->groups;
  model->shared_enabled = values->shared_enabled;
  model->metadata = *values->metadata;
  memset(model->metadata.model_digest, '0', 64);
  model->metadata.model_digest[64] = 0;
  if (!cr_metadata_valid(&model->metadata)) {
    free(model);
    return CR_INVALID;
  }
  memcpy(model->shared_scale, values->shared_scale,
         sizeof(model->shared_scale));
  for (uint32_t g = 0; g < model->groups; ++g) {
    cr_expert_values *expert = &model->experts[g];
    expert->uid = values->owner_uids[g];
    memcpy(expert->centroid, values->centroids + g * CR_FEATURES,
           sizeof(expert->centroid));
    memcpy(expert->code, values->code + g * CR_CODE_ACTIONS * CR_FEATURES,
           sizeof(expert->code));
    memcpy(expert->text, values->text + g * CR_TEXT_ACTIONS * CR_FEATURES,
           sizeof(expert->text));
  }
  const cr_status status = cr_model_identify(model);
  if (status != CR_OK) {
    free(model);
    return status;
  }
  *out = model;
  return CR_OK;
}

static int overlaps(const void *first, size_t first_length,
                     const void *second, size_t second_length) {
  const uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
  if (first_length > UINTPTR_MAX - a || second_length > UINTPTR_MAX - b)
    return 1;
  return a < b + second_length && b < a + first_length;
}

cr_status CR_CALL cr_model_predict_with_scratch(
    const cr_model *model, cr_head head, const double input[CR_FEATURES],
    uint32_t eligible, const double mass[CR_MAX_GROUPS], double *scratch,
    size_t scratch_doubles, double *probabilities, size_t capacity) {
  const uint32_t actions = head == CR_CODE   ? CR_CODE_ACTIONS
                           : head == CR_TEXT ? CR_TEXT_ACTIONS
                                             : 0u;
  double maximum = -INFINITY, sum = 0.0;
  uint32_t active = 0;
  if (!model || !input || !mass || !scratch || !probabilities || !actions ||
      !model->groups || model->groups > CR_MAX_GROUPS ||
      (eligible >> model->groups))
    return CR_INVALID;
  if (capacity < actions || scratch_doubles < CR_PREDICT_SCRATCH_DOUBLES)
    return CR_LIMIT;
  const size_t scratch_bytes = CR_PREDICT_SCRATCH_DOUBLES * sizeof(double);
  if (overlaps(scratch, scratch_bytes, input, CR_FEATURES * sizeof(double)) ||
      overlaps(scratch, scratch_bytes, mass, CR_MAX_GROUPS * sizeof(double)) ||
      overlaps(scratch, scratch_bytes, probabilities, actions * sizeof(double)) ||
      overlaps(scratch, scratch_bytes, model, sizeof(*model)))
    return CR_INVALID;
  double *encoded = scratch;
  double *scores = encoded + CR_FEATURES;
  double *route = scores + CR_MAX_GROUPS;
  double *mixed = route + CR_MAX_GROUPS;
  double *logits = mixed + CR_TEXT_ACTIONS;
  memset(scores, 0, 2u * CR_MAX_GROUPS * sizeof(double));
  memset(mixed, 0, CR_TEXT_ACTIONS * sizeof(double));
  for (size_t j = 0; j < CR_FEATURES; ++j) {
    if (!isfinite(input[j]))
      return CR_INVALID;
    encoded[j] =
        input[j] * (model->shared_enabled ? model->shared_scale[j] : 1.0);
    if (!isfinite(encoded[j]))
      return CR_INVALID;
  }
  for (uint32_t g = 0; g < model->groups; ++g) {
    if (!isfinite(mass[g]) || mass[g] < 0.0)
      return CR_INVALID;
    if (!(eligible & (1u << g)) || mass[g] == 0.0)
      continue;
    double distance = 0.0;
    for (size_t j = 0; j < CR_FEATURES; ++j) {
      const double delta = encoded[j] - model->experts[g].centroid[j];
      distance += delta * delta;
    }
    scores[g] = log(mass[g]) - distance;
    if (!isfinite(scores[g]))
      return CR_INVALID;
    active |= 1u << g;
    if (scores[g] > maximum)
      maximum = scores[g];
  }
  if (!active)
    return CR_DEFERRED;
  for (uint32_t g = 0; g < model->groups; ++g)
    if (active & (1u << g))
      sum += exp(scores[g] - maximum);
  const double log_sum = log(sum);
  for (uint32_t g = 0; g < model->groups; ++g)
    if (active & (1u << g))
      route[g] = exp(scores[g] - maximum - log_sum);
  for (uint32_t g = 0; g < model->groups; ++g)
    if (active & (1u << g)) {
      maximum = -INFINITY;
      sum = 0.0;
      for (uint32_t a = 0; a < actions; ++a) {
        const double *row = head == CR_CODE ? model->experts[g].code[a]
                                            : model->experts[g].text[a];
        double value = 0.0;
        for (size_t j = 0; j < CR_FEATURES; ++j)
          value += row[j] * encoded[j];
        if (!isfinite(value))
          return CR_INVALID;
        logits[a] = value;
        if (value > maximum)
          maximum = value;
      }
      for (uint32_t a = 0; a < actions; ++a) {
        if (!isfinite(logits[a] - maximum))
          return CR_INVALID;
        sum += exp(logits[a] - maximum);
      }
      const double log_expert_sum = log(sum);
      for (uint32_t a = 0; a < actions; ++a)
        mixed[a] += route[g] * exp(logits[a] - maximum - log_expert_sum);
    }
  memcpy(probabilities, mixed, actions * sizeof(*probabilities));
  return CR_OK;
}

cr_status CR_CALL cr_model_predict(const cr_model *model, cr_head head,
                                   const double input[CR_FEATURES],
                                   uint32_t eligible,
                                   const double mass[CR_MAX_GROUPS],
                                   double *probabilities, size_t capacity) {
  double scratch[CR_PREDICT_SCRATCH_DOUBLES];
  return cr_model_predict_with_scratch(model, head, input, eligible, mass,
                                        scratch, CR_PREDICT_SCRATCH_DOUBLES,
                                        probabilities, capacity);
}

cr_status CR_CALL cr_model_info_get(const cr_model *model, cr_model_info *out) {
  cr_model_info candidate;
  if (!model || !out || out->struct_size != sizeof(*out) ||
      out->api_version != CR_API_VERSION || !cr_model_values_valid(model) ||
      !cr_metadata_valid(&model->metadata))
    return CR_INVALID;
  memset(&candidate, 0, sizeof(candidate));
  candidate.struct_size = sizeof(candidate);
  candidate.api_version = CR_API_VERSION;
  candidate.abi_version = CR_ABI_VERSION;
  candidate.bundle_version = CR_BUNDLE_VERSION;
  candidate.groups = model->groups;
  candidate.shared_enabled = model->shared_enabled;
  candidate.model_bytes = sizeof(*model);
  candidate.bundle_bytes = cr_model_bundle_size(model);
  candidate.parameter_count =
      CR_FEATURES + (uint64_t)model->groups * CR_FEATURES *
                        (1u + CR_CODE_ACTIONS + CR_TEXT_ACTIONS);
  for (uint32_t g = 0; g < model->groups; ++g)
    candidate.owner_uids[g] = model->experts[g].uid;
  memcpy(candidate.recipe, CR_RECIPE_ID, sizeof(CR_RECIPE_ID));
  memcpy(candidate.encoder, CR_ENCODER_ID, sizeof(CR_ENCODER_ID));
  memcpy(candidate.framing, CR_FRAMING_ID, sizeof(CR_FRAMING_ID));
  memcpy(candidate.action_schema, CR_ACTION_SCHEMA_ID,
         sizeof(CR_ACTION_SCHEMA_ID));
  candidate.metadata = model->metadata;
  *out = candidate;
  return CR_OK;
}

uint64_t CR_CALL cr_model_group_uid(const cr_model *model, uint32_t group) {
  return model && group < model->groups && model->groups <= CR_MAX_GROUPS
             ? model->experts[group].uid
             : 0;
}

void CR_CALL cr_model_destroy(cr_model *model) { free(model); }
