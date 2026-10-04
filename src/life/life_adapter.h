/** @file life_adapter.h @brief Private built-in visible-input domain registry. */
#ifndef CGAI_LIFE_ADAPTER_H
#define CGAI_LIFE_ADAPTER_H
#include "centroid_life_domain.h"
#include "gameplay/gameplay_internal.h"

#define LIFE_PROBE_TEACHER UINT64_C(0x50524f4245000001)
#define LIFE_NPC_TEACHER UINT64_C(0x4e5043444f4d0001)

int life_adapter_config(cgai_life_domain_kind kind, uint64_t seed, uint32_t groups,
                        cgai_gameplay_config *config);
void life_adapter_initialize(cgai_life_domain_kind kind, cgai_gameplay_model *model);
int life_adapter_task_valid(cgai_life_domain_kind kind, const cgai_life_domain_task *task);
void life_adapter_encode(cgai_life_domain_kind kind, const cgai_life_domain_task *task,
                         cgai_gameplay_state *state);
int life_adapter_verify(cgai_life_domain_kind kind, const cgai_life_domain_task *task,
                        uint32_t *target, uint64_t *teacher);
int life_adapter_model_valid(cgai_life_domain_kind kind, const cgai_gameplay_model *model);
#endif
