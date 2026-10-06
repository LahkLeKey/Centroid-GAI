#ifndef CENTROID_RUNTIME_CONTEXT_H
#define CENTROID_RUNTIME_CONTEXT_H
#include "centroid_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CR_CONTEXT_PROFILE "evidence-lexical-cosine/v1"
#define CR_CONTEXT_MAX_RECORDS 4096u
#define CR_CONTEXT_MAX_HITS 16u
#define CR_CONTEXT_MAX_PATH 4096u
#define CR_CONTEXT_MAX_ATTRIBUTION 1024u
#define CR_TEXT_MAX_EVIDENCE 8u
#define CR_TEXT_MAX_FRAME_BYTES (1024u * 1024u)
typedef struct cr_context cr_context;
typedef uint32_t cr_record_kind;
enum { CR_SOURCE = 0u, CR_LLM_PROPOSAL = 1u, CR_ACTIVITY = 2u };
typedef uint32_t cr_split;
enum { CR_TRAIN = 0u, CR_DEV = 1u, CR_AUDIT = 2u };
typedef struct {
  uint32_t struct_size, api_version, max_records, reserved;
  uint64_t max_record_bytes, max_total_bytes;
} cr_context_options;
typedef struct {
  uint64_t id, version;
  uint32_t kind, split, current, reserved;
  const char *path, *attribution;
  const unsigned char *bytes;
  size_t length;
  char digest[CR_DIGEST_HEX];
} cr_record_view;
typedef struct {
  uint32_t struct_size, api_version, max_hits, kind_mask;
  uint64_t byte_budget, excerpt_bytes;
} cr_query_options;
typedef struct {
  cr_record_view record;
  size_t offset, length;
  double score;
} cr_evidence;
typedef struct {
  uint64_t returned, matched, omitted, excerpt_bytes;
  uint32_t abstained, reserved;
} cr_query_report;
typedef struct {
  uint64_t admitted, unchanged, excluded, failed, bytes;
} cr_scan_report;

CR_API void CR_CALL cr_context_options_init(cr_context_options *options);
CR_API void CR_CALL cr_query_options_init(cr_query_options *options);
CR_API cr_status CR_CALL cr_context_create(const cr_context_options *options,
                                           cr_context **out);
CR_API void CR_CALL cr_context_destroy(cr_context *context);
/* Records and output views remain valid until context destruction; admission
 * preserves earlier versions. Mutable contexts require host synchronization.
 * TRAIN reserved evaluation paths are rejected; DEV/AUDIT are quarantined. */
CR_API cr_status CR_CALL cr_context_admit(cr_context *context,
                                          cr_record_kind kind, cr_split split,
                                          const char *path,
                                          const char *attribution,
                                          const unsigned char *bytes,
                                          size_t length, uint64_t *id);
CR_API size_t CR_CALL cr_context_count(const cr_context *context);
CR_API cr_status CR_CALL cr_context_record(const cr_context *context,
                                           size_t index, cr_record_view *out);
/* Scans ordinary source files in deterministic path order. Excludes links,
 * hidden/build/data/tests/research/vendor directories and evaluation roots.
 * Refreshes only admitted paths; deletion is made explicit with forget_source.
 * A partial scan reports failures and returns an error; never silent success.
 */
CR_API cr_status CR_CALL cr_context_scan(cr_context *context, const char *root,
                                         cr_scan_report *report);
CR_API cr_status CR_CALL cr_context_forget_source(cr_context *context,
                                                  const char *path);
/* Query is allocation-free. Budget counts returned raw excerpt bytes; metadata
 * and host prompt formatting have separate budgets. No arbitrary LLM token
 * count is promised. Only current TRAIN records may be returned. */
CR_API cr_status CR_CALL cr_context_query(const cr_context *context,
                                          const unsigned char *query,
                                          size_t length,
                                          const cr_query_options *options,
                                          cr_evidence *out, size_t capacity,
                                          cr_query_report *report);
/* Formats attributed evidence into a caller buffer, including exact raw bytes.
 * Required includes a trailing NUL. Capacity failure leaves buffer untouched.
 * Output must not overlap evidence, borrowed record bytes or required.
 * Embedded NUL bytes are retained; use written lengths rather than strlen. */
CR_API cr_status CR_CALL cr_evidence_format(const cr_evidence *evidence,
                                            size_t count, unsigned char *out,
                                            size_t capacity, size_t *required);
/* Exact target-free role-v1 request/evidence/causal-prefix bytes. Request is a
 * TRAIN SOURCE or ACTIVITY; explicitly selected historical TRAIN records are
 * allowed. Evidence is TRAIN SOURCE/LLM_PROPOSAL/ACTIVITY, at most eight
 * distinct IDs. DEV/AUDIT fail. No answer record, target, trainer or provider
 * is used. Binary output has no trailing NUL. On capacity failure required is
 * the exact byte count and output is untouched. A frame over 1 MiB is refused.
 * Caller output and required must not overlap each other, inputs or borrowed
 * context storage. Call cr_encode on the returned frame to obtain role-v1 model
 * features; this path allocates nothing. */
CR_API cr_status CR_CALL
cr_text_frame_context(const cr_context *context, uint64_t request_id,
                      const uint64_t *evidence_ids, size_t evidence_count,
                      const unsigned char *prefix, size_t prefix_length,
                      unsigned char *out, size_t capacity, size_t *required);
#ifdef __cplusplus
}
#endif
#endif
