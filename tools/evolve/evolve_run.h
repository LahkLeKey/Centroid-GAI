/** @file evolve_run.h @brief Private bounded source-search orchestration. */
#ifndef CGAI_EVOLVE_RUN_H
#define CGAI_EVOLVE_RUN_H
#include "evolve_checkpoint.h"
#include "evolve_io.h"
#include "fitness.h"
#include "process.h"
#include <stdio.h>

#define EVOLVE_CONTEXT_PREPARATION_GENERATIONS 64U
#define EVOLVE_CONTEXT_ACTIVITY_GENERATIONS 64U

typedef struct evolve_options {
    const char *output;
    const char *context;
    const char *memory;
    const char *resume;
    uint32_t candidates;
    uint32_t seed;
    uint32_t generations;
    uint32_t pause_after;
} evolve_options;

typedef struct evolve_work {
    char directory[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    char source[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    char build[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    char evaluator[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    uint64_t source_checksum;
    int configured;
} evolve_work;

typedef struct evolve_run {
    evolve_options options;
    char output[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    char external_manifest[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    uint64_t inputs_checksum;
    uint64_t build_recipe_checksum;
    uint64_t external_checksum;
    uint64_t source_context_checksum;
    uint64_t llm_context_checksum;
    uint64_t initial_memory_checksum;
    uint64_t prepared_memory_checksum;
    uint64_t initial_parent_checksum;
    uint64_t predecessor_hash;
    uint64_t checkpoint_hash;
    char latest_checkpoint[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    evolve_mutation_source parent;
    char *parent_storage;
    life_fitness_report training;
    life_fitness_report development;
    life_fitness_report confirmation;
    life_fitness_report baseline_training;
    life_fitness_report baseline_development;
    life_fitness_report baseline_confirmation;
    cgai_life_context_choice_stats initial_choices;
    evolve_checkpoint_receipt ledger[EVOLVE_CHECKPOINT_MAX_RECEIPTS];
    cgai_life_context *memory;
    FILE *report;
    uint32_t proposals;
    uint32_t accepted;
    uint32_t completed_generations;
    uint32_t prepared_generation;
    uint32_t ledger_count;
    uint32_t finalized;
    uint32_t paused;
    uint32_t invocation_generations;
} evolve_run;

/** Frozen inputs/proposal are captured before measurement. Held-out reports never
 * enter choice input, utility or TRAIN activity. Each issued choice consumes once. */
typedef struct evolve_trial {
    evolve_mutation_candidate candidate;
    const cgai_life_collision_event *event;
    char input[CGAI_LIFE_CONTEXT_TEXT_BYTES + 1U];
    uint32_t actions[EVOLVE_MUTATION_MAX_ALTERNATIVES];
    size_t action_count;
    uint32_t fallback;
    cgai_life_context_choice choice;
    cgai_life_context_choice prediction_after;
    cgai_life_context_feedback feedback;
    cgai_life_context_choice_stats before;
    cgai_life_context_choice_stats after;
    life_fitness_report training;
    life_fitness_report development;
    life_fitness_report confirmation;
    double parent_training_loss;
    char feedback_proof[EVOLVE_CHECKPOINT_PROOF_BYTES];
    size_t feedback_proof_bytes;
    uint32_t audit_status;
} evolve_trial;

typedef enum evolve_evaluation_status {
    EVOLVE_EVALUATION_OK,
    EVOLVE_EVALUATION_CONFIGURE_REJECTED,
    EVOLVE_EVALUATION_BUILD_REJECTED,
    EVOLVE_EVALUATION_TEST_REJECTED,
    EVOLVE_EVALUATION_LINT_REJECTED,
    EVOLVE_EVALUATION_FITNESS_REJECTED,
    EVOLVE_EVALUATION_INPUTS_CHANGED
} evolve_evaluation_status;

int evolve_inputs_unchanged(const evolve_run *run);
/** Resolved outputs must be outside source root or in its excluded build subtree. */
int evolve_output_allowed(const char *path);
int evolve_work_unchanged(const evolve_run *run, const evolve_work *work);
int evolve_work_create(const evolve_run *run, const char *name, const char *bytes, size_t size,
                       evolve_work *work);
evolve_evaluation_status evolve_evaluate(const evolve_run *run, evolve_work *work,
                                         life_fitness_report *training,
                                         life_fitness_report *development,
                                         life_fitness_report *confirmation);
const char *evolve_evaluation_name(evolve_evaluation_status status);
int evolve_report_start(evolve_run *run);
int evolve_report_baseline(evolve_run *run);
int evolve_report_choice(const evolve_work *work, const evolve_trial *trial);
int evolve_report_candidate(evolve_run *run, const evolve_work *work, const evolve_trial *trial,
                            const char *status);
int evolve_report_finish(evolve_run *run, uint32_t generations);
#endif
