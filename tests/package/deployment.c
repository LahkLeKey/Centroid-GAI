#include <centroid_code_helper.h>
#include <stdio.h>
#include <string.h>

/* Manual release check using separately supplied assets. Ordinary installed
 * consumer tests do not require private or historical model files. */
static int same_recommendation(const cr_code_recommendation *a,
                               const cr_code_recommendation *b) {
  return a->action == b->action && a->legal_mask == b->legal_mask &&
         !strcmp(a->model_digest, b->model_digest) &&
         !memcmp(a->scores, b->scores, sizeof(a->scores));
}
int main(int argc, char **argv) {
  static const unsigned char corrupt[] = "incomplete replacement";
  int32_t values[2048];
  cr_model *model = NULL;
  cr_model_info selected = {0}, parent = {0}, restored = {0};
  cr_code_recommendation first = {0}, repeated = {0};
  const char *stage = "arguments";
  size_t index = 0;
  uint64_t comparisons = 0;
  int failed = 1;
  if (argc != 4) {
    fprintf(stderr, "usage: %s QUALIFIED_BUNDLE QUALIFICATION PARENT_BUNDLE\n",
            argv[0]);
    return 2;
  }
  for (size_t i = 0; i < 2048; ++i)
    values[i] = (int32_t)(i * 3);
  selected.struct_size = parent.struct_size = restored.struct_size =
      sizeof(selected);
  selected.api_version = parent.api_version = restored.api_version =
      CR_API_VERSION;
  first.struct_size = repeated.struct_size = sizeof(first);
  first.api_version = repeated.api_version = CR_API_VERSION;
  stage = "qualified adoption";
  if (cr_model_load_file(argv[1], &model) != CR_OK ||
      cr_model_info_get(model, &selected) != CR_OK ||
      selected.metadata.qualification != CR_QUALIFIED ||
      cr_model_check_qualification_file(model, argv[2]) != CR_OK ||
      cr_code_recommend(model, values, 2048, 17, 15, &first) != CR_OK ||
      cr_code_execute(first.action, values, 2048, 17, &index, &comparisons) !=
          CR_OK ||
      index != 6)
    goto done;
  stage = "failed replacement preserves incumbent";
  if (cr_model_load_bytes(corrupt, sizeof(corrupt) - 1, &model) == CR_OK ||
      cr_model_info_get(model, &restored) != CR_OK ||
      strcmp(selected.metadata.model_digest, restored.metadata.model_digest) ||
      cr_code_recommend(model, values, 2048, 17, 15, &repeated) != CR_OK ||
      !same_recommendation(&first, &repeated))
    goto done;
  stage = "explicit rollback and conventional fallback";
  if (cr_model_load_file(argv[3], &model) != CR_OK ||
      cr_model_info_get(model, &parent) != CR_OK ||
      strcmp(selected.metadata.parent_digest, parent.metadata.model_digest) ||
      parent.metadata.qualification != CR_EXPERIMENTAL ||
      cr_code_recommend(model, values, 2048, 17, 15, &repeated) !=
          CR_DEFERRED ||
      cr_code_execute(CR_CODE_BINARY, values, 2048, 17, &index, &comparisons) !=
          CR_OK ||
      index != 6)
    goto done;
  stage = "qualified re-adoption";
  if (cr_model_load_file(argv[1], &model) != CR_OK ||
      cr_model_check_qualification_file(model, argv[2]) != CR_OK ||
      cr_model_info_get(model, &restored) != CR_OK ||
      strcmp(selected.metadata.model_digest, restored.metadata.model_digest) ||
      cr_code_recommend(model, values, 2048, 17, 15, &repeated) != CR_OK ||
      !same_recommendation(&first, &repeated))
    goto done;
  printf("installed deployment: model=%s parent=%s action=%u; invalid "
         "replacement, rollback fallback and re-adoption pass\n",
         selected.metadata.model_digest, parent.metadata.model_digest,
         (unsigned)first.action);
  failed = 0;
done:
  if (failed)
    fprintf(stderr, "installed deployment failed: %s\n", stage);
  cr_model_destroy(model);
  return failed;
}
