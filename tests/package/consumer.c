#include <centroid_context.h>
#include <centroid_runtime.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  static const unsigned char bytes[] = {'C', 0, 0xff, '\n', '1'};
  double first[CR_FEATURES], second[CR_FEATURES];
  double workspace[CR_PREDICT_SCRATCH_DOUBLES];
  double probabilities[CR_CODE_ACTIONS] = {-1, -1, -1, -1};
  const double mass[CR_MAX_GROUPS] = {1, 0, 0, 0};
  cr_model *model = NULL;
  cr_context *context = NULL;
  cr_context_options context_options;
  cr_query_options query_options;
  cr_query_report query_report;
  cr_evidence evidence[CR_CONTEXT_MAX_HITS];
  uint64_t id = 0;
  static const unsigned char source[] = "int centroid_context_budget = 8;\n";
  static const unsigned char query[] = "centroid_context_budget";
  _Static_assert(sizeof(uint32_t) == 4, "fixed-width public ABI");
  if (cr_version() != CR_API_VERSION ||
      cr_encode(bytes, sizeof(bytes), first) != CR_OK ||
      cr_encode(bytes, sizeof(bytes), second) != CR_OK ||
      memcmp(first, second, sizeof(first)) != 0 ||
      strcmp(cr_status_string(CR_OK), "ok") != 0)
    return 1;
  for (size_t index = 0; index < CR_FEATURES; ++index)
    if (!isfinite(first[index]))
      return 2;
  if (cr_model_load_bytes(bytes, sizeof(bytes), &model) == CR_OK || model)
    return 3;
  if (cr_model_check_qualification_file(NULL, "unused") != CR_INVALID)
    return 6;
  if (cr_model_predict_with_scratch(NULL, CR_CODE, first, 1, mass, workspace,
                                    CR_PREDICT_SCRATCH_DOUBLES, probabilities,
                                    CR_CODE_ACTIONS) != CR_INVALID ||
      probabilities[0] != -1 || probabilities[3] != -1)
    return 7;
  /* Frozen zero-value fixture exercises the installed workspace ABI without
   * a training component or a separately supplied model asset. */
  double scale[CR_FEATURES], centroid[CR_FEATURES] = {0};
  double code[CR_CODE_ACTIONS * CR_FEATURES] = {0};
  double *text = calloc(CR_TEXT_ACTIONS * CR_FEATURES, sizeof(double));
  const uint64_t owner = 47;
  cr_model_metadata metadata = {0};
  if (!text)
    return 8;
  for (size_t i = 0; i < CR_FEATURES; ++i)
    scale[i] = 1;
  metadata.struct_size = sizeof(metadata);
  metadata.api_version = CR_API_VERSION;
  memcpy(metadata.task_profile, CR_LEGACY_PROFILE_ID,
         sizeof(CR_LEGACY_PROFILE_ID));
  memcpy(metadata.observation_schema, CR_LEGACY_OBSERVATION_ID,
         sizeof(CR_LEGACY_OBSERVATION_ID));
  memcpy(metadata.action_catalog, CR_LEGACY_CATALOG_ID,
         sizeof(CR_LEGACY_CATALOG_ID));
  memcpy(metadata.provenance, "installed frozen workspace fixture",
         sizeof("installed frozen workspace fixture"));
  const cr_model_values values = {
      sizeof(values), CR_API_VERSION, 1,    0,    &owner,
      scale,          centroid,       code, text, &metadata};
  cr_status imported = cr_model_create_values(&values, &model);
  free(text);
  if (imported != CR_OK)
    return 9;
  if (cr_model_predict_with_scratch(model, CR_CODE, first, 1, mass, workspace,
                                    CR_PREDICT_SCRATCH_DOUBLES, probabilities,
                                    CR_CODE_ACTIONS) != CR_OK ||
      probabilities[0] != 0.25 || probabilities[3] != 0.25) {
    cr_model_destroy(model);
    return 10;
  }
  cr_model_destroy(model);
  cr_context_options_init(&context_options);
  if (cr_context_create(&context_options, &context) != CR_OK)
    return 4;
  cr_query_options_init(&query_options);
  query_options.byte_budget = sizeof(source);
  if (cr_context_admit(context, CR_SOURCE, CR_TRAIN, "fixture.c",
                       "installed consumer fixture", source, sizeof(source) - 1,
                       &id) != CR_OK ||
      cr_context_query(context, query, sizeof(query) - 1, &query_options,
                       evidence, CR_CONTEXT_MAX_HITS, &query_report) != CR_OK ||
      query_report.returned != 1 || query_report.abstained ||
      evidence[0].record.id != id ||
      query_report.excerpt_bytes > query_options.byte_budget ||
      evidence[0].record.length != sizeof(source) - 1 ||
      memcmp(evidence[0].record.bytes, source, sizeof(source) - 1) != 0) {
    cr_context_destroy(context);
    return 5;
  }
  unsigned char framed[4096];
  size_t frame_length = 0;
  if (cr_text_frame_context(context, id, NULL, 0, NULL, 0, framed,
                            sizeof(framed), &frame_length) != CR_OK ||
      !frame_length || frame_length >= sizeof(framed) ||
      cr_encode(framed, frame_length, first) != CR_OK) {
    cr_context_destroy(context);
    return 11;
  }
  cr_context_destroy(context);
  puts("installed C11 runtime consumer: caller workspace, role framing, asset "
       "rejection and evidence pass");
  return 0;
}
