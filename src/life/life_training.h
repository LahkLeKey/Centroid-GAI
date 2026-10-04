/** @file life_training.h @brief Bounded collision teaching and synchronous learning runs. */
#ifndef CGAI_LIFE_TRAINING_H
#define CGAI_LIFE_TRAINING_H
#include "life_policy.h"
#include "life_world.h"

#define LIFE_REPLAY_CAPACITY 256U

typedef enum life_mode {
    LIFE_MODE_CONWAY = 0,
    LIFE_MODE_TEACHER = 1,
    LIFE_MODE_LEARNED = 2
} life_mode;

typedef struct life_run_config {
    uint64_t seed;
    uint32_t scenario;
    uint32_t training_epochs;
    life_mode mode;
    int enable_merges;
    uint32_t group_count; /**< Configured ownership slots; zero selects four. */
} life_run_config;

typedef struct life_run_stats {
    uint64_t collision_records;
    uint64_t training_updates;
    uint64_t edited_cells;
    uint64_t fallback_calls;
    uint64_t merge_attempts;
    uint64_t accepted_merges;
    uint64_t rejected_merges;
    uint64_t teacher_agreements;
    uint64_t policy_decisions;
    double loss_before;
    double loss_after;
} life_run_stats;

/** Complete simulation owner; policy inference finishes before its next update. */
typedef struct life_run {
    life_world world;
    life_policy *policy;
    life_run_config config;
    life_run_stats stats;
    life_record records[LIFE_REPLAY_CAPACITY];
    size_t record_count;
    size_t record_cursor;
    uint64_t last_merge_attempt;
    life_frame last_frame;
    uint32_t last_outputs[LIFE_MAX_PATCHES];
    uint32_t last_targets[LIFE_MAX_PATCHES];
} life_run;

int life_run_init(life_run *run, const life_run_config *config);
void life_run_destroy(life_run *run);
int life_run_tick(life_run *run);
/** Advance cells with the frozen policy; no teacher, replay admission, optimizer or merges.
 * Training targets and cumulative training diagnostics remain unchanged. */
int life_run_evaluate_tick(life_run *run);
void life_encode(const life_world *world, const life_patch *patch, cgai_gameplay_state *state);
int life_teach(const life_world *world, const life_frame *frame, size_t patch, uint32_t *target,
               int64_t *score);
uint64_t life_run_hash(const life_run *run);
#endif
