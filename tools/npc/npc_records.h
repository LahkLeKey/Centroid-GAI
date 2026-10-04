/** @file npc_records.h @brief Bounded verified teacher and recovery record fixtures. */
#ifndef CGAI_NPC_RECORDS_H
#define CGAI_NPC_RECORDS_H
#include "npc_policy.h"
/** Maximum selected records, including exactly replayed recovery demonstrations. */
#define NPC_RECORD_CAPACITY 8192U
/** Collect verified teacher and initialized-policy recovery trajectories.
 * @param examples Writable capacity-limited ordered records.
 * @param capacity Number of writable slots.
 * @param count Writable actual record count.
 * @param history Nonzero retains observation history; zero masks identical records.
 * @return Success after complete reproducible collection, failure otherwise. */
cgai_status npc_records_collect(cgai_gameplay_example *examples, size_t capacity, size_t *count,
                                int history);
/** Write the verified deterministic record fixture without updating a model.
 * @param path Borrowed destination filesystem path.
 * @param history Nonzero retains observation history; zero masks identical records.
 * @return Success after complete record/provenance write, failure otherwise. */
cgai_status npc_records_write(const char *path, int history);
#endif
