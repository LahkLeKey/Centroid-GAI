/** @file life_training.c @brief Independent lookahead targets and bounded online replay. */
#include "life_training.h"
#include <limits.h>
#include <math.h>
#include <string.h>

#define LIFE_LOOKAHEAD 8U
#define LIFE_BATCH_SIZE 16U

static unsigned int bit_count(uint32_t bits) {
    unsigned int count = 0U;
    while (bits != 0U) {
        count += bits & 1U;
        bits >>= 1U;
    }
    return count;
}

static uint32_t bucket(uint32_t value) {
    uint32_t result = 0U;
    while (value > 1U && result < 7U) {
        value >>= 1U;
        ++result;
    }
    return result;
}

static uint8_t cell_claim_class(uint8_t claims, uint8_t first, uint8_t second) {
    const int has_first = (claims & first) != 0U;
    const int has_second = (claims & second) != 0U;
    if (has_first && !has_second)
        return 0U;
    if (has_second && !has_first)
        return 1U;
    return 2U;
}

static void first_participants(uint8_t mask, uint8_t *first, uint8_t *second) {
    *first = 0U;
    *second = 0U;
    for (unsigned int i = 0U; i < LIFE_MODULES; ++i) {
        const uint8_t bit = (uint8_t)(1U << i);
        if ((mask & bit) != 0U) {
            if (*first == 0U)
                *first = bit;
            else if (*second == 0U)
                *second = bit;
        }
    }
}

static void encode_cell(const life_world *world, const life_patch *patch,
                        cgai_gameplay_state *state, size_t i, uint8_t first, uint8_t second) {
    state->values[i] = 54U;
    state->values[i + LIFE_PATCH_CELLS] = 4U;
    if (i < patch->cell_count) {
        const unsigned int index = patch->cells[i];
        const uint8_t claims = world->cells[index];
        const int x = (int)(index % LIFE_WIDTH), y = (int)(index / LIFE_WIDTH);
        const unsigned int neighbors = life_neighbor_count(world, x, y);
        const uint8_t role = cell_claim_class(claims, first, second);
        state->values[i] = (claims != 0U ? 9U : 0U) + neighbors + 18U * role;
        state->values[i + LIFE_PATCH_CELLS] = claims == 0U ? 0U : (uint32_t)role + 1U;
    }
}

void life_encode(const life_world *world, const life_patch *patch, cgai_gameplay_state *state) {
    uint8_t first, second;
    memset(state, 0, sizeof(*state));
    first_participants(patch->module_mask, &first, &second);
    for (size_t i = 0U; i < LIFE_PATCH_CELLS; ++i)
        encode_cell(world, patch, state, i, first, second);
    state->values[12] = bucket(patch->age);
    state->values[13] = bucket(life_population(world, first));
    state->values[14] = bucket(life_population(world, second));
    state->values[15] = bit_count(patch->module_mask);
}

typedef struct life_viability {
    uint32_t minimum;
    uint32_t total;
    uint32_t viable;
    uint32_t required;
} life_viability;

static void measure_module(const life_world *initial, const life_world *future, uint8_t modules,
                           uint8_t bit, life_viability *measure) {
    if ((modules & bit) != 0U && life_population(initial, bit) != 0U) {
        uint32_t count = life_population(future, bit);
        ++measure->required;
        measure->viable += count != 0U ? 1U : 0U;
        if (count > 16U)
            count = 16U;
        if (count < measure->minimum)
            measure->minimum = count;
        measure->total += count;
    }
}

static life_viability measure_viability(const life_world *initial, const life_world *future,
                                        uint8_t modules) {
    life_viability measure = {UINT32_MAX, 0U, 0U, 0U};
    for (unsigned int i = 0U; i < LIFE_MODULES; ++i)
        measure_module(initial, future, modules, (uint8_t)(1U << i), &measure);
    if (measure.minimum == UINT32_MAX)
        measure.minimum = 0U;
    return measure;
}

static int64_t measure_resolution(const life_world *future, uint8_t modules) {
    int64_t resolved = 0;
    for (size_t i = 0U; i < LIFE_MAX_CONFLICTS; ++i) {
        const life_conflict *conflict = &future->conflicts[i];
        if (conflict->module_mask == modules &&
            (conflict->outcome == LIFE_SEPARATED || conflict->outcome == LIFE_COUPLED))
            resolved = 1;
    }
    return resolved;
}

static int64_t rollout_score(const life_world *initial, const life_world *future, uint8_t modules,
                             uint32_t edits) {
    const life_viability measure = measure_viability(initial, future, modules);
    const int64_t resolved = measure_resolution(future, modules);
    /* Viability precedes resolution, then balanced support and minimal intervention. */
    return (measure.viable == measure.required && measure.required != 0U ? INT64_C(1000000000)
                                                                         : 0) +
           (int64_t)measure.viable * INT64_C(10000000) + resolved * INT64_C(1000000) +
           (int64_t)measure.minimum * INT64_C(10000) + (int64_t)measure.total * 10 - (int64_t)edits;
}

static int score_action(const life_world *world, const life_frame *frame, size_t patch,
                        uint32_t output, int64_t *score) {
    life_world future = *world;
    life_frame next;
    uint32_t outputs[LIFE_MAX_PATCHES] = {0};
    outputs[patch] = output;
    if (life_apply(&future, frame, outputs) != LIFE_OK)
        return 0;
    for (unsigned int tick = 0U; tick < LIFE_LOOKAHEAD; ++tick) {
        if (life_prepare(&future, &next) != LIFE_OK || life_apply(&future, &next, NULL) != LIFE_OK)
            return 0;
    }
    *score = rollout_score(world, &future, frame->patches[patch].module_mask,
                           bit_count(life_output_bits(output)));
    return 1;
}

typedef struct life_choice {
    uint32_t output;
    int64_t score;
} life_choice;

static int choose_teacher_action(const life_world *world, const life_frame *frame, size_t patch,
                                 life_choice *choice) {
    const uint32_t allowed = life_allowed_outputs(&frame->patches[patch]);
    /* Zero is forced fallback; deliberate no-change has its own supervised target. */
    for (uint32_t output = 1U; output < LIFE_OUTPUTS; ++output) {
        int64_t measured = 0;
        if ((allowed & (UINT32_C(1) << output)) == 0U)
            continue;
        if (!score_action(world, frame, patch, output, &measured))
            return 0;
        if (measured > choice->score) {
            choice->score = measured;
            choice->output = output;
        }
    }
    return 1;
}

int life_teach(const life_world *world, const life_frame *frame, size_t patch, uint32_t *target,
               int64_t *score) {
    if (world == NULL || frame == NULL || target == NULL || score == NULL ||
        patch >= frame->patch_count || world->tick > UINT32_MAX - LIFE_LOOKAHEAD - 1U)
        return 0;
    life_choice choice = {1U, INT64_MIN};
    if (!choose_teacher_action(world, frame, patch, &choice))
        return 0;
    *target = choice.output;
    *score = choice.score;
    return choice.score != INT64_MIN;
}

int life_run_init(life_run *run, const life_run_config *config) {
    if (run == NULL || config == NULL || config->seed > UINT32_MAX || config->scenario > 3U ||
        config->training_epochs > 64U || config->mode < LIFE_MODE_CONWAY ||
        config->mode > LIFE_MODE_LEARNED ||
        (config->enable_merges != 0 && config->enable_merges != 1) ||
        (config->group_count != 0U &&
         (config->group_count < LIFE_MIN_MODULES || config->group_count > LIFE_MODULES)))
        return 0;
    memset(run, 0, sizeof(*run));
    run->config = *config;
    run->config.group_count =
        config->group_count == 0U ? LIFE_DEFAULT_MODULES : config->group_count;
    if (life_world_init_groups(&run->world, (uint32_t)config->seed, config->scenario,
                               run->config.group_count) != LIFE_OK)
        return 0;
    run->policy = life_policy_create_groups(config->seed, run->config.group_count);
    return run->policy != NULL;
}

void life_run_destroy(life_run *run) {
    if (run != NULL) {
        life_policy_destroy(run->policy);
        run->policy = NULL;
    }
}

static void remember(life_run *run, const life_record *record) {
    run->records[run->record_cursor] = *record;
    run->record_cursor = (run->record_cursor + 1U) % LIFE_REPLAY_CAPACITY;
    if (run->record_count < LIFE_REPLAY_CAPACITY)
        ++run->record_count;
    ++run->stats.collision_records;
}

static int replay_loss(life_run *run, double *mean) {
    double total = 0.0;
    for (size_t i = 0U; i < run->record_count; ++i) {
        life_record record = run->records[i];
        record.module_mask &= life_policy_active_mask(run->policy);
        if (record.module_mask == 0U)
            continue;
        double loss = 0.0;
        if (!life_policy_evaluate(run->policy, &record, &loss))
            return 0;
        total += loss;
    }
    *mean = run->record_count != 0U ? total / (double)run->record_count : 0.0;
    return 1;
}

static uint32_t current_participants(const life_run *run) {
    uint32_t modules = 0U;
    for (size_t i = 0U; i < run->last_frame.patch_count; ++i)
        modules |= run->last_frame.patches[i].module_mask;
    return modules & life_policy_active_mask(run->policy);
}

static size_t sample_batch(const life_run *run, life_record batch[LIFE_BATCH_SIZE]) {
    const size_t count = run->record_count < LIFE_BATCH_SIZE ? run->record_count : LIFE_BATCH_SIZE;
    size_t used = 0U;
    for (size_t i = 0U; i < count; ++i) {
        const size_t index = (i * run->record_count / count + run->world.tick) % run->record_count;
        batch[used] = run->records[index];
        batch[used].module_mask &= current_participants(run);
        if (batch[used].module_mask != 0U)
            ++used;
    }
    return used;
}

static int train_replay(life_run *run) {
    life_record batch[LIFE_BATCH_SIZE];
    const size_t used = sample_batch(run, batch);
    if (!replay_loss(run, &run->stats.loss_before))
        return 0;
    if (used != 0U && run->config.training_epochs != 0U) {
        if (!life_policy_train(run->policy, batch, used, run->config.training_epochs))
            return 0;
        run->stats.training_updates += (uint64_t)used * run->config.training_epochs;
    }
    return replay_loss(run, &run->stats.loss_after);
}

static void remap_records(life_run *run, uint8_t mask) {
    const uint32_t survivor = (uint32_t)mask & (~(uint32_t)mask + 1U);
    for (size_t i = 0U; i < run->record_count; ++i) {
        if ((run->records[i].module_mask & mask) != 0U)
            run->records[i].module_mask =
                (run->records[i].module_mask & ~(uint32_t)mask) | survivor;
    }
}

static int publish_merge(life_run *run, life_policy *candidate, uint8_t mask) {
    life_world child_world = run->world;
    if (life_world_merge(&child_world, mask) != LIFE_OK) {
        life_policy_destroy(candidate);
        return 0;
    }
    life_policy_destroy(run->policy);
    run->policy = candidate;
    run->world = child_world;
    ++run->stats.accepted_merges;
    /* Remap retained participant records to the new consolidated survivor module. */
    remap_records(run, mask);
    return 1;
}

static int try_merge(life_run *run, uint8_t mask) {
    life_policy *candidate = life_policy_clone(run->policy);
    life_merge_report report = {0};
    if (candidate == NULL)
        return 0;
    const int valid =
        life_policy_try_merge(candidate, mask, run->records, run->record_count, 16U, &report);
    if (!valid) {
        life_policy_destroy(candidate);
        return 0;
    }
    if (!report.accepted) {
        ++run->stats.rejected_merges;
        life_policy_destroy(candidate);
        return 1;
    }
    return publish_merge(run, candidate, mask);
}

static int attempt_merge(life_run *run) {
    const uint8_t mask = life_merge_ready_mask(&run->world);
    if (!run->config.enable_merges || run->config.mode != LIFE_MODE_LEARNED || mask == 0U ||
        run->world.tick < run->last_merge_attempt + LIFE_RESOLUTION_TICKS)
        return 1;
    run->last_merge_attempt = run->world.tick;
    ++run->stats.merge_attempts;
    return try_merge(run, mask);
}

static int policy_proposal(life_run *run, const life_frame *frame, size_t i,
                           const life_record *record) {
    cgai_gameplay_result result;
    if (!life_policy_select(run->policy, &record->state, record->module_mask,
                            life_allowed_outputs(&frame->patches[i]), &result))
        return 0;
    run->last_outputs[i] = result.output;
    ++run->stats.policy_decisions;
    run->stats.teacher_agreements += result.output == record->target ? 1U : 0U;
    run->stats.fallback_calls += result.abstained != 0 ? 1U : 0U;
    return 1;
}

static int patch_proposal(life_run *run, const life_frame *frame, size_t i) {
    life_record record = {0};
    int64_t score = 0;
    life_encode(&run->world, &frame->patches[i], &record.state);
    record.module_mask = frame->patches[i].module_mask;
    if (!life_teach(&run->world, frame, i, &record.target, &score))
        return 0;
    run->last_targets[i] = record.target;
    remember(run, &record);
    if (run->config.mode == LIFE_MODE_TEACHER)
        run->last_outputs[i] = record.target;
    else if (!policy_proposal(run, frame, i, &record))
        return 0;
    run->stats.edited_cells += bit_count(life_output_bits(run->last_outputs[i]));
    return 1;
}

static int proposals(life_run *run, const life_frame *frame) {
    memset(run->last_outputs, 0, sizeof(run->last_outputs));
    memset(run->last_targets, 0, sizeof(run->last_targets));
    for (size_t i = 0U; i < frame->patch_count; ++i) {
        if (!patch_proposal(run, frame, i))
            return 0;
    }
    return 1;
}

int life_run_tick(life_run *run) {
    if (run == NULL || run->policy == NULL || run->world.tick > UINT32_MAX - LIFE_LOOKAHEAD - 1U)
        return 0;
    if (life_prepare(&run->world, &run->last_frame) != LIFE_OK)
        return 0;
    if (run->config.mode == LIFE_MODE_CONWAY) {
        memset(run->last_outputs, 0, sizeof(run->last_outputs));
        memset(run->last_targets, 0, sizeof(run->last_targets));
        return life_apply(&run->world, &run->last_frame, NULL) == LIFE_OK;
    }
    /* Inference sees one frozen version. Mutation begins only after the world commits. */
    if (!proposals(run, &run->last_frame) ||
        life_apply(&run->world, &run->last_frame, run->last_outputs) != LIFE_OK)
        return 0;
    if (run->last_frame.patch_count != 0U && run->config.mode == LIFE_MODE_LEARNED &&
        !train_replay(run))
        return 0;
    return attempt_merge(run);
}

static int frozen_outputs(life_run *run, const life_frame *frame,
                          uint32_t outputs[LIFE_MAX_PATCHES]) {
    for (size_t i = 0U; i < frame->patch_count; ++i) {
        cgai_gameplay_state state;
        cgai_gameplay_result selected;
        life_encode(&run->world, &frame->patches[i], &state);
        if (!life_policy_select(run->policy, &state, frame->patches[i].module_mask,
                                life_allowed_outputs(&frame->patches[i]), &selected))
            return 0;
        outputs[i] = selected.output;
    }
    return 1;
}

int life_run_evaluate_tick(life_run *run) {
    life_frame frame;
    uint32_t outputs[LIFE_MAX_PATCHES] = {0};
    if (run == NULL || run->policy == NULL || life_prepare(&run->world, &frame) != LIFE_OK)
        return 0;
    if (!frozen_outputs(run, &frame, outputs))
        return 0;
    life_world next = run->world;
    if (life_apply(&next, &frame, outputs) != LIFE_OK)
        return 0;
    run->world = next;
    run->last_frame = frame;
    memcpy(run->last_outputs, outputs, sizeof(outputs));
    return 1;
}

static uint64_t hash_word(uint64_t hash, uint64_t value) {
    for (unsigned int i = 0U; i < 8U; ++i) {
        hash ^= value & UINT64_C(255);
        hash *= UINT64_C(1099511628211);
        value >>= 8U;
    }
    return hash;
}

static uint64_t hash_recipe(uint64_t hash, const life_run *run) {
    if (run->config.group_count != LIFE_DEFAULT_MODULES)
        hash = hash_word(hash, run->config.group_count);
    hash = hash_word(hash, run->config.seed);
    hash = hash_word(hash, run->config.scenario);
    hash = hash_word(hash, run->config.training_epochs);
    hash = hash_word(hash, (uint64_t)run->config.mode);
    hash = hash_word(hash, (uint64_t)run->config.enable_merges);
    hash = hash_word(hash, run->record_count);
    hash = hash_word(hash, run->record_cursor);
    hash = hash_word(hash, run->last_merge_attempt);
    return hash;
}

static uint64_t hash_statistics(uint64_t hash, const life_run_stats *stats) {
    const uint64_t counters[] = {
        stats->collision_records, stats->training_updates,   stats->edited_cells,
        stats->fallback_calls,    stats->merge_attempts,     stats->accepted_merges,
        stats->rejected_merges,   stats->teacher_agreements, stats->policy_decisions};
    for (size_t i = 0U; i < sizeof(counters) / sizeof(counters[0]); ++i)
        hash = hash_word(hash, counters[i]);
    uint64_t before_bits = 0U, after_bits = 0U;
    _Static_assert(sizeof(double) == sizeof(uint64_t), "Life snapshots require 64-bit doubles");
    memcpy(&before_bits, &stats->loss_before, sizeof(before_bits));
    memcpy(&after_bits, &stats->loss_after, sizeof(after_bits));
    hash = hash_word(hash, before_bits);
    hash = hash_word(hash, after_bits);
    return hash;
}

uint64_t life_run_hash(const life_run *run) {
    if (run == NULL || run->policy == NULL)
        return 0U;
    uint64_t hash = hash_word(life_world_hash(&run->world), life_policy_hash(run->policy));
    hash = hash_recipe(hash, run);
    hash = hash_statistics(hash, &run->stats);
    for (size_t i = 0U; i < run->record_count; ++i) {
        hash = hash_word(hash, run->records[i].target);
        hash = hash_word(hash, run->records[i].module_mask);
        for (size_t field = 0U; field < CGAI_GAMEPLAY_MAX_FEATURES; ++field)
            hash = hash_word(hash, run->records[i].state.values[field]);
    }
    return hash;
}
