#include "life_world.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned int failures;

static void check(int condition, const char *expression, const char *file, int line) {
    if (!condition) {
        fprintf(stderr, "%s:%d: %s\n", file, line, expression);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

static void step(life_world *world) {
    life_frame frame;
    CHECK(life_prepare(world, &frame) == LIFE_OK);
    CHECK(life_apply(world, &frame, NULL) == LIFE_OK);
}

static void set_block(life_world *world, int x, int y, uint8_t claims) {
    CHECK(life_world_set(world, x, y, claims) == LIFE_OK);
    CHECK(life_world_set(world, x + 1, y, claims) == LIFE_OK);
    CHECK(life_world_set(world, x, y + 1, claims) == LIFE_OK);
    CHECK(life_world_set(world, x + 1, y + 1, claims) == LIFE_OK);
}

static void split_block(life_world *world) {
    memset(world->cells, 0, sizeof(world->cells));
    CHECK(life_world_set(world, 10, 10, 1) == LIFE_OK);
    CHECK(life_world_set(world, 11, 10, 1) == LIFE_OK);
    CHECK(life_world_set(world, 10, 11, 2) == LIFE_OK);
    CHECK(life_world_set(world, 11, 11, 2) == LIFE_OK);
}

static unsigned int seed_truth_pattern(life_world *world, unsigned int pattern) {
    unsigned int neighbors = 0;
    unsigned int bit = 0;
    int dx;
    int dy;
    for (dy = -1; dy <= 1; ++dy) {
        for (dx = -1; dx <= 1; ++dx) {
            if ((pattern & (1U << bit)) != 0U) {
                CHECK(life_world_set(world, 16 + dx, 16 + dy, 1) == LIFE_OK);
                if (dx != 0 || dy != 0) {
                    ++neighbors;
                }
            }
            ++bit;
        }
    }
    return neighbors;
}

static void check_truth_pattern(unsigned int pattern) {
    life_world world;
    life_frame frame;
    life_world_clear(&world, 17);
    const unsigned int neighbors = seed_truth_pattern(&world, pattern);
    const int expected = neighbors == 3U || ((pattern & (1U << 4U)) != 0U && neighbors == 2U);
    CHECK(life_neighbor_count(&world, 16, 16) == neighbors);
    CHECK(life_prepare(&world, &frame) == LIFE_OK);
    CHECK((frame.baseline.cells[16 * LIFE_WIDTH + 16] != 0U) == expected);
    CHECK(frame.patch_count == 0U);
}

static void test_rule_truth_table(void) {
    for (unsigned int pattern = 0; pattern < 512U; ++pattern)
        check_truth_pattern(pattern);
}

static void test_block(void) {
    life_world world;
    uint8_t initial[LIFE_CELLS];
    unsigned int i;
    life_world_clear(&world, 0);
    set_block(&world, 10, 10, 1);
    memcpy(initial, world.cells, sizeof(initial));
    for (i = 0; i < 16U; ++i) {
        step(&world);
        CHECK(memcmp(initial, world.cells, sizeof(initial)) == 0);
    }
}

static void test_blinker(void) {
    life_world world;
    uint8_t initial[LIFE_CELLS];
    life_world_clear(&world, 0);
    CHECK(life_world_set(&world, 10, 10, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 11, 10, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 12, 10, 1) == LIFE_OK);
    memcpy(initial, world.cells, sizeof(initial));
    step(&world);
    CHECK(world.cells[9 * LIFE_WIDTH + 11] == 1U);
    CHECK(world.cells[10 * LIFE_WIDTH + 11] == 1U);
    CHECK(world.cells[11 * LIFE_WIDTH + 11] == 1U);
    CHECK(life_population(&world, 15) == 3U);
    CHECK(life_neighbor_count(&world, INT_MAX, INT_MIN) == life_neighbor_count(&world, 31, 0));
    step(&world);
    CHECK(memcmp(initial, world.cells, sizeof(initial)) == 0);
}

static void test_glider(void) {
    life_world world;
    unsigned int i;
    life_world_clear(&world, 0);
    CHECK(life_world_set(&world, 1, 0, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 2, 1, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 0, 2, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 1, 2, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 2, 2, 1) == LIFE_OK);
    for (i = 0; i < 4U; ++i) {
        step(&world);
    }
    CHECK(life_population(&world, 15) == 5U);
    CHECK(world.cells[1 * LIFE_WIDTH + 2] == 1U);
    CHECK(world.cells[2 * LIFE_WIDTH + 3] == 1U);
    CHECK(world.cells[3 * LIFE_WIDTH + 1] == 1U);
    CHECK(world.cells[3 * LIFE_WIDTH + 2] == 1U);
    CHECK(world.cells[3 * LIFE_WIDTH + 3] == 1U);
}

static void test_toroidal_blinker(void) {
    life_world world;
    life_world_clear(&world, 0);
    CHECK(life_world_set(&world, -1, 0, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 0, 0, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 1, 0, 1) == LIFE_OK);
    step(&world);
    CHECK(world.cells[31 * LIFE_WIDTH] == 1U);
    CHECK(world.cells[0] == 1U);
    CHECK(world.cells[LIFE_WIDTH] == 1U);
    CHECK(life_population(&world, 15) == 3U);
}

static void test_standard_patterns(void) {
    test_block();
    test_blinker();
    test_glider();
    test_toroidal_blinker();
}

/* Independent occupancy oracle intentionally knows nothing about claims/conflicts. */
static unsigned int oracle_neighbors(const uint8_t old[LIFE_CELLS], int x, int y) {
    unsigned int count = 0;
    int dx;
    int dy;
    for (dy = -1; dy <= 1; ++dy) {
        for (dx = -1; dx <= 1; ++dx) {
            int nx = (x + dx + LIFE_WIDTH) % LIFE_WIDTH;
            int ny = (y + dy + LIFE_HEIGHT) % LIFE_HEIGHT;
            if ((dx != 0 || dy != 0) && old[(size_t)ny * LIFE_WIDTH + (size_t)nx] != 0U) {
                ++count;
            }
        }
    }
    return count;
}

static void oracle_step(const uint8_t old[LIFE_CELLS], uint8_t next[LIFE_CELLS]) {
    int x;
    int y;
    for (y = 0; y < LIFE_HEIGHT; ++y) {
        for (x = 0; x < LIFE_WIDTH; ++x) {
            const unsigned int count = oracle_neighbors(old, x, y);
            size_t index = (size_t)y * LIFE_WIDTH + (size_t)x;
            next[index] = (uint8_t)(count == 3U || (old[index] != 0U && count == 2U));
        }
    }
}

static void initialize_scenario(life_world *world, life_world *repeat, unsigned int scenario) {
    life_world translated;
    CHECK(life_world_init(world, 123, scenario) == LIFE_OK);
    CHECK(life_world_init(repeat, 123, scenario) == LIFE_OK);
    CHECK(life_world_init(&translated, 124, scenario) == LIFE_OK);
    CHECK(life_world_hash(world) == life_world_hash(repeat));
    CHECK(memcmp(world->cells, translated.cells, sizeof(world->cells)) != 0);
}

static void compare_oracle_tick(life_world *world, life_world *repeat) {
    uint8_t expected[LIFE_CELLS];
    size_t i;
    oracle_step(world->cells, expected);
    step(world);
    step(repeat);
    CHECK(life_world_hash(world) == life_world_hash(repeat));
    for (i = 0; i < LIFE_CELLS; ++i) {
        CHECK((world->cells[i] != 0U) == (expected[i] != 0U));
    }
}

static void test_scenarios_and_oracle(void) {
    unsigned int scenario;
    for (scenario = 0; scenario < 4U; ++scenario) {
        life_world world;
        life_world repeat;
        unsigned int tick;
        initialize_scenario(&world, &repeat, scenario);
        for (tick = 0; tick < 120U; ++tick) {
            compare_oracle_tick(&world, &repeat);
        }
        CHECK(world.stats.collisions > 0U);
        CHECK(world.stats.edits == 0U);
        CHECK(world.stats.merged == 0U);
    }
}

static unsigned int popbits(uint8_t bits) {
    unsigned int count = 0;
    while (bits != 0U) {
        count += bits & 1U;
        bits = (uint8_t)(bits >> 1U);
    }
    return count;
}

static void prepare_action_fixture(life_world *world, life_frame *frame) {
    life_world_clear(world, 0);
    split_block(world);
    CHECK(life_prepare(world, frame) == LIFE_OK);
    CHECK(frame->patch_count == 1U);
    CHECK(frame->patches[0].module_mask == 3U);
    CHECK(frame->patches[0].cell_count == 4U);
}

static void test_action_mapping(void) {
    CHECK(life_output_bits(2) == 1U);
    CHECK(life_output_bits(7) == 32U);
    CHECK(life_output_bits(8) == 3U);
    CHECK(life_output_bits(22) == 48U);
    CHECK(life_output_bits(23) == 0U);
}

static unsigned int occupancy_difference(const life_world *candidate, const life_frame *frame) {
    unsigned int difference = 0;
    for (size_t i = 0; i < LIFE_CELLS; ++i) {
        if ((candidate->cells[i] != 0U) != (frame->baseline.cells[i] != 0U)) {
            ++difference;
        }
    }
    return difference;
}

static unsigned int check_action(const life_world *world, const life_frame *frame,
                                 uint32_t output) {
    life_world candidate = *world;
    uint32_t outputs[LIFE_MAX_PATCHES] = {output, 0, 0, 0};
    if ((life_allowed_outputs(&frame->patches[0]) & (UINT32_C(1) << output)) == 0U) {
        CHECK(life_apply(&candidate, frame, outputs) == LIFE_INVALID_ACTION);
        CHECK(life_world_hash(&candidate) == life_world_hash(world));
        return 0;
    }
    CHECK(life_apply(&candidate, frame, outputs) == LIFE_OK);
    const unsigned int difference = occupancy_difference(&candidate, frame);
    CHECK(difference == popbits(life_output_bits(output)));
    CHECK(difference <= 2U);
    CHECK(candidate.stats.edits == difference);
    return difference > 0U ? 1U : 0U;
}

static void check_invalid_and_stale(life_world *world, life_frame *frame) {
    uint32_t outputs[LIFE_MAX_PATCHES] = {23, 0, 0, 0};
    uint64_t hash = life_world_hash(world);
    CHECK(life_apply(world, frame, outputs) == LIFE_INVALID_ACTION);
    CHECK(life_world_hash(world) == hash);
    outputs[0] = 0;
    frame->patches[0].cells[0] = 1023;
    CHECK(life_apply(world, frame, outputs) == LIFE_INVALID_ARGUMENT);
    CHECK(life_world_hash(world) == hash);
    CHECK(life_prepare(world, frame) == LIFE_OK);
    CHECK(life_world_set(world, 20, 20, 1) == LIFE_OK);
    hash = life_world_hash(world);
    CHECK(life_apply(world, frame, outputs) == LIFE_STALE_FRAME);
    CHECK(life_world_hash(world) == hash);
}

static void seed_disjoint_pairs(life_world *world, life_frame *frame) {
    /* Disjoint pairs produce separate patches; shared identities join them. */
    life_world_clear(world, 0);
    split_block(world);
    CHECK(life_world_set(world, 20, 20, 4) == LIFE_OK);
    CHECK(life_world_set(world, 21, 20, 4) == LIFE_OK);
    CHECK(life_world_set(world, 20, 21, 8) == LIFE_OK);
    CHECK(life_world_set(world, 21, 21, 8) == LIFE_OK);
    CHECK(life_prepare(world, frame) == LIFE_OK);
    CHECK(frame->patch_count == 2U);
    CHECK(frame->patches[0].module_mask == 3U);
    CHECK(frame->patches[1].module_mask == 12U);
}

static void test_disjoint_pair_atomicity(void) {
    life_world world;
    life_frame frame;
    uint32_t outputs[LIFE_MAX_PATCHES] = {2, 23, 0, 0};
    seed_disjoint_pairs(&world, &frame);
    const uint64_t hash = life_world_hash(&world);
    CHECK(life_apply(&world, &frame, outputs) == LIFE_INVALID_ACTION);
    CHECK(life_world_hash(&world) == hash);
    CHECK(life_world_set(&world, 20, 20, 2) == LIFE_OK);
    CHECK(life_prepare(&world, &frame) == LIFE_OK);
    CHECK(frame.patch_count == 1U);
    CHECK(frame.patches[0].module_mask == 15U);
    CHECK(frame.patches[0].cell_count == LIFE_PATCH_CELLS);
}

static void test_actions_and_atomicity(void) {
    life_world world;
    life_frame frame;
    unsigned int valid_edits = 0;
    prepare_action_fixture(&world, &frame);
    test_action_mapping();
    for (uint32_t output = 0; output < LIFE_OUTPUTS; ++output)
        valid_edits += check_action(&world, &frame, output);
    CHECK(valid_edits == 10U);
    check_invalid_and_stale(&world, &frame);
    test_disjoint_pair_atomicity();
}

static void verify_child_merge(life_world *world) {
    CHECK(life_world_merge(world, 3) == LIFE_OK);
    CHECK(world->entities[0].uid == 5U);
    CHECK(world->entities[0].ancestry == 3U);
    CHECK(world->entities[0].active == 1U);
    CHECK(world->entities[1].active == 0U);
    CHECK(life_population(world, 1) == 4U);
    CHECK(life_population(world, 2) == 0U);
    CHECK(world->stats.merged == 1U);
    CHECK(life_merge_ready_mask(world) == 0U);
    CHECK(life_world_merge(world, 3) == LIFE_INVALID_ARGUMENT);
}

static void test_coupled_merge(void) {
    life_world world;
    life_world_clear(&world, 0);
    split_block(&world);
    for (unsigned int i = 0; i < LIFE_RESOLUTION_TICKS - 1U; ++i) {
        step(&world);
        CHECK(life_merge_ready_mask(&world) == 0U);
    }
    step(&world);
    CHECK(life_merge_ready_mask(&world) == 3U);
    CHECK(world.stats.coupled == 1U);
    CHECK(world.stats.collisions == 1U);
    verify_child_merge(&world);
}

static uint32_t seed_separation(life_world *world) {
    life_world_clear(world, 0);
    split_block(world);
    step(world);
    const uint32_t id = world->conflicts[0].id;
    memset(world->cells, 0, sizeof(world->cells));
    set_block(world, 4, 4, 1);
    set_block(world, 20, 20, 2);
    return id;
}

static void check_reopened(life_world *world, uint32_t id) {
    split_block(world);
    step(world);
    CHECK(world->stats.reopened == 1U);
    CHECK(world->conflicts[0].id == id);
    CHECK(world->conflicts[0].age == 1U);
    CHECK(world->conflicts[0].outcome == LIFE_UNRESOLVED);
}

static void check_absorbed(life_world *world) {
    memset(world->cells, 0, sizeof(world->cells));
    set_block(world, 4, 4, 1);
    step(world);
    CHECK(world->stats.absorbed == 1U);
    CHECK(world->stats.extinct == 0U);
}

static void test_separation_and_reopen(void) {
    life_world world;
    const uint32_t id = seed_separation(&world);
    for (unsigned int i = 0; i < LIFE_RESOLUTION_TICKS - 1U; ++i) {
        step(&world);
        CHECK(world.stats.separated == 0U);
    }
    step(&world);
    CHECK(world.stats.separated == 1U);
    CHECK(world.conflicts[0].outcome == LIFE_SEPARATED);
    check_reopened(&world, id);
    check_absorbed(&world);
}

static void test_extinct(void) {
    life_world world;
    life_world_clear(&world, 0);
    split_block(&world);
    step(&world);
    memset(world.cells, 0, sizeof(world.cells));
    step(&world);
    CHECK(world.stats.extinct == 1U);
    CHECK(world.stats.absorbed == 0U);
}

static void test_lifecycle(void) {
    test_coupled_merge();
    test_separation_and_reopen();
    test_extinct();
}

static void test_mixed_lineage_birth(void) {
    life_world world;
    life_frame frame;
    life_world_clear(&world, 0);
    CHECK(life_world_set(&world, 9, 10, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 10, 9, 2) == LIFE_OK);
    CHECK(life_world_set(&world, 11, 10, 2) == LIFE_OK);
    CHECK(life_prepare(&world, &frame) == LIFE_OK);
    CHECK(frame.patch_count == 1U);
    CHECK(frame.patches[0].module_mask == 3U);
    CHECK(frame.baseline.cells[10 * LIFE_WIDTH + 10] == 3U);
    CHECK(life_apply(&world, &frame, NULL) == LIFE_OK);
}

static void test_supported_revival(void) {
    life_world world;
    life_frame frame;
    /* A supported intervention can revive a cell Conway would kill. */
    life_world_clear(&world, 0);
    CHECK(life_world_set(&world, 9, 10, 1) == LIFE_OK);
    CHECK(life_world_set(&world, 10, 10, 2) == LIFE_OK);
    CHECK(life_world_set(&world, 11, 10, 1) == LIFE_OK);
    CHECK(life_prepare(&world, &frame) == LIFE_OK);
    CHECK(frame.patches[0].cells[1] == 10 * LIFE_WIDTH + 9);
    CHECK(frame.baseline.cells[10 * LIFE_WIDTH + 9] == 0U);
    {
        const uint32_t outputs[LIFE_MAX_PATCHES] = {3, 0, 0, 0};
        CHECK(life_apply(&world, &frame, outputs) == LIFE_OK);
    }
    CHECK(world.cells[10 * LIFE_WIDTH + 9] == 3U);
    CHECK(world.stats.edits == 1U);
}

static void test_unsupported_intervention(void) {
    life_world world;
    life_frame frame;
    /* An isolated mixed claim has no supported intervention frontier. */
    life_world_clear(&world, 0);
    CHECK(life_world_set(&world, 10, 10, 3) == LIFE_OK);
    CHECK(life_prepare(&world, &frame) == LIFE_OK);
    CHECK(frame.patch_count == 1U);
    CHECK(frame.patches[0].cell_count == 0U);
    CHECK(life_allowed_outputs(&frame.patches[0]) == 3U);
    {
        const uint32_t outputs[LIFE_MAX_PATCHES] = {2, 0, 0, 0};
        uint64_t before = life_world_hash(&world);
        CHECK(life_apply(&world, &frame, outputs) == LIFE_INVALID_ACTION);
        CHECK(life_world_hash(&world) == before);
    }
}

static void test_mixed_birth(void) {
    test_mixed_lineage_birth();
    test_supported_revival();
    test_unsupported_intervention();
}

static void seed_child_identity_conflict(life_world *world) {
    life_world_clear(world, 0);
    split_block(world);
    CHECK(life_world_set(world, 10, 11, 4) == LIFE_OK);
    CHECK(life_world_set(world, 11, 11, 4) == LIFE_OK);
    step(world);
}

static void check_old_conflict_history(const life_world *world, uint32_t old_id) {
    int preserved = 0;
    for (size_t i = 0; i < LIFE_MAX_CONFLICTS; ++i) {
        if (world->conflicts[i].id == old_id) {
            CHECK(world->conflicts[i].outcome == LIFE_MERGED);
            CHECK(world->conflicts[i].active == 0U);
            preserved = 1;
        }
    }
    CHECK(preserved);
}

static void test_child_identity_conflict(void) {
    life_world world;
    life_frame frame;
    seed_child_identity_conflict(&world);
    const uint32_t old_id = world.conflicts[0].id;
    const uint32_t old_uid = world.entities[0].uid;
    CHECK(world.conflicts[0].module_mask == 5U);
    CHECK(life_world_merge(&world, 3) == LIFE_OK);
    CHECK(world.entities[0].uid != old_uid);
    CHECK(world.conflicts[0].outcome == LIFE_MERGED);
    CHECK(life_prepare(&world, &frame) == LIFE_OK);
    CHECK(frame.patch_count == 1U);
    CHECK(frame.patches[0].module_mask == 5U);
    CHECK(frame.patches[0].conflict_id != old_id);
    CHECK(life_apply(&world, &frame, NULL) == LIFE_OK);
    CHECK(world.stats.collisions == 2U);
    CHECK(world.stats.reopened == 0U);
    check_old_conflict_history(&world, old_id);
}

static void check_invalid_world(const life_world *source) {
    life_world world = *source;
    life_frame frame;
    const uint64_t hash = life_world_hash(&world);
    CHECK(life_prepare(&world, &frame) == LIFE_INVALID_ARGUMENT);
    CHECK(life_world_hash(&world) == hash);
}

static void test_invalid_metadata(void) {
    life_world world;
    life_world_clear(&world, 0);
    world.conflicts[0].module_mask = 3;
    check_invalid_world(&world);
    life_world_clear(&world, 0);
    split_block(&world);
    step(&world);
    world.conflicts[0].age = UINT32_MAX;
    check_invalid_world(&world);
    life_world_clear(&world, 0);
    world.next_conflict_id = UINT32_MAX;
    split_block(&world);
    check_invalid_world(&world);
}

int main(void) {
    test_rule_truth_table();
    test_standard_patterns();
    test_scenarios_and_oracle();
    test_actions_and_atomicity();
    test_lifecycle();
    test_mixed_birth();
    test_child_identity_conflict();
    test_invalid_metadata();
    if (failures != 0U) {
        fprintf(stderr, "%u Life world checks failed\n", failures);
        return 1;
    }
    puts("Life world: Conway oracle, deterministic collisions, actions, and lifecycle passed");
    return 0;
}
