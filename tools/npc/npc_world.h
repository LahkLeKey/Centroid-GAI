/** @file npc_world.h @brief Deterministic observable NPC episode contract. */
#ifndef CGAI_NPC_WORLD_H
#define CGAI_NPC_WORLD_H
#include "gameplay/gameplay_contract.h"
#include <stdint.h>

/** Authored episode and adapter version. */
#define NPC_CONTRACT_VERSION 1U
/** Maximum side of the bounded grid. */
#define NPC_GRID_SIDE 9U
/** Maximum authoritative decisions per episode. */
#define NPC_MAX_TICKS 64U
/** Distinct families in each isolated split. */
#define NPC_FAMILIES_PER_SPLIT 24U
/** Seed and rotation siblings assigned together to one family. */
#define NPC_VARIANTS_PER_FAMILY 48U
/** Number of policy categorical inputs. */
#define NPC_FEATURE_COUNT 16U

/** Stable host action IDs; repeated movement is permitted. */
typedef enum npc_action {
    NPC_FALLBACK = 0, /**< Safe idle consuming one decision. */
    NPC_WAIT = 1,     /**< Requested idle consuming one decision. */
    NPC_NORTH = 2,    /**< Move one cell north. */
    NPC_EAST = 3,     /**< Move one cell east. */
    NPC_SOUTH = 4,    /**< Move one cell south. */
    NPC_WEST = 5,     /**< Move one cell west. */
    NPC_INTERACT = 6  /**< Host-validated item or exit interaction. */
} npc_action;
/** Complete-family split assignments. */
typedef enum npc_split { NPC_TRAIN = 0, NPC_DEV = 1, NPC_AUDIT = 2 } npc_split;
/** Balanced mechanic classes. */
typedef enum npc_mechanic { NPC_ITEM = 0, NPC_HAZARD = 1, NPC_CUE = 2 } npc_mechanic;
/** Neighbor observations; an unrevealed obstruction looks like floor. */
typedef enum npc_tile {
    NPC_FLOOR = 0,      /**< Visible traversable floor. */
    NPC_WALL = 1,       /**< Visible forbidden collision. */
    NPC_DANGER = 2,     /**< Visible legal but fatal hazard. */
    NPC_ITEM_TILE = 3,  /**< Visible collectible prerequisite. */
    NPC_EXIT_TILE = 4,  /**< Visible prerequisite-checked exit. */
    NPC_OBSTRUCTION = 5 /**< Private obstruction tile; never directly encoded. */
} npc_tile;
/** Host outcomes visible on the next observation. */
typedef enum npc_outcome {
    NPC_INITIAL = 0,   /**< Initial observation without a previous action. */
    NPC_MOVED = 1,     /**< Executed cardinal movement. */
    NPC_BLOCKED = 2,   /**< Admitted movement encountered an unseen obstruction. */
    NPC_IDLE = 3,      /**< Idle decision consumed. */
    NPC_ACQUIRED = 4,  /**< Prerequisite acquired. */
    NPC_COMPLETED = 5, /**< Successful terminal interaction. */
    NPC_DIED = 6,      /**< Fatal terminal outcome. */
    NPC_INVALID = 7    /**< Host rejected an illegal proposal. */
} npc_outcome;
/** Authoritative terminal classification. */
typedef enum npc_terminal {
    NPC_RUNNING = 0, /**< Episode awaiting another decision. */
    NPC_SUCCESS = 1, /**< Authoritative objective completion. */
    NPC_DEATH = 2,   /**< Authoritative fatal outcome. */
    NPC_TIMEOUT = 3  /**< Decision budget exhausted. */
} npc_terminal;

/** Stable family provenance, without any learner-visible identity. */
typedef struct npc_family {
    uint32_t id;           /**< Unique family ID, zero through71. */
    npc_split split;       /**< Pinned family-level partition. */
    npc_mechanic mechanic; /**< Principal mechanic class. */
    uint32_t layout;       /**< Authored geometry index within the mechanic. */
} npc_family;
/** Only visible information; neither private branch choice nor map is exposed. */
typedef struct npc_observation {
    uint32_t neighbors[4]; /**< North, east, south and west observed tile IDs. */
    int32_t target_dx;     /**< Visible objective displacement, minus8 through8. */
    int32_t target_dy;     /**< Visible objective displacement, minus8 through8. */
    uint32_t inventory;    /**< Previously acquired prerequisite, zero or one. */
    uint32_t cue; /**< Currently visible route direction, zero unknown or one throughfour. */
    uint32_t last_action;     /**< Previous host proposal, zero throughsix. */
    uint32_t last_outcome;    /**< Observable previous outcome. */
    uint32_t stage;           /**< Approach0, choose1, acquire2 or exit3. */
    uint32_t at_target;       /**< Current position equals the visible objective. */
    uint32_t ticks;           /**< Decisions already consumed, zero through64. */
    uint32_t ticks_remaining; /**< Visible remaining decision budget. */
    uint32_t mechanic;        /**< Visible objective mechanic, zero throughtwo. */
    uint64_t allowed_actions; /**< Observable execution permissions, always fallback and wait. */
} npc_observation;
/** Independent caller-owned history containing only observed information. */
typedef struct npc_memory {
    uint32_t cue;           /**< Last actually observed route cue, zero unknown. */
    uint32_t cue_age;       /**< Saturating age in observations, zero throughseven. */
    uint32_t last_action;   /**< Most recently observed executed or rejected proposal. */
    uint32_t last_outcome;  /**< Most recently observed outcome. */
    uint32_t observed_tick; /**< Idempotent observation-update tick. */
    uint32_t initialized;   /**< Whether an observation has been consumed. */
} npc_memory;
/** Host-owned bounded world; never passed to an observation-limited controller. */
typedef struct npc_world {
    npc_family family;                             /**< Complete provenance. */
    uint32_t variant;                              /**< Family-local seed and rotation sibling. */
    uint32_t cells[NPC_GRID_SIDE * NPC_GRID_SIDE]; /**< Authoritative grid tiles. */
    uint32_t width;                                /**< Active grid width. */
    uint32_t height;                               /**< Active grid height. */
    uint32_t x;                                    /**< Actor coordinate. */
    uint32_t y;                                    /**< Actor coordinate. */
    uint32_t item_x;                               /**< Visible item coordinate. */
    uint32_t item_y;                               /**< Visible item coordinate. */
    uint32_t exit_x;                               /**< Visible exit coordinate. */
    uint32_t exit_y;                               /**< Visible exit coordinate. */
    uint32_t junction_x;                           /**< Visible route-choice landmark. */
    uint32_t junction_y;                           /**< Visible route-choice landmark. */
    uint32_t cue_direction;                        /**< Private cue value, only shown initially. */
    uint32_t branch_chosen;                        /**< Host branch commitment. */
    uint32_t inventory;                            /**< Host prerequisite state. */
    uint32_t obstruction_revealed;                 /**< Whether a bump exposed the obstruction. */
    uint32_t ticks;                                /**< Consumed decisions. */
    uint32_t last_action;                          /**< Last proposed action. */
    uint32_t last_outcome;                         /**< Last visible transition outcome. */
    uint32_t attempted_illegal;                    /**< Rejected mask or malformed proposals. */
    uint32_t executed_illegal;                     /**< Host invariant: always zero. */
    npc_terminal terminal;                         /**< Current authoritative terminal outcome. */
} npc_world;

/** @brief Return the pinned category cardinalities.
 * @param cards Writable array of at least16 entries. */
void npc_cardinalities(uint32_t cards[NPC_FEATURE_COUNT]);
/** @brief Resolve a pinned family without revealing it to the policy.
 * @param split Requested complete-family split.
 * @param index Split-local family index, zero through23.
 * @param family Writable result.
 * @return One on success, zero for invalid arguments. */
int npc_family_get(npc_split split, uint32_t index, npc_family *family);
/** @brief Construct one deterministic initial episode.
 * @param world Writable host world.
 * @param family Borrowed pinned family.
 * @param variant Family-local variant, zero through47.
 * @return One on success, zero without mutation for invalid arguments. */
int npc_world_init(npc_world *world, const npc_family *family, uint32_t variant);
/** @brief Extract only current visible information and permissions.
 * @param world Borrowed initialized host world.
 * @param observation Writable public observation. */
void npc_world_observe(const npc_world *world, npc_observation *observation);
/** @brief Consume one proposal using authoritative host validation.
 * @param world Mutable initialized world.
 * @param action Proposed stable action ID.
 * @return Visible transition outcome; terminal worlds remain unchanged. */
npc_outcome npc_world_step(npc_world *world, uint32_t action);
/** @brief Reset a session without carrying information across episodes.
 * @param memory Writable caller-owned history. */
void npc_memory_reset(npc_memory *memory);
/** @brief Update bounded memory from one visible observation, idempotently per tick.
 * @param memory Mutable independent session history.
 * @param observation Borrowed visible observation. */
void npc_memory_observe(npc_memory *memory, const npc_observation *observation);
/** @brief Encode the frozen contract without inspecting a world or teacher.
 * @param observation Borrowed current visible information.
 * @param memory Borrowed observable history.
 * @param history Nonzero enables history fields; zero zeros all four fields.
 * @param state Writable complete categorical state. */
void npc_encode(const npc_observation *observation, const npc_memory *memory, int history,
                cgai_gameplay_state *state);
/** @brief Return execution permissions already present in the observation.
 * @param observation Borrowed visible observation.
 * @return Action-bit mask, or fallback only for NULL. */
uint64_t npc_observation_actions(const npc_observation *observation);
#endif
