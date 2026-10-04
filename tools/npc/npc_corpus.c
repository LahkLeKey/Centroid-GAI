/** @file npc_corpus.c @brief Deterministically replayed teacher training data. */
#include "npc_corpus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** Internal selected-step record; no provenance is visible during inference. */
typedef struct npc_record {
    cgai_gameplay_example example;    /**< Frozen state and target. */
    npc_corpus_provenance provenance; /**< Training-only episode and step identity. */
} npc_record;
/** Single bounded temporary owner used only during corpus generation. */
typedef struct npc_corpus_workspace {
    npc_record records[NPC_CORPUS_CAPACITY];             /**< Verified canonical provenance. */
    cgai_gameplay_example examples[NPC_CORPUS_CAPACITY]; /**< Validated selected examples. */
    size_t count;                                        /**< Successful actual record count. */
} npc_corpus_workspace;
/** @brief Validate one complete categorical record.
 * @param example Borrowed selected example.
 * @param cards Borrowed frozen cardinalities.
 * @return One for a supported record. */
static int npc_record_valid(const cgai_gameplay_example *example, const uint32_t *cards) {
    if (example->task != 0U || example->target >= 7U)
        return 0;
    for (size_t field = 0; field < NPC_FEATURE_COUNT; ++field)
        if (example->state.values[field] >= cards[field])
            return 0;
    return 1;
}
/** @brief Detect one full-history target conflict with earlier records.
 * @param examples Borrowed ordered corpus.
 * @param index Record to compare against its earlier peers.
 * @return One if a conflicting target exists. */
static int npc_record_conflicts(const cgai_gameplay_example *examples, size_t index) {
    for (size_t earlier = 0; earlier < index; ++earlier)
        if (memcmp(&examples[index].state, &examples[earlier].state,
                   sizeof(examples[index].state)) == 0 &&
            examples[index].target != examples[earlier].target)
            return 1;
    return 0;
}
int npc_corpus_validate(const cgai_gameplay_example *examples, size_t count, int history) {
    uint32_t cards[NPC_FEATURE_COUNT];
    if (examples == NULL || count == 0U || count > NPC_CORPUS_CAPACITY)
        return 0;
    npc_cardinalities(cards);
    for (size_t index = 0; index < count; ++index)
        if (!npc_record_valid(&examples[index], cards) ||
            (history != 0 && npc_record_conflicts(examples, index)))
            return 0;
    return 1;
}
/** @brief Label one observed state before executing the reference action.
 * @param world Borrowed initialized host world.
 * @param memory Mutable independent observable history.
 * @param record Writable selected categorical state and provenance.
 * @return Executable teacher action. */
static uint32_t npc_teacher_record(const npc_world *world, npc_memory *memory, npc_record *record) {
    npc_observation observation;
    npc_world_observe(world, &observation);
    npc_memory_observe(memory, &observation);
    memset(record, 0, sizeof(*record));
    npc_encode(&observation, memory, 1, &record->example.state);
    record->example.target = npc_teacher_action(&observation, memory);
    record->provenance.family = world->family.id;
    record->provenance.variant = world->variant;
    record->provenance.step = world->ticks;
    return record->example.target;
}
/** @brief Produce a complete teacher trajectory before any record is admitted.
 * @param family Borrowed pinned training family.
 * @param variant Selected family-local sibling.
 * @param episode Writable storage for at most64 decisions.
 * @param count Writable exact decision count.
 * @param terminal Writable complete terminal world.
 * @return One only for legal verified goal completion. */
static int npc_teacher_rollout(const npc_family *family, uint32_t variant, npc_record *episode,
                               size_t *count, npc_world *terminal) {
    npc_world world;
    npc_memory memory;
    size_t length = 0;
    if (!npc_world_init(&world, family, variant))
        return 0;
    npc_memory_reset(&memory);
    while (world.terminal == NPC_RUNNING) {
        if (length >= NPC_MAX_TICKS)
            return 0;
        uint32_t action = npc_teacher_record(&world, &memory, &episode[length++]);
        (void)npc_world_step(&world, action);
    }
    *count = length;
    *terminal = world;
    return world.terminal == NPC_SUCCESS && world.attempted_illegal == 0U &&
           world.executed_illegal == 0U;
}
/** @brief Independently replay a proposed trajectory and compare every observed record.
 * @param terminal Borrowed proposed complete terminal world.
 * @param episode Borrowed proposed selected steps.
 * @param count Proposed decision count.
 * @return One if every record and complete terminal world replay exactly. */
static int npc_teacher_replay(const npc_world *terminal, const npc_record *episode, size_t count) {
    npc_world replay;
    npc_memory memory;
    if (!npc_world_init(&replay, &terminal->family, terminal->variant))
        return 0;
    npc_memory_reset(&memory);
    for (size_t step = 0; step < count; ++step) {
        npc_record repeated;
        uint32_t action = npc_teacher_record(&replay, &memory, &repeated);
        if (memcmp(&repeated, &episode[step], sizeof(repeated)) != 0)
            return 0;
        (void)npc_world_step(&replay, action);
    }
    return memcmp(terminal, &replay, sizeof(replay)) == 0;
}
/** @brief Append one complete and exactly replayed teacher episode.
 * @param workspace Mutable bounded canonical owner.
 * @param family Borrowed pinned training family.
 * @param variant Selected sibling.
 * @return One on complete validated append. */
static int npc_collect_episode(npc_corpus_workspace *workspace, const npc_family *family,
                               uint32_t variant) {
    npc_record episode[NPC_MAX_TICKS];
    npc_world terminal;
    size_t count = 0;
    if (!npc_teacher_rollout(family, variant, episode, &count, &terminal) ||
        !npc_teacher_replay(&terminal, episode, count) ||
        workspace->count + count > NPC_CORPUS_CAPACITY)
        return 0;
    memcpy(&workspace->records[workspace->count], episode, count * sizeof(episode[0]));
    workspace->count += count;
    return 1;
}
/** @brief Collect one complete pinned training family's selected siblings.
 * @param workspace Mutable bounded canonical owner.
 * @param index Training-family index.
 * @return One after every selected teacher trajectory is verified. */
static int npc_collect_family(npc_corpus_workspace *workspace, uint32_t index) {
    npc_family family;
    if (!npc_family_get(NPC_TRAIN, index, &family))
        return 0;
    for (uint32_t sibling = 0; sibling < NPC_CORPUS_VARIANTS; ++sibling) {
        uint32_t variant = (index / 3U % 6U) * 8U + sibling;
        if (!npc_collect_episode(workspace, &family, variant))
            return 0;
    }
    return 1;
}

/** @brief Generate the complete preregistered train-only selected sibling schedule.
 * @param workspace Writable bounded canonical owner.
 * @return One for verified generation and full-history duplicate audit. */
static int npc_collect(npc_corpus_workspace *workspace) {
    for (uint32_t index = 0; index < NPC_FAMILIES_PER_SPLIT; ++index)
        if (!npc_collect_family(workspace, index))
            return 0;
    for (size_t index = 0; index < workspace->count; ++index)
        workspace->examples[index] = workspace->records[index].example;
    return npc_corpus_validate(workspace->examples, workspace->count, 1);
}
/** @brief Mask only four history fields after full-history teacher verification.
 * @param examples Mutable canonical examples.
 * @param count Actual selected record count. */
static void npc_disable_history(cgai_gameplay_example *examples, size_t count) {
    for (size_t index = 0; index < count; ++index)
        for (size_t field = 8; field <= 11; ++field)
            examples[index].state.values[field] = 0U;
}
/** @brief Publish exact records and optional parallel source identities.
 * @param workspace Borrowed validated canonical owner.
 * @param examples Writable destination records.
 * @param provenance Optional writable parallel identities. */
static void npc_publish_corpus(const npc_corpus_workspace *workspace,
                               cgai_gameplay_example *examples, npc_corpus_provenance *provenance) {
    memcpy(examples, workspace->examples, workspace->count * sizeof(*examples));
    if (provenance != NULL)
        for (size_t index = 0; index < workspace->count; ++index)
            provenance[index] = workspace->records[index].provenance;
}
cgai_status npc_corpus_generate_ex(cgai_gameplay_example *examples, size_t capacity, size_t *count,
                                   int history, npc_corpus_provenance *provenance) {
    if (examples == NULL || count == NULL || capacity == 0U || capacity > NPC_CORPUS_CAPACITY)
        return CGAI_STATUS_ERROR;
    npc_corpus_workspace *workspace = calloc(1, sizeof(*workspace));
    if (workspace == NULL)
        return CGAI_STATUS_ERROR;
    int valid = npc_collect(workspace) && workspace->count <= capacity;
    if (valid) {
        if (history == 0)
            npc_disable_history(workspace->examples, workspace->count);
        npc_publish_corpus(workspace, examples, provenance);
        *count = workspace->count;
    }
    free(workspace);
    return valid ? CGAI_STATUS_OK : CGAI_STATUS_ERROR;
}
cgai_status npc_corpus_generate(cgai_gameplay_example *examples, size_t capacity, size_t *count,
                                int history) {
    return npc_corpus_generate_ex(examples, capacity, count, history, NULL);
}
/** @brief Write the explicit selected-step and feature header.
 * @param file Borrowed writable binary stream.
 * @return One after complete write. */
static int npc_corpus_header(FILE *file) {
    if (fprintf(file, "family\tvariant\tstep\tverified_completion\ttarget\thistory") < 0)
        return 0;
    for (size_t field = 0; field < NPC_FEATURE_COUNT; ++field)
        if (fprintf(file, "\tf%zu", field) < 0)
            return 0;
    return fputc('\n', file) != EOF;
}
/** @brief Write one provenance-bound categorical example.
 * @param file Borrowed binary stream.
 * @param record Borrowed verified selected step.
 * @param history Nonzero keeps history; zero emits the same history ablation.
 * @return One after complete write. */
static int npc_corpus_row(FILE *file, const npc_record *record, int history) {
    if (fprintf(file, "%u\t%u\t%u\t1\t%u\t%d", record->provenance.family,
                record->provenance.variant, record->provenance.step, record->example.target,
                history != 0) < 0)
        return 0;
    for (size_t field = 0; field < NPC_FEATURE_COUNT; ++field) {
        uint32_t value =
            history == 0 && field >= 8U && field <= 11U ? 0U : record->example.state.values[field];
        if (fprintf(file, "\t%u", value) < 0)
            return 0;
    }
    return fputc('\n', file) != EOF;
}
/** @brief Write and close a complete verified corpus.
 * @param path Trusted local destination.
 * @param workspace Borrowed validated canonical owner.
 * @param history Requested observation ablation.
 * @return One only for complete write and close. */
static int npc_corpus_stream(const char *path, const npc_corpus_workspace *workspace, int history) {
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    int valid = npc_corpus_header(file);
    for (size_t index = 0; valid && index < workspace->count; ++index)
        valid = npc_corpus_row(file, &workspace->records[index], history);
    int closed = fclose(file) == 0;
    return valid && closed;
}
cgai_status npc_corpus_write(const char *path, int history) {
    if (path == NULL)
        return CGAI_STATUS_ERROR;
    npc_corpus_workspace *workspace = calloc(1, sizeof(*workspace));
    if (workspace == NULL)
        return CGAI_STATUS_ERROR;
    int valid = npc_collect(workspace) && npc_corpus_stream(path, workspace, history);
    free(workspace);
    return valid ? CGAI_STATUS_OK : CGAI_STATUS_ERROR;
}
