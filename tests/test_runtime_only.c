#include "centroid_runtime.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "runtime-only failure %s:%d: %s\n", __FILE__, __LINE__,  \
              #condition);                                                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static void workspace_contract(void) {
  const uint64_t owners[CR_MAX_GROUPS] = {91, 37, 16, 88};
  double centroids[CR_MAX_GROUPS * CR_FEATURES];
  double code[CR_MAX_GROUPS * CR_CODE_ACTIONS * CR_FEATURES];
  double *text =
      malloc(CR_MAX_GROUPS * CR_TEXT_ACTIONS * CR_FEATURES * sizeof(double));
  double scale[CR_FEATURES], input[CR_FEATURES];
  const double mass[CR_MAX_GROUPS] = {2.0, 0.5, 1.5, 0.25};
  double workspace[CR_PREDICT_SCRATCH_DOUBLES + 2u];
  double workspace_before[CR_PREDICT_SCRATCH_DOUBLES + 2u];
  double expected[CR_TEXT_ACTIONS], actual[CR_TEXT_ACTIONS];
  cr_model_metadata metadata = {0};
  cr_model *model = NULL;
  CHECK(text);
  metadata.struct_size = sizeof(metadata);
  metadata.api_version = CR_API_VERSION;
  memcpy(metadata.task_profile, CR_LEGACY_PROFILE_ID,
         sizeof(CR_LEGACY_PROFILE_ID));
  memcpy(metadata.observation_schema, CR_LEGACY_OBSERVATION_ID,
         sizeof(CR_LEGACY_OBSERVATION_ID));
  memcpy(metadata.action_catalog, CR_LEGACY_CATALOG_ID,
         sizeof(CR_LEGACY_CATALOG_ID));
  memcpy(metadata.provenance, "caller workspace conformance fixture",
         sizeof("caller workspace conformance fixture"));
  for (size_t j = 0; j < CR_FEATURES; ++j)
    scale[j] = 0.5 + (double)j / 32.0;
  for (size_t i = 0; i < CR_MAX_GROUPS * CR_FEATURES; ++i)
    centroids[i] = ((double)(i % 7u) - 3.0) / 16.0;
  for (size_t i = 0; i < CR_MAX_GROUPS * CR_CODE_ACTIONS * CR_FEATURES; ++i)
    code[i] = ((double)(i % 11u) - 5.0) / 64.0;
  for (size_t i = 0; i < CR_MAX_GROUPS * CR_TEXT_ACTIONS * CR_FEATURES; ++i)
    text[i] = ((double)(i % 13u) - 6.0) / 128.0;
  const cr_model_values values = {
      sizeof(values), CR_API_VERSION, CR_MAX_GROUPS, 1,    owners,
      scale,          centroids,      code,          text, &metadata};
  CHECK(cr_model_create_values(&values, &model) == CR_OK);
  static const unsigned char bytes[] = {'x', 0, 255, 'C', '\n'};
  CHECK(cr_encode(bytes, sizeof(bytes), input) == CR_OK);
  workspace[0] = -777.5;
  workspace[CR_PREDICT_SCRATCH_DOUBLES + 1u] = 991.25;
  for (cr_head head = CR_TEXT; head <= CR_CODE; ++head)
    for (uint32_t mask = 1; mask < (1u << CR_MAX_GROUPS); ++mask) {
      const size_t actions =
          head == CR_TEXT ? CR_TEXT_ACTIONS : CR_CODE_ACTIONS;
      CHECK(cr_model_predict(model, head, input, mask, mass, expected,
                             CR_TEXT_ACTIONS) == CR_OK);
      CHECK(cr_model_predict_with_scratch(
                model, head, input, mask, mass, workspace + 1,
                CR_PREDICT_SCRATCH_DOUBLES, actual, CR_TEXT_ACTIONS) == CR_OK);
      CHECK(!memcmp(expected, actual, actions * sizeof(double)));
      double sum = 0.0;
      for (size_t i = 0; i < actions; ++i)
        sum += actual[i];
      CHECK(fabs(sum - 1.0) < 1e-13);
      CHECK(workspace[0] == -777.5 &&
            workspace[CR_PREDICT_SCRATCH_DOUBLES + 1u] == 991.25);
    }
  memcpy(expected, actual, sizeof(expected));
  CHECK(cr_model_predict_with_scratch(model, CR_TEXT, input, 15u, mass, NULL,
                                      CR_PREDICT_SCRATCH_DOUBLES, actual,
                                      CR_TEXT_ACTIONS) == CR_INVALID);
  CHECK(!memcmp(expected, actual, sizeof(expected)));
  CHECK(cr_model_predict_with_scratch(model, CR_TEXT, input, 15u, mass,
                                      workspace + 1,
                                      CR_PREDICT_SCRATCH_DOUBLES - 1u, actual,
                                      CR_TEXT_ACTIONS) == CR_LIMIT);
  CHECK(!memcmp(expected, actual, sizeof(expected)));
  CHECK(cr_model_predict_with_scratch(model, CR_TEXT, input, 15u, mass,
                                      workspace + 1, CR_PREDICT_SCRATCH_DOUBLES,
                                      actual,
                                      CR_TEXT_ACTIONS - 1u) == CR_LIMIT);
  CHECK(!memcmp(expected, actual, sizeof(expected)));
  memcpy(workspace_before, workspace, sizeof(workspace));
  CHECK(cr_model_predict_with_scratch(model, CR_CODE, workspace + 1, 15u, mass,
                                      workspace + 1, CR_PREDICT_SCRATCH_DOUBLES,
                                      actual, CR_TEXT_ACTIONS) == CR_INVALID);
  CHECK(!memcmp(expected, actual, sizeof(expected)));
  CHECK(!memcmp(workspace_before, workspace, sizeof(workspace)));
  CHECK(cr_model_predict_with_scratch(model, CR_CODE, input, 15u, workspace + 1,
                                      workspace + 1, CR_PREDICT_SCRATCH_DOUBLES,
                                      actual, CR_TEXT_ACTIONS) == CR_INVALID);
  CHECK(!memcmp(expected, actual, sizeof(expected)));
  CHECK(cr_model_predict_with_scratch(model, CR_CODE, input, 15u, mass,
                                      workspace + 1, CR_PREDICT_SCRATCH_DOUBLES,
                                      workspace + 2,
                                      CR_CODE_ACTIONS) == CR_INVALID);
  CHECK(!memcmp(workspace_before, workspace, sizeof(workspace)));
  CHECK(cr_model_predict_with_scratch(
            model, CR_CODE, input, 15u, mass, (double *)(void *)model,
            CR_PREDICT_SCRATCH_DOUBLES, actual, CR_TEXT_ACTIONS) == CR_INVALID);
  CHECK(!memcmp(expected, actual, sizeof(expected)));
  input[0] = NAN;
  CHECK(cr_model_predict_with_scratch(model, CR_CODE, input, 15u, mass,
                                      workspace + 1, CR_PREDICT_SCRATCH_DOUBLES,
                                      actual, CR_TEXT_ACTIONS) == CR_INVALID);
  CHECK(!memcmp(expected, actual, sizeof(expected)));
  input[0] = 0;
  CHECK(cr_model_predict_with_scratch(model, CR_CODE, input, 0u, mass,
                                      workspace + 1, CR_PREDICT_SCRATCH_DOUBLES,
                                      actual, CR_TEXT_ACTIONS) == CR_DEFERRED);
  CHECK(!memcmp(expected, actual, sizeof(expected)));
  cr_model_destroy(model);
  free(text);
}

int main(void) {
  workspace_contract();
  double input[CR_FEATURES], original[CR_FEATURES];
  double centroids[CR_FEATURES] = {0};
  double code[CR_CODE_ACTIONS * CR_FEATURES] = {0};
  double *text = calloc(CR_TEXT_ACTIONS * CR_FEATURES, sizeof(double));
  double scale[CR_FEATURES], scores[CR_TEXT_ACTIONS], before[CR_TEXT_ACTIONS];
  const uint64_t owner = 47;
  const double mass[CR_MAX_GROUPS] = {1, 0, 0, 0};
  cr_model *model = NULL, *loaded = NULL;
  cr_model_metadata metadata = {0};
  cr_model_info info = {0};
  CHECK(text);
  CHECK(cr_version() == CR_API_VERSION);
  CHECK(!strcmp(cr_status_string(CR_OK), "ok"));
  CHECK(cr_encode(NULL, 0, input) == CR_OK && input[0] == 1.0);
  memcpy(original, input, sizeof(original));
  CHECK(cr_encode(NULL, 1, input) == CR_INVALID);
  CHECK(!memcmp(input, original, sizeof(input)));
  CHECK(cr_encode((const unsigned char *)"x", CR_MAX_INPUT_BYTES + 1u, input) ==
        CR_LIMIT);
  CHECK(!memcmp(input, original, sizeof(input)));
  for (size_t j = 0; j < CR_FEATURES; ++j)
    scale[j] = 1.0;
  metadata.struct_size = sizeof(metadata);
  metadata.api_version = CR_API_VERSION;
  memcpy(metadata.task_profile, CR_LEGACY_PROFILE_ID,
         sizeof(CR_LEGACY_PROFILE_ID));
  memcpy(metadata.observation_schema, CR_LEGACY_OBSERVATION_ID,
         sizeof(CR_LEGACY_OBSERVATION_ID));
  memcpy(metadata.action_catalog, CR_LEGACY_CATALOG_ID,
         sizeof(CR_LEGACY_CATALOG_ID));
  memcpy(metadata.provenance, "native runtime-only conformance fixture",
         sizeof("native runtime-only conformance fixture"));
  cr_model_values values = {sizeof(values), CR_API_VERSION, 1,         0,
                            &owner,         scale,          centroids, code,
                            text,           &metadata};
  CHECK(cr_model_create_values(&values, &model) == CR_OK);
  info.struct_size = sizeof(info);
  info.api_version = CR_API_VERSION;
  CHECK(cr_model_info_get(model, &info) == CR_OK);
  CHECK(info.groups == 1 && info.owner_uids[0] == owner);
  CHECK(cr_model_predict(model, CR_CODE, input, 1, mass, scores,
                         CR_TEXT_ACTIONS) == CR_OK);
  for (size_t a = 0; a < CR_CODE_ACTIONS; ++a)
    CHECK(scores[a] == 0.25);
  CHECK(cr_model_predict(model, CR_TEXT, input, 1, mass, scores,
                         CR_TEXT_ACTIONS) == CR_OK);
  double sum = 0;
  for (size_t a = 0; a < CR_TEXT_ACTIONS; ++a)
    sum += scores[a];
  CHECK(fabs(sum - 1.0) < 1e-13);
  memcpy(before, scores, sizeof(before));
  CHECK(cr_model_predict(model, CR_CODE, input, 1, mass, scores, 3) ==
        CR_LIMIT);
  CHECK(!memcmp(scores, before, sizeof(before)));
  CHECK(cr_model_predict(model, CR_CODE, input, 0, mass, scores,
                         CR_TEXT_ACTIONS) == CR_DEFERRED);
  CHECK(!memcmp(scores, before, sizeof(before)));
  input[0] = NAN;
  CHECK(cr_model_predict(model, CR_CODE, input, 1, mass, scores,
                         CR_TEXT_ACTIONS) == CR_INVALID);
  CHECK(!memcmp(scores, before, sizeof(before)));
  input[0] = 1;
  CHECK(cr_model_predict(model, CR_CODE, input, 2, mass, scores,
                         CR_TEXT_ACTIONS) == CR_INVALID);
  CHECK(!memcmp(scores, before, sizeof(before)));
  const size_t length = cr_model_bundle_size(model);
  size_t written;
  unsigned char *bytes = malloc(length);
  CHECK(bytes);
  CHECK(cr_model_write_bundle(model, bytes, length, &written) == CR_OK);
  unsigned char *snapshot = malloc(length);
  CHECK(snapshot);
  memcpy(snapshot, bytes, length);
  CHECK(cr_model_write_bundle(model, bytes, length - 1u,
                              (size_t *)(void *)bytes) == CR_INVALID);
  CHECK(!memcmp(bytes, snapshot, length));
  CHECK(cr_model_write_bundle(model, bytes, length, (size_t *)(void *)model) ==
        CR_INVALID);
  CHECK(!memcmp(bytes, snapshot, length));
  free(snapshot);
  CHECK(cr_model_load_bytes(bytes, length, &loaded) == CR_OK);
  cr_model_destroy(model);
  model = NULL;
  CHECK(cr_model_predict(loaded, CR_CODE, input, 1, mass, scores,
                         CR_TEXT_ACTIONS) == CR_OK);
  for (size_t a = 0; a < CR_CODE_ACTIONS; ++a)
    CHECK(scores[a] == 0.25);
  centroids[0] = NAN;
  CHECK(cr_model_create_values(&values, &model) == CR_INVALID && !model);
  centroids[0] = 0;
  metadata.reserved = 1;
  CHECK(cr_model_create_values(&values, &model) == CR_INVALID && !model);
  metadata.reserved = 0;
  metadata.api_version = CR_API_VERSION + 1;
  CHECK(cr_model_create_values(&values, &model) == CR_INVALID && !model);
  metadata.api_version = CR_API_VERSION;
  memset(metadata.task_profile, 'x', sizeof(metadata.task_profile));
  CHECK(cr_model_create_values(&values, &model) == CR_INVALID && !model);
  free(bytes);
  free(text);
  cr_model_destroy(loaded);
  puts("standalone runtime contracts passed");
  return 0;
}
