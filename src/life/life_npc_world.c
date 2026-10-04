/** @file life_npc_world.c @brief Fresh reserved native NPC rooms and observation-only adapter. */
#include "life_npc_world.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/** Frozen cardinal column increments. */
static const int32_t life_npc_dx[4] = {0, 1, 0, -1};
/** Frozen cardinal row increments. */
static const int32_t life_npc_dy[4] = {-1, 0, 1, 0};
/** @brief Index a bounded coordinate.
 * @param x Column.
 * @param y Row.
 * @return Row-major index. */
static size_t life_npc_cell(uint32_t x, uint32_t y) { return (size_t)y * LIFE_NPC_GRID_SIDE + x; }
/** @brief Rotate one grid point clockwise.
 * @param x Mutable column.
 * @param y Mutable row.
 * @param rotations Quarter turns. */
static void life_npc_rotate_point(uint32_t *x, uint32_t *y, uint32_t rotations) {
    for (uint32_t i = 0; i < rotations; ++i) {
        uint32_t next_x = LIFE_NPC_GRID_SIDE - 1U - *y;
        *y = *x;
        *x = next_x;
    }
}
/** @brief Rotate the bounded grid.
 * @param world Mutable host world.
 * @param rotations Clockwise quarter turns. */
static void life_npc_rotate_grid(life_npc_world *world, uint32_t rotations) {
    uint32_t cells[LIFE_NPC_GRID_SIDE * LIFE_NPC_GRID_SIDE];
    for (uint32_t y = 0; y < LIFE_NPC_GRID_SIDE; ++y) {
        for (uint32_t x = 0; x < LIFE_NPC_GRID_SIDE; ++x) {
            uint32_t rx = x, ry = y;
            life_npc_rotate_point(&rx, &ry, rotations);
            cells[life_npc_cell(rx, ry)] = world->cells[life_npc_cell(x, y)];
        }
    }
    memcpy(world->cells, cells, sizeof(cells));
}
/** @brief Rotate host coordinates and the initially visible cue.
 * @param world Mutable host world.
 * @param rotations Clockwise quarter turns. */
static void life_npc_rotate(life_npc_world *world, uint32_t rotations) {
    life_npc_rotate_grid(world, rotations);
    life_npc_rotate_point(&world->x, &world->y, rotations);
    life_npc_rotate_point(&world->item_x, &world->item_y, rotations);
    life_npc_rotate_point(&world->exit_x, &world->exit_y, rotations);
    life_npc_rotate_point(&world->junction_x, &world->junction_y, rotations);
    if (world->cue_direction != 0U)
        world->cue_direction = (world->cue_direction - 1U + rotations) % 4U + 1U;
}
void life_npc_cardinalities(uint32_t cards[LIFE_NPC_FEATURE_COUNT]) {
    static const uint32_t values[LIFE_NPC_FEATURE_COUNT] = {6, 6, 6, 6, 17, 17, 2, 5,
                                                            5, 7, 8, 8, 4,  2,  8, 3};
    if (cards != NULL)
        memcpy(cards, values, sizeof(values));
}
int life_npc_family_get(life_npc_split split, uint32_t index, life_npc_family *family) {
    life_npc_family value;
    if (family == NULL || (uint32_t)split > (uint32_t)LIFE_NPC_CONFIRM ||
        index >= LIFE_NPC_FAMILIES_PER_SPLIT)
        return 0;
    value.id = (uint32_t)split * LIFE_NPC_FAMILIES_PER_SPLIT + index;
    value.split = split;
    value.mechanic = (life_npc_mechanic)(index % 3U);
    value.layout = 3U * (index / 3U) + (uint32_t)split;
    *family = value;
    return 1;
}
/** @brief Validate explicit family fields without comparing padding.
 * @param family Borrowed proposed family.
 * @return One if every authored field matches. */
static int life_npc_family_valid(const life_npc_family *family) {
    life_npc_family canonical;
    return family != NULL &&
           life_npc_family_get(family->split, family->id % LIFE_NPC_FAMILIES_PER_SPLIT,
                               &canonical) &&
           canonical.id == family->id && canonical.split == family->split &&
           canonical.mechanic == family->mechanic && canonical.layout == family->layout;
}
/** @brief Fill an open room with a solid perimeter.
 * @param world Writable zeroed host world. */
static void life_npc_open_room(life_npc_world *world) {
    for (uint32_t y = 0; y < LIFE_NPC_GRID_SIDE; ++y)
        for (uint32_t x = 0; x < LIFE_NPC_GRID_SIDE; ++x)
            world->cells[life_npc_cell(x, y)] =
                x == 0U || y == 0U || x == 8U || y == 8U ? LIFE_NPC_WALL : LIFE_NPC_FLOOR;
}
/** @brief Author the frozen height-three-through-five mandatory barrier.
 * @param world Mutable open room with family provenance. */
static void life_npc_room_geometry(life_npc_world *world) {
    const uint32_t layout = world->family.layout, height = 3U + (layout / 8U) % 3U;
    const uint32_t pattern = world->variant / 8U;
    world->x = 1U;
    world->y = world->family.mechanic == LIFE_NPC_HAZARD ? height : 1U;
    world->item_x = 4U + layout % 4U;
    world->item_y = 6U + pattern % 2U;
    world->exit_x = 1U + (layout / 4U) % 2U;
    world->exit_y = world->item_y;
    for (uint32_t y = 1U; y <= height; ++y)
        world->cells[life_npc_cell(2U, y)] = LIFE_NPC_WALL;
    world->cells[life_npc_cell(3U, height)] = LIFE_NPC_WALL;
}
/** @brief Add an occluded bump and a visible off-route hazard.
 * @param world Mutable non-cue world with barrier coordinates. */
static void life_npc_room_hazards(life_npc_world *world) {
    const uint32_t row = 1U + world->variant / 8U / 2U;
    const uint32_t height = 3U + (world->family.layout / 8U) % 3U;
    if (world->cells[life_npc_cell(3U, row)] == LIFE_NPC_FLOOR)
        world->cells[life_npc_cell(3U, row)] = LIFE_NPC_DANGER;
    if (world->family.mechanic == LIFE_NPC_HAZARD) {
        world->cells[life_npc_cell(2U, height)] = LIFE_NPC_OBSTRUCTION;
        world->cells[life_npc_cell(1U, height - 1U)] = LIFE_NPC_DANGER;
    }
}
/** @brief Author private cue endpoints without exposing them to the adapter.
 * @param world Mutable room with provenance. */
static void life_npc_cue_geometry(life_npc_world *world) {
    static const uint32_t branches[6][2] = {{1, 1}, {1, 2}, {1, 3}, {2, 2}, {2, 3}, {3, 3}};
    uint32_t layout = world->family.layout, pattern = world->variant / 8U;
    uint32_t correct_left = (world->variant / 4U) % 2U;
    uint32_t left = branches[layout % 6U][0], right = branches[layout % 6U][1];
    world->junction_x = world->x = 4U;
    world->junction_y = 5U;
    world->y = layout / 6U < 2U ? 0U : 1U;
    world->cue_direction = correct_left != 0U ? 4U : 2U;
    world->item_x = correct_left != 0U ? 4U - left : 4U + right;
    world->item_y = world->junction_y;
    world->exit_x = world->item_x;
    uint32_t exit_length = 1U + pattern % 3U;
    world->exit_y = pattern / 3U == 0U ? world->item_y + exit_length : world->item_y - exit_length;
}
/** @brief Open paired neutral straight stems without revealing the correct branch.
 * @param world Mutable unrotated cue world.
 * @param left_end Unrotated left branch endpoint.
 * @param right_end Unrotated right branch endpoint. */
static void life_npc_cue_exit_tiles(life_npc_world *world, uint32_t left_end, uint32_t right_end) {
    uint32_t length = 1U + world->variant / 8U % 3U;
    int32_t side = world->variant / 8U / 3U == 0U ? 1 : -1;
    for (uint32_t offset = 1U; offset <= length; ++offset) {
        uint32_t y = (uint32_t)((int32_t)world->junction_y + (int32_t)offset * side);
        world->cells[life_npc_cell(left_end, y)] = LIFE_NPC_FLOOR;
        world->cells[life_npc_cell(right_end, y)] = LIFE_NPC_FLOOR;
    }
}
/** @brief Open a mandatory dogleg approach four or five cells from the junction.
 * @param world Mutable unrotated cue world. */
static void life_npc_cue_approach(life_npc_world *world) {
    const uint32_t turn = 1U + world->family.layout / 6U % 2U;
    for (uint32_t y = world->y; y <= turn; ++y)
        world->cells[life_npc_cell(4U, y)] = LIFE_NPC_FLOOR;
    for (uint32_t y = turn; y <= 4U; ++y)
        world->cells[life_npc_cell(5U, y)] = LIFE_NPC_FLOOR;
    world->cells[life_npc_cell(4U, 4U)] = LIFE_NPC_FLOOR;
}
/** @brief Construct the frozen approach and both neutral branch entrances.
 * @param world Mutable world with cue coordinates. */
static void life_npc_cue_tiles(life_npc_world *world) {
    static const uint32_t branches[6][2] = {{1, 1}, {1, 2}, {1, 3}, {2, 2}, {2, 3}, {3, 3}};
    uint32_t left_end = 4U - branches[world->family.layout % 6U][0];
    uint32_t right_end = 4U + branches[world->family.layout % 6U][1];
    for (uint32_t i = 0; i < LIFE_NPC_GRID_SIDE * LIFE_NPC_GRID_SIDE; ++i)
        world->cells[i] = LIFE_NPC_WALL;
    life_npc_cue_approach(world);
    for (uint32_t x = left_end; x <= right_end; ++x)
        world->cells[life_npc_cell(x, world->junction_y)] = LIFE_NPC_FLOOR;
    life_npc_cue_exit_tiles(world, left_end, right_end);
}
/** @brief Construct unrotated geometry and visible objective tiles.
 * @param world Mutable zeroed world with provenance. */
static void life_npc_geometry(life_npc_world *world) {
    life_npc_open_room(world);
    if (world->family.mechanic == LIFE_NPC_CUE) {
        life_npc_cue_geometry(world);
        life_npc_cue_tiles(world);
    } else {
        life_npc_room_geometry(world);
        life_npc_room_hazards(world);
    }
    world->cells[life_npc_cell(world->item_x, world->item_y)] = LIFE_NPC_ITEM_TILE;
    world->cells[life_npc_cell(world->exit_x, world->exit_y)] = LIFE_NPC_EXIT_TILE;
}
/** @brief Reflect a room sibling without changing its public action contract.
 * @param world Mutable non-cue host world. */
static void life_npc_reflect_room(life_npc_world *world) {
    for (uint32_t y = 0U; y < LIFE_NPC_GRID_SIDE; ++y)
        for (uint32_t x = 0U; x < LIFE_NPC_GRID_SIDE / 2U; ++x) {
            const size_t left = life_npc_cell(x, y), right = life_npc_cell(8U - x, y);
            const uint32_t tile = world->cells[left];
            world->cells[left] = world->cells[right];
            world->cells[right] = tile;
        }
    world->x = 8U - world->x;
    world->item_x = 8U - world->item_x;
    world->exit_x = 8U - world->exit_x;
}
int life_npc_world_init(life_npc_world *world, const life_npc_family *family, uint32_t variant) {
    life_npc_world value;
    if (world == NULL || !life_npc_family_valid(family) || variant >= LIFE_NPC_VARIANTS_PER_FAMILY)
        return 0;
    memset(&value, 0, sizeof(value));
    value.family = *family;
    value.variant = variant;
    value.width = LIFE_NPC_GRID_SIDE;
    value.height = LIFE_NPC_GRID_SIDE;
    life_npc_geometry(&value);
    if (family->mechanic != LIFE_NPC_CUE && (variant / 4U) % 2U != 0U)
        life_npc_reflect_room(&value);
    life_npc_rotate(&value, variant % 4U);
    *world = value;
    return 1;
}
/** @brief Read one neighbor with obstruction and endpoint occlusion.
 * @param world Borrowed host world.
 * @param direction Cardinal index, zero throughthree.
 * @return Observable tile ID. */
static uint32_t life_npc_neighbor(const life_npc_world *world, uint32_t direction) {
    int32_t x = (int32_t)world->x + life_npc_dx[direction];
    int32_t y = (int32_t)world->y + life_npc_dy[direction];
    uint32_t tile = LIFE_NPC_WALL;
    if (x >= 0 && y >= 0 && x < (int32_t)world->width && y < (int32_t)world->height)
        tile = world->cells[life_npc_cell((uint32_t)x, (uint32_t)y)];
    if (tile == LIFE_NPC_OBSTRUCTION)
        tile = world->obstruction_revealed != 0U ? LIFE_NPC_WALL : LIFE_NPC_FLOOR;
    if (world->family.mechanic == LIFE_NPC_CUE && world->branch_chosen == 0U &&
        (tile == LIFE_NPC_ITEM_TILE || tile == LIFE_NPC_EXIT_TILE))
        tile = LIFE_NPC_FLOOR;
    return tile;
}
/** @brief Select an announced objective, never an unseen branch endpoint.
 * @param world Borrowed host world.
 * @param value Mutable public observation. */
static void life_npc_observe_target(const life_npc_world *world, life_npc_observation *value) {
    uint32_t tx, ty;
    if (world->family.mechanic == LIFE_NPC_CUE && world->branch_chosen == 0U) {
        tx = world->junction_x;
        ty = world->junction_y;
        value->stage = world->x == tx && world->y == ty ? 1U : 0U;
        if (world->ticks == 0U)
            value->cue = world->cue_direction;
    } else {
        tx = world->inventory == 0U ? world->item_x : world->exit_x;
        ty = world->inventory == 0U ? world->item_y : world->exit_y;
        value->stage = world->inventory == 0U ? 2U : 3U;
    }
    value->target_dx = (int32_t)tx - (int32_t)world->x;
    value->target_dy = (int32_t)ty - (int32_t)world->y;
    value->at_target = value->target_dx == 0 && value->target_dy == 0;
}
/** @brief Derive permissions only from visible collisions and staging.
 * @param world Borrowed host world.
 * @param value Mutable public observation with objective fields. */
static void life_npc_observe_permissions(const life_npc_world *world, life_npc_observation *value) {
    value->allowed_actions = UINT64_C(3);
    for (uint32_t direction = 0; direction < 4U; ++direction) {
        value->neighbors[direction] = life_npc_neighbor(world, direction);
        if (value->neighbors[direction] != LIFE_NPC_WALL)
            value->allowed_actions |= UINT64_C(1) << (direction + 2U);
    }
    if (value->at_target != 0U && value->stage >= 2U)
        value->allowed_actions |= UINT64_C(1) << LIFE_NPC_INTERACT;
    if ((world->family.mechanic != LIFE_NPC_CUE && world->ticks == 0U) ||
        world->terminal != LIFE_NPC_RUNNING)
        value->allowed_actions = UINT64_C(3);
}
void life_npc_world_observe(const life_npc_world *world, life_npc_observation *observation) {
    life_npc_observation value;
    if (world == NULL || observation == NULL)
        return;
    memset(&value, 0, sizeof(value));
    value.inventory = world->inventory;
    value.last_action = world->last_action;
    value.last_outcome = world->last_outcome;
    value.ticks = world->ticks;
    value.ticks_remaining = LIFE_NPC_MAX_TICKS - world->ticks;
    value.mechanic = (uint32_t)world->family.mechanic;
    life_npc_observe_target(world, &value);
    life_npc_observe_permissions(world, &value);
    *observation = value;
}
/** @brief Commit a route and apply its irreversible wrong-branch consequence.
 * @param world Mutable host world.
 * @param nx Admitted destination column.
 * @param ny Admitted destination row. */
static void life_npc_commit_branch(life_npc_world *world, uint32_t nx, uint32_t ny) {
    uint32_t rotation = world->variant % 4U;
    if (world->family.mechanic != LIFE_NPC_CUE || world->branch_chosen != 0U ||
        world->x != world->junction_x || world->y != world->junction_y)
        return;
    int32_t projection =
        ((int32_t)nx - (int32_t)world->junction_x) * life_npc_dx[(1U + rotation) % 4U] +
        ((int32_t)ny - (int32_t)world->junction_y) * life_npc_dy[(1U + rotation) % 4U];
    if (projection != 0) {
        uint32_t direction = projection > 0 ? (1U + rotation) % 4U : (3U + rotation) % 4U;
        world->branch_chosen = 1U;
        if (direction + 1U != world->cue_direction)
            world->terminal = LIFE_NPC_DEATH;
    }
}
/** @brief Apply admitted movement with authoritative collision and hazard effects.
 * @param world Mutable host world.
 * @param action Admitted cardinal action.
 * @return Visible host outcome. */
static life_npc_outcome life_npc_move(life_npc_world *world, uint32_t action) {
    uint32_t direction = action - LIFE_NPC_NORTH;
    uint32_t nx = (uint32_t)((int32_t)world->x + life_npc_dx[direction]);
    uint32_t ny = (uint32_t)((int32_t)world->y + life_npc_dy[direction]);
    uint32_t tile = world->cells[life_npc_cell(nx, ny)];
    if (tile == LIFE_NPC_OBSTRUCTION) {
        world->obstruction_revealed = 1U;
        return LIFE_NPC_BLOCKED;
    }
    life_npc_commit_branch(world, nx, ny);
    world->x = nx;
    world->y = ny;
    if (tile == LIFE_NPC_DANGER)
        world->terminal = LIFE_NPC_DEATH;
    return world->terminal == LIFE_NPC_DEATH ? LIFE_NPC_DIED : LIFE_NPC_MOVED;
}
/** @brief Acquire a prerequisite or validate it before opening the exit.
 * @param world Mutable host world.
 * @return Visible interaction outcome. */
static life_npc_outcome life_npc_interact(life_npc_world *world) {
    if (world->inventory == 0U && world->x == world->item_x && world->y == world->item_y) {
        world->inventory = 1U;
        world->cells[life_npc_cell(world->item_x, world->item_y)] = LIFE_NPC_FLOOR;
        return LIFE_NPC_ACQUIRED;
    }
    if (world->inventory != 0U && world->x == world->exit_x && world->y == world->exit_y) {
        world->terminal = LIFE_NPC_SUCCESS;
        return LIFE_NPC_COMPLETED;
    }
    ++world->attempted_illegal;
    return LIFE_NPC_INVALID;
}
/** @brief Dispatch an admitted action, keeping fallback as costly idle.
 * @param world Mutable host world.
 * @param action Admitted stable action.
 * @return Visible host outcome. */
static life_npc_outcome life_npc_execute(life_npc_world *world, uint32_t action) {
    if (action >= LIFE_NPC_NORTH && action <= LIFE_NPC_WEST)
        return life_npc_move(world, action);
    if (action == LIFE_NPC_INTERACT)
        return life_npc_interact(world);
    return LIFE_NPC_IDLE;
}
/** @brief Finalize the decision budget and visible outcome.
 * @param world Mutable host world.
 * @param outcome Executed or rejected proposal outcome.
 * @return The same visible outcome. */
static life_npc_outcome life_npc_finish_step(life_npc_world *world, life_npc_outcome outcome) {
    if (world->terminal == LIFE_NPC_RUNNING && world->ticks >= LIFE_NPC_MAX_TICKS)
        world->terminal = LIFE_NPC_TIMEOUT;
    world->last_outcome = (uint32_t)outcome;
    return outcome;
}

life_npc_outcome life_npc_world_step(life_npc_world *world, uint32_t action) {
    life_npc_observation observation;
    life_npc_outcome outcome;
    if (world == NULL)
        return LIFE_NPC_INVALID;
    if (world->terminal != LIFE_NPC_RUNNING)
        return (life_npc_outcome)world->last_outcome;
    life_npc_world_observe(world, &observation);
    world->last_action = action < 7U ? action : LIFE_NPC_FALLBACK;
    ++world->ticks;
    if (action >= 7U || (observation.allowed_actions & (UINT64_C(1) << action)) == 0U) {
        ++world->attempted_illegal;
        outcome = LIFE_NPC_INVALID;
    } else
        outcome = life_npc_execute(world, action);
    return life_npc_finish_step(world, outcome);
}
void life_npc_memory_reset(life_npc_memory *memory) {
    if (memory != NULL)
        memset(memory, 0, sizeof(*memory));
}
void life_npc_memory_observe(life_npc_memory *memory, const life_npc_observation *observation) {
    if (memory == NULL || observation == NULL ||
        (memory->initialized != 0U && memory->observed_tick == observation->ticks))
        return;
    if (observation->cue != 0U) {
        memory->cue = observation->cue;
        memory->cue_age = 0U;
    } else if (memory->cue != 0U && memory->cue_age < 7U)
        ++memory->cue_age;
    memory->last_action = observation->last_action;
    memory->last_outcome = observation->last_outcome;
    memory->observed_tick = observation->ticks;
    memory->initialized = 1U;
}
/** @brief Encode only explicit observed memory into its four frozen fields.
 * @param memory Borrowed updated caller history.
 * @param state Mutable categorical result. */
static void life_npc_encode_memory(const life_npc_memory *memory, cgai_gameplay_state *state) {
    state->values[8] = memory->cue;
    state->values[9] = memory->last_action;
    state->values[10] = memory->last_outcome;
    state->values[11] = memory->cue_age;
}
void life_npc_encode(const life_npc_observation *observation, const life_npc_memory *memory,
                     int history, cgai_gameplay_state *state) {
    if (observation == NULL || memory == NULL || state == NULL)
        return;
    memset(state, 0, sizeof(*state));
    memcpy(state->values, observation->neighbors, sizeof(observation->neighbors));
    state->values[4] = (uint32_t)(observation->target_dx + 8);
    state->values[5] = (uint32_t)(observation->target_dy + 8);
    state->values[6] = observation->inventory;
    state->values[7] = observation->cue;
    if (history != 0)
        life_npc_encode_memory(memory, state);
    state->values[12] = observation->stage;
    state->values[13] = observation->at_target;
    state->values[14] = observation->ticks < 7U ? observation->ticks : 7U;
    state->values[15] = observation->mechanic;
}
uint64_t life_npc_observation_actions(const life_npc_observation *observation) {
    return observation == NULL ? UINT64_C(1) : observation->allowed_actions;
}

/** @brief Check the complete fixed categorical observation without host state.
 * @param task Borrowed visible input.
 * @return One for bounded nonprivate categories and the actual tick encoding. */
static int life_npc_task_fields(const cgai_life_domain_task *task) {
    uint32_t cards[LIFE_NPC_FEATURE_COUNT];
    if (task == NULL || task->feature_count != LIFE_NPC_FEATURE_COUNT || task->x != 0U ||
        task->y != 0U || task->observed_fields != UINT32_C(65535) ||
        task->tick >= LIFE_NPC_MAX_TICKS ||
        task->features[14] != (task->tick < 7U ? task->tick : 7U))
        return 0;
    life_npc_cardinalities(cards);
    for (uint32_t i = 0U; i < LIFE_NPC_FEATURE_COUNT; ++i)
        if (task->features[i] >= cards[i] || (i < 4U && task->features[i] == LIFE_NPC_OBSTRUCTION))
            return 0;
    return 1;
}
/** @brief Check stage, inventory and announced-target coherence.
 * @param task Borrowed complete categorical observation.
 * @return One if these visible facts are mutually consistent. */
static int life_npc_task_stage(const cgai_life_domain_task *task) {
    const uint32_t *f = task->features;
    if ((f[4] == 8U && f[5] == 8U) != (f[13] != 0U))
        return 0;
    if (f[12] < 2U)
        return f[15] == LIFE_NPC_CUE && f[6] == 0U && f[13] == f[12];
    return f[6] == (f[12] == 3U ? 1U : 0U);
}
/** @brief Check visible cue and caller-declared observation memory categories.
 * @param task Borrowed complete categorical observation.
 * @return One for history that is not contradicted by the visible tick or cue. */
static int life_npc_task_history(const cgai_life_domain_task *task) {
    const uint32_t *f = task->features;
    if (f[7] != 0U && (f[15] != LIFE_NPC_CUE || task->tick != 0U || f[8] != f[7] || f[11] != 0U))
        return 0;
    if (f[15] != LIFE_NPC_CUE && (f[7] != 0U || f[8] != 0U || f[11] != 0U))
        return 0;
    if ((f[8] == 0U && f[11] != 0U) || f[11] > (task->tick < 7U ? task->tick : 7U))
        return 0;
    return task->tick != 0U || (f[9] == LIFE_NPC_FALLBACK && f[10] == LIFE_NPC_INITIAL);
}
/** @brief Derive legal actions solely from the visible collision and stage facts.
 * @param task Borrowed complete categorical observation.
 * @return Canonical seven-action permission mask. */
static uint32_t life_npc_task_actions(const cgai_life_domain_task *task) {
    uint32_t mask = 3U;
    if (task->tick == 0U && task->features[15] != LIFE_NPC_CUE)
        return mask;
    for (uint32_t direction = 0U; direction < 4U; ++direction)
        if (task->features[direction] != LIFE_NPC_WALL)
            mask |= UINT32_C(1) << (direction + LIFE_NPC_NORTH);
    if (task->features[13] != 0U && task->features[12] >= 2U)
        mask |= UINT32_C(1) << LIFE_NPC_INTERACT;
    return mask;
}
int life_npc_task_valid(const cgai_life_domain_task *task) {
    return life_npc_task_fields(task) && life_npc_task_stage(task) && life_npc_task_history(task) &&
           task->legal_actions == life_npc_task_actions(task) && task->fallback < 7U &&
           (task->legal_actions & (UINT32_C(1) << task->fallback)) != 0U;
}
/** @brief Decode only the declared16 visible and historical categorical fields.
 * @param task Borrowed validated categorical observation.
 * @param observation Writable teacher observation.
 * @param memory Writable teacher history. */
static void life_npc_task_decode(const cgai_life_domain_task *task,
                                 life_npc_observation *observation, life_npc_memory *memory) {
    const uint32_t *f = task->features;
    memset(observation, 0, sizeof(*observation));
    memcpy(observation->neighbors, f, sizeof(observation->neighbors));
    observation->target_dx = (int32_t)f[4] - 8;
    observation->target_dy = (int32_t)f[5] - 8;
    observation->inventory = f[6];
    observation->cue = f[7];
    observation->stage = f[12];
    observation->at_target = f[13];
    observation->ticks = task->tick;
    observation->mechanic = f[15];
    observation->allowed_actions = task->legal_actions;
    *memory = (life_npc_memory){f[8], f[11], f[9], f[10], task->tick, 1U};
}
/** @brief Resolve observed route memory, otherwise use a fixed visible branch order.
 * @param observation Borrowed junction observation.
 * @param memory Borrowed declared observation history.
 * @return Admitted route action or wait. */
static uint32_t life_npc_route_action(const life_npc_observation *observation,
                                      const life_npc_memory *memory) {
    const uint32_t cue = memory->cue;
    if (cue != 0U && (observation->allowed_actions & (UINT64_C(1) << (cue + 1U))) != 0U)
        return cue + 1U;
    for (uint32_t direction = 0U; direction < 4U; ++direction) {
        const uint32_t action = direction + LIFE_NPC_NORTH, opposite = (direction + 2U) % 4U;
        if ((observation->allowed_actions & (UINT64_C(1) << action)) != 0U &&
            observation->neighbors[opposite] != LIFE_NPC_WALL)
            return action;
    }
    return LIFE_NPC_WAIT;
}
/** @brief Score a visible one-step successor with a fixed dominant-axis preference.
 * @param observation Borrowed announced objective.
 * @param direction Cardinal index.
 * @return Unit-cost Manhattan score and deterministic tie penalty. */
static int32_t life_npc_successor_score(const life_npc_observation *observation,
                                        uint32_t direction) {
    int32_t score = 32 * (abs(observation->target_dx - life_npc_dx[direction]) +
                          abs(observation->target_dy - life_npc_dy[direction]));
    if (abs(observation->target_dx) >= abs(observation->target_dy)) {
        if (life_npc_dx[direction] == 0)
            ++score;
    } else if (life_npc_dy[direction] == 0)
        ++score;
    return score;
}
/** @brief Search at most four admitted nonhazardous visible successor cells.
 * @param observation Borrowed complete visible information.
 * @return Lowest-score legal action with north/east/south/west final tie order. */
static uint32_t life_npc_search_action(const life_npc_observation *observation) {
    uint32_t best = LIFE_NPC_WAIT;
    int32_t score = INT_MAX;
    for (uint32_t direction = 0U; direction < 4U; ++direction) {
        const uint32_t action = direction + LIFE_NPC_NORTH;
        if ((observation->allowed_actions & (UINT64_C(1) << action)) == 0U ||
            observation->neighbors[direction] == LIFE_NPC_DANGER)
            continue;
        const int32_t candidate = life_npc_successor_score(observation, direction);
        if (candidate < score) {
            best = action;
            score = candidate;
        }
    }
    return best;
}
uint32_t life_npc_teacher_task(const cgai_life_domain_task *task) {
    life_npc_observation observation;
    life_npc_memory memory;
    if (!life_npc_task_valid(task))
        return LIFE_NPC_FALLBACK;
    life_npc_task_decode(task, &observation, &memory);
    if ((observation.allowed_actions & ~UINT64_C(3)) == 0U)
        return observation.mechanic == LIFE_NPC_HAZARD ? LIFE_NPC_FALLBACK : LIFE_NPC_WAIT;
    if (observation.at_target != 0U && observation.stage >= 2U)
        return LIFE_NPC_INTERACT;
    return observation.stage == 1U ? life_npc_route_action(&observation, &memory)
                                   : life_npc_search_action(&observation);
}
