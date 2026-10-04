/** @file life_probe.h @brief Built-in categorical adapter using the common owned optimizer. */
#ifndef CGAI_LIFE_PROBE_H
#define CGAI_LIFE_PROBE_H
#include "centroid_life_domain.h"
#include "gameplay/gameplay_internal.h"
#include "life_adapter.h"
#include "life_optimizer.h"

typedef struct life_probe {
    cgai_life_domain_kind kind;
    cgai_gameplay_model *model;
    cgai_gameplay_session *session;
    double *gradient;
    unsigned char *owners;
    uint64_t steps[CGAI_LIFE_GROUPS];
    double mass[CGAI_LIFE_GROUPS];
    uint32_t uids[CGAI_LIFE_GROUPS];
} life_probe;

int life_probe_init(life_probe *probe, uint64_t seed, uint32_t groups);
int life_probe_init_kind(life_probe *probe, uint64_t seed, uint32_t groups,
                         cgai_life_domain_kind kind);
uint32_t life_probe_output_count(const life_probe *probe);
int life_probe_verify(const life_probe *probe, const cgai_life_domain_task *task, uint32_t *target,
                      uint64_t *teacher);
void life_probe_destroy(life_probe *probe);
int life_probe_clone(life_probe *destination, const life_probe *source);
int life_probe_task_valid(const life_probe *probe, const cgai_life_domain_task *task, int train);
uint32_t life_probe_uid_mask(const life_probe *probe, const uint32_t *uids, size_t count);
int life_probe_predict(const life_probe *probe, const cgai_life_domain_task *task, uint32_t mask,
                       cgai_life_domain_prediction *result);
int life_probe_update(life_probe *probe, const cgai_life_domain_task *task, uint32_t target,
                      uint32_t mask);
uint64_t life_domain_hash_word(uint64_t hash, uint64_t word);
uint64_t life_probe_task_hash(const cgai_life_domain_task *task);
uint64_t life_probe_slice_hash(const life_probe *probe, size_t group);
int life_probe_valid(const life_probe *probe);
#endif
