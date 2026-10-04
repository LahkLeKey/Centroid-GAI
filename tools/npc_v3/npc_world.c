/** @file npc_world.c @brief Fresh balanced v3 recovery rooms and observable adapter. */
#include "npc_world.h"
#include <string.h>
/** Frozen cardinal column increments. */
static const int32_t npc_dx[4] = {0, 1, 0, -1};
/** Frozen cardinal row increments. */
static const int32_t npc_dy[4] = {-1, 0, 1, 0};
/** @brief Index a bounded coordinate.
 * @param x Column.
 * @param y Row.
 * @return Row-major index. */
static size_t npc_cell(uint32_t x, uint32_t y) { return (size_t)y * NPC_GRID_SIDE + x; }
/** @brief Rotate one grid point clockwise.
 * @param x Mutable column.
 * @param y Mutable row.
 * @param rotations Quarter turns. */
static void npc_rotate_point(uint32_t *x, uint32_t *y, uint32_t rotations) {
    for (uint32_t i = 0; i < rotations; ++i) {
        uint32_t next_x = NPC_GRID_SIDE - 1U - *y;
        *y = *x;
        *x = next_x;
    }
}
/** @brief Rotate the bounded grid.
 * @param world Mutable host world.
 * @param rotations Clockwise quarter turns. */
static void npc_rotate_grid(npc_world *world, uint32_t rotations) {
    uint32_t cells[NPC_GRID_SIDE * NPC_GRID_SIDE];
    for (uint32_t y = 0; y < NPC_GRID_SIDE; ++y) {
        for (uint32_t x = 0; x < NPC_GRID_SIDE; ++x) {
            uint32_t rx = x, ry = y;
            npc_rotate_point(&rx, &ry, rotations);
            cells[npc_cell(rx, ry)] = world->cells[npc_cell(x, y)];
        }
    }
    memcpy(world->cells, cells, sizeof(cells));
}
/** @brief Rotate host coordinates and the initially visible cue.
 * @param world Mutable host world.
 * @param rotations Clockwise quarter turns. */
static void npc_rotate(npc_world *world, uint32_t rotations) {
    npc_rotate_grid(world, rotations);
    npc_rotate_point(&world->x, &world->y, rotations);
    npc_rotate_point(&world->item_x, &world->item_y, rotations);
    npc_rotate_point(&world->exit_x, &world->exit_y, rotations);
    npc_rotate_point(&world->junction_x, &world->junction_y, rotations);
    if (world->cue_direction != 0U)
        world->cue_direction = (world->cue_direction - 1U + rotations) % 4U + 1U;
}
void npc_cardinalities(uint32_t cards[NPC_FEATURE_COUNT]) {
    static const uint32_t values[NPC_FEATURE_COUNT] = {6, 6, 6, 6, 17, 17, 2, 5,
                                                       5, 7, 8, 8, 4,  2,  8, 3};
    if (cards != NULL)
        memcpy(cards, values, sizeof(values));
}
int npc_family_get(npc_split split, uint32_t index, npc_family *family) {
    npc_family value;
    if (family == NULL || (uint32_t)split > (uint32_t)NPC_AUDIT || index >= NPC_FAMILIES_PER_SPLIT)
        return 0;
    value.id = (uint32_t)split * NPC_FAMILIES_PER_SPLIT + index;
    value.split = split;
    value.mechanic = (npc_mechanic)(index % 3U);
    value.layout = 3U * (index / 3U) + (uint32_t)split;
    *family = value;
    return 1;
}
/** @brief Validate explicit family fields without comparing padding.
 * @param family Borrowed proposed family.
 * @return One if every authored field matches. */
static int npc_family_valid(const npc_family *family) {
    npc_family canonical;
    return family != NULL &&
           npc_family_get(family->split, family->id % NPC_FAMILIES_PER_SPLIT, &canonical) &&
           canonical.id == family->id && canonical.split == family->split &&
           canonical.mechanic == family->mechanic && canonical.layout == family->layout;
}
/** @brief Fill an open room with a solid perimeter.
 * @param world Writable zeroed host world. */
static void npc_open_room(npc_world *world) {
    for (uint32_t y = 0; y < NPC_GRID_SIDE; ++y)
        for (uint32_t x = 0; x < NPC_GRID_SIDE; ++x)
            world->cells[npc_cell(x, y)] =
                x == 0U || y == 0U || x == 8U || y == 8U ? NPC_WALL : NPC_FLOOR;
}
/** @brief Author a prerequisite geometry.
 * @param world Mutable open room with family provenance. */
static void npc_item_geometry(npc_world *world) {
    uint32_t layout = world->family.layout;
    world->x = 1U + layout % 2U;
    world->y = 1U + (layout / 2U) % 3U;
    world->item_x = 5U + (layout / 6U) % 3U;
    world->item_y = world->y + 2U + (layout / 12U) % 2U;
    world->exit_x = world->item_x;
    world->exit_y = 1U + (layout / 3U) % 3U;
}
/** @brief Author visible hazards and an unrevealed obstruction.
 * @param world Mutable open room with family provenance. */
static void npc_hazard_geometry(npc_world *world) {
    uint32_t layout = world->family.layout;
    world->x = 1U;
    uint32_t group = layout / 3U;
    int32_t safe_side = (world->variant / 4U) % 2U == 0U ? 1 : -1;
    world->y = 3U + group % 2U;
    world->item_x = 5U + group % 3U;
    world->item_y = (uint32_t)((int32_t)world->y + 2 * safe_side);
    world->exit_x = world->item_x;
    world->exit_y = world->y + group / 3U - 1U;
    world->cells[npc_cell(2U, world->y)] = NPC_OBSTRUCTION;
    world->cells[npc_cell(3U, world->y)] = NPC_DANGER;
}
/** @brief Author private cue endpoints without exposing them to the adapter.
 * @param world Mutable room with provenance. */
static void npc_cue_geometry(npc_world *world) {
    static const uint32_t branches[8][2] = {{1, 1}, {1, 2}, {1, 3}, {1, 4},
                                            {1, 5}, {2, 2}, {2, 3}, {2, 4}};
    uint32_t layout = world->family.layout, pattern = world->variant / 8U;
    uint32_t correct_left = (world->variant / 4U) % 2U;
    uint32_t left = branches[layout % 8U][0], right = branches[layout % 8U][1];
    world->junction_x = left + 1U;
    world->junction_y = 4U;
    world->x = world->junction_x;
    world->y = world->junction_y - (1U + pattern % 3U);
    world->cue_direction = correct_left != 0U ? 4U : 2U;
    world->item_x = correct_left != 0U ? 1U : world->junction_x + right;
    world->item_y = world->junction_y;
    world->exit_x = correct_left != 0U ? world->item_x - 1U : world->item_x + 1U;
    uint32_t exit_length = 2U + layout / 8U;
    world->exit_y = pattern / 3U == 0U ? world->item_y + exit_length : world->item_y - exit_length;
}
/** @brief Open paired neutral zigzag stems without revealing the correct branch.
 * @param world Mutable unrotated cue world.
 * @param right_end Unrotated right branch endpoint. */
static void npc_cue_exit_tiles(npc_world *world, uint32_t right_end) {
    uint32_t length = 2U + world->family.layout / 8U;
    int32_t side = world->variant / 8U / 3U == 0U ? 1 : -1;
    uint32_t first_y = (uint32_t)((int32_t)world->junction_y + side);
    world->cells[npc_cell(1U, first_y)] = NPC_FLOOR;
    world->cells[npc_cell(right_end, first_y)] = NPC_FLOOR;
    for (uint32_t offset = 1U; offset <= length; ++offset) {
        uint32_t y = (uint32_t)((int32_t)world->junction_y + (int32_t)offset * side);
        world->cells[npc_cell(0U, y)] = NPC_FLOOR;
        world->cells[npc_cell(right_end + 1U, y)] = NPC_FLOOR;
    }
}

/** @brief Construct the common approach and both neutral branch entrances.
 * @param world Mutable world with cue coordinates. */
static void npc_cue_tiles(npc_world *world) {
    static const uint32_t right_lengths[8] = {1, 2, 3, 4, 5, 2, 3, 4};
    uint32_t right_end = world->junction_x + right_lengths[world->family.layout % 8U];
    for (uint32_t i = 0; i < NPC_GRID_SIDE * NPC_GRID_SIDE; ++i)
        world->cells[i] = NPC_WALL;
    for (uint32_t y = world->y; y <= world->junction_y; ++y)
        world->cells[npc_cell(world->junction_x, y)] = NPC_FLOOR;
    for (uint32_t x = 1U; x <= right_end; ++x)
        world->cells[npc_cell(x, world->junction_y)] = NPC_FLOOR;
    npc_cue_exit_tiles(world, right_end);
}
/** @brief Make seed siblings distinct using public wall and hazard placement.
 * @param world Mutable non-cue room with objectives already placed. */
static void npc_seed_obstacles(npc_world *world) {
    uint32_t obstacle_x = 1U + world->variant / 8U;
    uint32_t hazard_x = 1U + (world->variant / 4U) % 2U;
    if (world->cells[npc_cell(obstacle_x, 7U)] == NPC_FLOOR)
        world->cells[npc_cell(obstacle_x, 7U)] = NPC_WALL;
    if (world->family.mechanic != NPC_HAZARD && world->cells[npc_cell(hazard_x, 6U)] == NPC_FLOOR)
        world->cells[npc_cell(hazard_x, 6U)] = NPC_DANGER;
}
/** @brief Add two fixed dangers whose accessible cells enlarge terrain coverage.
 * @param world Mutable non-cue room with objectives already placed. */
static void npc_room_dangers(npc_world *world) {
    for (uint32_t x = 3U; x <= 4U; ++x)
        if (world->cells[npc_cell(x, 6U)] == NPC_FLOOR)
            world->cells[npc_cell(x, 6U)] = NPC_DANGER;
}
/** @brief Force two public side-steps in an item room without hiding terrain.
 * @param world Mutable item room before rotation. */
static void npc_item_detour(npc_world *world) {
    uint32_t next_x = world->x + 1U;
    uint32_t danger_y = world->y == 1U ? world->y + 1U : world->y - 1U;
    world->cells[npc_cell(next_x, world->y)] = NPC_WALL;
    world->cells[npc_cell(next_x, world->y + 1U)] = NPC_WALL;
    if (world->y != 1U)
        world->cells[npc_cell(world->x, danger_y)] = NPC_DANGER;
}
/** @brief Require a second observed turn after an obstruction-driven detour.
 * @param world Mutable hazard room before rotation. */
static void npc_hazard_detour(npc_world *world) {
    uint32_t danger_y = (world->variant / 4U) % 2U == 0U ? world->y - 1U : world->y + 1U;
    uint32_t safe_y = (world->variant / 4U) % 2U == 0U ? world->y + 1U : world->y - 1U;
    uint32_t barrier_x = 2U + (world->family.layout / 3U + world->family.layout % 3U) % 3U;
    world->cells[npc_cell(world->x, danger_y)] = NPC_DANGER;
    world->cells[npc_cell(barrier_x, safe_y)] = NPC_DANGER;
}
/** @brief Build actual v3 room terrain rather than adding decorative identities.
 * @param world Mutable non-cue room before rotation. */
static void npc_room_detours(npc_world *world) {
    if (world->family.mechanic == NPC_HAZARD)
        npc_hazard_detour(world);
    else {
        npc_room_dangers(world);
        npc_item_detour(world);
    }
}
/** @brief Construct unrotated geometry and visible objective tiles.
 * @param world Mutable zeroed world with provenance. */
static void npc_geometry(npc_world *world) {
    npc_open_room(world);
    if (world->family.mechanic == NPC_CUE) {
        npc_cue_geometry(world);
        npc_cue_tiles(world);
    } else if (world->family.mechanic == NPC_HAZARD)
        npc_hazard_geometry(world);
    else
        npc_item_geometry(world);
    world->cells[npc_cell(world->item_x, world->item_y)] = NPC_ITEM_TILE;
    world->cells[npc_cell(world->exit_x, world->exit_y)] = NPC_EXIT_TILE;
    if (world->family.mechanic != NPC_CUE) {
        npc_seed_obstacles(world);
        npc_room_detours(world);
    }
}
int npc_world_init(npc_world *world, const npc_family *family, uint32_t variant) {
    npc_world value;
    if (world == NULL || !npc_family_valid(family) || variant >= NPC_VARIANTS_PER_FAMILY)
        return 0;
    memset(&value, 0, sizeof(value));
    value.family = *family;
    value.variant = variant;
    value.width = NPC_GRID_SIDE;
    value.height = NPC_GRID_SIDE;
    npc_geometry(&value);
    npc_rotate(&value, variant % 4U);
    *world = value;
    return 1;
}
/** @brief Read one neighbor with obstruction and endpoint occlusion.
 * @param world Borrowed host world.
 * @param direction Cardinal index, zero throughthree.
 * @return Observable tile ID. */
static uint32_t npc_neighbor(const npc_world *world, uint32_t direction) {
    int32_t x = (int32_t)world->x + npc_dx[direction];
    int32_t y = (int32_t)world->y + npc_dy[direction];
    uint32_t tile = NPC_WALL;
    if (x >= 0 && y >= 0 && x < (int32_t)world->width && y < (int32_t)world->height)
        tile = world->cells[npc_cell((uint32_t)x, (uint32_t)y)];
    if (tile == NPC_OBSTRUCTION)
        tile = world->obstruction_revealed != 0U ? NPC_WALL : NPC_FLOOR;
    if (world->family.mechanic == NPC_CUE && world->branch_chosen == 0U &&
        (tile == NPC_ITEM_TILE || tile == NPC_EXIT_TILE))
        tile = NPC_FLOOR;
    return tile;
}
/** @brief Select an announced objective, never an unseen branch endpoint.
 * @param world Borrowed host world.
 * @param value Mutable public observation. */
static void npc_observe_target(const npc_world *world, npc_observation *value) {
    uint32_t tx, ty;
    if (world->family.mechanic == NPC_CUE && world->branch_chosen == 0U) {
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
static void npc_observe_permissions(const npc_world *world, npc_observation *value) {
    value->allowed_actions = UINT64_C(3);
    for (uint32_t direction = 0; direction < 4U; ++direction) {
        value->neighbors[direction] = npc_neighbor(world, direction);
        if (value->neighbors[direction] != NPC_WALL)
            value->allowed_actions |= UINT64_C(1) << (direction + 2U);
    }
    if (value->at_target != 0U && value->stage >= 2U)
        value->allowed_actions |= UINT64_C(1) << NPC_INTERACT;
    if ((world->family.mechanic != NPC_CUE && world->ticks == 0U) || world->terminal != NPC_RUNNING)
        value->allowed_actions = UINT64_C(3);
}
void npc_world_observe(const npc_world *world, npc_observation *observation) {
    npc_observation value;
    if (world == NULL || observation == NULL)
        return;
    memset(&value, 0, sizeof(value));
    value.inventory = world->inventory;
    value.last_action = world->last_action;
    value.last_outcome = world->last_outcome;
    value.ticks = world->ticks;
    value.ticks_remaining = NPC_MAX_TICKS - world->ticks;
    value.mechanic = (uint32_t)world->family.mechanic;
    npc_observe_target(world, &value);
    npc_observe_permissions(world, &value);
    *observation = value;
}
/** @brief Commit a route and apply its irreversible wrong-branch consequence.
 * @param world Mutable host world.
 * @param nx Admitted destination column.
 * @param ny Admitted destination row. */
static void npc_commit_branch(npc_world *world, uint32_t nx, uint32_t ny) {
    uint32_t rotation = world->variant % 4U;
    if (world->family.mechanic != NPC_CUE || world->branch_chosen != 0U)
        return;
    int32_t projection = ((int32_t)nx - (int32_t)world->junction_x) * npc_dx[(1U + rotation) % 4U] +
                         ((int32_t)ny - (int32_t)world->junction_y) * npc_dy[(1U + rotation) % 4U];
    if (projection != 0) {
        uint32_t direction = projection > 0 ? (1U + rotation) % 4U : (3U + rotation) % 4U;
        world->branch_chosen = 1U;
        if (direction + 1U != world->cue_direction)
            world->terminal = NPC_DEATH;
    }
}
/** @brief Apply admitted movement with authoritative collision and hazard effects.
 * @param world Mutable host world.
 * @param action Admitted cardinal action.
 * @return Visible host outcome. */
static npc_outcome npc_move(npc_world *world, uint32_t action) {
    uint32_t direction = action - NPC_NORTH;
    uint32_t nx = (uint32_t)((int32_t)world->x + npc_dx[direction]);
    uint32_t ny = (uint32_t)((int32_t)world->y + npc_dy[direction]);
    uint32_t tile = world->cells[npc_cell(nx, ny)];
    if (tile == NPC_OBSTRUCTION) {
        world->obstruction_revealed = 1U;
        return NPC_BLOCKED;
    }
    npc_commit_branch(world, nx, ny);
    world->x = nx;
    world->y = ny;
    if (tile == NPC_DANGER)
        world->terminal = NPC_DEATH;
    return world->terminal == NPC_DEATH ? NPC_DIED : NPC_MOVED;
}
/** @brief Acquire a prerequisite or validate it before opening the exit.
 * @param world Mutable host world.
 * @return Visible interaction outcome. */
static npc_outcome npc_interact(npc_world *world) {
    if (world->inventory == 0U && world->x == world->item_x && world->y == world->item_y) {
        world->inventory = 1U;
        world->cells[npc_cell(world->item_x, world->item_y)] = NPC_FLOOR;
        return NPC_ACQUIRED;
    }
    if (world->inventory != 0U && world->x == world->exit_x && world->y == world->exit_y) {
        world->terminal = NPC_SUCCESS;
        return NPC_COMPLETED;
    }
    ++world->attempted_illegal;
    return NPC_INVALID;
}
/** @brief Dispatch an admitted action, keeping fallback as costly idle.
 * @param world Mutable host world.
 * @param action Admitted stable action.
 * @return Visible host outcome. */
static npc_outcome npc_execute(npc_world *world, uint32_t action) {
    if (action >= NPC_NORTH && action <= NPC_WEST)
        return npc_move(world, action);
    if (action == NPC_INTERACT)
        return npc_interact(world);
    return NPC_IDLE;
}
/** @brief Finalize the decision budget and visible outcome.
 * @param world Mutable host world.
 * @param outcome Executed or rejected proposal outcome.
 * @return The same visible outcome. */
static npc_outcome npc_finish_step(npc_world *world, npc_outcome outcome) {
    if (world->terminal == NPC_RUNNING && world->ticks >= NPC_MAX_TICKS)
        world->terminal = NPC_TIMEOUT;
    world->last_outcome = (uint32_t)outcome;
    return outcome;
}

npc_outcome npc_world_step(npc_world *world, uint32_t action) {
    npc_observation observation;
    npc_outcome outcome;
    if (world == NULL)
        return NPC_INVALID;
    if (world->terminal != NPC_RUNNING)
        return (npc_outcome)world->last_outcome;
    npc_world_observe(world, &observation);
    world->last_action = action < 7U ? action : NPC_FALLBACK;
    ++world->ticks;
    if (action >= 7U || (observation.allowed_actions & (UINT64_C(1) << action)) == 0U) {
        ++world->attempted_illegal;
        outcome = NPC_INVALID;
    } else
        outcome = npc_execute(world, action);
    return npc_finish_step(world, outcome);
}
void npc_memory_reset(npc_memory *memory) {
    if (memory != NULL)
        memset(memory, 0, sizeof(*memory));
}
void npc_memory_observe(npc_memory *memory, const npc_observation *observation) {
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
static void npc_encode_memory(const npc_memory *memory, cgai_gameplay_state *state) {
    state->values[8] = memory->cue;
    state->values[9] = memory->last_action;
    state->values[10] = memory->last_outcome;
    state->values[11] = memory->cue_age;
}
void npc_encode(const npc_observation *observation, const npc_memory *memory, int history,
                cgai_gameplay_state *state) {
    if (observation == NULL || memory == NULL || state == NULL)
        return;
    memset(state, 0, sizeof(*state));
    memcpy(state->values, observation->neighbors, sizeof(observation->neighbors));
    state->values[4] = (uint32_t)(observation->target_dx + 8);
    state->values[5] = (uint32_t)(observation->target_dy + 8);
    state->values[6] = observation->inventory;
    state->values[7] = observation->cue;
    if (history != 0)
        npc_encode_memory(memory, state);
    state->values[12] = observation->stage;
    state->values[13] = observation->at_target;
    state->values[14] = observation->ticks < 7U ? observation->ticks : 7U;
    state->values[15] = observation->mechanic;
}
uint64_t npc_observation_actions(const npc_observation *observation) {
    return observation == NULL ? UINT64_C(1) : observation->allowed_actions;
}
