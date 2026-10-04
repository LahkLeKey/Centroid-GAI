/** @file test_world.c @brief V3 reservations, observable detours and replay invariants.
 */
#include "npc_teacher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** @brief Resolve one immutable v1 family for initial-geometry comparisons.
 * @param split Requested v1 family split.
 * @param index Split-local family index.
 * @param family Writable immutable v1 provenance.
 * @return One when the v1 family exists. */
int npc_v1_family_get(npc_split split, uint32_t index, npc_family *family);
/** @brief Generate one immutable v1 initial state without actor outcomes.
 * @param world Writable host initial state.
 * @param family Borrowed v1 provenance.
 * @param variant Requested v1 sibling.
 * @return One when the v1 geometry exists. */
int npc_v1_world_init(npc_world *world, const npc_family *family, uint32_t variant);
/** @brief Resolve one frozen v2 family without consulting actor outcomes.
 * @param split Requested v2 split.
 * @param index Split-local index.
 * @param family Writable provenance.
 * @return One when the family exists. */
int npc_v2_family_get(npc_split split, uint32_t index, npc_family *family);
/** @brief Generate one frozen v2 initial geometry without actor outcomes.
 * @param world Writable initial world.
 * @param family Borrowed v2 provenance.
 * @param variant Selected sibling.
 * @return One when the geometry exists. */
int npc_v2_world_init(npc_world *world, const npc_family *family, uint32_t variant);
/** @brief Fail one acceptance check with its source line.
 * @param condition Nonzero successful assertion.
 * @param message Failure reason.
 * @param line Source line. */
static void npc_check(int condition, const char *message, int line) {
    if (!condition) {
        fprintf(stderr, "NPC check failed at line%d: %s\n", line, message);
        exit(EXIT_FAILURE);
    }
}
/** @brief Evaluate one acceptance assertion.
 * @param condition Nonzero successful condition.
 * @param message Failure description. */
#define NPC_CHECK(condition, message) npc_check((condition), (message), __LINE__)
/** Paired authoritative trajectories used only for replay verification. */
typedef struct replay_pair {
    npc_world world;            /**< Original host world. */
    npc_world repeated;         /**< Independent replay world. */
    npc_memory memory;          /**< Original observable history. */
    npc_memory repeated_memory; /**< Independent replay history. */
} replay_pair;
/** Earlier-cue pair with identical later visible information. */
typedef struct cue_pair {
    replay_pair episodes;   /**< Cue siblings with independent histories. */
    npc_observation first;  /**< First current observation. */
    npc_observation second; /**< Second current observation. */
} cue_pair;
/** Translation-normalized, rotation- and mirror-canonical initial geometry. */
typedef struct geometry_key {
    char bytes[83];   /**< Eighty-one neutral cells, transformed cue and mechanic. */
    uint32_t family;  /**< Provenance excluded from the canonical bytes. */
    uint32_t variant; /**< Sibling provenance excluded from the canonical bytes. */
    uint32_t version; /**< Generator profile excluded from the canonical bytes. */
} geometry_key;
/** One canonicalization trial's coordinate origin and symmetry. */
typedef struct geometry_transform {
    uint32_t x;        /**< Minimum non-wall transformed column. */
    uint32_t y;        /**< Minimum non-wall transformed row. */
    uint32_t symmetry; /**< Four rotations, optionally preceded by a reflection. */
} geometry_transform;

/** @brief Transform one initial coordinate by a grid symmetry.
 * @param x Mutable column.
 * @param y Mutable row.
 * @param symmetry Requested rotation and optional mirror. */
static void geometry_point(uint32_t *x, uint32_t *y, uint32_t symmetry) {
    if (symmetry >= 4U)
        *x = 8U - *x;
    for (uint32_t rotation = 0; rotation < symmetry % 4U; ++rotation) {
        uint32_t next_x = 8U - *y;
        *y = *x;
        *x = next_x;
    }
}

/** @brief Find the transformed reachable geometry's translation origin.
 * @param world Borrowed initial world, without any actor rollout.
 * @param transform Mutable requested symmetry and resulting origin. */
static void geometry_origin(const npc_world *world, geometry_transform *transform) {
    transform->x = 8U;
    transform->y = 8U;
    for (uint32_t cell = 0; cell < 81U; ++cell) {
        if (world->cells[cell] == NPC_WALL)
            continue;
        uint32_t x = cell % 9U, y = cell / 9U;
        geometry_point(&x, &y, transform->symmetry);
        if (x < transform->x)
            transform->x = x;
        if (y < transform->y)
            transform->y = y;
    }
}

/** @brief Render neutral terrain with translation removed.
 * @param world Borrowed initial world.
 * @param transform Borrowed selected symmetry and origin.
 * @param key Writable canonical bytes. */
static void geometry_terrain(const npc_world *world, const geometry_transform *transform,
                             char *key) {
    for (uint32_t cell = 0; cell < 81U; ++cell) {
        uint32_t tile = world->cells[cell];
        if (tile == NPC_WALL)
            continue;
        uint32_t x = cell % 9U, y = cell / 9U;
        geometry_point(&x, &y, transform->symmetry);
        if (tile == NPC_ITEM_TILE || tile == NPC_EXIT_TILE)
            tile = NPC_FLOOR;
        key[(y - transform->y) * 9U + x - transform->x] = (char)('0' + tile);
    }
}

/** @brief Mark an actor or objective in normalized coordinates.
 * @param key Writable canonical bytes.
 * @param transform Borrowed selected symmetry and origin.
 * @param x Original column.
 * @param y Original row.
 * @param mark Actor or objective marker. */
static void geometry_mark(char *key, const geometry_transform *transform, uint32_t x, uint32_t y,
                          char mark) {
    geometry_point(&x, &y, transform->symmetry);
    key[(y - transform->y) * 9U + x - transform->x] = mark;
}

/** @brief Render a complete trial including transformed initial cue semantics.
 * @param world Borrowed initial episode metadata.
 * @param symmetry One of eight square symmetries.
 * @param key Writable83-byte trial. */
static void geometry_trial(const npc_world *world, uint32_t symmetry, char key[83]) {
    geometry_transform transform = {0, 0, symmetry};
    memset(key, '1', 83U);
    geometry_origin(world, &transform);
    geometry_terrain(world, &transform, key);
    geometry_mark(key, &transform, world->x, world->y, 'A');
    geometry_mark(key, &transform, world->item_x, world->item_y, 'I');
    geometry_mark(key, &transform, world->exit_x, world->exit_y, 'E');
    uint32_t cue = world->cue_direction == 0U ? 4U : world->cue_direction - 1U;
    if (world->family.mechanic == NPC_CUE)
        geometry_mark(key, &transform, world->junction_x, world->junction_y, 'J');
    if (symmetry >= 4U && cue < 4U && cue % 2U != 0U)
        cue = 4U - cue;
    key[81] = (char)('0' + (cue < 4U ? (cue + symmetry % 4U) % 4U : 4U));
    key[82] = (char)('0' + (uint32_t)world->family.mechanic);
}

/** @brief Canonicalize all eight square symmetries for one initial episode.
 * @param world Borrowed initial world, without actor outcomes.
 * @param key Writable canonical bytes. */
static void canonical_geometry(const npc_world *world, char key[83]) {
    geometry_trial(world, 0U, key);
    for (uint32_t symmetry = 1; symmetry < 8U; ++symmetry) {
        char trial[83];
        geometry_trial(world, symmetry, trial);
        if (memcmp(trial, key, 83U) < 0)
            memcpy(key, trial, 83U);
    }
}

/** @brief Reject any individual sibling shared by two distinct families.
 * @param keys Mutable bounded collection of earlier canonical initial episodes.
 * @param count Number of earlier siblings.
 * @param world Borrowed newly generated initial world. */
static void geometry_admit(geometry_key *keys, size_t count, const npc_world *world) {
    canonical_geometry(world, keys[count].bytes);
    keys[count].family = world->family.id;
    keys[count].variant = world->variant;
    keys[count].version = 3U;
    for (size_t earlier = 0; earlier < count; ++earlier) {
        if (keys[earlier].version == 3U && keys[earlier].family == world->family.id)
            continue;
        if (memcmp(keys[earlier].bytes, keys[count].bytes, 83U) != 0)
            continue;
        fprintf(stderr, "equivalent geometry: v%u family%u variant%u / v3 family%u variant%u\n",
                keys[earlier].version, keys[earlier].family, keys[earlier].variant,
                world->family.id, world->variant);
        NPC_CHECK(0, "rotation, mirror or translation siblings assigned to different families");
    }
}
/** @brief Collect immutable v1 geometry without consulting any actor outcome.
 * @param keys Mutable complete initial-geometry collection.
 * @param count Mutable actual sibling count. */
static void geometry_v1(geometry_key *keys, size_t *count) {
    for (uint32_t id = 0; id < 72U; ++id) {
        npc_family family;
        NPC_CHECK(npc_v1_family_get((npc_split)(id / 24U), id % 24U, &family), "v1 family missing");
        for (uint32_t variant = 0; variant < 48U; ++variant) {
            npc_world world;
            NPC_CHECK(npc_v1_world_init(&world, &family, variant), "v1 geometry missing");
            canonical_geometry(&world, keys[*count].bytes);
            keys[*count].family = id;
            keys[*count].variant = variant;
            keys[*count].version = 1U;
            ++*count;
        }
    }
}

/** @brief Collect all frozen v2 initial geometry without actor outcomes.
 * @param keys Mutable complete geometry collection.
 * @param count Mutable actual sibling count. */
static void geometry_v2(geometry_key *keys, size_t *count) {
    for (uint32_t id = 0; id < 72U; ++id) {
        npc_family family;
        NPC_CHECK(npc_v2_family_get((npc_split)(id / 24U), id % 24U, &family), "v2 family missing");
        for (uint32_t variant = 0; variant < 48U; ++variant) {
            npc_world world;
            NPC_CHECK(npc_v2_world_init(&world, &family, variant), "v2 geometry missing");
            canonical_geometry(&world, keys[*count].bytes);
            keys[*count].family = id;
            keys[*count].variant = variant;
            keys[*count].version = 2U;
            ++*count;
        }
    }
}

/** @brief Canonicalize every variant of one authored family.
 * @param keys Mutable complete initial-geometry collection.
 * @param count Mutable actual sibling count.
 * @param family Borrowed pinned family. */
static void geometry_family(geometry_key *keys, size_t *count, const npc_family *family) {
    for (uint32_t variant = 0; variant < NPC_VARIANTS_PER_FAMILY; ++variant) {
        npc_world world;
        NPC_CHECK(npc_world_init(&world, family, variant), "geometry initialization failed");
        geometry_admit(keys, *count, &world);
        ++*count;
    }
}

/** @brief Inspect all reserved initial layouts without consulting any audit outcome. */
static void test_geometry_splits(void) {
    geometry_key *keys = calloc(3U * 72U * 48U, sizeof(*keys));
    size_t count = 0;
    NPC_CHECK(keys != NULL, "geometry audit allocation failed");
    geometry_v1(keys, &count);
    geometry_v2(keys, &count);
    for (uint32_t id = 0; id < 72U; ++id) {
        npc_family family;
        NPC_CHECK(npc_family_get((npc_split)(id / 24U), id % 24U, &family),
                  "geometry family missing");
        geometry_family(keys, &count, &family);
    }
    NPC_CHECK(count == 3U * 72U * 48U, "geometry audit omitted siblings");
    free(keys);
}
/** @brief Audit one categorical state against the frozen vocabulary.
 * @param observation Borrowed visible information.
 * @param memory Borrowed updated history. */
static void check_categories(const npc_observation *observation, const npc_memory *memory) {
    uint32_t cards[NPC_FEATURE_COUNT];
    cgai_gameplay_state state;
    npc_cardinalities(cards);
    npc_encode(observation, memory, 1, &state);
    for (uint32_t field = 0; field < NPC_FEATURE_COUNT; ++field)
        NPC_CHECK(state.values[field] < cards[field], "category outside pinned vocabulary");
}
/** @brief Replay one independently requested controller decision.
 * @param pair Mutable independent host and memory pair.
 * @param reactive Nonzero disables remembered cue use. */
static void replay_step(replay_pair *pair, int reactive) {
    npc_observation observed, repeated;
    npc_world_observe(&pair->world, &observed);
    npc_world_observe(&pair->repeated, &repeated);
    NPC_CHECK(memcmp(&observed, &repeated, sizeof(observed)) == 0, "observation replay differs");
    npc_memory_observe(&pair->memory, &observed);
    npc_memory_observe(&pair->repeated_memory, &repeated);
    NPC_CHECK(memcmp(&pair->memory, &pair->repeated_memory, sizeof(pair->memory)) == 0,
              "memory replay differs");
    check_categories(&observed, &pair->memory);
    uint32_t action =
        reactive ? npc_reactive_action(&observed) : npc_teacher_action(&observed, &pair->memory);
    NPC_CHECK((observed.allowed_actions & (UINT64_C(1) << action)) != 0U,
              "reference proposed a visibly illegal action");
    NPC_CHECK(npc_world_step(&pair->world, action) == npc_world_step(&pair->repeated, action),
              "transition replay differs");
    NPC_CHECK(memcmp(&pair->world, &pair->repeated, sizeof(pair->world)) == 0,
              "world replay differs");
}
/** @brief Run an actor from its own observations through a complete episode.
 * @param family Borrowed pinned family.
 * @param variant Requested sibling.
 * @param reactive Nonzero selects the authored memoryless controller.
 * @return Authoritative terminal classification. */
static npc_terminal run_episode(const npc_family *family, uint32_t variant, int reactive) {
    replay_pair pair;
    NPC_CHECK(npc_world_init(&pair.world, family, variant), "episode initialization failed");
    NPC_CHECK(npc_world_init(&pair.repeated, family, variant), "replay initialization failed");
    npc_memory_reset(&pair.memory);
    npc_memory_reset(&pair.repeated_memory);
    while (pair.world.terminal == NPC_RUNNING)
        replay_step(&pair, reactive);
    NPC_CHECK(pair.world.executed_illegal == 0U, "host executed an illegal action");
    return pair.world.terminal;
}
/** @brief Reject decorative identities that duplicate another actual initial episode.
 * @param family Borrowed pinned family.
 * @param variant Requested sibling. */
static void check_unique_variant(const npc_family *family, uint32_t variant) {
    npc_world initial;
    NPC_CHECK(npc_world_init(&initial, family, variant), "missing variant");
    for (uint32_t earlier = 0; earlier < variant; ++earlier) {
        npc_world other;
        NPC_CHECK(npc_world_init(&other, family, earlier), "missing earlier variant");
        NPC_CHECK(memcmp(initial.cells, other.cells, sizeof(initial.cells)) != 0 ||
                      initial.x != other.x || initial.y != other.y ||
                      initial.item_x != other.item_x || initial.item_y != other.item_y ||
                      initial.exit_x != other.exit_x || initial.exit_y != other.exit_y ||
                      initial.cue_direction != other.cue_direction,
                  "a seed sibling duplicates another initial episode");
    }
}
/** @brief Verify train/development quality while keeping reserved audit outcomes unopened.
 * @param family Borrowed pinned family.
 * @return Reactive cue successes for this family, zero for other mechanics. */
static uint32_t check_family_variants(const npc_family *family) {
    uint32_t successes = 0U;
    for (uint32_t variant = 0; variant < NPC_VARIANTS_PER_FAMILY; ++variant) {
        check_unique_variant(family, variant);
        if (family->split == NPC_AUDIT)
            continue;
        NPC_CHECK(run_episode(family, variant, 0) == NPC_SUCCESS,
                  "observable reference failed an authored family");
        if (family->mechanic == NPC_CUE && run_episode(family, variant, 1) == NPC_SUCCESS)
            ++successes;
    }
    return successes;
}
/** @brief Verify split assignment and balanced mechanic counts.
 * @param split Requested authored split.
 * @return Reactive cue successes from unreserved outcomes. */
static uint32_t check_split(npc_split split) {
    uint32_t counts[3] = {0, 0, 0}, successes = 0;
    for (uint32_t index = 0; index < NPC_FAMILIES_PER_SPLIT; ++index) {
        npc_family family;
        NPC_CHECK(npc_family_get(split, index, &family), "missing family");
        NPC_CHECK(family.id == (uint32_t)split * NPC_FAMILIES_PER_SPLIT + index,
                  "split identity differs");
        NPC_CHECK(family.layout == 3U * (index / 3U) + (uint32_t)split,
                  "layout is not interleaved across splits");
        ++counts[family.mechanic];
        successes += check_family_variants(&family);
    }
    NPC_CHECK(counts[0] == 8U && counts[1] == 8U && counts[2] == 8U, "mechanic imbalance");
    return successes;
}
/** @brief Verify all initial variants without consulting any audit actor outcome. */
static void test_families(void) {
    uint32_t reactive = check_split(NPC_TRAIN) + check_split(NPC_DEV);
    NPC_CHECK(reactive == 8U * 2U * NPC_VARIANTS_PER_FAMILY / 2U,
              "paired cue tasks do not expose exactly half success");
    (void)check_split(NPC_AUDIT);
}
/** Coverage of all explicitly supported categorical symbols and teacher actions. */
typedef struct vocabulary_coverage {
    uint32_t fields[NPC_FEATURE_COUNT]; /**< Bitset of visited values for each feature. */
    uint32_t actions;                   /**< Bitset of verified reference action labels. */
} vocabulary_coverage;
/** @brief Admit one actually observed reference decision into categorical coverage.
 * @param coverage Mutable training-only coverage.
 * @param observation Borrowed current public information.
 * @param memory Borrowed observed history.
 * @param action Admitted observation-limited teacher action. */
static void coverage_observation(vocabulary_coverage *coverage, const npc_observation *observation,
                                 const npc_memory *memory, uint32_t action) {
    cgai_gameplay_state state;
    npc_encode(observation, memory, 1, &state);
    for (uint32_t field = 0; field < NPC_FEATURE_COUNT; ++field)
        coverage->fields[field] |= 1U << state.values[field];
    coverage->actions |= 1U << action;
}
/** @brief Collect categorical coverage from one complete verified training trajectory.
 * @param coverage Mutable training-only coverage.
 * @param family Borrowed training family.
 * @param variant Selected sibling. */
static void coverage_episode(vocabulary_coverage *coverage, const npc_family *family,
                             uint32_t variant) {
    npc_world world = {0};
    npc_memory memory;
    NPC_CHECK(family->split == NPC_TRAIN && npc_world_init(&world, family, variant),
              "coverage may use only valid training episodes");
    npc_memory_reset(&memory);
    while (world.terminal == NPC_RUNNING) {
        npc_observation observation;
        npc_world_observe(&world, &observation);
        npc_memory_observe(&memory, &observation);
        uint32_t action = npc_teacher_action(&observation, &memory);
        coverage_observation(coverage, &observation, &memory, action);
        (void)npc_world_step(&world, action);
    }
    NPC_CHECK(world.terminal == NPC_SUCCESS, "coverage trajectory was not verified successful");
}
/** @brief Require all declared non-reserved categorical symbols and every teacher label. */
static void test_vocabulary_coverage(void) {
    static const uint32_t expected[NPC_FEATURE_COUNT] = {
        31U, 31U, 31U, 31U, 32764U, 32764U, 3U, 31U, 31U, 127U, 31U, 255U, 15U, 3U, 255U, 7U};
    vocabulary_coverage coverage = {{0}, 0};
    for (uint32_t index = 0; index < 24U; ++index) {
        npc_family family;
        NPC_CHECK(npc_family_get(NPC_TRAIN, index, &family), "coverage family missing");
        for (uint32_t variant = 0; variant < 48U; ++variant)
            coverage_episode(&coverage, &family, variant);
    }
    for (uint32_t field = 0; field < NPC_FEATURE_COUNT; ++field)
        NPC_CHECK(coverage.fields[field] == expected[field],
                  "supported category lacks training coverage");
    NPC_CHECK(coverage.actions == 127U, "supported action lacks successful teacher coverage");
}
/** @brief Initialize the minimal opposite-cue vertical slice.
 * @param pair Writable independently reset cue pair. */
static void cue_pair_init(cue_pair *pair) {
    npc_family family;
    NPC_CHECK(npc_family_get(NPC_TRAIN, 2U, &family), "cue family missing");
    NPC_CHECK(npc_world_init(&pair->episodes.world, &family, 0U),
              "first cue initialization failed");
    NPC_CHECK(npc_world_init(&pair->episodes.repeated, &family, 4U),
              "second cue initialization failed");
    npc_memory_reset(&pair->episodes.memory);
    npc_memory_reset(&pair->episodes.repeated_memory);
}
/** @brief Request another shared-information action until the visible common landmark.
 * @param pair Mutable cue pair. */
static void cue_pair_approach(cue_pair *pair) {
    do {
        npc_world_observe(&pair->episodes.world, &pair->first);
        npc_world_observe(&pair->episodes.repeated, &pair->second);
        npc_memory_observe(&pair->episodes.memory, &pair->first);
        npc_memory_observe(&pair->episodes.repeated_memory, &pair->second);
        if (pair->first.stage == 1U)
            break;
        uint32_t first_action = npc_teacher_action(&pair->first, &pair->episodes.memory);
        uint32_t second_action = npc_teacher_action(&pair->second, &pair->episodes.repeated_memory);
        NPC_CHECK(first_action == second_action, "approach leaked hidden route truth");
        (void)npc_world_step(&pair->episodes.world, first_action);
        (void)npc_world_step(&pair->episodes.repeated, second_action);
    } while (pair->episodes.world.terminal == NPC_RUNNING &&
             pair->episodes.repeated.terminal == NPC_RUNNING);
}
/** @brief Verify equal current inputs with and without the remembered cue.
 * @param pair Borrowed pair at the common landmark. */
static void cue_pair_inputs(const cue_pair *pair) {
    cgai_gameplay_state first_state, second_state;
    NPC_CHECK(pair->first.stage == 1U && pair->second.stage == 1U, "junction was not reached");
    NPC_CHECK(memcmp(&pair->first, &pair->second, sizeof(pair->first)) == 0,
              "current observation or mask reveals private route truth");
    NPC_CHECK(pair->first.cue == 0U && pair->first.target_dx == 0 && pair->first.target_dy == 0,
              "cue or hidden destination remains visible");
    npc_encode(&pair->first, &pair->episodes.memory, 0, &first_state);
    npc_encode(&pair->second, &pair->episodes.repeated_memory, 0, &second_state);
    NPC_CHECK(memcmp(&first_state, &second_state, sizeof(first_state)) == 0,
              "history-disabled inputs reveal the cue");
    npc_encode(&pair->first, &pair->episodes.memory, 1, &first_state);
    npc_encode(&pair->second, &pair->episodes.repeated_memory, 1, &second_state);
    NPC_CHECK(first_state.values[8] != second_state.values[8], "observable history lost the cue");
}
/** @brief Establish cue necessity, wrong-branch consequence and independent reset. */
static void test_cue_pair(void) {
    cue_pair pair;
    cue_pair_init(&pair);
    cue_pair_approach(&pair);
    cue_pair_inputs(&pair);
    uint32_t first_action = npc_teacher_action(&pair.first, &pair.episodes.memory);
    uint32_t second_action = npc_teacher_action(&pair.second, &pair.episodes.repeated_memory);
    NPC_CHECK(first_action != second_action, "memory controller cannot distinguish the pair");
    NPC_CHECK(npc_world_step(&pair.episodes.world, second_action) == NPC_DIED &&
                  pair.episodes.world.terminal == NPC_DEATH,
              "wrong branch lacks its declared irreversible consequence");
    npc_memory_reset(&pair.episodes.memory);
    NPC_CHECK(pair.episodes.memory.cue == 0U && pair.episodes.memory.initialized == 0U,
              "reset leaked another NPC cue");
}

/** @brief Compare one pair of shared approach observations and advance once if necessary.
 * @param pair Mutable opposite-cue worlds with independent observable histories.
 * @return One at the common landmark, zero after advancing the shared approach. */
static int shared_cue_step(replay_pair *pair) {
    npc_observation first, second;
    npc_world_observe(&pair->world, &first);
    npc_world_observe(&pair->repeated, &second);
    npc_memory_observe(&pair->memory, &first);
    npc_memory_observe(&pair->repeated_memory, &second);
    NPC_CHECK(first.ticks == 0U || (first.cue == 0U && second.cue == 0U),
              "a vanished cue reappeared before route commitment");
    first.cue = second.cue = 0U;
    NPC_CHECK(memcmp(&first, &second, sizeof(first)) == 0,
              "pre-commit observation or mask leaks route truth");
    if (first.stage == 1U)
        return 1;
    uint32_t action = npc_teacher_action(&first, &pair->memory);
    NPC_CHECK(action == npc_teacher_action(&second, &pair->repeated_memory),
              "shared approach action differs");
    (void)npc_world_step(&pair->world, action);
    (void)npc_world_step(&pair->repeated, action);
    NPC_CHECK(pair->world.terminal == NPC_RUNNING && pair->repeated.terminal == NPC_RUNNING,
              "shared approach committed a branch before the landmark");
    return 0;
}

/** @brief Compare every shared approach observation except the initial cue value.
 * @param family Borrowed pinned cue family.
 * @param variant Requested right-cue sibling, paired with its opposite cue. */
static void check_cue_approach(const npc_family *family, uint32_t variant) {
    replay_pair pair;
    NPC_CHECK(npc_world_init(&pair.world, family, variant), "cue approach initialization failed");
    NPC_CHECK(npc_world_init(&pair.repeated, family, variant + 4U),
              "paired approach initialization failed");
    npc_memory_reset(&pair.memory);
    npc_memory_reset(&pair.repeated_memory);
    while (!shared_cue_step(&pair)) {
    }
}

/** @brief Compare all cue siblings through approach without opening any audit terminal outcome. */
static void test_all_cue_approaches(void) {
    for (uint32_t id = 2U; id < 72U; id += 3U) {
        npc_family family;
        NPC_CHECK(npc_family_get((npc_split)(id / 24U), id % 24U, &family),
                  "cue approach family missing");
        for (uint32_t variant = 0; variant < 48U; ++variant)
            if ((variant / 4U) % 2U == 0U)
                check_cue_approach(&family, variant);
    }
}

/** @brief Reject a wrong early entry into either neutral corridor before reaching the junction. */
static void test_early_cue_commitment(void) {
    npc_family family;
    npc_world world;
    npc_observation observation;
    NPC_CHECK(npc_family_get(NPC_TRAIN, 2U, &family), "early cue family missing");
    NPC_CHECK(npc_world_init(&world, &family, 24U), "early cue initialization failed");
    npc_world_observe(&world, &observation);
    NPC_CHECK(observation.stage == 0U &&
                  (observation.allowed_actions & (UINT64_C(1) << NPC_WEST)) != 0U,
              "neutral stem is not identically visible before commitment");
    NPC_CHECK(npc_world_step(&world, NPC_WEST) == NPC_DIED && world.terminal == NPC_DEATH,
              "an early stem entry bypassed the wrong-route consequence");
}
/** @brief Verify that duplicated visible observations cannot age memory twice.
 * @param observation Borrowed post-bump public information. */
static void check_memory_idempotence(const npc_observation *observation) {
    npc_memory memory, once;
    npc_memory_reset(&memory);
    npc_memory_observe(&memory, observation);
    once = memory;
    npc_memory_observe(&memory, observation);
    NPC_CHECK(memcmp(&memory, &once, sizeof(memory)) == 0, "duplicate update aged memory twice");
}
/** @brief Verify observable obstruction permissions and rejected malformed actions. */
static void test_obstruction(void) {
    npc_family family;
    npc_world world, unobstructed;
    npc_observation observation, other;
    NPC_CHECK(npc_family_get(NPC_TRAIN, 1U, &family), "hazard family missing");
    NPC_CHECK(npc_world_init(&world, &family, 0U), "hazard initialization failed");
    (void)npc_world_step(&world, NPC_FALLBACK);
    unobstructed = world;
    unobstructed.cells[world.y * NPC_GRID_SIDE + 2U] = NPC_FLOOR;
    npc_world_observe(&world, &observation);
    npc_world_observe(&unobstructed, &other);
    NPC_CHECK(memcmp(&observation, &other, sizeof(observation)) == 0, "unseen obstruction leaked");
    NPC_CHECK(npc_world_step(&world, NPC_EAST) == NPC_BLOCKED,
              "admitted obstruction did not block");
    npc_world_observe(&world, &observation);
    NPC_CHECK(observation.neighbors[1] == NPC_WALL, "bump did not reveal obstruction");
    check_memory_idempotence(&observation);
    NPC_CHECK(npc_world_step(&world, NPC_EAST) == NPC_INVALID && world.attempted_illegal == 1U,
              "forbidden movement bypassed validation");
    NPC_CHECK(npc_world_step(&world, UINT32_MAX) == NPC_INVALID && world.executed_illegal == 0U,
              "malformed action bypassed host validation");
}
/** @brief Require the two public fatal sides to induce opposite real bump-recovery actions. */
static void test_hazard_detour_sides(void) {
    npc_family family;
    NPC_CHECK(npc_family_get(NPC_TRAIN, 1U, &family), "hazard detour family missing");
    for (uint32_t variant = 0; variant <= 4U; variant += 4U) {
        npc_world world;
        npc_observation observation;
        npc_memory memory;
        NPC_CHECK(npc_world_init(&world, &family, variant), "hazard detour missing");
        npc_memory_reset(&memory);
        (void)npc_world_step(&world, NPC_FALLBACK);
        NPC_CHECK(npc_world_step(&world, NPC_EAST) == NPC_BLOCKED, "detour bypassed obstruction");
        npc_world_observe(&world, &observation);
        npc_memory_observe(&memory, &observation);
        NPC_CHECK(npc_teacher_action(&observation, &memory) ==
                      (uint32_t)(variant == 0U ? NPC_SOUTH : NPC_NORTH),
                  "visible opposite hazards did not alter the actual recovery route");
    }
}
/** @brief Reach the second visible hazard using only training/development host steps.
 * @param family Borrowed unreserved hazard family.
 * @param variant Unrotated opposite-side sibling.
 * @param world Writable host state at the second hazard.
 * @param memory Writable independently observed history. */
static void secondary_hazard_approach(const npc_family *family, uint32_t variant, npc_world *world,
                                      npc_memory *memory) {
    uint32_t barrier_x = 2U + (family->layout / 3U + family->layout % 3U) % 3U;
    NPC_CHECK(family->split != NPC_AUDIT && npc_world_init(world, family, variant),
              "secondary hazard test cannot open reserved outcomes");
    npc_memory_reset(memory);
    (void)npc_world_step(world, NPC_FALLBACK);
    NPC_CHECK(npc_world_step(world, NPC_EAST) == NPC_BLOCKED, "missing initial bump");
    uint32_t side = (uint32_t)(variant == 0U ? NPC_SOUTH : NPC_NORTH);
    NPC_CHECK(npc_world_step(world, side) == NPC_MOVED, "first safe side step failed");
    while (world->x + 1U < barrier_x)
        NPC_CHECK(npc_world_step(world, NPC_EAST) == NPC_MOVED,
                  "secondary hazard approach was blocked");
}
/** @brief Prove the additional fatal tile changes the actual observable recovery action.
 * @param family Borrowed unreserved hazard family.
 * @param variant Unrotated opposite-side sibling. */
static void check_secondary_hazard(const npc_family *family, uint32_t variant) {
    npc_world world, without_hazard;
    npc_memory memory;
    npc_observation observed, neutral;
    secondary_hazard_approach(family, variant, &world, &memory);
    npc_world_observe(&world, &observed);
    npc_memory_observe(&memory, &observed);
    uint32_t side = (uint32_t)(variant == 0U ? NPC_SOUTH : NPC_NORTH);
    NPC_CHECK(observed.neighbors[1] == NPC_DANGER && npc_teacher_action(&observed, &memory) == side,
              "second public hazard did not force a second safe side step");
    without_hazard = world;
    without_hazard.cells[world.y * NPC_GRID_SIDE + world.x + 1U] = NPC_FLOOR;
    npc_world_observe(&without_hazard, &neutral);
    NPC_CHECK(npc_teacher_action(&neutral, &memory) == NPC_EAST,
              "second hazard is decorative rather than an actual route change");
    NPC_CHECK(npc_world_step(&world, side) == NPC_MOVED && world.y == world.item_y,
              "second side step did not reach the forward safe row");
    npc_world_observe(&world, &observed);
    NPC_CHECK(observed.neighbors[1] == NPC_FLOOR,
              "second detour does not restore a traversable forward route");
}
/** @brief Check both actual recovery sides throughout the unreserved hazard families. */
static void test_secondary_hazards(void) {
    for (uint32_t id = 1U; id < 48U; id += 3U) {
        npc_family family;
        NPC_CHECK(npc_family_get((npc_split)(id / 24U), id % 24U, &family),
                  "secondary hazard family missing");
        check_secondary_hazard(&family, 0U);
        check_secondary_hazard(&family, 4U);
    }
}
/** @brief Inspect one split's initial hazard factors without advancing any world.
 * @param split Pinned initial-geometry partition.
 * @param factors Writable displacement, secondary-hazard and exit-displacement masks. */
static void initial_hazard_factors(npc_split split, uint32_t factors[4]) {
    for (uint32_t index = 1U; index < 24U; index += 3U) {
        npc_family family;
        NPC_CHECK(npc_family_get(split, index, &family), "initial hazard family missing");
        for (uint32_t variant = 0U; variant <= 4U; variant += 4U) {
            npc_world world;
            NPC_CHECK(npc_world_init(&world, &family, variant), "initial hazard geometry missing");
            int32_t displacement = (int32_t)world.item_y - (int32_t)world.y;
            uint32_t barrier_x = 2U + (family.layout / 3U + family.layout % 3U) % 3U;
            uint32_t safe_y = (uint32_t)((int32_t)world.y + displacement / 2);
            NPC_CHECK(world.cells[safe_y * NPC_GRID_SIDE + barrier_x] == NPC_DANGER,
                      "declared secondary hazard differs from actual geometry");
            factors[0] |= 1U << (world.item_x - world.x);
            factors[1] |= 1U << (uint32_t)(displacement + 8);
            factors[2] |= 1U << barrier_x;
            factors[3] |= 1U << (uint32_t)((int32_t)world.exit_y - (int32_t)world.item_y + 8);
        }
    }
}
/** @brief Require shared observable displacement and recovery-factor support across partitions. */
static void test_hazard_factor_support(void) {
    const uint32_t expected[4] = {112U, 1088U, 28U, 3808U};
    for (uint32_t split = 0U; split < 3U; ++split) {
        uint32_t factors[4] = {0U, 0U, 0U, 0U};
        initial_hazard_factors((npc_split)split, factors);
        NPC_CHECK(memcmp(factors, expected, sizeof(expected)) == 0,
                  "a reserved displacement or recovery factor lacks training support");
    }
}
/** @brief Require an actual item-route side-step before approaching the prerequisite. */
static void test_item_detour(void) {
    npc_family family;
    npc_world world;
    npc_observation observation;
    npc_memory memory;
    NPC_CHECK(npc_family_get(NPC_TRAIN, 0U, &family), "item detour family missing");
    NPC_CHECK(npc_world_init(&world, &family, 0U), "item detour missing");
    npc_memory_reset(&memory);
    (void)npc_world_step(&world, NPC_WAIT);
    npc_world_observe(&world, &observation);
    npc_memory_observe(&memory, &observation);
    NPC_CHECK(observation.neighbors[1] == NPC_WALL &&
                  npc_teacher_action(&observation, &memory) == NPC_SOUTH,
              "public barrier did not induce the declared item detour");
    NPC_CHECK(npc_world_step(&world, NPC_SOUTH) == NPC_MOVED, "first public item side step failed");
    npc_world_observe(&world, &observation);
    NPC_CHECK(observation.neighbors[1] == NPC_WALL &&
                  npc_teacher_action(&observation, &memory) == NPC_SOUTH,
              "second public barrier is decorative rather than another route change");
    NPC_CHECK(npc_world_step(&world, NPC_SOUTH) == NPC_MOVED,
              "second public item side step failed");
    npc_world_observe(&world, &observation);
    NPC_CHECK(observation.neighbors[1] == NPC_FLOOR,
              "two-cell item detour does not restore the forward route");
}
/** @brief Verify the item prerequisite remains under host authority. */
static void test_prerequisite(void) {
    npc_family family;
    npc_world world;
    NPC_CHECK(npc_family_get(NPC_TRAIN, 0U, &family), "item family missing");
    NPC_CHECK(npc_world_init(&world, &family, 0U), "item initialization failed");
    (void)npc_world_step(&world, NPC_WAIT);
    world.x = world.exit_x;
    world.y = world.exit_y;
    NPC_CHECK(npc_world_step(&world, NPC_INTERACT) == NPC_INVALID && world.terminal == NPC_RUNNING,
              "exit succeeded without the prerequisite item");
}
/** @brief Verify fallback cost, terminal immutability and failed initialization preservation. */
static void test_timeout(void) {
    npc_family family;
    npc_world world, unchanged;
    NPC_CHECK(npc_family_get(NPC_TRAIN, 0U, &family), "timeout family missing");
    NPC_CHECK(npc_world_init(&world, &family, 0U), "timeout initialization failed");
    for (uint32_t tick = 0; tick < NPC_MAX_TICKS; ++tick)
        (void)npc_world_step(&world, NPC_FALLBACK);
    NPC_CHECK(world.terminal == NPC_TIMEOUT && world.ticks == NPC_MAX_TICKS,
              "fallback escaped decision accounting");
    unchanged = world;
    (void)npc_world_step(&world, NPC_INTERACT);
    NPC_CHECK(memcmp(&world, &unchanged, sizeof(world)) == 0, "terminal transition mutated world");
    NPC_CHECK(!npc_world_init(&world, &family, NPC_VARIANTS_PER_FAMILY) &&
                  memcmp(&world, &unchanged, sizeof(world)) == 0,
              "invalid initialization mutated the caller world");
}
/** @brief Execute the simulator and corpus acceptance checks.
 * @return Zero after all checks pass. */
int main(void) {
    test_geometry_splits();
    test_all_cue_approaches();
    test_early_cue_commitment();
    test_cue_pair();
    test_obstruction();
    test_hazard_detour_sides();
    test_secondary_hazards();
    test_hazard_factor_support();
    test_item_detour();
    test_prerequisite();
    test_timeout();
    test_families();
    test_vocabulary_coverage();

    puts("V3 NPC world: 2304 train/dev teacher episodes, replay, 10368 initial geometry checks, "
         "observable recovery factors and cue privacy pass");
    return EXIT_SUCCESS;
}
