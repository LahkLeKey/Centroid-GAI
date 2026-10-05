#ifndef CENTROID_EXTENSIONS_H
#define CENTROID_EXTENSIONS_H
#include "centroid.h"
/* Candidate forks retain immutable context and consumed receipt history. They
 * never replace the parent implicitly. Training still needs physical contacts.
 */
c_status c_trainer_shared_candidate(const c_trainer *parent, c_trainer **out);
c_status c_trainer_policy_candidate(const c_trainer *parent, c_trainer **out);
c_status c_trainer_merge_candidate(const c_trainer *parent, unsigned first,
                                   unsigned second, c_trainer **out);
size_t c_trainer_retired_count(const c_trainer *trainer);
/* Explicitly consider at most maximum immutable retired entries starting at
 * first. Only TEXT teachers may enqueue new tasks, each with a fresh task ID
 * and the caller's current paired eligibility mask. Original source/request/
 * evidence bytes and retired supervision stay immutable. CODE is never
 * replayed. Any failure preserves the entire incumbent queue and output. */
c_status c_trainer_replay_retired(c_trainer *trainer, size_t first,
                                  size_t maximum, unsigned eligible,
                                  size_t *requeued);
typedef struct {
  uint64_t record_id;
  size_t offset;
} c_source_probe;
typedef struct {
  size_t cases[3], correct_parent[3], correct_candidate[3];
  double loss_parent[3], loss_candidate[3];
  size_t retained_tasks;
  double retention_parent, retention_candidate;
  int accepted;
} c_extension_report;
/* Probe labels are recomputed from immutable SOURCE/DEV/AUDIT bytes. All three
 * splits need >=8 cases. Gate margins are explicit and fixed before evaluation.
 * Candidate selection uses TRAIN/DEV only; no AUDIT fitting is performed. */
c_status c_extension_evaluate(const c_trainer *parent,
                              const c_trainer *candidate,
                              const c_source_probe *probes, size_t count,
                              double maximum_loss_regression,
                              int require_training_gain,
                              c_extension_report *report);
/* Promote only a fully measured accepted fork. Caller retains ownership of the
 * replaced parent for rollback. Inference sessions never call this operation.
 */
c_status c_trainer_promote(c_trainer **incumbent, c_trainer **candidate,
                           const c_source_probe *probes, size_t count,
                           double margin, int require_gain,
                           c_extension_report *report, c_trainer **previous);
c_status c_extensions_run(const char *new_output_directory);
#endif
