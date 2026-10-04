/** @file centroid_life_domain.h @brief Typed native domain adapters owned by Life encounters. */
#ifndef CENTROID_LIFE_DOMAIN_H
#define CENTROID_LIFE_DOMAIN_H
#include "centroid_life.h"

#define CGAI_LIFE_DOMAIN_VERSION 1U
#define CGAI_LIFE_DOMAIN_FEATURES 16U
#define CGAI_LIFE_DOMAIN_QUEUE 64U
#define CGAI_LIFE_DOMAIN_REPLAY 128U
#define CGAI_LIFE_DOMAIN_TASKS_PER_CONTACT 2U
#define CGAI_LIFE_DOMAIN_ROUND_RECORDS                                                             \
    (CGAI_LIFE_MAX_COLLISION_EVENTS * CGAI_LIFE_DOMAIN_TASKS_PER_CONTACT)
#define CGAI_LIFE_DOMAIN_FUTURE_EXPERT_CAPACITY 128U

typedef struct cgai_life_domain cgai_life_domain;
typedef enum cgai_life_domain_kind {
    CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE = 1,
    CGAI_LIFE_DOMAIN_NATIVE_NPC = 2
} cgai_life_domain_kind;

/** Typed visible categorical input. Probe uses x/y and verifier (x+2*y)%3;
 * NPC uses sixteen complete observed/history fields and its visible-only teacher.
 * Source identity names attributed input bytes, not trusted instructions or proof.
 * Eligibility uses stable model UIDs. All unused UID entries must be zero.
 * Only reviewed TRAIN tasks can enter the training queue. No target is admitted. */
typedef struct cgai_life_domain_task {
    uint32_t version, family;
    cgai_life_context_split split;
    uint32_t reviewed;
    uint64_t source_hash;
    uint32_t x, y;
    uint32_t observed_fields; /**< Probe3, NPC0xffff: all independently visible fields. */
    uint32_t legal_actions;   /**< Nonzero adapter-specific categorical permission mask. */
    uint32_t fallback;        /**< One permitted categorical action. */
    uint32_t eligible_count;
    uint32_t eligible_uids[CGAI_LIFE_GROUPS];
    /** NPC adapter: all sixteen categorical visible/history fields. Probe fields
     * remain zero. NPC x/y remain zero and observed_fields is exactly0xffff. */
    uint32_t feature_count;
    uint32_t features[CGAI_LIFE_DOMAIN_FEATURES];
    uint64_t episode_id; /**< NPC provenance only; never a numeric model feature. */
    uint32_t tick;       /**< Actual NPC observation tick, before its host action. */
} cgai_life_domain_task;

typedef struct cgai_life_domain_prediction {
    uint64_t context_hash, model_version;
    uint32_t action, participant_count;
    double probability;
} cgai_life_domain_prediction;

typedef enum cgai_life_domain_admission {
    CGAI_LIFE_DOMAIN_ADMITTED = 0,
    CGAI_LIFE_DOMAIN_UNSUPPORTED_TARGET = 1
} cgai_life_domain_admission;

/** One independently verified outcome; domain action IDs are never cell toggles.
 * Proposals use stable domain binding order; nonparticipant entries are zero.
 * Every record in a round captures the same frozen domain parent version. */
typedef struct cgai_life_domain_record {
    uint64_t task_id, context_hash, parent_version, teacher_identity, evidence_hash;
    cgai_life_domain_task task;
    cgai_life_collision_event contact;
    uint32_t participant_mask;
    uint32_t participant_uids[CGAI_LIFE_GROUPS];
    uint32_t proposals[CGAI_LIFE_GROUPS];
    double probabilities[CGAI_LIFE_GROUPS];
    /** Frozen contact selection. External NPC host actions are recorded by the
     * episode owner separately and can use a different serving UID mixture. */
    uint32_t executed_action, target, verified;
    cgai_life_domain_admission admission;
} cgai_life_domain_record;

/** Complete-round optimizer provenance. Hashes include weights and moments.
 * Domain arrays use stable model binding order; cell arrays use physical Life
 * slots, whose UID/ancestry bindings are recorded separately in each contact. */
typedef struct cgai_life_domain_boundary {
    uint64_t shared_hash, cell_shared_hash;
    uint64_t group_steps[CGAI_LIFE_GROUPS], group_hashes[CGAI_LIFE_GROUPS];
    uint64_t cell_group_steps[CGAI_LIFE_GROUPS], cell_group_hashes[CGAI_LIFE_GROUPS];
} cgai_life_domain_boundary;

typedef struct cgai_life_domain_round {
    uint32_t generation, count;
    uint64_t parent_version, result_version;
    cgai_life_domain_boundary before, after;
    cgai_life_domain_record records[CGAI_LIFE_DOMAIN_ROUND_RECORDS];
} cgai_life_domain_round;

typedef struct cgai_life_domain_stats {
    uint32_t generation, group_count, expert_count, queued, replay_records;
    uint64_t version, rounds, observations, deferred, training_updates;
    uint64_t encounter_renewals;
    uint64_t shared_hash, cell_policy_hash, cell_shared_hash;
    uint32_t group_uids[CGAI_LIFE_GROUPS];
    uint64_t group_steps[CGAI_LIFE_GROUPS], group_hashes[CGAI_LIFE_GROUPS];
    uint64_t cell_group_steps[CGAI_LIFE_GROUPS], cell_group_hashes[CGAI_LIFE_GROUPS];
    uint64_t group_visits[CGAI_LIFE_GROUPS], group_deferred[CGAI_LIFE_GROUPS];
} cgai_life_domain_stats;

/** Built-in native registry: categorical probe and visible/history NPC actions.
 * Uses configured2..8 groups, learned cell mode, and disabled merges. Output starts NULL. */
cgai_life_status cgai_life_domain_create(const cgai_life_config *config, cgai_life_domain_kind kind,
                                         cgai_life_domain **output);
void cgai_life_domain_destroy(cgai_life_domain *owner);
/** Copy a TRAIN input into FIFO without fitting. Exact admissions are idempotent. */
cgai_life_status cgai_life_domain_enqueue(cgai_life_domain *owner,
                                          const cgai_life_domain_task *task, uint64_t *task_id);
/** Authentic world rounds: freeze all proposals, verify all results, then update
 * current-contact owned slices on a private candidate. Task/UID pairs are reserved
 * once per round; disjoint contacts may process the same queued context. Unsupported
 * targets cannot trigger replay updates. Each generation is atomic. Authored
 * encounters renew every32 training generations independently of task labels. */
cgai_life_status cgai_life_domain_train_step(cgai_life_domain *owner, uint32_t generations,
                                             uint32_t *completed);
/** Frozen cell evolution. No domain verifier, queue/replay admission, optimizer,
 * training diagnostics or merges. Supplied task scoring uses predict separately. */
cgai_life_status cgai_life_domain_evaluate_step(cgai_life_domain *owner, uint32_t generations,
                                                uint32_t *completed);
/** Frozen domain inference from supplied visible input and stable UID restrictions.
 * Accepts held-out inputs without admitting them; no contact or teacher is consulted. */
cgai_life_status cgai_life_domain_predict(const cgai_life_domain *owner,
                                          const cgai_life_domain_task *task, const uint32_t *uids,
                                          size_t count, cgai_life_domain_prediction *output);
cgai_life_status cgai_life_domain_get_stats(const cgai_life_domain *owner,
                                            cgai_life_domain_stats *output);
cgai_life_status cgai_life_domain_get_round(const cgai_life_domain *owner,
                                            cgai_life_domain_round *output);
uint64_t cgai_life_domain_hash(const cgai_life_domain *owner);
/** Complete versioned world/auxiliary/domain/queue/replay/round continuation.
 * Saves atomically; a failed load preserves the entire incumbent. Retains the
 * latest complete round and bounded admitted replay, not a full outcome ledger. */
cgai_life_status cgai_life_domain_save(const cgai_life_domain *owner, const char *path);
cgai_life_status cgai_life_domain_load(cgai_life_domain *owner, const char *path);
#endif
