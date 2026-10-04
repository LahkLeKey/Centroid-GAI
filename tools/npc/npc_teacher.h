/** @file npc_teacher.h @brief Executable observable-information NPC controllers. */
#ifndef CGAI_NPC_TEACHER_H
#define CGAI_NPC_TEACHER_H
#include "npc_world.h"
/** @brief Select one deterministic one-step search action from shared information.
 * @param observation Borrowed current visible information.
 * @param memory Borrowed bounded observable history.
 * @return Stable proposed action; no world or oracle is accessed. */
uint32_t npc_teacher_action(const npc_observation *observation, const npc_memory *memory);
/** @brief Select an authored controller action with history disabled.
 * @param observation Borrowed current visible information.
 * @return Stable proposed action using deterministic cardinal tie breaking. */
uint32_t npc_reactive_action(const npc_observation *observation);
#endif
