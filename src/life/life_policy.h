/** @file life_policy.h @brief Fixed-capacity collision specialists and exact continuation. */
#ifndef CGAI_LIFE_POLICY_H
#define CGAI_LIFE_POLICY_H
#include "gameplay/gameplay_contract.h"
#include <stdio.h>

#define LIFE_POLICY_MODULES 8U
#define LIFE_POLICY_DEFAULT_MODULES 4U
#define LIFE_POLICY_FEATURES 16U
#define LIFE_POLICY_HIDDEN 48U
#define LIFE_POLICY_OUTPUTS 23U
#define LIFE_POLICY_MAX_RECORDS 10000U

typedef struct life_policy life_policy;
typedef struct life_record {
    cgai_gameplay_state state;
    uint32_t target;
    uint32_t module_mask;
} life_record;

/** Hashes include both parameters and Adam state for the named slices. */
typedef struct life_policy_info {
    uint32_t group_count;
    uint64_t seed;
    uint32_t active_mask;
    uint64_t version;
    uint64_t epochs;
    uint64_t updates;
    uint64_t module_steps[LIFE_POLICY_MODULES];
    double module_mass[LIFE_POLICY_MODULES];
    uint64_t module_hash[LIFE_POLICY_MODULES];
    uint64_t shared_hash;
} life_policy_info;

/** A rejected candidate leaves the complete policy byte-identical. */
typedef struct life_merge_report {
    int accepted;
    uint32_t survivor;
    uint32_t retired_mask;
    size_t parent_samples[LIFE_POLICY_MODULES];
    double parent_loss_before[LIFE_POLICY_MODULES];
    double parent_loss_after[LIFE_POLICY_MODULES];
    double parent_agreement[LIFE_POLICY_MODULES];
    double global_loss_before;
    double global_loss_after;
    double global_agreement;
    double runtime_agreement;
} life_merge_report;

life_policy *life_policy_create(uint64_t seed);
life_policy *life_policy_create_groups(uint64_t seed, uint32_t group_count);
uint32_t life_policy_group_count(const life_policy *policy);
void life_policy_destroy(life_policy *policy);
life_policy *life_policy_clone(const life_policy *policy);
cgai_status life_policy_select(life_policy *policy, const cgai_gameplay_state *state,
                               uint32_t modules, uint64_t allowed_outputs,
                               cgai_gameplay_result *result);
/** Learning rate .01, clipping 5, AdamW decay .0001. Only record modules advance. */
cgai_status life_policy_train(life_policy *policy, const life_record *records, size_t count,
                              size_t epochs);
cgai_status life_policy_evaluate(life_policy *policy, const life_record *record, double *loss);
uint32_t life_policy_active_mask(const life_policy *policy);
uint64_t life_policy_version(const life_policy *policy);
uint64_t life_policy_steps(const life_policy *policy);
uint64_t life_policy_hash(const life_policy *policy);
cgai_status life_policy_inspect(const life_policy *policy, life_policy_info *info);
cgai_status life_policy_module_vector(const life_policy *policy, uint32_t module,
                                      double vector[LIFE_POLICY_HIDDEN]);
/** Hard replay distillation into the lowest slot. Requires replay from every parent,
 * at least 95% per-parent/global argmax and runtime legal-action agreement,
 * and <=.10 mean NLL degradation. Runtime masks reconstruct present frontier slots.
 * Train/validate a private clone; only accepted candidates publish. Caller coordinates
 * world identity changes after acceptance (or tries this on its own transaction clone). */
cgai_status life_policy_try_merge(life_policy *policy, uint32_t parents, const life_record *records,
                                  size_t count, size_t epochs, life_merge_report *report);
/** Version-two bounded text declares configured groups and retains all numerical state.
 * The loader also accepts the original four-group version-one format exactly. */
cgai_status life_policy_checkpoint_save(const life_policy *policy, const char *path);
life_policy *life_policy_checkpoint_load(const char *path);
/** Write/read one policy on a borrowed stream; reading leaves following tokens unread. */
cgai_status life_policy_checkpoint_write(const life_policy *policy, FILE *file);
life_policy *life_policy_checkpoint_read(FILE *file);
#endif
