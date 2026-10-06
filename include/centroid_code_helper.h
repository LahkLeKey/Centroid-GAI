#ifndef CENTROID_CODE_HELPER_H
#define CENTROID_CODE_HELPER_H
#include "centroid_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CR_CODE_HELPER_PROFILE "lower-bound-strategy/v1"
#define CR_CODE_OBSERVATION_SCHEMA "lower-bound-band2-byte-pos32/v1"
#define CR_CODE_ACTION_CATALOG "lower-bound-binary-forward-reverse-gallop4/v1"
#define CR_CODE_MAX_VALUES 4096u
#define CR_CODE_INPUT_BYTES 2u

enum {
  CR_CODE_BINARY = 0u,
  CR_CODE_FORWARD = 1u,
  CR_CODE_REVERSE = 2u,
  CR_CODE_GALLOP = 3u
};
typedef struct {
  uint32_t struct_size, api_version, band, reserved;
  uint64_t count, estimated_rank;
  unsigned char
      bytes[8]; /* First two bytes are the exact encoded observation. */
  double features[CR_FEATURES];
} cr_code_observation;
typedef struct {
  uint32_t struct_size, api_version, action, legal_mask;
  double scores[CR_CODE_ACTIONS];
  char model_digest[CR_DIGEST_HEX];
} cr_code_recommendation;

/* Caller supplies a nondecreasing int32 array. This is the standard lower-bound
 * precondition; observation reads only count, key and the two endpoints, not a
 * full sortedness scan. NULL with zero count is valid. Count is bounded by
 * 4096. Initialize output struct_size and api_version. Failures leave outputs
 * intact. No API here trains, allocates, changes the array, or executes
 * external tools. */
CR_API cr_status CR_CALL cr_code_observe(const int32_t *values, size_t count,
                                         int32_t key, cr_code_observation *out);
/* Requires a qualified model with the exact profile/schema/catalog above.
 * Hosts verify its referenced qualification record before adoption. Scores are
 * normalized over the nonzero four-bit legal mask; ties use lowest action ID.
 * Selection runs outside the host's search hot path: comparison savings do not
 * imply that model selection is faster than one binary search. */
CR_API cr_status CR_CALL cr_code_recommend(const cr_model *model,
                                           const int32_t *values, size_t count,
                                           int32_t key, uint32_t legal_mask,
                                           cr_code_recommendation *out);
/* Pure candidate implementation selected explicitly by the host. Comparisons
 * count reads comparing an array value with key. Result is the first index with
 * value >= key, or count. Both scalar outputs are required and preserved on
 * failure. Sortedness remains the caller precondition. */
CR_API cr_status CR_CALL cr_code_execute(uint32_t action, const int32_t *values,
                                         size_t count, int32_t key,
                                         size_t *index, uint64_t *comparisons);
#ifdef __cplusplus
}
#endif
#endif
