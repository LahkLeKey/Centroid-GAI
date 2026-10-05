#ifndef CENTROID_ALGORITHMS_H
#define CENTROID_ALGORITHMS_H
#include "centroid.h"

/* Checked affine byte count. Failure leaves *out unchanged. */
c_status c_size_affine(size_t count, size_t stride, size_t extra, size_t *out);
/* First index with values[index] >= key in a sorted array. Empty arrays accept
 * NULL. Invalid nonempty NULL input returns SIZE_MAX. Optional comparisons is
 * an exact operation counter, not a wall-clock performance estimate. */
size_t c_lower_bound(const int *values, size_t count, int key,
                     size_t *comparisons);
uint64_t c_u64_saturating_add(uint64_t left, uint64_t right);

/* Three independently executed finite native catalogs. Source/proposal/activity
 * are inputs; only TRAIN measurements provide targets. Captures the declared
 * project source and emits a verified lower-bound patch artifact. */
c_status c_code_suite_run(c_trainer *trainer, const char *compiler,
                          const char *project_root,
                          const char *new_output_directory);
/* Explicit local publication. The only permitted target is
 * src/domain/algorithms.c. Checks parent identity and all accepted artifacts;
 * failures preserve the target. No tool execution or model update occurs. */
c_status c_patch_apply(const char *accepted_artifact_directory,
                       const char *project_root,
                       const char *expected_parent_digest);
#endif
