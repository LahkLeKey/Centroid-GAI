#ifndef CENTROID_INTERNAL_H
#define CENTROID_INTERNAL_H
#include "centroid.h"
#include <stdio.h>

#define C_WORLD_SIDE 16u
#define C_WORLD_CELLS (C_WORLD_SIDE * C_WORLD_SIDE)
#define C_MAX_TASKS 256u
#define C_MAX_RECEIPTS 1024u
#define C_MAX_RETIRED_TASKS (2u * C_MAX_TASKS)
#define C_MAX_FILE_BYTES (16u * 1024u * 1024u)
#define C_RECIPE                                                               \
  "centroid-life/1:byte-pos32:role-v1:mix-softmax:adam-owned:b3s23-torus16"

typedef struct {
  double value, first, second;
} c_scalar;
typedef struct {
  uint64_t uid, clock;
  c_scalar centroid[C_FEATURES];
  c_scalar code[C_CODE_ACTIONS][C_FEATURES];
  c_scalar text[C_TEXT_ACTIONS][C_FEATURES];
} c_expert;
struct c_model {
  unsigned groups;
  uint64_t seed;
  unsigned shared_enabled;
  uint64_t shared_clock;
  c_scalar shared_scale[C_FEATURES];
  c_expert expert[C_MAX_GROUPS];
};
typedef struct {
  double centroid[C_FEATURES];
  double readout[C_TEXT_ACTIONS][C_FEATURES];
} c_gradient;
typedef struct {
  uint64_t id, source_id, offset;
  c_head head;
  unsigned target, eligible, done;
  double input[C_FEATURES];
  char parent[C_DIGEST_HEX], evaluator[C_DIGEST_HEX], receipt[C_DIGEST_HEX];
  char input_digest[C_DIGEST_HEX];
  unsigned format, evidence_count;
  uint64_t request_id, evidence_ids[8];
} c_task;
typedef struct {
  c_task task; /* Original completed task and binding never rewritten. */
  unsigned groups, merge_index;
  uint64_t owner_uids[C_MAX_GROUPS];
} c_retired_task;
typedef struct {
  unsigned enabled, editing_enabled;
  uint64_t clock, interventions, contact_changes;
  c_scalar readout[4][C_FEATURES];
} c_policy;
struct c_trainer {
  c_model *model;
  c_context *context;
  unsigned char cells[C_WORLD_CELLS]; /* zero dead, nonzero lineage bitmask */
  uint64_t generation, rng, next_task;
  c_task tasks[C_MAX_TASKS];
  size_t task_count;
  char receipts[C_MAX_RECEIPTS][C_DIGEST_HEX];
  char receipt_bindings[C_MAX_RECEIPTS][C_DIGEST_HEX];
  unsigned char receipt_consumed[C_MAX_RECEIPTS];
  size_t receipt_count;
  c_training_report report;
  unsigned research_mode;
  c_policy policy;
  unsigned merge_count;
  uint64_t retired_clock;
  uint64_t merge_parents[2][2], merge_children[2];
  uint64_t merge_generations[2];
  unsigned char merge_worlds[2][C_WORLD_CELLS];
  c_retired_task retired[C_MAX_RETIRED_TASKS];
  size_t retired_count;
};

void c_hash(const void *bytes, size_t length, char out[C_DIGEST_HEX]);
uint64_t c_random(uint64_t *state);
int c_checked_add(size_t a, size_t b, size_t *out);
int c_checked_mul(size_t a, size_t b, size_t *out);
c_status c_read_file(const char *path, unsigned char **bytes, size_t *length);
c_status c_write_atomic(const char *path, const void *bytes, size_t length);
c_status c_make_directory(const char *path);
c_status c_new_directory(const char *path);
uint64_t c_monotonic_ms(void);
uint64_t c_monotonic_ns(void);
uint64_t c_process_peak_memory_bytes(void);
c_status c_process_capture_file(const c_process_options *options,
                                const char *raw_output_path,
                                c_process_result *result);

/* Numerical kernels are internal; only trainer.c invokes apply in production.
 */
c_status c_model_gradient(const c_model *model, c_head head,
                          const double input[C_FEATURES], unsigned eligible,
                          const double mass[C_MAX_GROUPS], unsigned target,
                          c_gradient gradients[C_MAX_GROUPS], double *loss);
c_status c_model_apply(c_model *model, c_head head, unsigned participants,
                       const c_gradient gradients[C_MAX_GROUPS], double rate);
c_status c_model_shared_gradient(const c_model *model, c_head head,
                                 const double input[C_FEATURES],
                                 unsigned eligible,
                                 const double mass[C_MAX_GROUPS],
                                 unsigned target, double out[C_FEATURES]);
c_status c_model_apply_shared(c_model *model, c_head head,
                              unsigned participants,
                              const c_gradient gradients[C_MAX_GROUPS],
                              const double shared[C_FEATURES], double rate);
int c_scalar_adam(c_scalar *scalar, double gradient, double rate,
                  double first_correction, double second_correction);
c_status c_policy_prepare(const c_trainer *trainer,
                          const unsigned char evolved[C_WORLD_CELLS],
                          unsigned contacts, c_policy *candidate,
                          unsigned char edited[C_WORLD_CELLS]);
c_status c_trainer_research_create(unsigned groups, uint64_t seed,
                                   c_trainer **out);
c_status c_trainer_research_enqueue(c_trainer *trainer, uint64_t source_id,
                                    size_t offset, unsigned eligible,
                                    const double input[C_FEATURES]);
c_status c_trainer_research_step(c_trainer *trainer, size_t generations,
                                 unsigned scheduler, c_training_report *report);
c_status c_context_pack(const c_context *context, unsigned char **bytes,
                        size_t *length);
c_status c_context_unpack(const unsigned char *bytes, size_t length,
                          c_context **out);
void c_life_evolve(const unsigned char before[C_WORLD_CELLS],
                   unsigned char after[C_WORLD_CELLS], unsigned *contacts);
void c_life_seed(c_trainer *trainer);
c_status c_task_binding(const c_task *task, char digest[C_DIGEST_HEX]);
c_status c_text_prepare_task(const c_context *context, uint64_t request_id,
                             const uint64_t *evidence_ids,
                             size_t evidence_count, uint64_t answer_id,
                             size_t offset, unsigned eligible, c_task *out);
c_status c_text_verify_task(const c_context *context, const c_task *task);

/* Small canonical little-endian codec. Reader errors are sticky. */
typedef struct {
  unsigned char *data;
  size_t length, capacity;
  c_status status;
} c_writer;
typedef struct {
  const unsigned char *data;
  size_t length, offset;
  c_status status;
} c_reader;
void c_put_bytes(c_writer *writer, const void *data, size_t length);
void c_put_u32(c_writer *writer, uint32_t value);
void c_put_u64(c_writer *writer, uint64_t value);
void c_put_double(c_writer *writer, double value);
void c_get_bytes(c_reader *reader, void *out, size_t length);
uint32_t c_get_u32(c_reader *reader);
uint64_t c_get_u64(c_reader *reader);
double c_get_double(c_reader *reader);
void c_model_write(c_writer *writer, const c_model *model, int extensions);
c_status c_model_read(c_reader *reader, c_model **out, int extensions);
c_status c_trainer_validate(const c_trainer *trainer);
/* Read-only code-trial encounter proof. The whole retained same-build parent
 * checkpoint and canonical starting-world proof are bound into TRAIN receipts.
 * No preflight or snapshot operation advances Life or mutates parameters. */
typedef struct {
  uint64_t generation, owner_uids[C_MAX_GROUPS];
  unsigned groups, eligible, participants, graph;
  unsigned char cells[C_WORLD_CELLS];
  char checkpoint_digest[C_DIGEST_HEX], proof_digest[C_DIGEST_HEX];
} c_experiment_contact;
c_status c_experiment_contact_preflight(const c_trainer *trainer,
                                        unsigned eligible,
                                        c_experiment_contact *out);
c_status c_experiment_contact_snapshot(const c_trainer *trainer,
                                       const char *directory,
                                       c_experiment_contact *contact);
size_t c_trainer_retention_count(const c_trainer *trainer);
c_status c_trainer_retention_task(const c_trainer *trainer, size_t index,
                                  const c_task **out);
c_status c_trainer_task_scope(const c_trainer *parent,
                              const c_trainer *candidate, const c_task *task,
                              unsigned *mask);
c_status c_envelope_write(const char *path, const char magic[8],
                          const unsigned char *data, size_t length);
c_status c_envelope_read(const char *path, const char magic[8],
                         unsigned char **data, size_t *length);
#endif
