/** @file evolve_checkpoint.h @brief Immutable complete-generation source-search bundles. */
#ifndef CGAI_EVOLVE_CHECKPOINT_H
#define CGAI_EVOLVE_CHECKPOINT_H

#include "centroid_life.h"
#include "fitness.h"
#include "mutation.h"
#include <stdint.h>

#define EVOLVE_CHECKPOINT_MAX_RECEIPTS 8U
#define EVOLVE_CHECKPOINT_PROOF_BYTES 1024U

typedef enum evolve_checkpoint_audit_status {
    EVOLVE_CHECKPOINT_ACCEPTED = 0,
    EVOLVE_CHECKPOINT_DEV_REJECTED = 1,
    EVOLVE_CHECKPOINT_CONFIRM_REJECTED = 2,
    EVOLVE_CHECKPOINT_CONFIGURE_REJECTED = 3,
    EVOLVE_CHECKPOINT_BUILD_REJECTED = 4,
    EVOLVE_CHECKPOINT_TEST_REJECTED = 5,
    EVOLVE_CHECKPOINT_LINT_REJECTED = 6,
    EVOLVE_CHECKPOINT_FITNESS_REJECTED = 7
} evolve_checkpoint_audit_status;

struct evolve_run;
struct evolve_trial;

/** Completed observations bind source lineage and the action-head update chain. */
typedef struct evolve_checkpoint_receipt {
    uint64_t token;
    uint64_t input_hash;
    uint64_t evidence_hash;
    uint64_t parent_checksum;
    uint64_t candidate_checksum;
    uint64_t version_before;
    uint64_t version_after;
    uint32_t generation;
    uint32_t action;
    uint32_t participant_mask;
    uint32_t accepted;
    uint32_t verified;
    uint32_t deferred;
    double utility;
    uint64_t chain_hash;
    char input[CGAI_LIFE_CONTEXT_TEXT_BYTES + 1U];
    char proof[EVOLVE_CHECKPOINT_PROOF_BYTES];
    uint32_t proof_bytes;
    cgai_life_collision_event event;
    cgai_life_context_choice choice;
    cgai_life_context_choice prediction_after;
    cgai_life_context_choice_stats before;
    cgai_life_context_choice_stats after;
    uint32_t actions[EVOLVE_MUTATION_MAX_ALTERNATIVES];
    uint32_t action_count;
    uint32_t fallback;
    uint32_t audit_status;
    uint32_t domain_frontier_bits;
    uint32_t edit_count;
    evolve_mutation_edit edits[EVOLVE_MUTATION_MAX_EDITS];
    life_fitness_report training;
    life_fitness_report development;
    life_fitness_report confirmation;
    double parent_training_loss;
} evolve_checkpoint_receipt;

/** Canonical identity of the configured native compiler/evaluator build recipe. */
uint64_t evolve_checkpoint_build_recipe(void);

/** Append one observed trial after proposals++, before adopting/incrementing accepted.
 * Failed or duplicate observations preserve the ledger. Receipt is TRAIN feedback;
 * an accepted candidate must have verified, nondeferred evidence. */
int evolve_checkpoint_record(struct evolve_run *run, const struct evolve_trial *trial,
                             uint32_t accepted);

/** Publish a NEW checkpoint-NNNN directory after an entire search generation.
 * Generation zero is the prepared/measured baseline. Parent bytes, model, current
 * and baseline reports, budgets, counters and receipts are self-contained. Stage
 * directory rename is atomic; prior bundles are never replaced. Pending choices
 * reject. Inputs and source/context identities are rechecked before publication.
 * On success update run checkpoint_hash/predecessor_hash/latest_checkpoint only. */
int evolve_checkpoint_publish(struct evolve_run *run, const char *source_root,
                              const char *inputs_manifest);

/** Validate the complete bundle and live pinned identities before replacing state.
 * Preserve the caller's output branch/options.output and report stream. Success
 * replaces owned parent_storage/memory; failure preserves the entire caller.
 * Caller creates a NEW output directory after restore. This is boundary resume,
 * without mid-evaluation recovery or reconstruction-from-initialization claims. */
int evolve_checkpoint_restore(const char *path, const char *source_root,
                              const char *inputs_manifest, struct evolve_run *run);

#endif
