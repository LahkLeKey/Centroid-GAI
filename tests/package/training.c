#include <centroid_training.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  static const unsigned char input_bytes[] = "external installed trainer";
  c_trainer *trainer = NULL;
  cr_model *model = NULL, *loaded = NULL;
  unsigned char *bundle = NULL;
  double input[CR_FEATURES], original[CR_TEXT_ACTIONS],
      exported[CR_TEXT_ACTIONS];
  const double mass[CR_MAX_GROUPS] = {1.0, 1.0, 0.0, 0.0};
  int failed = 1;
  const char *stage = "create trainer";
  size_t length = 0, written = 0;
  uint64_t source_id = 0;
  c_training_report progress;
  char before[C_DIGEST_HEX], after[C_DIGEST_HEX];
  if (c_trainer_create(2, 7, &trainer) != C_OK)
    goto done;
  stage = "installed Life-authorized update";
  if (c_model_fingerprint(c_trainer_model(trainer), 0, before) != C_OK ||
      c_context_admit(c_trainer_context(trainer), C_SOURCE, C_TRAIN,
                      "installed-source.c", "native installed host",
                      input_bytes, sizeof(input_bytes) - 1,
                      &source_id) != C_OK ||
      c_trainer_enqueue_source(trainer, source_id, 1, 3) != C_OK ||
      c_trainer_step(trainer, 128, &progress) != C_OK || !progress.updates ||
      !progress.contacts || progress.queued ||
      !c_model_group_clock(c_trainer_model(trainer), 0) ||
      c_model_fingerprint(c_trainer_model(trainer), 0, after) != C_OK ||
      !strcmp(before, after))
    goto done;
  stage = "read-only trained export";
  if (c_trainer_export_model(trainer, NULL, &model) != C_OK ||
      cr_encode(input_bytes, sizeof(input_bytes) - 1, input) != CR_OK)
    goto done;
  length = cr_model_bundle_size(model);
  stage = "bundle round trip";
  bundle = malloc(length);
  if (!bundle ||
      cr_model_write_bundle(model, bundle, length, &written) != CR_OK ||
      written != length ||
      cr_model_load_bytes(bundle, length, &loaded) != CR_OK)
    goto done;
  if (cr_model_check_qualification_file(loaded, "unused") != CR_DEFERRED)
    goto done;
  for (unsigned head = 0; head < 2; ++head) {
    stage = "both-head prediction parity";
    size_t actions = head == CR_CODE ? CR_CODE_ACTIONS : CR_TEXT_ACTIONS;
    if (c_model_predict(c_trainer_model(trainer), (c_head)head, input, 3, mass,
                        original, actions) != C_OK ||
        cr_model_predict(loaded, head, input, 3, mass, exported, actions) !=
            CR_OK)
      goto done;
    for (size_t action = 0; action < actions; ++action)
      if (fabs(original[action] - exported[action]) > 1e-12)
        goto done;
  }
  puts(
      "installed training consumer: Life update, portable export and inference "
      "parity pass");
  failed = 0;
done:
  if (failed)
    fprintf(stderr, "installed training consumer failed: %s\n", stage);
  free(bundle);
  cr_model_destroy(loaded);
  cr_model_destroy(model);
  c_trainer_destroy(trainer);
  return failed;
}
