#include "centroid_training.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

static int copy_reference(char *out, size_t capacity, const char *value) {
  if (!value)
    return 1;
  size_t length = 0;
  while (length < capacity && value[length])
    ++length;
  if (length == capacity)
    return 0;
  memcpy(out, value, length);
  return 1;
}

c_status c_trainer_export_model(const c_trainer *trainer,
                                const cr_export_options *options,
                                cr_model **out) {
  typedef struct {
    uint64_t uids[CR_MAX_GROUPS];
    double scale[CR_FEATURES];
    double centroid[CR_MAX_GROUPS * CR_FEATURES];
    double code[CR_MAX_GROUPS * CR_CODE_ACTIONS * CR_FEATURES];
    double text[CR_MAX_GROUPS * CR_TEXT_ACTIONS * CR_FEATURES];
  } export_values;
  export_values *frozen;
  cr_model_metadata metadata = {0};
  if (!trainer || !out || *out ||
      (options &&
       (options->struct_size != sizeof(*options) ||
        options->api_version != CR_API_VERSION || options->reserved)))
    return C_INVALID;
  /* Whole-owner validation preserves the strict continuation and provenance
   * contract without serializing or mutating the source owner. */
  c_status training_status = c_trainer_validate(trainer);
  if (training_status != C_OK)
    return training_status;
  frozen = calloc(1, sizeof(*frozen));
  if (!frozen)
    return C_NOMEM;
  metadata.struct_size = sizeof(metadata);
  metadata.api_version = CR_API_VERSION;
  if (options) {
    metadata.qualification = options->qualification;
    if (!copy_reference(metadata.parent_digest, CR_DIGEST_HEX,
                        options->parent_digest) ||
        !copy_reference(metadata.checkpoint_digest, CR_DIGEST_HEX,
                        options->checkpoint_digest) ||
        !copy_reference(metadata.qualification_digest, CR_DIGEST_HEX,
                        options->qualification_digest) ||
        !copy_reference(metadata.task_profile, CR_ID_BYTES,
                        options->task_profile) ||
        !copy_reference(metadata.observation_schema, CR_ID_BYTES,
                        options->observation_schema) ||
        !copy_reference(metadata.action_catalog, CR_ID_BYTES,
                        options->action_catalog) ||
        !copy_reference(metadata.provenance, CR_REFERENCE_BYTES,
                        options->provenance) ||
        !copy_reference(metadata.qualification_reference, CR_REFERENCE_BYTES,
                        options->qualification_reference)) {
      free(frozen);
      return C_LIMIT;
    }
  }
  if (!metadata.provenance[0])
    memcpy(metadata.provenance, "explicit local trainer export",
           sizeof("explicit local trainer export"));
  if (metadata.qualification == CR_QUALIFIED &&
      (!metadata.task_profile[0] || !metadata.observation_schema[0] ||
       !metadata.action_catalog[0])) {
    free(frozen);
    return C_INVALID;
  }
  if (!metadata.task_profile[0])
    memcpy(metadata.task_profile, CR_LEGACY_PROFILE_ID,
           sizeof(CR_LEGACY_PROFILE_ID));
  if (!metadata.observation_schema[0])
    memcpy(metadata.observation_schema, CR_LEGACY_OBSERVATION_ID,
           sizeof(CR_LEGACY_OBSERVATION_ID));
  if (!metadata.action_catalog[0])
    memcpy(metadata.action_catalog, CR_LEGACY_CATALOG_ID,
           sizeof(CR_LEGACY_CATALOG_ID));
  for (size_t j = 0; j < CR_FEATURES; ++j)
    frozen->scale[j] = trainer->model->shared_scale[j].value;
  for (uint32_t g = 0; g < trainer->model->groups; ++g) {
    const c_expert *source = &trainer->model->expert[g];
    frozen->uids[g] = source->uid;
    for (size_t j = 0; j < CR_FEATURES; ++j)
      frozen->centroid[g * CR_FEATURES + j] = source->centroid[j].value;
    for (size_t a = 0; a < CR_CODE_ACTIONS; ++a)
      for (size_t j = 0; j < CR_FEATURES; ++j)
        frozen->code[(g * CR_CODE_ACTIONS + a) * CR_FEATURES + j] =
            source->code[a][j].value;
    for (size_t a = 0; a < CR_TEXT_ACTIONS; ++a)
      for (size_t j = 0; j < CR_FEATURES; ++j)
        frozen->text[(g * CR_TEXT_ACTIONS + a) * CR_FEATURES + j] =
            source->text[a][j].value;
  }
  const cr_model_values values = {
      sizeof(values),         CR_API_VERSION,
      trainer->model->groups, trainer->model->shared_enabled,
      frozen->uids,           frozen->scale,
      frozen->centroid,       frozen->code,
      frozen->text,           &metadata};
  const cr_status status = cr_model_create_values(&values, out);
  free(frozen);
  return status <= CR_DEFERRED ? (c_status)status : C_INVALID;
}
