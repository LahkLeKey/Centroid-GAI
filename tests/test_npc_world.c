/** @file test_npc_world.c @brief Closed-loop episode, hidden-state and corpus invariants. */
#include "npc_corpus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
/** Bounded corpus test owner. */
typedef struct corpus_pair {
    cgai_gameplay_example first[NPC_CORPUS_CAPACITY];  /**< Full-history records. */
    cgai_gameplay_example second[NPC_CORPUS_CAPACITY]; /**< Repeated or ablated records. */
    npc_corpus_provenance
        provenance[NPC_CORPUS_CAPACITY]; /**< Parallel selected-step identities. */
    size_t first_count;                  /**< Actual original count. */
    size_t second_count;                 /**< Actual repeated count. */
} corpus_pair;

/** Translation-normalized, rotation- and mirror-canonical initial geometry. */
typedef struct geometry_key {
    char bytes[83];   /**< Eighty-one neutral cells, transformed cue and mechanic. */
    uint32_t family;  /**< Provenance excluded from the canonical bytes. */
    uint32_t variant; /**< Sibling provenance excluded from the canonical bytes. */
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
    for (size_t earlier = 0; earlier < count; ++earlier) {
        if (keys[earlier].family == world->family.id)
            continue;
        if (memcmp(keys[earlier].bytes, keys[count].bytes, 83U) != 0)
            continue;
        fprintf(stderr, "equivalent geometry: family%u variant%u / family%u variant%u\n",
                keys[earlier].family, keys[earlier].variant, world->family.id, world->variant);
        NPC_CHECK(0, "rotation, mirror or translation siblings assigned to different families");
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
    geometry_key *keys = calloc(72U * 48U, sizeof(*keys));
    size_t count = 0;
    NPC_CHECK(keys != NULL, "geometry audit allocation failed");
    for (uint32_t id = 0; id < 72U; ++id) {
        npc_family family;
        NPC_CHECK(npc_family_get((npc_split)(id / 24U), id % 24U, &family),
                  "geometry family missing");
        geometry_family(keys, &count, &family);
    }
    NPC_CHECK(count == 72U * 48U, "geometry audit omitted siblings");
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
        NPC_CHECK(family.layout == (uint32_t)split * 8U + index / 3U, "layout crossed split");
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
/** @brief Generate canonical records twice and validate their action and source coverage.
 * @param corpus Mutable bounded test owner. */
static void check_canonical_corpus(corpus_pair *corpus) {
    uint32_t labels = 0U;
    NPC_CHECK(npc_corpus_generate_ex(corpus->first, NPC_CORPUS_CAPACITY, &corpus->first_count, 1,
                                     corpus->provenance) == CGAI_STATUS_OK,
              "full-history corpus verification failed");
    NPC_CHECK(npc_corpus_generate(corpus->second, NPC_CORPUS_CAPACITY, &corpus->second_count, 1) ==
                      CGAI_STATUS_OK &&
                  corpus->first_count == corpus->second_count &&
                  memcmp(corpus->first, corpus->second,
                         corpus->first_count * sizeof(corpus->first[0])) == 0,
              "canonical generation differs across repetitions");
    NPC_CHECK(npc_corpus_validate(corpus->first, corpus->first_count, 1), "duplicate audit failed");
    for (size_t index = 0; index < corpus->first_count; ++index) {
        labels |= 1U << corpus->first[index].target;
        NPC_CHECK(corpus->provenance[index].family < NPC_FAMILIES_PER_SPLIT &&
                      corpus->provenance[index].variant / 8U ==
                          (corpus->provenance[index].family / 3U) % 6U &&
                      corpus->provenance[index].step < NPC_MAX_TICKS,
                  "selected provenance crossed a split");
    }
    NPC_CHECK(labels == 127U, "one declared action label lacks training coverage");
}
/** @brief Verify history ablation retains record ordering and non-history information.
 * @param corpus Mutable bounded test owner. */
static void check_history_ablation(corpus_pair *corpus) {
    NPC_CHECK(npc_corpus_generate(corpus->second, NPC_CORPUS_CAPACITY, &corpus->second_count, 0) ==
                      CGAI_STATUS_OK &&
                  corpus->first_count == corpus->second_count,
              "history ablation changed the training budget");
    for (size_t index = 0; index < corpus->first_count; ++index) {
        NPC_CHECK(corpus->first[index].target == corpus->second[index].target,
                  "history ablation changed target ordering");
        for (size_t field = 0; field < NPC_FEATURE_COUNT; ++field)
            NPC_CHECK(
                corpus->second[index].state.values[field] ==
                    (field >= 8U && field <= 11U ? 0U : corpus->first[index].state.values[field]),
                "history ablation changed a non-history feature");
    }
}
/** @brief Reject undeclared conflicting targets and out-of-vocabulary inputs.
 * @param corpus Mutable bounded test owner. */
static void check_corpus_rejections(corpus_pair *corpus) {
    corpus->second[0] = corpus->first[0];
    corpus->second[1] = corpus->first[0];
    corpus->second[1].target = (corpus->second[0].target + 1U) % 7U;
    NPC_CHECK(!npc_corpus_validate(corpus->second, 2U, 1), "conflicting targets admitted");
    NPC_CHECK(npc_corpus_validate(corpus->second, 2U, 0),
              "declared memoryless ambiguity forbidden");
    corpus->second[1].state.values[4] = 17U;
    NPC_CHECK(!npc_corpus_validate(corpus->second, 2U, 0), "out-of-vocabulary input admitted");
}
/** @brief Verify selected records, history ablation and duplicate rejection. */
static void test_corpus(void) {
    corpus_pair *corpus = calloc(1, sizeof(*corpus));
    NPC_CHECK(corpus != NULL, "corpus allocation failed");
    check_canonical_corpus(corpus);
    check_history_ablation(corpus);
    check_corpus_rejections(corpus);
    free(corpus);
}
/** @brief Execute the simulator and corpus acceptance checks.
 * @return Zero after all checks pass. */
int main(void) {
    test_geometry_splits();
    test_all_cue_approaches();
    test_early_cue_commitment();
    test_cue_pair();
    test_obstruction();
    test_prerequisite();
    test_timeout();
    test_families();
    test_corpus();
    puts("NPC world: 2304 train/dev teacher episodes, replay, hidden-state masks and corpus pass");
    return EXIT_SUCCESS;
}
