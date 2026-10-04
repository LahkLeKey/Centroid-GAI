/** @file test_npc_domain.c @brief Native NPC input, frozen Life ownership and exact restart. */
#include "centroid_life_domain.h"
#include "life_domain.h"
#include "life_npc_world.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CHECK(condition) TEST_CHECK(condition, "Native NPC domain check failed")
#define PATH_BYTES 4097U

static cgai_life_domain *new_owner(cgai_life_domain_kind kind, uint32_t groups, uint32_t epochs) {
    cgai_life_domain *owner = NULL;
    cgai_life_config config = cgai_life_config_default();
    config.group_count = groups;
    config.training_epochs = epochs;
    config.enable_merges = 0U;
    CHECK(cgai_life_domain_create(&config, kind, &owner) == CGAI_LIFE_OK);
    CHECK(owner != NULL && life_domain_valid(owner) && owner->probe.kind == kind);
    return owner;
}

static void task_metadata(cgai_life_domain_task *task, const life_npc_world *world, uint32_t mask,
                          uint64_t episode) {
    task->version = CGAI_LIFE_DOMAIN_VERSION;
    task->family = world->family.id + 1U;
    task->split = (cgai_life_context_split)world->family.split;
    task->reviewed = task->split == CGAI_LIFE_CONTEXT_TRAIN ? 1U : 0U;
    task->observed_fields = UINT32_C(65535);
    task->feature_count = LIFE_NPC_FEATURE_COUNT;
    task->episode_id = episode;
    task->tick = world->ticks;
    task->source_hash = life_domain_hash_word(UINT64_C(14695981039346656037), task->family);
    task->source_hash = life_domain_hash_word(task->source_hash, episode);
    task->source_hash = life_domain_hash_word(task->source_hash, world->variant);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        if ((mask & (1U << group)) != 0U)
            task->eligible_uids[task->eligible_count++] = (uint32_t)group + 1U;
}

static cgai_life_domain_task observe_task(const life_npc_world *world, life_npc_memory *memory,
                                          uint32_t mask, uint64_t episode) {
    life_npc_observation observation;
    cgai_gameplay_state state;
    cgai_life_domain_task task = {0};
    life_npc_world_observe(world, &observation);
    life_npc_memory_observe(memory, &observation);
    const life_npc_memory updated = *memory;
    life_npc_memory_observe(memory, &observation);
    CHECK(memcmp(memory, &updated, sizeof(updated)) == 0);
    life_npc_encode(&observation, memory, 1, &state);
    task_metadata(&task, world, mask, episode);
    task.legal_actions = (uint32_t)observation.allowed_actions;
    memcpy(task.features, state.values, sizeof(task.features));
    CHECK(life_npc_task_valid(&task));
    return task;
}

/* Real authored simulator observations, never caller-supplied teacher labels.
 * Episode/source identities here name this unit fixture, not a production pack. */
static cgai_life_domain_task fixture(life_npc_split split, uint32_t mask, uint32_t variant,
                                     uint64_t episode, uint32_t steps) {
    life_npc_family family;
    life_npc_world world;
    life_npc_memory memory;
    CHECK(life_npc_family_get(split, 0U, &family));
    CHECK(life_npc_world_init(&world, &family, variant));
    life_npc_memory_reset(&memory);
    cgai_life_domain_task task = observe_task(&world, &memory, mask, episode);
    for (uint32_t step = 0U; step < steps; ++step) {
        CHECK(world.terminal == LIFE_NPC_RUNNING);
        (void)life_npc_world_step(&world, life_npc_teacher_task(&task));
        task = observe_task(&world, &memory, mask, episode);
    }
    return task;
}

static uint64_t enqueue(cgai_life_domain *owner, const cgai_life_domain_task *task) {
    uint64_t id = 0U;
    CHECK(cgai_life_domain_enqueue(owner, task, &id) == CGAI_LIFE_OK && id != 0U);
    return id;
}

static void enqueue_pair(cgai_life_domain *owner, uint32_t mask, uint64_t first) {
    const cgai_life_domain_task a = fixture(LIFE_NPC_TRAIN, mask, 0U, first, 0U),
                                b = fixture(LIFE_NPC_TRAIN, mask, 1U, first + 1U, 0U);
    CHECK(memcmp(a.features, b.features, sizeof(a.features)) != 0);
    CHECK(enqueue(owner, &a) != enqueue(owner, &b));
}

static cgai_life_domain_stats stats(const cgai_life_domain *owner) {
    cgai_life_domain_stats result;
    CHECK(cgai_life_domain_get_stats(owner, &result) == CGAI_LIFE_OK);
    return result;
}

static cgai_life_domain_prediction prediction(const cgai_life_domain *owner,
                                              const cgai_life_domain_task *task) {
    cgai_life_domain_prediction result;
    const uint64_t before = cgai_life_domain_hash(owner);
    const cgai_life_status status =
        cgai_life_domain_predict(owner, task, task->eligible_uids, task->eligible_count, &result);
    if (status != CGAI_LIFE_OK)
        fprintf(stderr,
                "Prediction status=%d kind=%u groups=%zu tick=%u native=%d typed=%d source=%llu\n",
                status, (unsigned int)owner->kind, owner->probe.model->config.module_count,
                task->tick, life_npc_task_valid(task),
                life_probe_task_valid(&owner->probe, task, 0),
                (unsigned long long)task->source_hash);
    CHECK(status == CGAI_LIFE_OK);
    CHECK(before == cgai_life_domain_hash(owner));
    CHECK(isfinite(result.probability) && result.probability >= 0.0 && result.probability <= 1.0);
    CHECK(result.context_hash == life_probe_task_hash(task) &&
          result.model_version == owner->version);
    return result;
}

static void reset_world(cgai_life_domain *owner) {
    life_world next;
    const life_world *previous = &owner->run.world;
    CHECK(life_world_clear_groups(&next, previous->seed, previous->group_count) == LIFE_OK);
    next.tick = previous->tick;
    next.stats = previous->stats;
    next.next_uid = previous->next_uid;
    next.next_conflict_id = previous->next_conflict_id;
    memcpy(next.entities, previous->entities, sizeof(next.entities));
    owner->run.world = next;
}

static void block(cgai_life_domain *owner, int x, int y, uint8_t mask) {
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
            CHECK(life_world_set(&owner->run.world, x + dx, y + dy, mask) == LIFE_OK);
}

static void contact(cgai_life_domain *owner, uint8_t mask) {
    reset_world(owner);
    block(owner, 10, 10, mask);
    uint32_t completed = 77U;
    CHECK(cgai_life_domain_train_step(owner, 1U, &completed) == CGAI_LIFE_OK && completed == 1U);
    CHECK(life_domain_valid(owner));
}

static void configured_groups(void) {
    for (uint32_t groups = 2U; groups <= 8U; ++groups) {
        cgai_life_domain *owner = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, groups, 0U);
        const cgai_life_domain_stats info = stats(owner);
        CHECK(info.group_count == groups && info.expert_count == groups * 4U);
        CHECK(life_probe_output_count(&owner->probe) == 7U);
        CHECK(owner->probe.model->category_count == 110U &&
              owner->probe.model->config.hidden_dimensions == 48U);
        const cgai_life_domain_task task = fixture(LIFE_NPC_TRAIN, 3U, 0U, 1U, 0U);
        CHECK(prediction(owner, &task).action == LIFE_NPC_FALLBACK);
        cgai_life_domain_destroy(owner);
    }
}

static void encode_category(life_probe *probe, size_t feature, uint32_t value, double hidden[48]) {
    cgai_gameplay_state state = {0};
    state.values[feature] = value;
    CHECK(cgai_gameplay_forward(probe->session, &state, 0U, 3U));
    memcpy(hidden, probe->session->hidden, 48U * sizeof(double));
}

static uint32_t category_bits(uint32_t cardinality) {
    uint32_t bits = 0U;
    for (uint32_t maximum = cardinality - 1U; maximum != 0U; maximum >>= 1U)
        ++bits;
    return bits;
}

static void encoded_axes(const double hidden[48], const double baseline[48], size_t offset,
                         uint32_t bits, uint32_t value) {
    for (size_t axis = 0U; axis < 48U; ++axis)
        if (axis >= offset && axis < offset + bits)
            CHECK(hidden[axis] == tanh((value & (1U << (axis - offset))) != 0U ? 1.0 : -1.0));
        else
            CHECK(hidden[axis] == baseline[axis]);
}

static void feature_encoding(life_probe *probe, size_t feature, uint32_t card, size_t offset) {
    double states[17][48], baseline[48];
    const uint32_t bits = category_bits(card);
    encode_category(probe, feature, 0U, baseline);
    for (uint32_t value = 0U; value < card; ++value) {
        encode_category(probe, feature, value, states[value]);
        encoded_axes(states[value], baseline, offset, bits, value);
        for (uint32_t earlier = 0U; earlier < value; ++earlier)
            CHECK(memcmp(states[value], states[earlier], sizeof(states[value])) != 0);
    }
}

static void encoding_injective(void) {
    cgai_life_domain *owner = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 8U, 1U);
    uint32_t cards[LIFE_NPC_FEATURE_COUNT];
    life_npc_cardinalities(cards);
    const uint64_t before = cgai_life_domain_hash(owner);
    size_t offset = 0U, categories = 0U;
    /* Frozen representation preflight includes every supported category. Private
     * tile5 is never admitted as an NPC observation or used for optimization. */
    for (size_t feature = 0U; feature < LIFE_NPC_FEATURE_COUNT; ++feature) {
        feature_encoding(&owner->probe, feature, cards[feature], offset);
        offset += category_bits(cards[feature]);
        categories += cards[feature];
    }
    CHECK(offset == 46U && categories == 110U);
    CHECK(before == cgai_life_domain_hash(owner));
    cgai_life_domain_destroy(owner);
}

static void low_unchanged(const cgai_life_domain_stats *before,
                          const cgai_life_domain_stats *after) {
    CHECK(before->shared_hash == after->shared_hash &&
          before->cell_shared_hash == after->cell_shared_hash);
    for (size_t group = 0U; group < 4U; ++group) {
        CHECK(before->group_steps[group] == after->group_steps[group]);
        CHECK(before->group_hashes[group] == after->group_hashes[group]);
        CHECK(before->cell_group_steps[group] == after->cell_group_steps[group]);
        CHECK(before->cell_group_hashes[group] == after->cell_group_hashes[group]);
    }
}

static void cold_record(const cgai_life_domain_record *record) {
    CHECK(record->parent_version == 0U && record->participant_mask == 240U);
    CHECK(record->executed_action == LIFE_NPC_FALLBACK && record->target == LIFE_NPC_WAIT);
    CHECK(record->verified == 1U && record->teacher_identity == LIFE_NPC_TEACHER);
    CHECK(record->admission == CGAI_LIFE_DOMAIN_ADMITTED && record->evidence_hash != 0U);
    CHECK(record->contact.participant_mask == 240U && record->contact.teacher_target_valid == 1U);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group) {
        CHECK(record->proposals[group] == 0U);
        CHECK(group >= 4U ? fabs(record->probabilities[group] - 1.0 / 7.0) < 1e-12
                          : record->probabilities[group] == 0.0);
    }
}

static void high_updates(const cgai_life_domain_stats *before,
                         const cgai_life_domain_stats *after) {
    CHECK(after->training_updates == 8U && after->observations == 2U &&
          after->replay_records == 2U);
    for (size_t group = 4U; group < 8U; ++group)
        CHECK(after->group_steps[group] == 8U &&
              before->group_hashes[group] != after->group_hashes[group]);
}

static void high_learning(void) {
    cgai_life_domain *owner = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 8U, 4U);
    const cgai_life_domain_task task = fixture(LIFE_NPC_TRAIN, 240U, 0U, 1U, 0U);
    const cgai_life_domain_prediction cold = prediction(owner, &task);
    const cgai_life_domain_stats before = stats(owner);
    enqueue_pair(owner, 240U, 1U);
    contact(owner, 240U);
    const cgai_life_domain_prediction trained = prediction(owner, &task);
    const cgai_life_domain_stats after = stats(owner);
    CHECK(owner->round.count == 2U && owner->round.parent_version == 0U &&
          owner->round.result_version == 1U);
    cold_record(&owner->round.records[0]);
    cold_record(&owner->round.records[1]);
    CHECK(memcmp(owner->round.records[0].task.features, owner->round.records[1].task.features,
                 sizeof(task.features)) != 0);
    CHECK(cold.action == 0U && trained.action == LIFE_NPC_WAIT &&
          trained.probability > cold.probability);
    low_unchanged(&before, &after);
    high_updates(&before, &after);
    cgai_life_domain_destroy(owner);
}

static void replay_isolation(void) {
    cgai_life_domain *owner = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 8U, 4U);
    enqueue_pair(owner, 3U, 1U);
    contact(owner, 3U);
    const cgai_life_domain_stats before = stats(owner);
    enqueue_pair(owner, 240U, 3U);
    contact(owner, 240U);
    const cgai_life_domain_stats after = stats(owner);
    low_unchanged(&before, &after);
    CHECK(before.group_steps[0] == 8U && after.training_updates == before.training_updates + 8U);
    contact(owner, 240U);
    const cgai_life_domain_stats idle = stats(owner);
    CHECK(owner->round.count == 0U && idle.training_updates == after.training_updates);
    CHECK(idle.version == after.version && idle.observations == after.observations);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        CHECK(idle.group_hashes[group] == after.group_hashes[group]);
    cgai_life_domain_destroy(owner);
}

static void own_history_input(void) {
    cgai_life_domain *owner = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 8U, 2U);
    life_npc_family family;
    life_npc_world world;
    life_npc_memory memory;
    CHECK(life_npc_family_get(LIFE_NPC_TRAIN, 0U, &family));
    CHECK(life_npc_world_init(&world, &family, 0U));
    life_npc_memory_reset(&memory);
    (void)observe_task(&world, &memory, 240U, 1U);
    CHECK(life_npc_world_step(&world, LIFE_NPC_FALLBACK) == LIFE_NPC_IDLE);
    const cgai_life_domain_task task = observe_task(&world, &memory, 240U, 1U);
    CHECK(task.tick == 1U && task.features[9] == LIFE_NPC_FALLBACK &&
          task.features[10] == LIFE_NPC_IDLE);
    CHECK(prediction(owner, &task).action == LIFE_NPC_FALLBACK);
    enqueue(owner, &task);
    contact(owner, 240U);
    CHECK(owner->round.count == 1U && owner->round.records[0].task.tick == 1U);
    CHECK(owner->round.records[0].target == life_npc_teacher_task(&task));
    cgai_life_domain_destroy(owner);
}

static void rejected_task(cgai_life_domain *owner, const cgai_life_domain_task *task) {
    const uint64_t before = cgai_life_domain_hash(owner);
    uint64_t id = 77U;
    CHECK(cgai_life_domain_enqueue(owner, task, &id) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(id == 77U && before == cgai_life_domain_hash(owner));
}

static void invalid_visible_input(void) {
    cgai_life_domain *owner = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 8U, 4U);
    const cgai_life_domain_task valid = fixture(LIFE_NPC_TRAIN, 240U, 0U, 1U, 0U);
    cgai_life_domain_task task = valid;
    task.feature_count = 15U;
    rejected_task(owner, &task);
    task = valid;
    task.features[0] = LIFE_NPC_OBSTRUCTION;
    rejected_task(owner, &task);
    task = valid;
    task.features[9] = LIFE_NPC_NORTH;
    rejected_task(owner, &task);
    task = valid;
    task.legal_actions |= 1U << LIFE_NPC_NORTH;
    rejected_task(owner, &task);
    task = valid;
    task.x = 1U;
    rejected_task(owner, &task);
    cgai_life_domain_destroy(owner);
}

static cgai_life_domain_task categorical_task(void) {
    cgai_life_domain_task categorical = {0};
    categorical.version = CGAI_LIFE_DOMAIN_VERSION;
    categorical.family = 1U;
    categorical.split = CGAI_LIFE_CONTEXT_TRAIN;
    categorical.reviewed = 1U;
    categorical.source_hash = 1U;
    categorical.x = 1U;
    categorical.observed_fields = 3U;
    categorical.legal_actions = 7U;
    categorical.eligible_count = 2U;
    categorical.eligible_uids[0] = 1U;
    categorical.eligible_uids[1] = 2U;
    return categorical;
}

static void heldout_and_kind(void) {
    cgai_life_domain *owner = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 8U, 1U),
                     *probe = new_owner(CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE, 8U, 1U);
    const cgai_life_domain_task heldout = fixture(LIFE_NPC_DEV, 240U, 0U, 1U, 0U),
                                training = fixture(LIFE_NPC_TRAIN, 240U, 0U, 2U, 0U);
    const uint64_t before = cgai_life_domain_hash(owner);
    rejected_task(owner, &heldout);
    CHECK(prediction(owner, &heldout).action == 0U && before == cgai_life_domain_hash(owner));
    rejected_task(probe, &training);
    const cgai_life_domain_task categorical = categorical_task();
    rejected_task(owner, &categorical);
    cgai_life_domain_destroy(owner);
    cgai_life_domain_destroy(probe);
}

static void queue_uniqueness(void) {
    cgai_life_domain *owner = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 8U, 1U);
    cgai_life_domain_task task = fixture(LIFE_NPC_TRAIN, 240U, 0U, 1U, 0U);
    const uint64_t id = enqueue(owner, &task), before = cgai_life_domain_hash(owner);
    CHECK(enqueue(owner, &task) == id && before == cgai_life_domain_hash(owner));
    ++task.source_hash;
    rejected_task(owner, &task);
    task = fixture(LIFE_NPC_TRAIN, 240U, 1U, 2U, 0U);
    enqueue(owner, &task);
    reset_world(owner);
    block(owner, 4, 4, 48U);
    block(owner, 20, 20, 192U);
    CHECK(cgai_life_domain_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(owner->round.count == 2U && owner->queue_count == 0U);
    CHECK(owner->round.records[0].task.episode_id != owner->round.records[1].task.episode_id);
    CHECK(owner->round.records[0].participant_mask == 48U &&
          owner->round.records[1].participant_mask == 48U);
    CHECK(owner->probe.steps[6] == 0U && owner->probe.steps[7] == 0U);
    cgai_life_domain_destroy(owner);
}

static void zero_epochs(void) {
    cgai_life_domain *owner = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 8U, 0U);
    const cgai_life_domain_stats before = stats(owner);
    enqueue_pair(owner, 240U, 1U);
    contact(owner, 240U);
    const cgai_life_domain_stats after = stats(owner);
    CHECK(after.observations == 2U && after.training_updates == 0U && after.version == 0U);
    CHECK(before.cell_policy_hash == after.cell_policy_hash &&
          before.shared_hash == after.shared_hash);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        CHECK(before.group_hashes[group] == after.group_hashes[group]);
    cgai_life_domain_destroy(owner);
}

static void path_name(char *path, const char *directory, const char *label) {
#ifdef _WIN32
    const unsigned int pid = (unsigned int)GetCurrentProcessId();
#else
    const unsigned int pid = (unsigned int)getpid();
#endif
    const int written =
        snprintf(path, PATH_BYTES, "%s/npc-domain-%u-%s.bin", directory, pid, label);
    CHECK(written > 0 && (size_t)written < PATH_BYTES);
}

static uint64_t wire_version(const char *path) {
    FILE *file = fopen(path, "rb");
    unsigned char header[16];
    uint64_t version = 0U;
    CHECK(file != NULL && fread(header, 1U, sizeof(header), file) == sizeof(header));
    CHECK(fclose(file) == 0);
    for (size_t byte = 0U; byte < 8U; ++byte)
        version |= (uint64_t)header[8U + byte] << (byte * 8U);
    return version;
}

static void prefix_stream(const cgai_life_domain *owner, cgai_life_domain *restored,
                          const char *path) {
    FILE *file = fopen(path, "w+b");
    char marker[4];
    CHECK(file != NULL && fwrite("PRE0", 1U, 4U, file) == 4U);
    CHECK(life_domain_checkpoint_write(file, owner));
    CHECK(fwrite("END0", 1U, 4U, file) == 4U && fseek(file, 0L, SEEK_SET) == 0);
    CHECK(fread(marker, 1U, 4U, file) == 4U && memcmp(marker, "PRE0", 4U) == 0);
    CHECK(life_domain_checkpoint_read(file, restored));
    CHECK(cgai_life_domain_hash(owner) == cgai_life_domain_hash(restored));
    CHECK(fread(marker, 1U, 4U, file) == 4U && memcmp(marker, "END0", 4U) == 0);
    CHECK(fgetc(file) == EOF && fclose(file) == 0);
    const uint64_t before = cgai_life_domain_hash(restored);
    CHECK(cgai_life_domain_load(restored, path) == CGAI_LIFE_IO_ERROR);
    CHECK(before == cgai_life_domain_hash(restored));
}

static uint64_t legacy_task_hash(const cgai_life_domain_task *task) {
    const uint64_t fields[] = {task->version,  task->family,          (uint64_t)task->split,
                               task->reviewed, task->source_hash,     task->x,
                               task->y,        task->observed_fields, task->legal_actions,
                               task->fallback, task->eligible_count};
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t field = 0U; field < sizeof(fields) / sizeof(fields[0]); ++field)
        hash = life_domain_hash_word(hash, fields[field]);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        hash = life_domain_hash_word(hash, task->eligible_uids[group]);
    return hash;
}

static void cross_kind_load(cgai_life_domain *owner, const char *path) {
    cgai_life_domain *probe = new_owner(CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE, 4U, 2U);
    const cgai_life_domain_task categorical = categorical_task();
    CHECK(life_probe_task_hash(&categorical) == legacy_task_hash(&categorical));
    enqueue(probe, &categorical);
    contact(probe, 3U);
    const uint64_t before = cgai_life_domain_hash(probe);
    CHECK(cgai_life_domain_load(probe, path) == CGAI_LIFE_IO_ERROR);
    CHECK(before == cgai_life_domain_hash(probe));
    CHECK(cgai_life_domain_save(probe, path) == CGAI_LIFE_OK && wire_version(path) == 1U);
    const uint64_t npc_before = cgai_life_domain_hash(owner);
    CHECK(cgai_life_domain_load(owner, path) == CGAI_LIFE_IO_ERROR);
    CHECK(npc_before == cgai_life_domain_hash(owner));
    cgai_life_domain *same = new_owner(CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE, 2U, 1U);
    CHECK(cgai_life_domain_load(same, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_domain_hash(same) == before);
    CHECK(prediction(same, &categorical).action == 1U);
    cgai_life_domain_destroy(probe);
    cgai_life_domain_destroy(same);
}

static void exact_restart(const char *directory) {
    cgai_life_domain *whole = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 8U, 4U),
                     *restored = new_owner(CGAI_LIFE_DOMAIN_NATIVE_NPC, 2U, 0U);
    char path[PATH_BYTES];
    path_name(path, directory, "resume");
    enqueue_pair(whole, 240U, 1U);
    contact(whole, 240U);
    enqueue_pair(whole, 240U, 3U);
    CHECK(cgai_life_domain_save(whole, path) == CGAI_LIFE_OK && wire_version(path) == 2U);
    CHECK(cgai_life_domain_load(restored, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_domain_hash(whole) == cgai_life_domain_hash(restored));
    contact(whole, 240U);
    contact(restored, 240U);
    CHECK(cgai_life_domain_hash(whole) == cgai_life_domain_hash(restored));
    CHECK(restored->probe.kind == CGAI_LIFE_DOMAIN_NATIVE_NPC &&
          restored->probe.model->config.module_count == 8U);
    cross_kind_load(restored, path);
    prefix_stream(whole, restored, path);
    CHECK(remove(path) == 0);
    cgai_life_domain_destroy(whole);
    cgai_life_domain_destroy(restored);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    configured_groups();
    encoding_injective();
    high_learning();
    replay_isolation();
    own_history_input();
    invalid_visible_input();
    heldout_and_kind();
    queue_uniqueness();
    zero_epochs();
    exact_restart(argv[1]);
    puts("Native NPC domain: visible inputs, frozen contact learning, UID isolation and restart "
         "pass");
    return 0;
}
