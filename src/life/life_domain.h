/** @file life_domain.h @brief Private transactional native domain round owner. */
#ifndef CGAI_LIFE_DOMAIN_INTERNAL_H
#define CGAI_LIFE_DOMAIN_INTERNAL_H
#include "life_probe.h"
#include "life_training.h"
#include <stdio.h>

typedef struct life_domain_queued {
    uint64_t id;
    cgai_life_domain_task task;
} life_domain_queued;

struct cgai_life_domain {
    life_run run;
    life_probe probe;
    cgai_life_domain_kind kind;
    uint64_t version, rounds, observations, deferred, next_task_id;
    uint64_t encounter_renewals;
    uint64_t group_visits[CGAI_LIFE_GROUPS], group_deferred[CGAI_LIFE_GROUPS];
    uint32_t queue_count, replay_count, replay_cursor;
    life_domain_queued queue[CGAI_LIFE_DOMAIN_QUEUE];
    cgai_life_domain_record replay[CGAI_LIFE_DOMAIN_REPLAY];
    cgai_life_domain_round round;
};

cgai_life_domain *life_domain_clone(const cgai_life_domain *owner);
int life_domain_kind_valid(cgai_life_domain_kind kind);
int life_domain_boundary_capture(const cgai_life_domain *owner,
                                 cgai_life_domain_boundary *boundary);
int life_domain_collect(cgai_life_domain *owner, const cgai_life_events *events);
int life_domain_learn(cgai_life_domain *owner);
uint64_t life_domain_evidence(const cgai_life_domain_record *record);
int life_domain_record_valid(const cgai_life_domain *owner, const cgai_life_domain_record *record);
int life_domain_valid(const cgai_life_domain *owner);
uint64_t life_domain_payload_hash(const cgai_life_domain *owner);
int life_domain_payload_write(FILE *file, const cgai_life_domain *owner);
int life_domain_payload_read(FILE *file, cgai_life_domain *owner);
/** Encode/decode one complete block without requiring stream EOF. Decode validates
 * a private candidate before replacing an initialized or entirely zero owner. */
int life_domain_checkpoint_write(FILE *file, const cgai_life_domain *owner);
int life_domain_checkpoint_read(FILE *file, cgai_life_domain *owner);
#endif
