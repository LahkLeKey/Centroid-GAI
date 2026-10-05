#ifndef CENTROID_H
#define CENTROID_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C_FEATURES 32u
#define C_MAX_GROUPS 4u
#define C_CODE_ACTIONS 4u
#define C_TEXT_ACTIONS 257u
#define C_EOS 256u
#define C_DIGEST_HEX 65u

typedef enum {
  C_OK,
  C_INVALID,
  C_LIMIT,
  C_IO,
  C_NOMEM,
  C_CORRUPT,
  C_NOT_FOUND,
  C_DEFERRED
} c_status;
const char *c_status_string(c_status status);
typedef enum {
  C_SOURCE,
  C_LLM_PROPOSAL,
  C_ACTIVITY,
  C_TRAIN_MEASUREMENT,
  C_DEVELOPMENT,
  C_AUDIT
} c_record_kind;
typedef enum { C_TRAIN, C_DEV, C_HOLDOUT } c_split;
typedef enum { C_TEXT, C_CODE } c_head;
typedef struct c_context c_context;
typedef struct c_model c_model;
typedef struct c_trainer c_trainer;

/* Target-free typed text framing. Complete request and TRAIN evidence remain
 * separate from the visible causal prefix. No answer record, teacher, world or
 * optimizer is required. On failure output is untouched. */
c_status c_text_encode_context(const c_context *context, uint64_t request_id,
                               const uint64_t *evidence_ids,
                               size_t evidence_count,
                               const unsigned char *prefix,
                               size_t prefix_length, double out[C_FEATURES]);

typedef struct {
  uint64_t id, version;
  c_record_kind kind;
  c_split split;
  const char *path, *attribution;
  const unsigned char *bytes;
  size_t length;
  char digest[C_DIGEST_HEX];
  int current;
} c_record;
typedef struct {
  size_t admitted, unchanged, excluded, failed, bytes;
} c_scan_report;
typedef struct {
  uint64_t generation, updates, contacts, queued, completed, deferred, reseeds;
  uint64_t group_updates[C_MAX_GROUPS];
  double last_loss;
} c_training_report;

/* Returned record views are borrowed until the context is destroyed. Admission
 * is bounded, preserves all bytes, and is idempotent for the current version.
 * Admission's id output is optional.
 */
c_status c_context_create(c_context **out);
void c_context_destroy(c_context *context);
c_status c_context_admit(c_context *context, c_record_kind kind, c_split split,
                         const char *path, const char *attribution,
                         const unsigned char *bytes, size_t length,
                         uint64_t *id);
c_status c_context_admit_file(c_context *context, c_record_kind kind,
                              c_split split, const char *path,
                              const char *attribution, uint64_t *id);
c_status c_context_scan(c_context *context, const char *root,
                        c_scan_report *report);
size_t c_context_count(const c_context *context);
c_status c_context_record(const c_context *context, size_t index,
                          c_record *out);
/* Current TRAIN source/proposal/activity only. An empty or unsupported query
 * abstains. score is lexical evidence similarity, never truth confidence. */
c_status c_context_retrieve(const c_context *context,
                            const unsigned char *query, size_t length,
                            c_record *out, double *score);
c_status c_context_save(const c_context *context, const char *path);
c_status c_context_retrieve_kind(const c_context *context, c_record_kind kind,
                                 const unsigned char *query, size_t length,
                                 c_record *out, double *score);
c_status c_context_load(const char *path, c_context **out);

/* Ordered byte encoder; no text decoding, folding, or unknown-token loss. */
c_status c_encode(const unsigned char *bytes, size_t length,
                  double out[C_FEATURES]);
c_status c_model_create(unsigned groups, uint64_t seed, c_model **out);
void c_model_destroy(c_model *model);
c_status c_model_predict(const c_model *model, c_head head,
                         const double input[C_FEATURES], unsigned eligible,
                         const double mass[C_MAX_GROUPS], double *probabilities,
                         size_t capacity);
uint64_t c_model_group_clock(const c_model *model, unsigned group);
unsigned c_model_group_count(const c_model *model);
c_status c_model_fingerprint(const c_model *model, unsigned group,
                             char digest[C_DIGEST_HEX]);

/* The trainer owns a fresh model and context. Borrowed handles allow inference
 * and admission, but the public API exposes no unrestricted model update. */
c_status c_trainer_create(unsigned groups, uint64_t seed, c_trainer **out);
void c_trainer_destroy(c_trainer *trainer);
c_context *c_trainer_context(c_trainer *trainer);
const c_model *c_trainer_model(const c_trainer *trainer);
/* Source target is checked against immutable TRAIN SOURCE bytes. offset may
 * equal length for EOS. Prefix is causal; a future byte never enters its input.
 */
c_status c_trainer_enqueue_source(c_trainer *trainer, uint64_t record_id,
                                  size_t offset, unsigned eligible);
/* Requests and evidence are input-only immutable TRAIN records. Answers must
 * be SOURCE records, not LLM proposals. At most8 evidence records; role tags
 * are formatter metadata, never answer targets. */
c_status c_trainer_enqueue_text(c_trainer *trainer, uint64_t request_record_id,
                                const uint64_t *evidence_ids,
                                size_t evidence_count,
                                uint64_t answer_source_id, size_t offset,
                                unsigned eligible);
/* Explicit bounded replay of completed source/text targets only. Code receipts
 * are never replayed. Eligibility and immutable provenance remain attached. */
c_status c_trainer_replay(c_trainer *trainer, size_t maximum, size_t *requeued);
/* Finite-code supervision requires an independently measured receipt. Parent,
 * evaluator, frozen input, and action set identities must be explicit. Receipts
 * are idempotent and conflict checked; proposals/activity cannot teach. */
c_status c_trainer_enqueue_measurement(
    c_trainer *trainer, const unsigned char *input, size_t length,
    unsigned target, unsigned eligible, const char *parent_digest,
    const char *evaluator_digest, const char *receipt_id);
c_status c_trainer_step(c_trainer *trainer, size_t generations,
                        c_training_report *report);
c_status c_trainer_report(const c_trainer *trainer, c_training_report *report);
c_status c_trainer_save(const c_trainer *trainer, const char *path);
/* Successful load requires *out==NULL. On failure *out is untouched.
 * Load checks the whole canonical checkpoint before publishing the owner. */
c_status c_trainer_load(const char *path, c_trainer **out);

typedef struct {
  const char *program;
  const char *const *argv; /* includes argv[0], terminated by NULL */
  const char *cwd;
  unsigned timeout_ms;
  size_t output_limit;
} c_process_options;
typedef struct {
  int exit_code, timed_out, output_truncated;
  unsigned char *output;
  size_t length, observed_bytes;
  uint64_t elapsed_ms;
} c_process_result;
/* Direct argv execution with null stdin and merged bounded output; no shell. */
c_status c_process_run(const c_process_options *options,
                       c_process_result *result);
void c_process_dispose(c_process_result *result);
/* Explicit work capture: complete raw output is retained at a new immutable
 * artifact path, while the admitted ACTIVITY view is bounded and reports every
 * omitted byte. argv and cwd are attributed observations, not utility labels.
 */
c_status c_context_capture_run(c_context *context,
                               const c_process_options *options,
                               const char *attribution,
                               const char *raw_output_path, uint64_t *id,
                               c_process_result *result);

/* Authored TRAIN and independent AUDIT families, fixed finite C candidate
 * catalog, isolated builds, raw logs and reviewable winning source. Does not
 * apply a candidate to the working tree. Compiler is an explicit executable. */
c_status c_experiment_run(c_trainer *trainer, const char *compiler,
                          const char *directory);
c_status c_benchmark_run(const char *new_output_directory);
c_status c_research_run(const char *new_output_directory);

#ifdef __cplusplus
}
#endif
#endif
