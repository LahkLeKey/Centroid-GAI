#ifndef CENTROID_RUNTIME_INTERNAL_H
#define CENTROID_RUNTIME_INTERNAL_H

#include "centroid_runtime.h"

typedef struct {
  uint64_t uid;
  double centroid[CR_FEATURES];
  double code[CR_CODE_ACTIONS][CR_FEATURES];
  double text[CR_TEXT_ACTIONS][CR_FEATURES];
} cr_expert_values;

struct cr_model {
  uint32_t groups, shared_enabled;
  double shared_scale[CR_FEATURES];
  cr_model_metadata metadata;
  cr_expert_values experts[CR_MAX_GROUPS];
};

int cr_model_values_valid(const cr_model *model);
int cr_metadata_valid(const cr_model_metadata *metadata);
void cr_hash(const void *bytes, size_t length, char out[CR_DIGEST_HEX]);
void cr_hash_bytes(const void *bytes, size_t length, char out[CR_DIGEST_HEX]);
cr_status cr_model_identify(cr_model *model);

#endif
