#include "centroid_training.h"
#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "runtime check failed %s:%d: %s\n", __FILE__, __LINE__,  \
              #condition);                                                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static void info_init(cr_model_info *info) {
  memset(info, 0, sizeof(*info));
  info->struct_size = sizeof(*info);
  info->api_version = CR_API_VERSION;
}

static void compare_predictions(const c_model *source,
                                const cr_model *runtime) {
  static const unsigned char samples[][8] = {
      {0, 0, 0, 0, 0, 0, 0, 0},
      {'a', 'b', 'c', 0, 255, 128, 'x', '\n'},
      {255, 1, 9, 64, 129, 7, 3, 250}};
  const double mass[CR_MAX_GROUPS] = {2.0, 0.5, 1.5, 0.25};
  const unsigned groups = c_model_group_count(source);
  for (size_t n = 0; n < sizeof(samples) / sizeof(samples[0]); ++n) {
    double input[C_FEATURES], encoded[CR_FEATURES];
    CHECK(c_encode(samples[n], sizeof(samples[n]), input) == C_OK);
    CHECK(cr_encode(samples[n], sizeof(samples[n]), encoded) == CR_OK);
    CHECK(memcmp(input, encoded, sizeof(input)) == 0);
    for (unsigned head = C_TEXT; head <= C_CODE; ++head)
      for (unsigned mask = 1; mask < (1u << groups); ++mask) {
        double expected[C_TEXT_ACTIONS], actual[CR_TEXT_ACTIONS];
        const size_t actions = head == C_TEXT ? C_TEXT_ACTIONS : C_CODE_ACTIONS;
        CHECK(c_model_predict(source, (c_head)head, input, mask, mass, expected,
                              C_TEXT_ACTIONS) == C_OK);
        CHECK(cr_model_predict(runtime, (cr_head)head, encoded, mask, mass,
                               actual, CR_TEXT_ACTIONS) == CR_OK);
        for (size_t a = 0; a < actions; ++a)
          CHECK(fabs(expected[a] - actual[a]) <= 1e-13);
        CHECK(memcmp(expected, actual, actions * sizeof(double)) == 0);
      }
  }
}

static void resign(unsigned char *bytes, size_t length) {
  char digest[CR_DIGEST_HEX];
  c_hash(bytes, length - 64, digest);
  memcpy(bytes + length - 64, digest, 64);
}

static void assert_preserved(const unsigned char *bytes, size_t length,
                             cr_model **incumbent, cr_status expected) {
  cr_model *prior = *incumbent;
  cr_model_info before, after;
  info_init(&before);
  info_init(&after);
  CHECK(cr_model_info_get(prior, &before) == CR_OK);
  CHECK(cr_model_load_bytes(bytes, length, incumbent) == expected);
  CHECK(*incumbent == prior);
  CHECK(cr_model_info_get(*incumbent, &after) == CR_OK);
  CHECK(memcmp(&before, &after, sizeof(before)) == 0);
}

static void test_portable_bundle(void) {
  c_trainer *trainer = NULL, *resumed = NULL;
  cr_model *runtime = NULL, *loaded = NULL;
  unsigned char *bytes, *mutant, *before, *after;
  size_t length, written = 0, before_length, after_length;
  const char *before_path = "runtime-parent-before.clife";
  const char *after_path = "runtime-parent-after.clife";
  const char *model_path = "runtime-frozen-test.cmodel";
  remove(before_path);
  remove(after_path);
  remove(model_path);
  CHECK(c_trainer_create(3, 67383, &trainer) == C_OK);
  static const unsigned char source[] = "exact native source bytes\n";
  uint64_t id;
  CHECK(c_context_admit(c_trainer_context(trainer), C_SOURCE, C_TRAIN,
                        "runtime-source.c", "runtime parity test", source,
                        sizeof(source) - 1, &id) == C_OK);
  for (size_t i = 0; i <= sizeof(source) - 1; ++i)
    CHECK(c_trainer_enqueue_source(trainer, id, i, 7u) == C_OK);
  c_training_report report;
  CHECK(c_trainer_step(trainer, 64, &report) == C_OK);
  CHECK(report.updates > 0);
  CHECK(c_trainer_save(trainer, before_path) == C_OK);
  CHECK(c_trainer_export_model(trainer, NULL, &runtime) == C_OK);
  compare_predictions(c_trainer_model(trainer), runtime);
  cr_model_info info;
  info_init(&info);
  CHECK(cr_model_info_get(runtime, &info) == CR_OK);
  CHECK(info.metadata.qualification == CR_EXPERIMENTAL);
  CHECK(!strcmp(info.metadata.task_profile, CR_LEGACY_PROFILE_ID));
  CHECK(info.model_bytes < sizeof(c_model));
  CHECK(info.groups == 3u && info.owner_uids[2] == 3u);
  CHECK(cr_model_group_uid(runtime, 3) == 0);
  CHECK(cr_model_write_bundle(runtime, NULL, 0, &written) == CR_LIMIT);
  length = cr_model_bundle_size(runtime);
  CHECK(written == length && info.bundle_bytes == length);
  bytes = malloc(length);
  mutant = malloc(length);
  CHECK(bytes && mutant);
  memset(bytes, 0x83, length);
  CHECK(cr_model_write_bundle(runtime, bytes, length - 1, &written) ==
        CR_LIMIT);
  CHECK(bytes[0] == 0x83 && bytes[length - 1] == 0x83);
  CHECK(cr_model_write_bundle(runtime, bytes, length, &written) == CR_OK);
  CHECK(cr_model_load_bytes(bytes, length, &loaded) == CR_OK);
  compare_predictions(c_trainer_model(trainer), loaded);
  CHECK(cr_model_write_bundle(loaded, mutant, length, &written) == CR_OK);
  CHECK(memcmp(bytes, mutant, length) == 0);

  assert_preserved(bytes, length - 1, &loaded, CR_CORRUPT);
  assert_preserved(bytes, CR_MAX_BUNDLE_BYTES + 1u, &loaded, CR_LIMIT);
  memcpy(mutant, bytes, length);
  mutant[length / 2] ^= 1;
  assert_preserved(mutant, length, &loaded, CR_CORRUPT);
  memcpy(mutant, bytes, length);
  mutant[8] = 2; /* Valid digest, unsupported format. */
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_UNSUPPORTED);
  memcpy(mutant, bytes, length);
  mutant[12] = 2; /* Distinct ABI compatibility check. */
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_UNSUPPORTED);
  memcpy(mutant, bytes, length);
  mutant[48] = 'x'; /* Correct checksum, unsupported recipe. */
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_UNSUPPORTED);
  memcpy(mutant, bytes, length);
  mutant[24] = 5; /* Groups exceed registered resource envelope. */
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_CORRUPT);
  memcpy(mutant, bytes, length);
  mutant[64] = 0;
  mutant[65] = 1; /* Noncanonical string padding. */
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_CORRUPT);
  memcpy(mutant, bytes, length);
  /* Value payload starts at1500. A forged checksum cannot admit a NaN. */
  memset(mutant + 1500, 0, 8);
  mutant[1506] = 0xf8;
  mutant[1507] = 0x7f;
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_CORRUPT);
  memcpy(mutant, bytes, length);
  /* Altered finite parameter without matching value identity is rejected. */
  mutant[1500 + CR_FEATURES * 8u + 8u] ^= 1;
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_CORRUPT);
  memcpy(mutant, bytes, length);
  /* Duplicate owner UIDs do not survive a recomputed integrity digest. */
  const size_t first_owner = 1500u + CR_FEATURES * 8u;
  const size_t expert_bytes =
      8u + CR_FEATURES * (1u + CR_CODE_ACTIONS + CR_TEXT_ACTIONS) * 8u;
  memcpy(mutant + first_owner + expert_bytes, mutant + first_owner, 8);
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_CORRUPT);
  memcpy(mutant, bytes, length);
  mutant[436] = 1; /* Reserved metadata cannot silently change semantics. */
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_CORRUPT);
  memcpy(mutant, bytes, length);
  mutant[432] = CR_QUALIFIED; /* Label without explicit evidence is invalid. */
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_CORRUPT);
  memcpy(mutant, bytes, length);
  mutant[700] ^=
      1; /* Semantic profile is covered by the model value identity. */
  resign(mutant, length);
  assert_preserved(mutant, length, &loaded, CR_CORRUPT);

  CHECK(cr_model_save_file(runtime, model_path) == CR_OK);
  CHECK(cr_model_save_file(runtime, model_path) == CR_IO);
  CHECK(cr_model_load_file(model_path, &loaded) == CR_OK);
  compare_predictions(c_trainer_model(trainer), loaded);
  CHECK(c_trainer_save(trainer, after_path) == C_OK);
  CHECK(c_read_file(before_path, &before, &before_length) == C_OK);
  CHECK(c_read_file(after_path, &after, &after_length) == C_OK);
  CHECK(before_length == after_length && !memcmp(before, after, before_length));
  CHECK(c_trainer_load(before_path, &resumed) == C_OK);
  CHECK(c_trainer_step(trainer, 3, &report) == C_OK);
  CHECK(c_trainer_step(resumed, 3, &report) == C_OK);
  for (unsigned g = 0; g < 3; ++g) {
    char first[C_DIGEST_HEX], second[C_DIGEST_HEX];
    CHECK(c_model_fingerprint(c_trainer_model(trainer), g, first) == C_OK);
    CHECK(c_model_fingerprint(c_trainer_model(resumed), g, second) == C_OK);
    CHECK(!strcmp(first, second));
  }
  free(before);
  free(after);
  free(bytes);
  free(mutant);
  cr_model_destroy(loaded);
  cr_model_destroy(runtime);
  c_trainer_destroy(resumed);
  c_trainer_destroy(trainer);
  remove(before_path);
  remove(after_path);
  remove(model_path);
}

static void test_qualification_semantics(void) {
  c_trainer *trainer = NULL;
  cr_model *model = NULL, *other = NULL;
  cr_export_options options = {0};
  cr_model_info first, second;
  CHECK(c_trainer_create(2, 773, &trainer) == C_OK);
  options.struct_size = sizeof(options);
  options.api_version = CR_API_VERSION;
  options.qualification = CR_QUALIFIED;
  CHECK(c_trainer_export_model(trainer, &options, &model) == C_INVALID);
  CHECK(!model);
  options.task_profile = "finite-test/v1";
  options.observation_schema = "test-byte-schema/v1";
  options.action_catalog = "test-catalog4/v1";
  CHECK(c_trainer_export_model(trainer, &options, &model) == C_INVALID);
  options.qualification_digest =
      "1111111111111111111111111111111111111111111111111111111111111111";
  options.qualification_reference = "caller accepted independent test record";
  CHECK(c_trainer_export_model(trainer, &options, &model) == C_OK);
  info_init(&first);
  CHECK(cr_model_info_get(model, &first) == CR_OK);
  CHECK(first.metadata.qualification == CR_QUALIFIED);
  options.action_catalog = "different-catalog4/v1";
  CHECK(c_trainer_export_model(trainer, &options, &other) == C_OK);
  info_init(&second);
  CHECK(cr_model_info_get(other, &second) == CR_OK);
  CHECK(strcmp(first.metadata.model_digest, second.metadata.model_digest));
  cr_model_destroy(other);
  cr_model_destroy(model);
  c_trainer_destroy(trainer);
}

static void test_shared_values(void) {
  c_trainer *trainer = NULL;
  cr_model *runtime = NULL;
  CHECK(c_trainer_create(2, 9931, &trainer) == C_OK);
  /* This is a value-codec conformance fixture. It does not train or promote
   * this artificial shared-representation owner into production. */
  trainer->model->shared_enabled = 1;
  for (size_t j = 0; j < C_FEATURES; ++j)
    trainer->model->shared_scale[j].value = 0.75 + (double)j / 64.0;
  CHECK(c_trainer_export_model(trainer, NULL, &runtime) == C_OK);
  compare_predictions(c_trainer_model(trainer), runtime);
  cr_model_destroy(runtime);
  c_trainer_destroy(trainer);
}

int main(void) {
  test_portable_bundle();
  test_qualification_semantics();
  test_shared_values();
  puts("runtime bundle, immutable inference and continuation contracts passed");
  return 0;
}
