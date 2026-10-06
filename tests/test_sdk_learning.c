#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "centroid_code_helper.h"
#include "centroid_training.h"
#include "internal.h"
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "sdk learning line %d: %s\n", __LINE__, #x);             \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static void algorithms(void) {
  int32_t values[CR_CODE_MAX_VALUES];
  for (size_t count = 0; count <= CR_CODE_MAX_VALUES;
       count += count < 32u ? 1u : 127u) {
    for (size_t i = 0; i < count; ++i)
      values[i] = (int32_t)(i / 3u) - 300;
    for (int32_t key = -302; key < (int32_t)(count / 3u) - 297; ++key) {
      size_t expected = 0u;
      while (expected < count && values[expected] < key)
        ++expected;
      for (uint32_t action = 0; action < 4u; ++action) {
        size_t index = SIZE_MAX;
        uint64_t comparisons = UINT64_MAX;
        CHECK(cr_code_execute(action, values, count, key, &index,
                              &comparisons) == CR_OK);
        CHECK(index == expected && comparisons <= 2u * CR_CODE_MAX_VALUES);
      }
    }
  }
  const int32_t extremes[] = {INT32_MIN, INT32_MIN, -1, 0, INT32_MAX};
  const int32_t keys[] = {INT32_MIN, -1, 0, INT32_MAX};
  for (unsigned i = 0; i < 4u; ++i) {
    cr_code_observation observation = {0};
    observation.struct_size = sizeof(observation);
    observation.api_version = CR_API_VERSION;
    CHECK(cr_code_observe(extremes, 5u, keys[i], &observation) == CR_OK);
    double encoded[CR_FEATURES];
    CHECK(cr_encode(observation.bytes, CR_CODE_INPUT_BYTES, encoded) == CR_OK);
    CHECK(!memcmp(encoded, observation.features, sizeof(encoded)));
    CHECK(observation.band < 8u && observation.estimated_rank <= 5u);
  }
  cr_code_observation out, before;
  memset(&out, 0xa5, sizeof(out));
  out.struct_size = sizeof(out);
  out.api_version = CR_API_VERSION;
  before = out;
  CHECK(cr_code_observe(NULL, 1u, 0, &out) == CR_INVALID &&
        !memcmp(&before, &out, sizeof(out)));
  CHECK(cr_code_observe(values, CR_CODE_MAX_VALUES + 1u, 0, &out) == CR_LIMIT &&
        !memcmp(&before, &out, sizeof(out)));
  CHECK(cr_code_observe(NULL, 0u, 0, &out) == CR_OK && out.band == 0u);
  size_t index = 99u;
  uint64_t cost = 88u;
  CHECK(cr_code_execute(4u, NULL, 0u, 0, &index, &cost) == CR_INVALID &&
        index == 99u && cost == 88u);
  CHECK(cr_code_execute(0u, NULL, 1u, 0, &index, &cost) == CR_INVALID &&
        index == 99u && cost == 88u);
}
static void metadata_contract(void) {
  c_trainer *trainer = NULL;
  cr_model *model = NULL;
  CHECK(c_trainer_create(2u, 817u, &trainer) == C_OK);
  CHECK(c_trainer_export_model(trainer, NULL, &model) == C_OK);
  cr_code_recommendation out, before;
  memset(&out, 0x5a, sizeof(out));
  out.struct_size = sizeof(out);
  out.api_version = CR_API_VERSION;
  before = out;
  CHECK(cr_code_recommend(model, NULL, 0u, 0, 15u, &out) == CR_UNSUPPORTED &&
        !memcmp(&before, &out, sizeof(out)));
  cr_model_destroy(model);
  model = NULL;
  cr_export_options opts = {0};
  opts.struct_size = sizeof(opts);
  opts.api_version = CR_API_VERSION;
  opts.task_profile = CR_CODE_HELPER_PROFILE;
  opts.observation_schema = CR_CODE_OBSERVATION_SCHEMA;
  opts.action_catalog = CR_CODE_ACTION_CATALOG;
  CHECK(c_trainer_export_model(trainer, &opts, &model) == C_OK);
  CHECK(cr_code_recommend(model, NULL, 0u, 0, 15u, &out) == CR_DEFERRED &&
        !memcmp(&before, &out, sizeof(out)));
  cr_model_destroy(model);
  model = NULL;
  char fixture[65];
  c_hash("explicit synthetic API fixture, no quality claim", 46u, fixture);
  opts.qualification = CR_QUALIFIED;
  opts.qualification_digest = fixture;
  opts.qualification_reference = "synthetic-test-fixture";
  CHECK(c_trainer_export_model(trainer, &opts, &model) == C_OK);
  for (uint32_t mask = 1u; mask <= 15u; ++mask) {
    CHECK(cr_code_recommend(model, NULL, 0u, 0, mask, &out) == CR_OK);
    CHECK(mask & (1u << out.action));
    double sum = 0.0;
    for (uint32_t a = 0; a < 4u; ++a) {
      CHECK((mask & (1u << a)) || out.scores[a] == 0.0);
      sum += out.scores[a];
    }
    CHECK(fabs(sum - 1.0) < 1e-14 && strlen(out.model_digest) == 64u);
  }
  before = out;
  CHECK(cr_code_recommend(model, NULL, 0u, 0, 0u, &out) == CR_INVALID &&
        !memcmp(&before, &out, sizeof(out)));
  CHECK(cr_code_recommend(model, NULL, 0u, 0, 16u, &out) == CR_INVALID &&
        !memcmp(&before, &out, sizeof(out)));
  cr_model_destroy(model);
  c_trainer_destroy(trainer);
}
static void verify_manifest(const char *directory) {
  char filename[4096];
  CHECK(snprintf(filename, sizeof(filename), "%s/evidence.sha256.tsv",
                 directory) > 0);
  unsigned char *manifest = NULL;
  size_t length = 0;
  CHECK(c_read_file(filename, &manifest, &length) == C_OK);
  size_t offset = 0, entries = 0;
  while (offset < length) {
    size_t end = offset;
    while (end < length && manifest[end] != '\n')
      ++end;
    CHECK(end < length && end - offset < 256u);
    char line[256], expected[65], name[96];
    size_t expected_length;
    memcpy(line, manifest + offset, end - offset);
    line[end - offset] = 0;
    CHECK(sscanf(line, "%64s\t%zu\t%95s", expected, &expected_length, name) ==
          3);
    CHECK(!strchr(name, '/') && !strchr(name, '\\') && !strstr(name, ".."));
    CHECK(snprintf(filename, sizeof(filename), "%s/%s", directory, name) > 0);
    unsigned char *bytes = NULL;
    size_t actual_length = 0;
    char actual[65];
    CHECK(c_read_file(filename, &bytes, &actual_length) == C_OK &&
          actual_length == expected_length);
    c_hash(bytes, actual_length, actual);
    free(bytes);
    CHECK(!strcmp(expected, actual));
    ++entries;
    offset = end + 1u;
  }
  CHECK(entries >= 60u);
  free(manifest);
}
int main(int argc, char **argv) {
  CHECK(argc == 4);
  algorithms();
  metadata_contract();
  char directory[4096];
  CHECK(snprintf(directory, sizeof(directory), "%s/sdk-learning-%" PRIu64,
                 argv[1], c_monotonic_ns()) > 0);
  const char *args[] = {argv[2], argv[3], directory, NULL};
  c_process_options options = {0};
  options.program = argv[2];
  options.argv = args;
  options.timeout_ms = 120000u;
  options.output_limit = 65536u;
  c_process_result result = {0};
  CHECK(c_process_run(&options, &result) == C_OK);
  if (result.exit_code)
    fwrite(result.output, 1u, result.length, stderr);
  CHECK(!result.exit_code && !result.timed_out && !result.output_truncated);
  c_process_dispose(&result);
  verify_manifest(directory);
  char filename[4096];
  CHECK(snprintf(filename, sizeof(filename), "%s/qualified.crmodel",
                 directory) > 0);
  cr_model *model = NULL;
  CHECK(cr_model_load_file(filename, &model) == CR_OK);
  cr_model_info info = {0};
  info.struct_size = sizeof(info);
  info.api_version = CR_API_VERSION;
  CHECK(cr_model_info_get(model, &info) == CR_OK);
  CHECK(info.metadata.qualification == CR_QUALIFIED &&
        !strcmp(info.metadata.task_profile, CR_CODE_HELPER_PROFILE));
  CHECK(snprintf(filename, sizeof(filename), "%s/qualification.md", directory) >
        0);
  unsigned char *bytes = NULL;
  size_t length = 0;
  char digest[65];
  CHECK(c_read_file(filename, &bytes, &length) == C_OK);
  c_hash(bytes, length, digest);
  free(bytes);
  CHECK(!strcmp(digest, info.metadata.qualification_digest));
  CHECK(cr_model_check_qualification_file(model, filename) == CR_OK);
  CHECK(snprintf(filename, sizeof(filename), "%s/report.md", directory) > 0);
  CHECK(cr_model_check_qualification_file(model, filename) == CR_CORRUPT);
  const int32_t smoke[] = {-9, -3, -3, 0, 4, 7, 19, 20, 40};
  size_t bundle_length = cr_model_bundle_size(model), written = 0;
  unsigned char *frozen_bytes = malloc(bundle_length),
                *after_bytes = malloc(bundle_length);
  CHECK(frozen_bytes && after_bytes);
  CHECK(cr_model_write_bundle(model, frozen_bytes, bundle_length, &written) ==
        CR_OK);
  for (uint32_t mask = 1u; mask < 16u; ++mask) {
    cr_code_recommendation recommendation = {0};
    recommendation.struct_size = sizeof(recommendation);
    recommendation.api_version = CR_API_VERSION;
    CHECK(cr_code_recommend(model, smoke, 9u, 7, mask, &recommendation) ==
          CR_OK);
    CHECK(mask & (1u << recommendation.action));
    size_t index = SIZE_MAX;
    uint64_t comparisons = 0;
    CHECK(cr_code_execute(recommendation.action, smoke, 9u, 7, &index,
                          &comparisons) == CR_OK &&
          index == 5u);
  }
  CHECK(cr_model_write_bundle(model, after_bytes, bundle_length, &written) ==
            CR_OK &&
        !memcmp(frozen_bytes, after_bytes, bundle_length));
  free(frozen_bytes);
  free(after_bytes);
  CHECK(c_process_run(&options, &result) == C_OK && result.exit_code != 0);
  c_process_dispose(&result);
  CHECK(snprintf(filename, sizeof(filename), "%s/qualified.crmodel",
                 directory) > 0);
  cr_model *again = NULL;
  CHECK(cr_model_load_file(filename, &again) == CR_OK);
  cr_model_info same = {0};
  same.struct_size = sizeof(same);
  same.api_version = CR_API_VERSION;
  CHECK(cr_model_info_get(again, &same) == CR_OK &&
        !memcmp(&same, &info, sizeof(info)));
  cr_model_destroy(again);
  cr_model_destroy(model);
  printf("sdk learning native contracts and retained qualification passed\n");
  return 0;
}
