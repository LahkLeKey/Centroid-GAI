#include "life_world.h"

#include <stddef.h>
#include <string.h>

static unsigned int bit_count(uint8_t mask) {
    unsigned int count = 0;
    while (mask != 0U) {
        count += (unsigned int)(mask & 1U);
        mask = (uint8_t)(mask >> 1U);
    }
    return count;
}

static int wrap(int value, int size) {
    int result = value % size;
    return result < 0 ? result + size : result;
}

static size_t index_at(int x, int y) {
    return (size_t)wrap(y, LIFE_HEIGHT) * LIFE_WIDTH + (size_t)wrap(x, LIFE_WIDTH);
}

static uint8_t count_neighbor(const life_world *world, size_t index, unsigned int *count) {
    const uint8_t cell = world->cells[index];
    if (cell != 0U)
        ++*count;
    return cell;
}

static uint8_t neighbors(const life_world *world, int x, int y, unsigned int *count) {
    uint8_t claims = 0;
    int dx;
    int dy;
    x = wrap(x, LIFE_WIDTH);
    y = wrap(y, LIFE_HEIGHT);
    *count = 0;
    for (dy = -1; dy <= 1; ++dy) {
        for (dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            claims = (uint8_t)(claims | count_neighbor(world, index_at(x + dx, y + dy), count));
        }
    }
    return claims;
}

static int entities_valid(const life_world *world, uint8_t *active) {
    size_t i;
    for (i = 0; i < LIFE_MODULES; ++i) {
        const life_entity *entity = &world->entities[i];
        if (i >= world->group_count) {
            if (entity->uid != 0U || entity->ancestry != 0U || entity->active != 0U)
                return 0;
            continue;
        }
        if (entity->active > 1U || entity->uid == 0U || entity->uid >= world->next_uid ||
            entity->ancestry == 0U || (entity->ancestry & ~life_world_group_mask(world)) != 0U) {
            return 0;
        }
        if (entity->active != 0U) {
            *active = (uint8_t)(*active | (1U << i));
        }
    }
    return 1;
}

static int record_valid(const life_world *world, const life_conflict *record) {
    if (record->id == 0U) {
        return record->age == 0U && record->last_tick == 0U && record->module_mask == 0U &&
               record->active == 0U && record->contact_streak == 0U &&
               record->separation_streak == 0U && record->outcome == LIFE_UNRESOLVED;
    }
    return record->id < world->next_conflict_id &&
           (record->module_mask & ~life_world_group_mask(world)) == 0U &&
           bit_count(record->module_mask) >= 2U && record->active <= 1U &&
           record->outcome <= LIFE_MERGED && record->last_tick <= world->tick &&
           record->age <= world->tick && record->contact_streak <= LIFE_RESOLUTION_TICKS &&
           record->separation_streak <= LIFE_RESOLUTION_TICKS &&
           (record->active == 0U || record->outcome == LIFE_UNRESOLVED ||
            record->outcome == LIFE_COUPLED);
}

static int conflict_id_unique(const life_world *world, uint32_t id, size_t index) {
    size_t j;
    if (id == 0U)
        return 1;
    for (j = index + 1U; j < LIFE_MAX_CONFLICTS; ++j)
        if (world->conflicts[j].id == id)
            return 0;
    return 1;
}

static int conflicts_valid(const life_world *world) {
    size_t i;
    for (i = 0; i < LIFE_MAX_CONFLICTS; ++i) {
        const life_conflict *record = &world->conflicts[i];
        if (!record_valid(world, record)) {
            return 0;
        }
        if (!conflict_id_unique(world, record->id, i))
            return 0;
    }
    return 1;
}

static int world_valid(const life_world *world) {
    uint8_t active = 0;
    size_t i;
    if (world == NULL || world->group_count < LIFE_MIN_MODULES ||
        world->group_count > LIFE_MODULES || world->next_uid == 0U || world->next_conflict_id == 0U)
        return 0;
    if (!entities_valid(world, &active))
        return 0;
    for (i = 0; i < LIFE_CELLS; ++i)
        if ((world->cells[i] & (uint8_t)~active) != 0U)
            return 0;
    return conflicts_valid(world);
}

uint8_t life_world_group_mask(const life_world *world) {
    return world != NULL && world->group_count >= LIFE_MIN_MODULES &&
                   world->group_count <= LIFE_MODULES
               ? (uint8_t)((1U << world->group_count) - 1U)
               : 0U;
}

int life_world_clear_groups(life_world *world, uint32_t seed, uint32_t group_count) {
    unsigned int i;
    if (group_count == 0U)
        group_count = LIFE_DEFAULT_MODULES;
    if (world == NULL || group_count < LIFE_MIN_MODULES || group_count > LIFE_MODULES)
        return LIFE_INVALID_ARGUMENT;
    memset(world, 0, sizeof(*world));
    world->group_count = group_count;
    world->seed = seed;
    world->next_uid = group_count + 1U;
    world->next_conflict_id = 1;
    for (i = 0; i < group_count; ++i) {
        world->entities[i].uid = i + 1U;
        world->entities[i].ancestry = (uint8_t)(1U << i);
        world->entities[i].active = 1;
    }
    return LIFE_OK;
}

void life_world_clear(life_world *world, uint32_t seed) {
    (void)life_world_clear_groups(world, seed, LIFE_DEFAULT_MODULES);
}

int life_world_set(life_world *world, int x, int y, uint8_t claims) {
    unsigned int module;
    if (world == NULL || life_world_group_mask(world) == 0U ||
        (claims & ~life_world_group_mask(world)) != 0U) {
        return LIFE_INVALID_ARGUMENT;
    }
    for (module = 0; module < LIFE_MODULES; ++module) {
        if ((claims & (1U << module)) != 0U && world->entities[module].active == 0U) {
            return LIFE_INVALID_ARGUMENT;
        }
    }
    world->cells[index_at(x, y)] = claims;
    return LIFE_OK;
}

static void pattern(life_world *world, int x, int y, uint8_t claims, const int (*points)[2],
                    size_t count, unsigned int orientation) {
    const int flip_x = (orientation & 1U) != 0U ? -1 : 1;
    const int flip_y = (orientation & 2U) != 0U ? -1 : 1;
    size_t i;
    for (i = 0; i < count; ++i) {
        size_t index = index_at(x + points[i][0] * flip_x, y + points[i][1] * flip_y);
        world->cells[index] = (uint8_t)(world->cells[index] | claims);
    }
}

int life_world_init_groups(life_world *world, uint32_t seed, unsigned int scenario,
                           uint32_t group_count) {
    static const int glider[][2] = {{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
    static const int block[][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    static const int blinker[][2] = {{0, 0}, {1, 0}, {2, 0}};
    static const int (*const points[])[2] = {glider, block, blinker};
    static const size_t counts[] = {5, 4, 3};
    /* Each placement stores x, y, pattern kind, and x/y reflection bits. */
    static const unsigned int placements[4][4][4] = {
        {{8, 8, 0, 0}, {18, 18, 0, 3}, {4, 23, 1, 0}, {24, 4, 2, 0}},
        {{8, 8, 0, 0}, {14, 14, 1, 0}, {3, 24, 0, 2}, {26, 5, 2, 0}},
        {{8, 8, 0, 0}, {14, 14, 2, 0}, {3, 24, 1, 0}, {25, 5, 0, 1}},
        {{12, 12, 0, 0}, {17, 17, 0, 3}, {15, 12, 2, 0}, {12, 16, 1, 0}}};
    int offset_x = (int)(seed % 5U) - 2;
    int offset_y = (int)((seed / 5U) % 5U) - 2;
    if (world == NULL || scenario > 3U ||
        life_world_clear_groups(world, seed, group_count) != LIFE_OK) {
        return LIFE_INVALID_ARGUMENT;
    }
    for (unsigned int module = 0; module < world->group_count; ++module) {
        const unsigned int *placement = placements[scenario][module % LIFE_DEFAULT_MODULES];
        const unsigned int kind = placement[2];
        /* Higher slots reuse the same target-independent fixtures as the lower four. */
        pattern(world, (int)placement[0] + offset_x, (int)placement[1] + offset_y,
                (uint8_t)(1U << module), points[kind], counts[kind], placement[3]);
    }
    return LIFE_OK;
}

int life_world_init(life_world *world, uint32_t seed, unsigned int scenario) {
    return life_world_init_groups(world, seed, scenario, LIFE_DEFAULT_MODULES);
}

unsigned int life_neighbor_count(const life_world *world, int x, int y) {
    unsigned int count = 0;
    if (world != NULL) {
        (void)neighbors(world, x, y, &count);
    }
    return count;
}

static void add_contact(uint8_t graph[LIFE_MODULES], uint8_t mask) {
    unsigned int i;
    if (bit_count(mask) < 2U) {
        return;
    }
    for (i = 0; i < LIFE_MODULES; ++i) {
        if ((mask & (1U << i)) != 0U) {
            graph[i] = (uint8_t)(graph[i] | mask);
        }
    }
}

static void causal_cell(const life_world *world, uint8_t graph[LIFE_MODULES],
                        uint8_t contested[LIFE_CELLS], int x, int y) {
    size_t index = index_at(x, y);
    unsigned int count;
    uint8_t support = neighbors(world, x, y, &count);
    uint8_t cause = (uint8_t)(support | world->cells[index]);
    if ((world->cells[index] != 0U || count == 3U) && bit_count(cause) > 1U) {
        add_contact(graph, cause);
        if (contested != NULL)
            contested[index] = cause;
    }
}

/* Only neighborhoods that determine a live cell or an actual birth cause contact. */
static void causal_graph(const life_world *world, uint8_t graph[LIFE_MODULES],
                         uint8_t contested[LIFE_CELLS]) {
    int x;
    int y;
    memset(graph, 0, LIFE_MODULES);
    if (contested != NULL) {
        memset(contested, 0, LIFE_CELLS);
    }
    for (y = 0; y < LIFE_HEIGHT; ++y) {
        for (x = 0; x < LIFE_WIDTH; ++x) {
            causal_cell(world, graph, contested, x, y);
        }
    }
}

static uint8_t connected_mask(const uint8_t graph[LIFE_MODULES], uint8_t start) {
    uint8_t result = start;
    uint8_t previous;
    unsigned int i;
    do {
        previous = result;
        for (i = 0; i < LIFE_MODULES; ++i) {
            if ((result & (1U << i)) != 0U) {
                result = (uint8_t)(result | graph[i]);
            }
        }
    } while (result != previous);
    return result;
}

static const life_conflict *find_conflict(const life_world *world, uint8_t mask) {
    size_t i;
    const life_conflict *result = NULL;
    for (i = 0; i < LIFE_MAX_CONFLICTS; ++i) {
        const life_conflict *record = &world->conflicts[i];
        if (record->id != 0U && record->module_mask == mask && record->outcome != LIFE_MERGED &&
            (result == NULL || record->id > result->id)) {
            result = record;
        }
    }
    return result;
}

static void prepare_conway(const life_world *world, life_world *baseline) {
    int x;
    int y;
    for (y = 0; y < LIFE_HEIGHT; ++y) {
        for (x = 0; x < LIFE_WIDTH; ++x) {
            size_t index = index_at(x, y);
            unsigned int count;
            uint8_t support = neighbors(world, x, y, &count);
            uint8_t current = world->cells[index];
            baseline->cells[index] = current != 0U ? ((count == 2U || count == 3U) ? current : 0U)
                                                   : (count == 3U ? support : 0U);
        }
    }
}

static int prepare_patch(const life_world *world, life_patch *patch, uint8_t mask,
                         const uint8_t contested[LIFE_CELLS], uint32_t *next_id) {
    const life_conflict *record;
    size_t index;
    patch->module_mask = mask;
    record = find_conflict(world, mask);
    if (record == NULL && *next_id == UINT32_MAX)
        return LIFE_INVALID_ARGUMENT;
    patch->age = record != NULL && record->active != 0U ? record->age + 1U : 1U;
    patch->conflict_id = record != NULL ? record->id : (*next_id)++;
    for (index = 0; index < LIFE_CELLS && patch->cell_count < LIFE_PATCH_CELLS; ++index) {
        if ((contested[index] & mask) != 0U &&
            life_neighbor_count(world, (int)(index % LIFE_WIDTH), (int)(index / LIFE_WIDTH)) != 0U)
            patch->cells[patch->cell_count++] = (uint16_t)index;
    }
    return LIFE_OK;
}

static int prepare_patches(const life_world *world, life_frame *frame,
                           const uint8_t graph[LIFE_MODULES], const uint8_t contested[LIFE_CELLS]) {
    uint8_t visited = 0;
    uint32_t next_id = world->next_conflict_id;
    unsigned int module;
    for (module = 0; module < LIFE_MODULES; ++module) {
        uint8_t bit = (uint8_t)(1U << module);
        uint8_t mask;
        if ((visited & bit) != 0U || graph[module] == 0U) {
            continue;
        }
        mask = connected_mask(graph, bit);
        visited = (uint8_t)(visited | mask);
        if (prepare_patch(world, &frame->patches[frame->patch_count], mask, contested, &next_id) !=
            LIFE_OK) {
            return LIFE_INVALID_ARGUMENT;
        }
        ++frame->patch_count;
    }
    return LIFE_OK;
}

int life_prepare(const life_world *world, life_frame *frame) {
    uint8_t graph[LIFE_MODULES];
    uint8_t contested[LIFE_CELLS];
    if (!world_valid(world) || frame == NULL || world->tick == UINT32_MAX)
        return LIFE_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    frame->baseline = *world;
    frame->baseline.tick = world->tick + 1U;
    frame->source_hash = life_world_hash(world);
    prepare_conway(world, &frame->baseline);
    causal_graph(world, graph, contested);
    return prepare_patches(world, frame, graph, contested);
}

static uint8_t pair_output_bits(uint32_t output) {
    uint32_t id = 8;
    unsigned int i;
    unsigned int j;
    for (i = 0; i < LIFE_PATCH_CELLS; ++i) {
        for (j = i + 1U; j < LIFE_PATCH_CELLS; ++j) {
            if (id++ == output) {
                return (uint8_t)((1U << i) | (1U << j));
            }
        }
    }
    return 0;
}

uint8_t life_output_bits(uint32_t output) {
    if (output < 2U || output >= LIFE_OUTPUTS)
        return 0;
    if (output < 8U)
        return (uint8_t)(1U << (output - 2U));
    return pair_output_bits(output);
}

uint32_t life_allowed_outputs(const life_patch *patch) {
    uint32_t allowed = 3;
    uint32_t output;
    uint8_t cells_mask;
    if (patch == NULL || patch->cell_count > LIFE_PATCH_CELLS) {
        return 0;
    }
    cells_mask = (uint8_t)((1U << patch->cell_count) - 1U);
    for (output = 2; output < LIFE_OUTPUTS; ++output) {
        if ((life_output_bits(output) & (uint8_t)~cells_mask) == 0U) {
            allowed |= UINT32_C(1) << output;
        }
    }
    return allowed;
}

static life_conflict *record_slot(life_world *world, uint8_t mask) {
    const life_conflict *existing = find_conflict(world, mask);
    size_t candidate = 0;
    size_t i;
    if (existing != NULL) {
        return &world->conflicts[(size_t)(existing - world->conflicts)];
    }
    for (i = 0; i < LIFE_MAX_CONFLICTS; ++i) {
        if (world->conflicts[i].id == 0U) {
            return &world->conflicts[i];
        }
        if ((world->conflicts[candidate].active != 0U && world->conflicts[i].active == 0U) ||
            (world->conflicts[candidate].active == world->conflicts[i].active &&
             world->conflicts[i].last_tick < world->conflicts[candidate].last_tick)) {
            candidate = i;
        }
    }
    return &world->conflicts[candidate];
}

static void resolve(life_world *world, life_conflict *record, uint8_t outcome) {
    if (record->outcome == outcome) {
        return;
    }
    record->outcome = outcome;
    if (outcome == LIFE_COUPLED) {
        ++world->stats.coupled;
    } else {
        record->active = 0;
        if (outcome == LIFE_SEPARATED) {
            ++world->stats.separated;
        } else if (outcome == LIFE_ABSORBED) {
            ++world->stats.absorbed;
        } else if (outcome == LIFE_EXTINCT) {
            ++world->stats.extinct;
        }
    }
}

static void start_conflict(life_world *world, life_conflict *record, const life_patch *patch) {
    if (record->id != patch->conflict_id) {
        memset(record, 0, sizeof(*record));
        record->id = patch->conflict_id;
        record->module_mask = patch->module_mask;
        ++world->stats.collisions;
    } else if (record->active == 0U) {
        ++world->stats.reopened;
        record->contact_streak = 0;
        record->separation_streak = 0;
        record->outcome = LIFE_UNRESOLVED;
    }
}

static void register_patch(life_world *world, const life_patch *patch) {
    life_conflict *record = record_slot(world, patch->module_mask);
    start_conflict(world, record, patch);
    record->active = 1;
    record->age = patch->age;
    record->last_tick = world->tick;
    if (world->next_conflict_id <= patch->conflict_id)
        world->next_conflict_id = patch->conflict_id + 1U;
}

static int record_has_contact(const uint8_t graph[LIFE_MODULES], uint8_t mask) {
    unsigned int module;
    for (module = 0; module < LIFE_MODULES; ++module) {
        uint8_t bit = (uint8_t)(1U << module);
        if ((mask & bit) != 0U && bit_count((uint8_t)(graph[module] & mask)) > 1U)
            return 1;
    }
    return 0;
}

static void update_contact(life_world *world, life_conflict *record,
                           const uint8_t graph[LIFE_MODULES]) {
    uint8_t lowest = (uint8_t)(record->module_mask & (uint8_t)(0U - record->module_mask));
    uint8_t component = connected_mask(graph, lowest);
    record->separation_streak = 0;
    if ((component & record->module_mask) == record->module_mask) {
        if (record->contact_streak < LIFE_RESOLUTION_TICKS)
            ++record->contact_streak;
        if (record->contact_streak == LIFE_RESOLUTION_TICKS)
            resolve(world, record, LIFE_COUPLED);
    } else {
        record->contact_streak = 0;
    }
}

static void update_separation(life_world *world, life_conflict *record) {
    record->contact_streak = 0;
    if (record->outcome == LIFE_COUPLED)
        record->outcome = LIFE_UNRESOLVED;
    if (record->separation_streak < LIFE_RESOLUTION_TICKS)
        ++record->separation_streak;
    if (record->separation_streak == LIFE_RESOLUTION_TICKS)
        resolve(world, record, LIFE_SEPARATED);
}

static void update_record(life_world *world, life_conflict *record,
                          const uint8_t graph[LIFE_MODULES], uint8_t present) {
    uint8_t alive = (uint8_t)(present & record->module_mask);
    if (record->id == 0U || record->active == 0U)
        return;
    if (alive != record->module_mask) {
        resolve(world, record, alive == 0U ? LIFE_EXTINCT : LIFE_ABSORBED);
        return;
    }
    if (record_has_contact(graph, record->module_mask))
        update_contact(world, record, graph);
    else
        update_separation(world, record);
    if (record->last_tick != world->tick) {
        ++record->age;
        record->last_tick = world->tick;
    }
}

static void update_lifecycle(life_world *world, const life_frame *frame) {
    uint8_t graph[LIFE_MODULES];
    uint8_t present = 0;
    size_t i;
    unsigned int p;
    for (p = 0; p < frame->patch_count; ++p)
        register_patch(world, &frame->patches[p]);
    for (i = 0; i < LIFE_CELLS; ++i)
        present = (uint8_t)(present | world->cells[i]);
    causal_graph(world, graph, NULL);
    for (i = 0; i < LIFE_MAX_CONFLICTS; ++i)
        update_record(world, &world->conflicts[i], graph, present);
}

static int patches_equal(const life_patch *a, const life_patch *b) {
    unsigned int c;
    if (a->module_mask != b->module_mask || a->cell_count != b->cell_count || a->age != b->age ||
        a->conflict_id != b->conflict_id)
        return 0;
    for (c = 0; c < a->cell_count; ++c)
        if (a->cells[c] != b->cells[c])
            return 0;
    return 1;
}

static int frames_equal(const life_frame *a, const life_frame *b) {
    unsigned int p;
    if (a->source_hash != b->source_hash || a->patch_count != b->patch_count ||
        life_world_hash(&a->baseline) != life_world_hash(&b->baseline))
        return 0;
    for (p = 0; p < a->patch_count; ++p)
        if (!patches_equal(&a->patches[p], &b->patches[p]))
            return 0;
    return 1;
}

static int toggle_cell(const life_world *world, life_world *next, const life_patch *patch,
                       size_t index) {
    if (next->cells[index] != 0U) {
        next->cells[index] = 0;
    } else {
        unsigned int count;
        uint8_t support =
            neighbors(world, (int)(index % LIFE_WIDTH), (int)(index / LIFE_WIDTH), &count);
        uint8_t claims = (uint8_t)((support | world->cells[index]) & patch->module_mask);
        if (count == 0U || claims == 0U)
            return LIFE_INVALID_ACTION;
        next->cells[index] = claims;
    }
    ++next->stats.edits;
    return LIFE_OK;
}

static int apply_patch(const life_world *world, life_world *next, const life_patch *patch,
                       uint32_t output) {
    uint8_t toggles;
    unsigned int c;
    if (output >= LIFE_OUTPUTS || (life_allowed_outputs(patch) & (UINT32_C(1) << output)) == 0U)
        return LIFE_INVALID_ACTION;
    toggles = life_output_bits(output);
    for (c = 0; c < patch->cell_count; ++c) {
        if ((toggles & (1U << c)) == 0U)
            continue;
        if (toggle_cell(world, next, patch, patch->cells[c]) != LIFE_OK)
            return LIFE_INVALID_ACTION;
    }
    return LIFE_OK;
}

static int apply_actions(const life_world *world, const life_frame *frame, life_world *next,
                         const uint32_t outputs[LIFE_MAX_PATCHES]) {
    unsigned int p;
    for (p = 0; p < frame->patch_count; ++p)
        if (apply_patch(world, next, &frame->patches[p], outputs == NULL ? 0U : outputs[p]) !=
            LIFE_OK)
            return LIFE_INVALID_ACTION;
    return LIFE_OK;
}

int life_apply(life_world *world, const life_frame *frame,
               const uint32_t outputs[LIFE_MAX_PATCHES]) {
    life_frame expected;
    life_world next;
    if (!world_valid(world) || frame == NULL)
        return LIFE_INVALID_ARGUMENT;
    if (frame->source_hash != life_world_hash(world))
        return LIFE_STALE_FRAME;
    if (life_prepare(world, &expected) != LIFE_OK || !frames_equal(frame, &expected))
        return LIFE_INVALID_ARGUMENT;
    next = frame->baseline;
    if (apply_actions(world, frame, &next, outputs) != LIFE_OK)
        return LIFE_INVALID_ACTION;
    update_lifecycle(&next, frame);
    *world = next;
    return LIFE_OK;
}

static uint64_t hash_byte(uint64_t hash, uint8_t value) {
    return (hash ^ value) * UINT64_C(1099511628211);
}

static uint64_t hash_u32(uint64_t hash, uint32_t value) {
    unsigned int i;
    for (i = 0; i < 4U; ++i) {
        hash = hash_byte(hash, (uint8_t)(value >> (i * 8U)));
    }
    return hash;
}

static uint64_t hash_entities(uint64_t hash, const life_world *world) {
    size_t i;
    for (i = 0; i < world->group_count; ++i) {
        hash = hash_u32(hash, world->entities[i].uid);
        hash = hash_byte(hash, world->entities[i].ancestry);
        hash = hash_byte(hash, world->entities[i].active);
    }
    return hash;
}

static uint64_t hash_conflict(uint64_t hash, const life_conflict *record) {
    hash = hash_u32(hash, record->id);
    hash = hash_u32(hash, record->age);
    hash = hash_u32(hash, record->last_tick);
    hash = hash_byte(hash, record->module_mask);
    hash = hash_byte(hash, record->active);
    hash = hash_byte(hash, record->contact_streak);
    hash = hash_byte(hash, record->separation_streak);
    return hash_byte(hash, record->outcome);
}

static uint64_t hash_conflicts(uint64_t hash, const life_world *world) {
    size_t i;
    for (i = 0; i < LIFE_MAX_CONFLICTS; ++i) {
        hash = hash_conflict(hash, &world->conflicts[i]);
    }
    return hash;
}

static uint64_t hash_stats(uint64_t hash, const life_stats *stats) {
    hash = hash_u32(hash, stats->collisions);
    hash = hash_u32(hash, stats->reopened);
    hash = hash_u32(hash, stats->separated);
    hash = hash_u32(hash, stats->coupled);
    hash = hash_u32(hash, stats->absorbed);
    hash = hash_u32(hash, stats->extinct);
    hash = hash_u32(hash, stats->merged);
    return hash_u32(hash, stats->edits);
}

uint64_t life_world_hash(const life_world *world) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    if (world == NULL || life_world_group_mask(world) == 0U)
        return 0;
    for (i = 0; i < LIFE_CELLS; ++i)
        hash = hash_byte(hash, world->cells[i]);
    hash = hash_entities(hash, world);
    hash = hash_conflicts(hash, world);
    hash = hash_stats(hash, &world->stats);
    hash = hash_u32(hash, world->tick);
    hash = hash_u32(hash, world->seed);
    hash = hash_u32(hash, world->next_uid);
    return hash_u32(hash, world->next_conflict_id);
}

uint32_t life_population(const life_world *world, uint8_t module_mask) {
    size_t i;
    uint32_t count = 0;
    if (world == NULL) {
        return 0;
    }
    for (i = 0; i < LIFE_CELLS; ++i) {
        if ((world->cells[i] & module_mask) != 0U) {
            ++count;
        }
    }
    return count;
}

uint8_t life_merge_ready_mask(const life_world *world) {
    size_t i;
    uint8_t mask = 0;
    if (world == NULL) {
        return 0;
    }
    for (i = 0; i < LIFE_MAX_CONFLICTS; ++i) {
        const life_conflict *record = &world->conflicts[i];
        if (record->active != 0U && record->outcome == LIFE_COUPLED &&
            record->contact_streak >= LIFE_RESOLUTION_TICKS &&
            (mask == 0U || record->module_mask < mask)) {
            mask = record->module_mask;
        }
    }
    return mask;
}

static int merge_ancestry(const life_world *world, uint8_t module_mask, uint8_t *ancestry) {
    unsigned int module;
    for (module = 0; module < LIFE_MODULES; ++module) {
        if ((module_mask & (1U << module)) != 0U) {
            if (world->entities[module].active == 0U) {
                return LIFE_INVALID_ARGUMENT;
            }
            *ancestry = (uint8_t)(*ancestry | world->entities[module].ancestry);
        }
    }
    return LIFE_OK;
}

static void merge_cells(life_world *world, uint8_t module_mask, uint8_t target_bit) {
    size_t i;
    for (i = 0; i < LIFE_CELLS; ++i) {
        if ((world->cells[i] & module_mask) != 0U) {
            world->cells[i] = (uint8_t)((world->cells[i] & (uint8_t)~module_mask) | target_bit);
        }
    }
}

static void merge_entities(life_world *world, uint8_t module_mask, unsigned int target,
                           uint8_t ancestry) {
    unsigned int module;
    for (module = 0; module < LIFE_MODULES; ++module) {
        if ((module_mask & (1U << module)) != 0U) {
            world->entities[module].active = (uint8_t)(module == target);
        }
    }
    world->entities[target].uid = world->next_uid++;
    world->entities[target].ancestry = ancestry;
}

static void retire_conflicts(life_world *world, uint8_t module_mask) {
    size_t i;
    for (i = 0; i < LIFE_MAX_CONFLICTS; ++i) {
        life_conflict *record = &world->conflicts[i];
        if ((record->module_mask & module_mask) != 0U) {
            record->active = 0;
            record->outcome = LIFE_MERGED;
            record->contact_streak = 0;
        }
    }
}

int life_world_merge(life_world *world, uint8_t module_mask) {
    unsigned int target = 0;
    uint8_t ancestry = 0;
    if (!world_valid(world) || (module_mask & ~life_world_group_mask(world)) != 0U ||
        bit_count(module_mask) < 2U || world->next_uid == UINT32_MAX)
        return LIFE_INVALID_ARGUMENT;
    while ((module_mask & (1U << target)) == 0U)
        ++target;
    if (merge_ancestry(world, module_mask, &ancestry) != LIFE_OK)
        return LIFE_INVALID_ARGUMENT;
    merge_cells(world, module_mask, (uint8_t)(1U << target));
    merge_entities(world, module_mask, target, ancestry);
    retire_conflicts(world, module_mask);
    ++world->stats.merged;
    return LIFE_OK;
}
