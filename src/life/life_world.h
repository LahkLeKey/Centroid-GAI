#ifndef LIFE_WORLD_H
#define LIFE_WORLD_H

#include <stdint.h>

#define LIFE_WIDTH 32
#define LIFE_HEIGHT 32
#define LIFE_CELLS (LIFE_WIDTH * LIFE_HEIGHT)
#define LIFE_MODULES 8
#define LIFE_DEFAULT_MODULES 4U
#define LIFE_MIN_MODULES 2U
#define LIFE_MAX_PATCHES 4
#define LIFE_PATCH_CELLS 6
#define LIFE_OUTPUTS 23
#define LIFE_MAX_CONFLICTS 16
#define LIFE_RESOLUTION_TICKS 8

typedef enum life_status {
    LIFE_OK = 0,
    LIFE_INVALID_ARGUMENT = -1,
    LIFE_STALE_FRAME = -2,
    LIFE_INVALID_ACTION = -3
} life_status;

typedef enum life_outcome {
    LIFE_UNRESOLVED = 0,
    LIFE_SEPARATED = 1,
    LIFE_COUPLED = 2,
    LIFE_ABSORBED = 3,
    LIFE_EXTINCT = 4,
    LIFE_MERGED = 5
} life_outcome;

typedef struct life_entity {
    uint32_t uid;
    uint8_t ancestry;
    uint8_t active;
} life_entity;

typedef struct life_conflict {
    uint32_t id;
    uint32_t age;
    uint32_t last_tick;
    uint8_t module_mask;
    uint8_t active;
    uint8_t contact_streak;
    uint8_t separation_streak;
    uint8_t outcome;
} life_conflict;

typedef struct life_stats {
    uint32_t collisions;
    uint32_t reopened;
    uint32_t separated;
    uint32_t coupled;
    uint32_t absorbed;
    uint32_t extinct;
    uint32_t merged;
    uint32_t edits;
} life_stats;

typedef struct life_world {
    uint32_t group_count;
    uint8_t cells[LIFE_CELLS];
    life_entity entities[LIFE_MODULES];
    life_conflict conflicts[LIFE_MAX_CONFLICTS];
    life_stats stats;
    uint32_t tick;
    uint32_t seed;
    uint32_t next_uid;
    uint32_t next_conflict_id;
} life_world;

typedef struct life_patch {
    uint16_t cells[LIFE_PATCH_CELLS];
    uint32_t age;
    uint32_t conflict_id;
    uint8_t module_mask;
    uint8_t cell_count;
} life_patch;

typedef struct life_frame {
    life_world baseline;
    life_patch patches[LIFE_MAX_PATCHES];
    uint64_t source_hash;
    uint8_t patch_count;
} life_frame;

/* Coordinates wrap around both edges. Scenario 0..3 seeds collision fixtures. */
void life_world_clear(life_world *world, uint32_t seed);
int life_world_init(life_world *world, uint32_t seed, unsigned int scenario);
/* Configured slots2..8; zero selects four. Extra slots reuse lower-four placements.
 * Invalid count/arguments preserve the world. Legacy wrappers select four. */
int life_world_clear_groups(life_world *world, uint32_t seed, uint32_t group_count);
int life_world_init_groups(life_world *world, uint32_t seed, unsigned int scenario,
                           uint32_t group_count);
uint8_t life_world_group_mask(const life_world *world);
int life_world_set(life_world *world, int x, int y, uint8_t claims);
unsigned int life_neighbor_count(const life_world *world, int x, int y);
int life_prepare(const life_world *world, life_frame *frame);
/* All actions are checked before any mutation. NULL outputs means all fallback. */
int life_apply(life_world *world, const life_frame *frame,
               const uint32_t outputs[LIFE_MAX_PATCHES]);
/* 0 and 1 have no toggles; 2..7 one toggle; 8..22 lexicographic pairs. */
uint8_t life_output_bits(uint32_t output);
/* Bit n is set when output n fits this patch's cell_count. */
uint32_t life_allowed_outputs(const life_patch *patch);
uint64_t life_world_hash(const life_world *world);
uint32_t life_population(const life_world *world, uint8_t module_mask);
/* Returns the lowest canonical coupled mask ready for model consolidation. */
uint8_t life_merge_ready_mask(const life_world *world);
/* Explicit topology merge; caller validates/consolidates learned models first. */
int life_world_merge(life_world *world, uint8_t module_mask);

#endif
