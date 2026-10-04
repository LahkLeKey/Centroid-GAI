/** @file fitness.h @brief Fixed physical collision packs and source-candidate admission. */
#ifndef CGAI_EVOLVE_FITNESS_H
#define CGAI_EVOLVE_FITNESS_H

#include "life_training.h"
#include <stdint.h>

#define LIFE_FITNESS_VERSION 1U
#define LIFE_FITNESS_PACK_RECORDS 128U
#define LIFE_FITNESS_MODELS 3U
#define LIFE_FITNESS_TRAINING_EPOCHS 4U
#define LIFE_FITNESS_TRAINING_GENERATIONS 24U
#define LIFE_FITNESS_MINIMUM_IMPROVEMENT 1e-6

/** Fixed seed and physical sampling recipes; TRAIN is the only feedback authority.
 * DEV and CONFIRM are regression gates, never labels for the source-choice model.
 * All three retain the four authored fixture families; no fresh-family audit claim. */
typedef enum life_fitness_split {
    LIFE_FITNESS_DEV = 0,
    LIFE_FITNESS_CONFIRM = 1,
    LIFE_FITNESS_TRAIN = 2
} life_fitness_split;

/** Candidate-independent teacher labels and legal domains collected with Conway evolution.
 * Hash includes the split, source worlds, frontiers, targets and categorical observations.
 * Packs cover the existing four authored physical fixtures, not arbitrary unseen domains. */
typedef struct life_fitness_pack {
    life_record records[LIFE_FITNESS_PACK_RECORDS];
    uint32_t allowed_outputs[LIFE_FITNESS_PACK_RECORDS];
    uint32_t count;
    uint64_t hash;
} life_fitness_pack;

/** Fixed-budget training followed by frozen scoring on one common pack per model.
 * records is pack_records * model_count. mean_loss is unrestricted target NLL;
 * correct counts legal-action selections matching independently computed targets.
 * Work counters measure successful replay updates and scoring forward operations.
 * active_modules/active_centroids count selection work, rather than owned heap bytes.
 * A valid complete report has all three models and all 384 evaluations. */
typedef struct life_fitness_report {
    uint32_t version;
    life_fitness_split split;
    uint32_t complete;
    uint64_t pack_hash;
    uint32_t pack_records;
    uint64_t records;
    uint64_t correct;
    double mean_loss;
    uint32_t model_count;
    uint32_t training_epochs;
    uint32_t training_generations;
    uint64_t training_updates;
    uint64_t forward_passes;
    uint64_t active_modules;
    uint64_t active_centroids;
} life_fitness_report;

/** Return the fixed CLI spelling, or NULL for an invalid split. */
const char *life_fitness_split_name(life_fitness_split split);
/** Parse exactly train, dev or confirm; failure preserves output. */
int life_fitness_split_parse(const char *text, life_fitness_split *output);
/** Build an entire pack before publication; failure preserves output. No model is created. */
int life_fitness_build_pack(life_fitness_split split, life_fitness_pack *output);
/** Train fresh models with merges disabled, then score frozen policies; publish only on success. */
int life_fitness_measure(life_fitness_split split, life_fitness_report *output);
/** Validate a complete report from this exact version's fixed recipe. */
int life_fitness_report_valid(const life_fitness_report *report);
/** Pure gate: same pack/coverage/scoring work, no agreement or replay-work regression,
 * and mean NLL lower by at least 1e-6. Passing regressions alone never admits a candidate. */
int life_fitness_admits(const life_fitness_report *baseline, const life_fitness_report *candidate);
/** Exclusively create a new version-one key/value TSV. Existing paths remain untouched.
 * Failed writes remove only this invocation's newly created incomplete output. */
int life_fitness_write(const char *path, const life_fitness_report *report);
/** Read only the exact bounded, complete version-one TSV schema; failure preserves output. */
int life_fitness_read(const char *path, life_fitness_report *output);

#endif
