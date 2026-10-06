#ifndef CENTROID_TRAINING_H
#define CENTROID_TRAINING_H

#include "centroid.h"
#include "centroid_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t struct_size, api_version;
  uint32_t qualification, reserved;
  const char *parent_digest;
  const char *checkpoint_digest;
  const char *qualification_digest;
  const char *task_profile;
  const char *observation_schema;
  const char *action_catalog;
  const char *provenance;
  const char *qualification_reference;
} cr_export_options;

/* Explicit read-only export. Production mutation remains in trainer.c. This
 * copies parameter values and identities only, never moments, world, tasks,
 * receipts, context or training continuation bytes. *out must be NULL. A
 * qualified label must explicitly reference an independently accepted record;
 * this bridge does not evaluate or promote a candidate. NULL options select an
 * experimental export with no implied qualification. */
c_status c_trainer_export_model(const c_trainer *trainer,
                                const cr_export_options *options,
                                cr_model **out);

#ifdef __cplusplus
}
#endif
#endif
