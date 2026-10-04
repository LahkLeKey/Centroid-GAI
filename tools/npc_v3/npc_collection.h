/** @file npc_collection.h @brief Bounded verified closed-loop training records. */
#ifndef CGAI_NPC_V3_COLLECTION_H
#define CGAI_NPC_V3_COLLECTION_H
#include "npc_policy.h"
/** Maximum records admitted by one immutable collection round. */
#define NPC_TRAINING_LIMIT 8192U
/** Complete eight-variant blocks across the forty-eight training siblings. */
#define NPC_TRAINING_ROUNDS 6U
/** Provenance of one observation-limited teacher correction. */
typedef struct npc_collection_source {
    uint32_t round;    /**< Frozen collection round. */
    uint32_t family;   /**< Training-only complete family identity. */
    uint32_t variant;  /**< Family-local variant. */
    uint32_t step;     /**< Pre-action authoritative tick. */
    uint32_t phase;    /**< Base0, policy1/recovery2, exploration3/recovery4. */
    uint32_t executed; /**< Actual replayed host proposal, distinct from target. */
} npc_collection_source;
/** Owned canonical immutable full-history corpus. */
typedef struct npc_collection {
    cgai_gameplay_example examples[NPC_TRAINING_LIMIT]; /**< Ordered correction labels. */
    npc_collection_source sources[NPC_TRAINING_LIMIT];  /**< Parallel complete provenance. */
    uint32_t roles[NPC_TRAINING_LIMIT]; /**< Original observed role retained before ablation. */
    size_t count;                       /**< Actual admitted record count. */
    uint32_t round;                     /**< Frozen round, zero through five. */
    uint32_t prefix;                    /**< Frozen snapshot-policy decision prefix. */
    uint64_t source_epochs;             /**< Snapshot complete-pass progress. */
    uint64_t source_steps;              /**< Snapshot successful update progress. */
    size_t base_episodes;               /**< Admitted complete teacher trajectories. */
    size_t policy_episodes;      /**< Admitted complete snapshot and recovery trajectories. */
    size_t rejected_episodes;    /**< Dead or unsuccessful snapshot trajectories excluded. */
    size_t guarded_episodes;     /**< Admitted trajectories corrected before a fatal proposal. */
    size_t exploration_episodes; /**< Admitted complete safe-alternative recovery trajectories. */
    size_t exploration_rejected_episodes; /**< Failed complete exploration attempts excluded. */
} npc_collection;
/** @brief Collect and independently replay training-only trajectories from frozen weights.
 * @param checkpoint Immutable compatible source checkpoint.
 * @param corpus New generated corpus path.
 * @param coverage New compact coverage path.
 * @param round Frozen zero-based round.
 * @param prefix Frozen policy-prefix decisions, four times round.
 * @return OK only after complete bounded verified writes. */
cgai_status npc_collection_write(const char *checkpoint, const char *corpus, const char *coverage,
                                 uint32_t round, uint32_t prefix);
/** @brief Load strict canonical records and reexecute every trajectory in the current world.
 * @param path Existing immutable full-history corpus.
 * @return Owned verified corpus, or NULL on malformed or unverified records. */
npc_collection *npc_collection_load(const char *path);
/** @brief Mask only observed-history fields on already verified ordered records.
 * @param collection Mutable private copy of one verified corpus. */
void npc_collection_disable_history(npc_collection *collection);
/** @brief Reject incompatible targets within and across the complete ordered round schedule.
 * @param paths Borrowed canonical corpus paths in increasing round order.
 * @param count Number of rounds, one through six.
 * @return OK after every corpus and cross-round label is verified. */
cgai_status npc_collection_validate(const char *const *paths, size_t count);
#endif
