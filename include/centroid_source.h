#ifndef CENTROID_SOURCE_H
#define CENTROID_SOURCE_H

#include "centroid.h"

#ifdef __cplusplus
extern "C" {
#endif

#define C_SOURCE_TRAIN_RECORDS 32u
#define C_SOURCE_TRAIN_POSITIONS 4u
#define C_SOURCE_TRAIN_EPOCHS 1000u
#define C_SOURCE_TRAIN_GENERATIONS 1000000u

typedef struct {
  size_t epochs_completed;
  /* Counts describe the fixed selection used in every epoch. A SOURCE alias is
   * a separate immutable record. Selection uses ascending immutable record IDs.
   */
  size_t available_sources, selected_sources, omitted_sources;
  /* Includes successful idempotent enqueue attempts and the failed attempt, if
   * any. Four locations per selected record include byte zero and EOS; short
   * records can have duplicate locations, so attempts are not unique tasks.
   */
  size_t enqueue_attempts;
  c_training_report training;
} c_source_train_report;

/* Bounded source reconstruction workflow, using current TRAIN SOURCE only.
 * Each epoch enqueues at most 128 attempts at fixed, target-independent
 * offsets, then delegates the requested work quantum to the sole Life trainer
 * authority. epochs must be 1..EPOCHS, generations_per_epoch 0..GENERATIONS. No
 * automatic retrieval, tool execution, corpus-wide fluency or quality claim is
 * implied.
 *
 * Invalid arguments and research owners return C_INVALID before modifying the
 * trainer or output. Other outcomes fill progress and the current committed
 * training state. C_NOT_FOUND means no eligible source was present. A runtime
 * failure preserves earlier committed work; the whole multi-epoch call is not
 * atomic. Queue capacity failures are explicit. The caller owns persistence.
 */
c_status c_source_train(c_trainer *trainer, size_t epochs,
                        size_t generations_per_epoch,
                        c_source_train_report *out);

#ifdef __cplusplus
}
#endif
#endif
