/** @file npc_teacher.c @brief Pinned one-step observable search controllers. */
#include "npc_teacher.h"
#include <limits.h>
#include <stdlib.h>
/** Frozen cardinal column increments. */
static const int32_t npc_teacher_dx[4] = {0, 1, 0, -1};
/** Frozen cardinal row increments. */
static const int32_t npc_teacher_dy[4] = {-1, 0, 1, 0};
/** @brief Resolve a remembered route, otherwise use a fixed observable branch order.
 * @param observation Borrowed junction observation.
 * @param memory Optional shared observable history.
 * @return Admitted route action or wait if there is no observable branch. */
static uint32_t npc_route_action(const npc_observation *observation, const npc_memory *memory) {
    uint32_t cue = memory != NULL ? memory->cue : 0U;
    if (cue != 0U && cue <= 4U &&
        (observation->allowed_actions & (UINT64_C(1) << (cue + 1U))) != 0U)
        return cue + 1U;
    for (uint32_t direction = 0; direction < 4U; ++direction) {
        uint32_t action = direction + NPC_NORTH;
        uint32_t opposite = (direction + 2U) % 4U;
        if ((observation->allowed_actions & (UINT64_C(1) << action)) != 0U &&
            observation->neighbors[opposite] != NPC_WALL)
            return action;
    }
    return NPC_WAIT;
}
/** @brief Score a visible one-step successor with pinned detour tie breaking.
 * @param observation Borrowed complete visible objective.
 * @param direction Cardinal index.
 * @return Unit-cost Manhattan score with dominant-axis tie preference. */
static int32_t npc_successor_score(const npc_observation *observation, uint32_t direction) {
    int32_t score = 32 * (abs(observation->target_dx - npc_teacher_dx[direction]) +
                          abs(observation->target_dy - npc_teacher_dy[direction]));
    if (abs(observation->target_dx) >= abs(observation->target_dy)) {
        if (npc_teacher_dx[direction] == 0)
            ++score;
    } else if (npc_teacher_dy[direction] == 0)
        ++score;
    return score;
}
/** @brief Search at most four admitted non-hazardous observable successor cells.
 * @param observation Borrowed shared visible information.
 * @return Lowest-score action with north/east/south/west final tie order. */
static uint32_t npc_search_action(const npc_observation *observation) {
    uint32_t best = NPC_WAIT;
    int32_t score = INT_MAX;
    for (uint32_t direction = 0; direction < 4U; ++direction) {
        uint32_t action = direction + NPC_NORTH;
        if ((observation->allowed_actions & (UINT64_C(1) << action)) == 0U ||
            observation->neighbors[direction] == NPC_DANGER)
            continue;
        int32_t candidate = npc_successor_score(observation, direction);
        if (candidate < score) {
            best = action;
            score = candidate;
        }
    }
    return best;
}
/** @brief Apply observable staging, interaction, route and movement rules.
 * @param observation Borrowed complete visible information.
 * @param memory Optional shared observable history.
 * @return Stable admitted action, without any host-world access. */
static uint32_t npc_shared_action(const npc_observation *observation, const npc_memory *memory) {
    if (observation == NULL)
        return NPC_FALLBACK;
    if ((observation->allowed_actions & ~UINT64_C(3)) == 0U)
        return observation->mechanic == NPC_HAZARD ? NPC_FALLBACK : NPC_WAIT;
    if (observation->at_target != 0U && observation->stage >= 2U &&
        (observation->allowed_actions & (UINT64_C(1) << NPC_INTERACT)) != 0U)
        return NPC_INTERACT;
    if (observation->stage == 1U)
        return npc_route_action(observation, memory);
    return npc_search_action(observation);
}
uint32_t npc_teacher_action(const npc_observation *observation, const npc_memory *memory) {
    return npc_shared_action(observation, memory);
}
uint32_t npc_reactive_action(const npc_observation *observation) {
    return npc_shared_action(observation, NULL);
}
