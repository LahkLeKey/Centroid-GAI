#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* A code experiment may inspect a genuinely available next encounter, but only
 * trainer.c may advance the world or consume its measured training receipts. */
static unsigned physical_participants(unsigned graph, unsigned eligible,
                                      unsigned groups) {
  unsigned participants = 0;
  for (unsigned first = 0; first < groups; ++first)
    for (unsigned second = first + 1u; second < groups; ++second)
      if ((graph & (1u << (first * C_MAX_GROUPS + second))) &&
          (eligible & (1u << first)) && (eligible & (1u << second)))
        participants |= (1u << first) | (1u << second);
  return participants;
}

c_status c_experiment_contact_preflight(const c_trainer *trainer,
                                        unsigned eligible,
                                        c_experiment_contact *out) {
  unsigned char after[C_WORLD_CELLS];
  c_experiment_contact contact = {0};
  if (!trainer || !trainer->model || !out || trainer->research_mode ||
      trainer->model->groups < 2u || trainer->model->groups > C_MAX_GROUPS ||
      !eligible || (eligible >> trainer->model->groups))
    return C_INVALID;
  c_status status = c_trainer_validate(trainer);
  if (status != C_OK)
    return status;
  if (trainer->report.queued)
    return C_DEFERRED;
  c_life_evolve(trainer->cells, after, &contact.graph);
  unsigned admitted_graph = 0;
  for (unsigned first = 0; first < trainer->model->groups; ++first)
    for (unsigned second = first + 1u; second < trainer->model->groups;
         ++second)
      if ((eligible & (1u << first)) && (eligible & (1u << second)))
        admitted_graph |=
            contact.graph & (1u << (first * C_MAX_GROUPS + second));
  contact.graph = admitted_graph;
  contact.participants =
      physical_participants(contact.graph, eligible, trainer->model->groups);
  if (!contact.participants ||
      (trainer->model->shared_enabled &&
       contact.participants != (1u << trainer->model->groups) - 1u))
    return C_DEFERRED;
  contact.generation = trainer->generation;
  contact.groups = trainer->model->groups;
  contact.eligible = eligible;
  for (unsigned group = 0; group < contact.groups; ++group)
    contact.owner_uids[group] = trainer->model->expert[group].uid;
  memcpy(contact.cells, trainer->cells, sizeof(contact.cells));
  *out = contact;
  return C_OK;
}

static c_status file_digest(const char *path, char digest[C_DIGEST_HEX]) {
  unsigned char *bytes = NULL;
  size_t length = 0;
  c_status status = c_read_file(path, &bytes, &length);
  if (status == C_OK)
    c_hash(bytes, length, digest);
  free(bytes);
  return status;
}

c_status c_experiment_contact_snapshot(const c_trainer *trainer,
                                       const char *directory,
                                       c_experiment_contact *contact) {
  char path[4096];
  c_experiment_contact actual;
  c_writer writer = {0};
  if (!contact || !directory || !directory[0])
    return C_INVALID;
  c_status status =
      c_experiment_contact_preflight(trainer, contact->eligible, &actual);
  if (status != C_OK)
    return status;
  if (actual.generation != contact->generation ||
      actual.groups != contact->groups || actual.graph != contact->graph ||
      actual.participants != contact->participants ||
      memcmp(actual.owner_uids, contact->owner_uids,
             sizeof(actual.owner_uids)) ||
      memcmp(actual.cells, contact->cells, sizeof(actual.cells)))
    return C_CORRUPT;
  int n = snprintf(path, sizeof(path), "%s/model-parent.centroid", directory);
  if (n < 0 || (size_t)n >= sizeof(path))
    return C_LIMIT;
  status = c_trainer_save(trainer, path);
  if (status == C_OK)
    status = file_digest(path, actual.checkpoint_digest);
  if (status == C_OK) {
    c_put_u32(&writer, 1u);
    c_put_u64(&writer, actual.generation);
    c_put_u32(&writer, actual.groups);
    for (unsigned group = 0; group < actual.groups; ++group)
      c_put_u64(&writer, actual.owner_uids[group]);
    c_put_u32(&writer, actual.eligible);
    c_put_u32(&writer, actual.participants);
    c_put_u32(&writer, actual.graph);
    c_put_bytes(&writer, actual.checkpoint_digest, C_DIGEST_HEX - 1u);
    c_put_bytes(&writer, actual.cells, sizeof(actual.cells));
    status = writer.status;
  }
  if (status == C_OK) {
    n = snprintf(path, sizeof(path), "%s/contact.bin", directory);
    status = n < 0 || (size_t)n >= sizeof(path) ? C_LIMIT : C_OK;
  }
  if (status == C_OK)
    status = c_envelope_write(path, "CCONT001", writer.data, writer.length);
  if (status == C_OK)
    status = file_digest(path, actual.proof_digest);
  free(writer.data);
  if (status == C_OK)
    *contact = actual;
  return status;
}
