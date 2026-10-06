#ifndef CENTROID_RUNTIME_H
#define CENTROID_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define CR_CALL __cdecl
#if defined(CENTROID_RUNTIME_SHARED)
#if defined(CENTROID_RUNTIME_EXPORTS)
#define CR_API __declspec(dllexport)
#else
#define CR_API __declspec(dllimport)
#endif
#else
#define CR_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define CR_CALL
#define CR_API __attribute__((visibility("default")))
#else
#define CR_CALL
#define CR_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CR_API_VERSION 1u
#define CR_ABI_VERSION 1u
#define CR_BUNDLE_VERSION 1u
#define CR_FEATURES 32u
#define CR_MAX_GROUPS 4u
#define CR_CODE_ACTIONS 4u
#define CR_TEXT_ACTIONS 257u
#define CR_EOS 256u
#define CR_DIGEST_HEX 65u
#define CR_ID_BYTES 96u
#define CR_REFERENCE_BYTES 256u
#define CR_MAX_INPUT_BYTES (16u * 1024u * 1024u)
#define CR_MAX_BUNDLE_BYTES (512u * 1024u)
#define CR_PREDICT_SCRATCH_DOUBLES                                             \
  (CR_FEATURES + 2u * CR_MAX_GROUPS + 2u * CR_TEXT_ACTIONS)
#define CR_RECIPE_ID                                                           \
  "centroid-life/1:byte-pos32:role-v1:mix-softmax:adam-owned:b3s23-torus16"
#define CR_ENCODER_ID "byte-pos32/v1"
#define CR_FRAMING_ID "role-v1"
#define CR_ACTION_SCHEMA_ID "code-finite4-text-byte-eos257/v1"
#define CR_LEGACY_PROFILE_ID "legacy-code-text/v1"
#define CR_LEGACY_OBSERVATION_ID "byte-pos32-input/v1"
#define CR_LEGACY_CATALOG_ID "legacy-code4-and-text257/v1"

/* Fixed-width ABI values. Scores are model output mass, not calibrated truth.
 */
typedef uint32_t cr_status;
enum {
  CR_OK = 0u,
  CR_INVALID = 1u,
  CR_LIMIT = 2u,
  CR_IO = 3u,
  CR_NOMEM = 4u,
  CR_CORRUPT = 5u,
  CR_NOT_FOUND = 6u,
  CR_DEFERRED = 7u,
  CR_UNSUPPORTED = 8u,
  CR_CAPACITY = CR_LIMIT
};
typedef uint32_t cr_head;
enum { CR_TEXT = 0u, CR_CODE = 1u };
typedef uint32_t cr_qualification;
enum { CR_EXPERIMENTAL = 0u, CR_QUALIFIED = 1u };

typedef struct cr_model cr_model;

/* Metadata is attributed evidence, not a proof verified by the runtime. A
 * qualified label requires an explicit evidence digest and reference. Hosts
 * still verify the referenced qualification record for their actual use case.
 */
typedef struct {
  uint32_t struct_size, api_version;
  uint32_t qualification, reserved;
  char model_digest[CR_DIGEST_HEX];
  char parent_digest[CR_DIGEST_HEX];
  char checkpoint_digest[CR_DIGEST_HEX];
  char qualification_digest[CR_DIGEST_HEX];
  char task_profile[CR_ID_BYTES];
  char observation_schema[CR_ID_BYTES];
  char action_catalog[CR_ID_BYTES];
  char provenance[CR_REFERENCE_BYTES];
  char qualification_reference[CR_REFERENCE_BYTES];
} cr_model_metadata;

typedef struct {
  uint32_t struct_size, api_version;
  uint32_t abi_version, bundle_version, groups, shared_enabled;
  uint64_t model_bytes, bundle_bytes, parameter_count;
  uint64_t owner_uids[CR_MAX_GROUPS];
  char recipe[CR_ID_BYTES], encoder[CR_ID_BYTES];
  char framing[CR_ID_BYTES], action_schema[CR_ID_BYTES];
  cr_model_metadata metadata;
} cr_model_info;

/* Explicit immutable import for already frozen values. Arrays use group-major,
 * then action-major, then feature-major order. Their exact lengths are groups,
 * groups*32, groups*4*32 and groups*257*32 respectively. All values are copied;
 * caller buffers may be released on return. This is not a training interface.
 */
typedef struct {
  uint32_t struct_size, api_version, groups, shared_enabled;
  const uint64_t *owner_uids;
  const double *shared_scale;
  const double *centroids;
  const double *code;
  const double *text;
  const cr_model_metadata *metadata;
} cr_model_values;

CR_API uint32_t CR_CALL cr_version(void);
CR_API const char *CR_CALL cr_status_string(cr_status status);
/* Exact ordered byte projection, identical to the registered legacy encoder.
 * On failure out is untouched. Prediction uses bounded stack memory and makes
 * no allocation. Model handles may be shared between simultaneous readers. */
CR_API cr_status CR_CALL cr_encode(const unsigned char *bytes, size_t length,
                                   double out[CR_FEATURES]);
CR_API cr_status CR_CALL cr_model_predict(const cr_model *model, cr_head head,
                                          const double input[CR_FEATURES],
                                          uint32_t eligible,
                                          const double mass[CR_MAX_GROUPS],
                                          double *probabilities,
                                          size_t capacity);
/* Caller-owned inference workspace. Only the first
 * CR_PREDICT_SCRATCH_DOUBLES writable double entries are used. This used region
 * must not overlap input, mass, model or the required probability output
 * region. Each concurrent call owns independent scratch. Scratch may change on
 * failure; probabilities remain untouched. NULL or overlapping scratch is
 * CR_INVALID; insufficient scratch/output capacity is CR_LIMIT. No allocation
 * occurs. */
CR_API cr_status CR_CALL cr_model_predict_with_scratch(
    const cr_model *model, cr_head head, const double input[CR_FEATURES],
    uint32_t eligible, const double mass[CR_MAX_GROUPS], double *scratch,
    size_t scratch_doubles, double *probabilities, size_t capacity);
/* *out must be NULL. model_digest is calculated from semantic IDs and values,
 * so metadata.model_digest is ignored. Unsupported/incomplete labels fail. */
CR_API cr_status CR_CALL cr_model_create_values(const cr_model_values *values,
                                                cr_model **out);
CR_API cr_status CR_CALL cr_model_info_get(const cr_model *model,
                                           cr_model_info *out);
CR_API uint64_t CR_CALL cr_model_group_uid(const cr_model *model,
                                           uint32_t group);
CR_API void CR_CALL cr_model_destroy(cr_model *model);

/* Canonical little-endian IEEE binary64 bundles, with SHA-256 integrity.
 * Load validates a complete candidate before replacing and freeing *inout.
 * A failed load leaves the incumbent and all its bytes untouched. A host must
 * synchronize replacements/destruction with outstanding readers. Bytes are
 * copied by the loader; caller retains ownership of its buffer. */
CR_API cr_status CR_CALL cr_model_load_bytes(const unsigned char *bytes,
                                             size_t length, cr_model **inout);
CR_API cr_status CR_CALL cr_model_load_file(const char *path, cr_model **inout);
/* Checks qualification sidecar bytes against the recorded SHA256. This checks
 * identity only: the host still reviews the referenced quality record and
 * trusts its publisher. No evaluation, training or model replacement occurs. */
CR_API cr_status CR_CALL
cr_model_check_qualification_file(const cr_model *model, const char *path);
CR_API size_t CR_CALL cr_model_bundle_size(const cr_model *model);
/* A capacity failure sets *written to the required length; bytes are untouched.
 * Successful serialization is deterministic across supported toolchains.
 * bytes and written must not overlap each other or immutable model storage;
 * detectable overlap is rejected before either output is written. */
CR_API cr_status CR_CALL cr_model_write_bundle(const cr_model *model,
                                               unsigned char *bytes,
                                               size_t capacity,
                                               size_t *written);
/* Creates a new immutable asset. Existing paths are refused and preserved. */
CR_API cr_status CR_CALL cr_model_save_file(const cr_model *model,
                                            const char *path);

#ifdef __cplusplus
}
#endif
#endif
