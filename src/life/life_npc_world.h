/** @file life_npc_world.h @brief Fresh native NPC worlds and strictly visible teaching. */
#ifndef LIFE_NPC_WORLD_H
#define LIFE_NPC_WORLD_H
#include "centroid_life_domain.h"
#include "gameplay/gameplay_contract.h"
#include <stdint.h>

#define LIFE_NPC_CONTRACT_VERSION 1U
#define LIFE_NPC_GRID_SIDE 9U
#define LIFE_NPC_MAX_TICKS 64U
#define LIFE_NPC_FAMILIES_PER_SPLIT 24U
#define LIFE_NPC_VARIANTS_PER_FAMILY 48U
#define LIFE_NPC_FEATURE_COUNT 16U

typedef enum life_npc_action {
    LIFE_NPC_FALLBACK = 0,
    LIFE_NPC_WAIT = 1,
    LIFE_NPC_NORTH = 2,
    LIFE_NPC_EAST = 3,
    LIFE_NPC_SOUTH = 4,
    LIFE_NPC_WEST = 5,
    LIFE_NPC_INTERACT = 6
} life_npc_action;
typedef enum life_npc_split {
    LIFE_NPC_TRAIN = 0,
    LIFE_NPC_DEV = 1,
    LIFE_NPC_CONFIRM = 2
} life_npc_split;
typedef enum life_npc_mechanic {
    LIFE_NPC_ITEM = 0,
    LIFE_NPC_HAZARD = 1,
    LIFE_NPC_CUE = 2
} life_npc_mechanic;
typedef enum life_npc_tile {
    LIFE_NPC_FLOOR = 0,
    LIFE_NPC_WALL = 1,
    LIFE_NPC_DANGER = 2,
    LIFE_NPC_ITEM_TILE = 3,
    LIFE_NPC_EXIT_TILE = 4,
    LIFE_NPC_OBSTRUCTION = 5 /**< Host-private; observations map it to floor or wall. */
} life_npc_tile;
typedef enum life_npc_outcome {
    LIFE_NPC_INITIAL = 0,
    LIFE_NPC_MOVED = 1,
    LIFE_NPC_BLOCKED = 2,
    LIFE_NPC_IDLE = 3,
    LIFE_NPC_ACQUIRED = 4,
    LIFE_NPC_COMPLETED = 5,
    LIFE_NPC_DIED = 6,
    LIFE_NPC_INVALID = 7
} life_npc_outcome;
typedef enum life_npc_terminal {
    LIFE_NPC_RUNNING = 0,
    LIFE_NPC_SUCCESS = 1,
    LIFE_NPC_DEATH = 2,
    LIFE_NPC_TIMEOUT = 3
} life_npc_terminal;

/** Family identity is provenance only. All48 siblings share one split. */
typedef struct life_npc_family {
    uint32_t id;
    life_npc_split split;
    life_npc_mechanic mechanic;
    uint32_t layout;
} life_npc_family;
typedef struct life_npc_observation {
    uint32_t neighbors[4];
    int32_t target_dx, target_dy;
    uint32_t inventory, cue, last_action, last_outcome;
    uint32_t stage, at_target, ticks, ticks_remaining, mechanic;
    uint64_t allowed_actions;
} life_npc_observation;
/** Caller-owned history records only prior observations, reset per episode. */
typedef struct life_npc_memory {
    uint32_t cue, cue_age, last_action, last_outcome, observed_tick, initialized;
} life_npc_memory;
/** Host-only private state. Neither a policy nor teacher receives this object. */
typedef struct life_npc_world {
    life_npc_family family;
    uint32_t variant;
    uint32_t cells[LIFE_NPC_GRID_SIDE * LIFE_NPC_GRID_SIDE];
    uint32_t width, height, x, y, item_x, item_y, exit_x, exit_y;
    uint32_t junction_x, junction_y, cue_direction, branch_chosen, inventory;
    uint32_t obstruction_revealed, ticks, last_action, last_outcome;
    uint32_t attempted_illegal, executed_illegal;
    life_npc_terminal terminal;
} life_npc_world;

void life_npc_cardinalities(uint32_t cards[LIFE_NPC_FEATURE_COUNT]);
int life_npc_family_get(life_npc_split split, uint32_t index, life_npc_family *family);
int life_npc_world_init(life_npc_world *world, const life_npc_family *family, uint32_t variant);
void life_npc_world_observe(const life_npc_world *world, life_npc_observation *observation);
life_npc_outcome life_npc_world_step(life_npc_world *world, uint32_t action);
void life_npc_memory_reset(life_npc_memory *memory);
void life_npc_memory_observe(life_npc_memory *memory, const life_npc_observation *observation);
void life_npc_encode(const life_npc_observation *observation, const life_npc_memory *memory,
                     int history, cgai_gameplay_state *state);
uint64_t life_npc_observation_actions(const life_npc_observation *observation);
/** Complete visible input, canonical permissions, and tick/history category checks.
 * This does not establish that externally supplied history actually occurred. */
int life_npc_task_valid(const cgai_life_domain_task *task);
/** Only the declared visible/history16 fields, legal mask and tick are decoded.
 * Invalid or incomplete inputs yield fallback. No private world is consulted. */
uint32_t life_npc_teacher_task(const cgai_life_domain_task *task);
#endif
