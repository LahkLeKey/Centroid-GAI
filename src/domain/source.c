#include "centroid_source.h"
#include "internal.h"

/* Keep the earliest immutable IDs even if a context implementation changes its
 * enumeration order. Record byte views remain immutable during this workflow.
 */
static void select_source(c_record selected[C_SOURCE_TRAIN_RECORDS],
                          size_t *count, const c_record *record) {
  size_t position = 0;
  while (position < *count && selected[position].id < record->id)
    ++position;
  if (position == C_SOURCE_TRAIN_RECORDS)
    return;
  size_t end = *count < C_SOURCE_TRAIN_RECORDS ? *count : *count - 1u;
  for (size_t i = end; i > position; --i)
    selected[i] = selected[i - 1u];
  selected[position] = *record;
  if (*count < C_SOURCE_TRAIN_RECORDS)
    ++*count;
}

c_status c_source_train(c_trainer *trainer, size_t epochs,
                        size_t generations_per_epoch,
                        c_source_train_report *out) {
  c_source_train_report progress = {0};
  c_training_report training = {0};
  c_record selected[C_SOURCE_TRAIN_RECORDS];
  c_status status;
  if (!trainer || !out || !epochs || epochs > C_SOURCE_TRAIN_EPOCHS ||
      generations_per_epoch > C_SOURCE_TRAIN_GENERATIONS ||
      trainer->research_mode || !trainer->model || !trainer->context ||
      trainer->model->groups < 2u || trainer->model->groups > C_MAX_GROUPS)
    return C_INVALID;
  status = c_trainer_report(trainer, &training);
  progress.training = training;
  for (size_t i = 0; status == C_OK && i < c_context_count(trainer->context);
       ++i) {
    c_record record;
    status = c_context_record(trainer->context, i, &record);
    if (status == C_OK && record.kind == C_SOURCE && record.split == C_TRAIN &&
        record.current) {
      ++progress.available_sources;
      select_source(selected, &progress.selected_sources, &record);
    }
  }
  progress.omitted_sources =
      progress.available_sources - progress.selected_sources;
  if (status == C_OK && !progress.selected_sources)
    status = C_NOT_FOUND;
  const unsigned eligible = (1u << trainer->model->groups) - 1u;
  for (size_t epoch = 0; status == C_OK && epoch < epochs; ++epoch) {
    for (size_t i = 0; status == C_OK && i < progress.selected_sources; ++i) {
      const c_record *record = &selected[i];
      for (size_t k = 0; status == C_OK && k < C_SOURCE_TRAIN_POSITIONS; ++k) {
        const size_t offset =
            (record->length / 3u) * k + (record->length % 3u) * k / 3u;
        ++progress.enqueue_attempts;
        status =
            c_trainer_enqueue_source(trainer, record->id, offset, eligible);
      }
    }
    if (status == C_OK)
      status = c_trainer_step(trainer, generations_per_epoch, &training);
    if (status == C_OK)
      ++progress.epochs_completed;
  }
  /* An enqueue failure can follow earlier admissions. Report what is committed
   * now rather than the last successfully completed epoch's queue snapshot.
   */
  c_status reported = c_trainer_report(trainer, &training);
  progress.training = training;
  if (status == C_OK)
    status = reported;
  *out = progress;
  return status;
}
