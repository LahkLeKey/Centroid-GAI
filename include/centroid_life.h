/** @file centroid_life.h @brief C11 ownership boundary for centroid Life training. */
#ifndef CENTROID_LIFE_H
#define CENTROID_LIFE_H

#include <stddef.h>
#include <stdint.h>

#define CGAI_LIFE_API_VERSION 3U           /**< Current breaking native contract version. */
#define CGAI_LIFE_GROUPS 8U                /**< Maximum specialist capacity. */
#define CGAI_LIFE_DEFAULT_GROUPS 4U        /**< Default configured ownership groups. */
#define CGAI_LIFE_MIN_GROUPS 2U            /**< Minimum configured encounter participants. */
#define CGAI_LIFE_WIDTH 32U                /**< Toroidal grid width. */
#define CGAI_LIFE_HEIGHT 32U               /**< Toroidal grid height. */
#define CGAI_LIFE_MAX_TRAINING_EPOCHS 64U  /**< Maximum replay epochs per generation. */
#define CGAI_LIFE_MAX_STEP_GENERATIONS 64U /**< Maximum generations per API call. */
#define CGAI_LIFE_MAX_COLLISION_EVENTS 4U  /**< Maximum committed frontiers per generation. */
#define CGAI_LIFE_MAX_FRONTIER_CELLS 6U    /**< Maximum candidate cells in one frontier. */

/** An owner has independent world, policy, optimizer, replay and diagnostics. */
typedef struct cgai_life cgai_life;

/** Public operation outcomes; zero indicates success. */
typedef enum cgai_life_status {
    CGAI_LIFE_OK = 0,                /**< Operation completed. */
    CGAI_LIFE_INVALID_ARGUMENT = -1, /**< Invalid pointer, configuration or quantum. */
    CGAI_LIFE_OUT_OF_MEMORY = -2,    /**< Initialization or transaction allocation failed. */
    CGAI_LIFE_ENGINE_ERROR = -3,     /**< Collision policy or world computation failed. */
    CGAI_LIFE_IO_ERROR = -4,         /**< Bundle access, decoding or validation failed. */
    CGAI_LIFE_LIMIT_REACHED = -5     /**< The bounded generation counter cannot advance. */
} cgai_life_status;

/** Modes of the same Life world and collision engine. */
typedef enum cgai_life_mode {
    CGAI_LIFE_CONWAY = 0,  /**< Exact Conway world evolution without collision edits. */
    CGAI_LIFE_TEACHER = 1, /**< Lookahead collision edits with target recording. */
    CGAI_LIFE_LEARNED = 2  /**< Policy collision edits followed by replay learning. */
} cgai_life_mode;

/** Configured two-to-eight-group, 32 by 32 toroidal fixtures; scenario is 0..3.
 * Seed is 0..UINT32_MAX, epochs are 0..64, and enable_merges is exactly 0 or 1.
 * Zero epochs disables gradient updates; it still records training targets.
 * Learned mode uses collision predictions to edit cells before replay updates. */
typedef struct cgai_life_config {
    uint64_t seed;            /**< Deterministic world and policy initialization seed. */
    uint32_t scenario;        /**< Collision fixture index. */
    uint32_t training_epochs; /**< Replay passes when a learned collision occurs. */
    cgai_life_mode mode;      /**< Training evolution mode. */
    uint32_t enable_merges;   /**< Whether validated learned consolidation is enabled. */
    uint32_t group_count;     /**< Configured slots, 2..8; zero selects the four-group default. */
} cgai_life_config;

/** Snapshot of cumulative training diagnostics and current physical/model state.
 * Active group bits describe model ownership; zero population does not retire weights.
 * Group and shared hashes include parameters and optimizer state. Hashes are
 * deterministic continuation checks, not cryptographic integrity guarantees. */
typedef struct cgai_life_stats {
    uint32_t group_count;       /**< Allocated ownership universe; unchanged by retirement. */
    uint32_t generation;        /**< Current world generation. */
    uint32_t population;        /**< Live cells, counting shared claims once. */
    uint32_t active_group_mask; /**< Active policy ownership slots. */
    uint32_t active_conflicts;  /**< Active physical records, including coupled contacts. */
    uint32_t replay_records;    /**< Retained collision records, bounded at 256. */
    uint32_t collisions;        /**< Physical collision openings. */
    uint32_t separated;         /**< Physical separation resolutions. */
    uint32_t coupled;           /**< Stable coupled resolutions. */
    uint32_t absorbed;          /**< Resolutions retaining a nonempty proper subset of groups. */
    uint32_t extinct;           /**< Resolutions where all participating claims vanish. */
    uint32_t merged;            /**< Validated physical/model consolidations. */
    uint32_t group_uids[CGAI_LIFE_GROUPS];        /**< Stable world identity per slot. */
    uint32_t group_populations[CGAI_LIFE_GROUPS]; /**< Live claims per ownership slot. */
    uint64_t model_version;                       /**< Published policy update version. */
    uint64_t policy_hash;                         /**< Complete policy continuation hash. */
    uint64_t shared_hash;                         /**< Shared parameter and optimizer slice hash. */
    uint64_t group_steps[CGAI_LIFE_GROUPS];       /**< Independent specialist Adam clocks. */
    uint64_t group_hashes[CGAI_LIFE_GROUPS];      /**< Specialist continuation slice hashes. */
    double group_mass[CGAI_LIFE_GROUPS];          /**< Accumulated specialist support. */
    uint64_t collision_records;                   /**< All admitted training collision records. */
    uint64_t training_updates;                    /**< Applied replay record updates. */
    uint64_t edited_cells;                        /**< Training proposal toggle count. */
    uint64_t fallback_calls;                      /**< Training policy abstentions. */
    uint64_t merge_attempts;                      /**< Consolidation candidates evaluated. */
    uint64_t accepted_merges;                     /**< Consolidation candidates published. */
    uint64_t rejected_merges;                     /**< Consolidation candidates rejected. */
    uint64_t teacher_agreements; /**< Training policy decisions matching targets. */
    uint64_t policy_decisions;   /**< Training collision policy decisions. */
    double loss_before;          /**< Latest replay loss before updates. */
    double loss_after;           /**< Latest replay loss after updates. */
} cgai_life_stats;

/** Physical outcome after the generation, including any accepted consolidation. */
typedef enum cgai_life_collision_outcome {
    CGAI_LIFE_COLLISION_UNRESOLVED = 0, /**< Conflict remains unresolved. */
    CGAI_LIFE_COLLISION_SEPARATED = 1,  /**< Sustained absence of causal contact. */
    CGAI_LIFE_COLLISION_COUPLED = 2,    /**< Sustained coupled causal contact. */
    CGAI_LIFE_COLLISION_ABSORBED = 3,   /**< Only a proper subset of participants survives. */
    CGAI_LIFE_COLLISION_EXTINCT = 4,    /**< All participating claims vanish. */
    CGAI_LIFE_COLLISION_MERGED = 5,     /**< Accepted physical/model consolidation. */
    CGAI_LIFE_COLLISION_UNKNOWN = 6     /**< Conflict record is no longer retained. */
} cgai_life_collision_outcome;

/** One committed causal frontier, in canonical engine order.
 * Identity arrays use pre-generation ownership slots; nonparticipants are zero.
 * Ancestry bits identify original seed groups. A merge can change the result slots
 * without changing these captured parent identities. Frontier indices are y*32+x;
 * toggle bit n refers to frontier_cells[n], not to an ownership group.
 * Outcome describes the retained conflict record after edits, learning and merges.
 * Teacher targets are valid only for training in teacher or learned mode. */
typedef struct cgai_life_collision_event {
    uint32_t generation;                       /**< Committed result generation. */
    uint32_t conflict_id;                      /**< Stable physical conflict identifier. */
    uint32_t conflict_age;                     /**< Prepared conflict age for this generation. */
    uint32_t participant_mask;                 /**< Participating pre-generation ownership slots. */
    uint32_t group_uids[CGAI_LIFE_GROUPS];     /**< Pre-generation participant identities. */
    uint32_t group_ancestry[CGAI_LIFE_GROUPS]; /**< Pre-generation participant ancestry bits. */
    uint32_t frontier_cells[CGAI_LIFE_MAX_FRONTIER_CELLS]; /**< Canonical linear cell indices. */
    uint32_t frontier_count;             /**< Number of populated frontier indices. */
    uint32_t selected_output;            /**< Action 0..22; zero is Conway fallback. */
    uint32_t toggle_bits;                /**< At most two toggles within this frontier. */
    uint32_t legal_output_mask;          /**< Bit n permits action n for this frontier. */
    cgai_life_collision_outcome outcome; /**< Retained post-generation physical outcome. */
    uint32_t teacher_target_valid;       /**< Exactly one when a teacher target was computed. */
    uint32_t teacher_target;             /**< Recorded target, or zero when invalid. */
    uint64_t source_world_hash;          /**< World hash immediately before the generation. */
    uint64_t result_world_hash;          /**< World hash after the successful generation. */
    uint64_t source_policy_version; /**< Frozen version used for this generation's selection. */
    uint64_t result_policy_version; /**< Published version after learning and merges. */
} cgai_life_collision_event;

/** Observational feed for the last successful generation, bounded at four frontiers.
 * A successful generation publishes a valid batch even when event_count is zero.
 * New owners and successfully loaded owners have an entirely zeroed, invalid batch.
 * Events cover prepared causal frontiers, rather than every historical lifecycle change.
 * Unused entries and unused frontier/identity slots are zero. This feed is excluded
 * from continuation hashes and checkpoints. */
typedef struct cgai_life_events {
    uint32_t generation_valid; /**< Exactly one after a successful generation. */
    uint32_t generation;       /**< Last successful generation, or zero when invalid. */
    uint32_t event_count;      /**< Number of populated events. */
    cgai_life_collision_event events[CGAI_LIFE_MAX_COLLISION_EVENTS]; /**< Ordered frontiers. */
} cgai_life_events;

/** Return learned crowd-fixture defaults: seed 42, one epoch, merges enabled.
 * @return A valid configuration owned by the caller. */
cgai_life_config cgai_life_config_default(void);

/** Publish a newly allocated owner only on success. Output must point to NULL.
 * Caller owns the result and releases it exactly once with cgai_life_destroy.
 * Each owner requires exclusive access during mutation; distinct owners are independent.
 * @param config Borrowed validated initialization configuration.
 * @param output Caller-owned pointer initialized to NULL.
 * @return Success, invalid argument, or allocation failure. */
cgai_life_status cgai_life_create(const cgai_life_config *config, cgai_life **output);

/** Release an owner; NULL is allowed. Other aliases become invalid.
 * @param owner Owned simulation to release, or NULL. */
void cgai_life_destroy(cgai_life *owner);

/** Run 1..64 complete generations using the configured collision training mode.
 * Each successful generation commits independently. A failed generation leaves
 * its starting state intact; earlier completed generations remain committed.
 * Optional completed receives that count. Invalid arguments leave it untouched.
 * @param owner Borrowed owner with exclusive access for this call.
 * @param generations Requested complete generations, in 1..64.
 * @param completed Optional caller-owned committed generation count.
 * @return Success or the first invalid, allocation, engine or limit failure. */
cgai_life_status cgai_life_train_step(cgai_life *owner, uint32_t generations, uint32_t *completed);

/** Run 1..64 generations with frozen policy inference, without teacher targets,
 * replay admission, learning or merges. World/frame/outputs may advance; policy,
 * optimizer, replay and cumulative training diagnostics remain unchanged.
 * Commit and completed semantics match cgai_life_train_step.
 * @param owner Borrowed owner with exclusive access for this call.
 * @param generations Requested frozen generations, in 1..64.
 * @param completed Optional caller-owned committed generation count.
 * @return Success or the first invalid, allocation, engine or limit failure. */
cgai_life_status cgai_life_evaluate_step(cgai_life *owner, uint32_t generations,
                                         uint32_t *completed);

/** Read diagnostics without changing the owner; failure leaves output untouched.
 * @param owner Borrowed owner with no concurrent mutation.
 * @param output Caller-owned diagnostics destination.
 * @return Success, invalid argument, or engine failure. */
cgai_life_status cgai_life_get_stats(const cgai_life *owner, cgai_life_stats *output);

/** Copy the last committed frontier batch without changing continuation state.
 * Multi-generation calls expose their last successful generation. Invalid or failed
 * generations preserve the preceding batch. Frozen evaluation emits events with
 * invalid teacher targets; Conway training emits physical frontiers with fallback zero.
 * A successful load clears the batch. Failure leaves output untouched.
 * @param owner Borrowed owner with no concurrent mutation.
 * @param output Caller-owned event batch destination.
 * @return Success or invalid argument. */
cgai_life_status cgai_life_get_events(const cgai_life *owner, cgai_life_events *output);

/** Hash exact continuation state; returns zero for NULL. Does not mutate the owner.
 * @param owner Borrowed owner with no concurrent mutation, or NULL.
 * @return Deterministic continuation hash, or zero for NULL. */
uint64_t cgai_life_hash(const cgai_life *owner);

/** Atomically publish a self-contained state, policy and optimizer checkpoint at path.
 * A failed save preserves any existing destination. Temporary files use the same directory.
 * Borrowed path must be a nonempty NUL-terminated filesystem path.
 * @param owner Borrowed owner with no concurrent mutation.
 * @param path Borrowed destination filesystem path.
 * @return Success, invalid argument, or checkpoint write/replacement failure. */
cgai_life_status cgai_life_save(const cgai_life *owner, const char *path);

/** Restore a complete checkpoint into an existing owner. Success replaces its state;
 * any failure preserves the complete current owner. Path is borrowed as in save.
 * Legacy version-one snapshots also require their adjacent path.policy file.
 * @param owner Borrowed existing owner with exclusive access for this call.
 * @param path Borrowed source filesystem path.
 * @return Success, invalid argument, or checkpoint read/validation failure. */
cgai_life_status cgai_life_load(cgai_life *owner, const char *path);

/** Bounded local lexical-centroid memory trained only at physical Life contacts.
 * This adapter selects stored excerpts; it does not generate text or execute notes. */
#define CGAI_LIFE_CONTEXT_MAX_RECORDS 4096U
#define CGAI_LIFE_CONTEXT_SOURCE_BYTES 512U
#define CGAI_LIFE_CONTEXT_TEXT_BYTES 2048U
#define CGAI_LIFE_CONTEXT_FEATURES 64U
#define CGAI_LIFE_CONTEXT_ENCOUNTER_GENERATIONS 32U
typedef struct cgai_life_context cgai_life_context;

typedef enum cgai_life_context_kind {
    CGAI_LIFE_CONTEXT_SOURCE = 0,
    CGAI_LIFE_CONTEXT_LLM = 1,
    CGAI_LIFE_CONTEXT_ACTIVITY = 2
} cgai_life_context_kind;

typedef enum cgai_life_context_split {
    CGAI_LIFE_CONTEXT_TRAIN = 0,
    CGAI_LIFE_CONTEXT_DEVELOPMENT = 1,
    CGAI_LIFE_CONTEXT_AUDIT = 2
} cgai_life_context_split;

/** Borrowed NUL-terminated inputs. Reviewed admits exact bytes to lexical fitting,
 * not factual truth. LLM/activity records remain attributed context. All fields
 * are data, never instructions. Eligibility is a nonzero configured-group mask. */
typedef struct cgai_life_context_input {
    const char *source;
    const char *text;
    uint32_t first_line;
    uint32_t last_line;
    uint32_t family;
    cgai_life_context_kind kind;
    cgai_life_context_split split;
    uint32_t reviewed;
    uint32_t eligible_mask;
} cgai_life_context_input;

typedef struct cgai_life_context_result {
    char source[CGAI_LIFE_CONTEXT_SOURCE_BYTES + 1U];
    char text[CGAI_LIFE_CONTEXT_TEXT_BYTES + 1U];
    uint32_t first_line;
    uint32_t last_line;
    uint32_t record_id;
    cgai_life_context_kind kind;
    uint32_t learned_mask;
    uint64_t content_hash;
    double score;
} cgai_life_context_result;

typedef struct cgai_life_context_stats {
    uint32_t group_count; /**< Configured ownership universe; unused group slots report zero. */
    uint32_t records;
    uint32_t trained_records;
    uint32_t deferred_records;
    uint32_t generation;
    uint64_t domain_updates;
    uint64_t contact_events;
    uint64_t source_hash;
    uint64_t group_updates[CGAI_LIFE_GROUPS];
    uint64_t group_hashes[CGAI_LIFE_GROUPS];
} cgai_life_context_stats;

/** Same Life configuration; context requires learned mode and disabled merges
 * because domain topology consolidation is not implemented. Output starts NULL. */
cgai_life_status cgai_life_context_create(const cgai_life_config *config,
                                          cgai_life_context **output);
void cgai_life_context_destroy(cgai_life_context *owner);
/** Copy one bounded record transactionally, without training. Identical admissions
 * are idempotent; changed source bytes create a distinct version. */
cgai_life_status cgai_life_context_add(cgai_life_context *owner,
                                       const cgai_life_context_input *input);
/** Admission with a stable one-based record ID; invalid calls preserve the output. */
cgai_life_status cgai_life_context_add_record(cgai_life_context *owner,
                                              const cgai_life_context_input *input,
                                              uint32_t *record_id);
/** Fit only reviewed train records and participating group slices at actual Life
 * contacts. Each generation commits independently, with the same quantum as Life.
 * Fixed 32-generation encounter rounds renew configured seed placement, retaining
 * learned policy, replay, domain state, identities and the cumulative generation.
 * No synthetic text-to-cell mapping or independent production trainer is used. */
cgai_life_status cgai_life_context_train_step(cgai_life_context *owner, uint32_t generations,
                                              uint32_t *completed);
/** Frozen nearest-centroid excerpt lookup. Only fitted train records are candidates;
 * held-out and unreviewed data never become answers. Capacity is 1..16. Queries are
 * bounded by TEXT_BYTES; no lexical overlap returns an empty result set. */
cgai_life_status cgai_life_context_query(const cgai_life_context *owner, const char *query,
                                         cgai_life_context_result *results, size_t capacity,
                                         size_t *count);
/** Frozen lookup restricted to one valid kind before ranking. The same fitting,
 * split, review and lexical-overlap checks apply; unrelated kinds cannot crowd
 * this kind out of bounded results. Invalid calls preserve output arguments. */
cgai_life_status cgai_life_context_query_kind(const cgai_life_context *owner, const char *query,
                                              cgai_life_context_kind kind,
                                              cgai_life_context_result *results, size_t capacity,
                                              size_t *count);
cgai_life_status cgai_life_context_get_stats(const cgai_life_context *owner,
                                             cgai_life_context_stats *output);
uint64_t cgai_life_context_hash(const cgai_life_context *owner);
/** Atomic, self-contained domain plus Life checkpoint. Failure preserves destination. */
cgai_life_status cgai_life_context_save(const cgai_life_context *owner, const char *path);
/** Complete validation before replacing the incumbent. Failure preserves owner. */
cgai_life_status cgai_life_context_load(cgai_life_context *owner, const char *path);

/** Outcome-conditioned native action selection, separate from Life cell edits.
 * Action IDs describe complete domain actions, not frontier-toggle IDs. */
#define CGAI_LIFE_CONTEXT_ACTIONS 81U
#define CGAI_LIFE_CONTEXT_MAX_CHOICES 16U
typedef struct cgai_life_context_choice {
    uint64_t token;            /**< Owner-issued pending decision identity. */
    uint64_t input_hash;       /**< Exact frozen decision input identity. */
    uint64_t model_version;    /**< Outcome model version before observation. */
    uint64_t world_hash;       /**< Authentic contact's source-world identity. */
    uint32_t generation;       /**< Committed physical contact generation. */
    uint32_t conflict_id;      /**< Authentic physical conflict. */
    uint32_t participant_mask; /**< Only these slices may learn the observed result. */
    uint32_t action;           /**< Selected complete action, 0..80. */
    uint32_t supported_groups; /**< Bitmask of groups with relevant outcome experience. */
    double predicted_utility;  /**< Frozen bounded prediction; not measured quality. */
} cgai_life_context_choice;

typedef struct cgai_life_context_feedback {
    cgai_life_context_split split; /**< Only TRAIN can teach. */
    uint32_t verified;             /**< One for independently measured input/action outcome. */
    uint32_t deferred;             /**< One closes the pending decision without learning. */
    uint64_t evidence_hash;        /**< Nonzero content-bound measurement identity. */
    double utility;                /**< Independently measured utility in [-1,1]. */
} cgai_life_context_feedback;

typedef struct cgai_life_context_choice_stats {
    uint32_t group_count;  /**< Configured ownership slots; unused trailing arrays are zero. */
    uint64_t version;      /**< Published measured feedback updates. */
    uint64_t decisions;    /**< Issued pending decisions. */
    uint64_t observations; /**< Successfully consumed measured results. */
    uint64_t deferred;     /**< Consumed results that did not train. */
    uint64_t group_steps[CGAI_LIFE_GROUPS];  /**< Independent optimizer clocks. */
    uint64_t group_hashes[CGAI_LIFE_GROUPS]; /**< Owned model/optimizer identities. */
    uint32_t pending; /**< Exactly one while a decision awaits observation. */
} cgai_life_context_choice_stats;

/** Latest authentic contacts; a successful load clears this observational feed.
 * A pending decision remains resumable independently of that feed. */
cgai_life_status cgai_life_context_get_events(const cgai_life_context *owner,
                                              cgai_life_events *output);
/** Freeze one code/domain choice at an unconsumed latest physical contact.
 * Inputs contain source/LLM context, never the result or held-out labels. The caller
 * must execute the selected action and independently measure its outcome. Fallback
 * must belong to the distinct allowed action list. Only one decision may be pending.
 * Unsupported alternatives have neutral utility; the deterministic fallback wins ties. */
cgai_life_status cgai_life_context_choice_begin(cgai_life_context *owner, uint32_t event_index,
                                                const char *input, const uint32_t *actions,
                                                size_t action_count, uint32_t fallback,
                                                cgai_life_context_choice *output);
/** Consume the exact owner-issued pending decision once. Verified TRAIN outcomes
 * fit only its contact participants; LLM proposals are not measurement authority.
 * Deferred observations close pending state without fitting. Invalid, stale,
 * held-out or mismatched calls preserve the entire owner. */
cgai_life_status cgai_life_context_choice_observe(cgai_life_context *owner,
                                                  const cgai_life_context_choice *decision,
                                                  const cgai_life_context_feedback *feedback);
/** Read-only outcome prediction without contact, labels or admission. */
cgai_life_status cgai_life_context_choice_predict(const cgai_life_context *owner, const char *input,
                                                  uint32_t participant_mask,
                                                  const uint32_t *actions, size_t action_count,
                                                  uint32_t fallback,
                                                  cgai_life_context_choice *output);
cgai_life_status cgai_life_context_choice_get_stats(const cgai_life_context *owner,
                                                    cgai_life_context_choice_stats *output);

#endif
