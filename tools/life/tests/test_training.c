/** @file test_training.c @brief Authoritative teacher targets and causal online learning. */
#include "life_training.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void check_teacher_patch(const life_world *world, const life_frame *frame, size_t patch) {
    cgai_gameplay_state state;
    life_encode(world, &frame->patches[patch], &state);
    for (size_t i = 0U; i < 6U; ++i)
        TEST_CHECK(state.values[i] < 55U && state.values[i + 6U] < 5U,
                   "candidate encoding exceeded categorical contract");
    uint32_t first, second;
    int64_t first_score, second_score;
    TEST_CHECK(life_teach(world, frame, patch, &first, &first_score), "teacher failed");
    TEST_CHECK(life_teach(world, frame, patch, &second, &second_score), "repeat failed");
    TEST_CHECK(first == second && first_score == second_score && first != 0U,
               "teacher targets depend on external model state or randomness");
    TEST_CHECK((life_allowed_outputs(&frame->patches[patch]) & (1U << first)) != 0U,
               "lookahead teacher selected an inadmissible edit");
}

static void check_encoding_and_teacher(void) {
    life_world world;
    life_frame frame;
    TEST_CHECK(life_world_init(&world, 42U, 3U) == LIFE_OK, "crowd fixture failed");
    unsigned int examples = 0U;
    for (unsigned int tick = 0U; tick < 8U; ++tick) {
        TEST_CHECK(life_prepare(&world, &frame) == LIFE_OK, "teacher frame failed");
        for (size_t patch = 0U; patch < frame.patch_count; ++patch) {
            check_teacher_patch(&world, &frame, patch);
            ++examples;
        }
        TEST_CHECK(life_apply(&world, &frame, NULL) == LIFE_OK, "Conway collection failed");
    }
    TEST_CHECK(examples != 0U, "authored collection never encountered a collision");
}

static void check_conway_run(void) {
    const life_run_config config = {42U, 0U, 8U, LIFE_MODE_CONWAY, 1, LIFE_DEFAULT_MODULES};
    life_run run;
    life_world oracle;
    TEST_CHECK(life_run_init(&run, &config), "Conway run creation failed");
    oracle = run.world;
    const uint64_t policy_hash = life_policy_hash(run.policy);
    for (unsigned int i = 0U; i < 20U; ++i) {
        life_frame frame;
        TEST_CHECK(life_prepare(&oracle, &frame) == LIFE_OK &&
                       life_apply(&oracle, &frame, NULL) == LIFE_OK,
                   "Conway oracle failed");
        TEST_CHECK(life_run_tick(&run), "Conway run failed");
        TEST_CHECK(life_world_hash(&run.world) == life_world_hash(&oracle),
                   "Conway mode changed authoritative dynamics");
    }
    TEST_CHECK(run.record_count == 0U && run.stats.training_updates == 0U &&
                   life_policy_hash(run.policy) == policy_hash,
               "Conway baseline unexpectedly trained or consolidated the model");
    life_run_destroy(&run);
}

static void check_replay_generation(life_run *first, life_run *second, life_world *baseline,
                                    int *changed_occupancy) {
    life_frame frame;
    TEST_CHECK(life_prepare(baseline, &frame) == LIFE_OK &&
                   life_apply(baseline, &frame, NULL) == LIFE_OK,
               "comparison baseline failed");
    TEST_CHECK(life_run_tick(first) && life_run_tick(second), "learned generation failed");
    TEST_CHECK(life_run_hash(first) == life_run_hash(second),
               "same-seed learned collision training failed exact replay");
    for (size_t cell = 0U; cell < LIFE_CELLS; ++cell)
        if ((first->world.cells[cell] != 0U) != (baseline->cells[cell] != 0U))
            *changed_occupancy = 1;
}

static void check_online_replay(void) {
    const life_run_config config = {42U, 3U, 8U, LIFE_MODE_LEARNED, 0, LIFE_DEFAULT_MODULES};
    life_run first, second;
    life_world baseline;
    TEST_CHECK(life_run_init(&first, &config) && life_run_init(&second, &config),
               "learned run creation failed");
    baseline = first.world;
    int changed_occupancy = 0, improved_loss = 0;
    for (unsigned int i = 0U; i < 16U; ++i) {
        check_replay_generation(&first, &second, &baseline, &changed_occupancy);
        if (first.stats.loss_after < first.stats.loss_before)
            improved_loss = 1;
    }
    TEST_CHECK(first.stats.collision_records > 0U && first.stats.training_updates > 0U,
               "collisions did not become native training events");
    TEST_CHECK(changed_occupancy && first.stats.edited_cells > 0U,
               "learned conflict proposals did not change cell evolution");
    TEST_CHECK(improved_loss && isfinite(first.stats.loss_after),
               "collision supervision did not reduce any observed replay loss");
    life_run_destroy(&first);
    life_run_destroy(&second);
}

static uint32_t world_active_mask(const life_world *world) {
    uint32_t mask = 0U;
    for (size_t i = 0U; i < LIFE_MODULES; ++i)
        if (world->entities[i].active != 0U)
            mask |= 1U << i;
    return mask;
}

static int has_consolidated_child(const life_world *world) {
    for (size_t i = 0U; i < LIFE_MODULES; ++i) {
        const life_entity *entity = &world->entities[i];
        if (entity->active != 0U && entity->uid > world->group_count &&
            (entity->ancestry & (entity->ancestry - 1U)) != 0U)
            return 1;
    }
    return 0;
}

static void check_coordinated_merges(void) {
    const life_run_config config = {42U, 3U, 8U, LIFE_MODE_LEARNED, 1, LIFE_DEFAULT_MODULES};
    life_run run;
    TEST_CHECK(life_run_init(&run, &config), "merge run creation failed");
    for (unsigned int tick = 0U; tick < 64U; ++tick) {
        TEST_CHECK(life_run_tick(&run), "merge run generation failed");
        const uint32_t active = life_policy_active_mask(run.policy);
        TEST_CHECK(active == world_active_mask(&run.world),
                   "policy and world published different identity topologies");
        for (size_t i = 0U; i < run.record_count; ++i)
            TEST_CHECK(run.records[i].module_mask != 0U &&
                           (run.records[i].module_mask & ~active) == 0U,
                       "consolidation left replay routed to retired parents");
    }
    TEST_CHECK(run.stats.accepted_merges > 0U && run.world.stats.merged > 0U,
               "learned crowd did not validate and publish any consolidation");
    TEST_CHECK(has_consolidated_child(&run.world),
               "published consolidation omitted a new child UID and combined ancestry");
    life_run_destroy(&run);
}

static void check_invalid_config(void) {
    life_run run;
    life_run_config config = {UINT64_MAX, 0U, 8U, LIFE_MODE_LEARNED, 1, LIFE_DEFAULT_MODULES};
    TEST_CHECK(!life_run_init(&run, &config), "out-of-range world seed accepted");
    config.seed = 42U;
    config.training_epochs = 65U;
    TEST_CHECK(!life_run_init(&run, &config), "unbounded generation learning accepted");
    config.training_epochs = 8U;
    config.scenario = 4U;
    TEST_CHECK(!life_run_init(&run, &config), "unknown scenario accepted");
}

int main(void) {
    check_encoding_and_teacher();
    check_conway_run();
    check_online_replay();
    check_coordinated_merges();
    check_invalid_config();
    puts("Centroid Life teaching and online replay checks passed");
    return 0;
}
