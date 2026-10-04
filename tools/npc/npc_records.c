/** @file npc_records.c @brief Verified teacher and initialized-policy recovery record generation.
 */
#include "npc_records.h"
#include "internal/error.h"
#include "npc_corpus.h"
#include "npc_teacher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** One bounded initialized-policy recovery trajectory, admitted only after completion. */
typedef struct npc_recovery_trace {
    cgai_gameplay_example
        examples[NPC_MAX_TICKS]; /**< Teacher labels on independently caused states. */
    npc_corpus_provenance provenance[NPC_MAX_TICKS]; /**< Complete episode and selected-step IDs. */
    size_t count;       /**< Teacher-recovery decisions, excluding unlabelled policy prefix. */
    npc_world terminal; /**< Complete authoritative result for exact trajectory replay. */
} npc_recovery_trace;

/** @brief Choose a seeded policy prefix action or record one recovery target.
 * @param session Borrowed initialized-policy adapter.
 * @param world Host state used only for visible extraction and provenance.
 * @param trace Writable bounded selected records.
 * @param action Writable selected action.
 * @return OK on complete choice, ERROR otherwise. */
static cgai_status recovery_decide(npc_policy_session *session, const npc_world *world,
                                   npc_recovery_trace *trace, uint32_t *action) {
    npc_observation observation;
    npc_world_observe(world, &observation);
    npc_memory_observe(&session->memory, &observation);
    if (world->ticks < 3U) {
        cgai_gameplay_result selected;
        if (!npc_policy_decide(session, &observation, 3U, &selected))
            return CGAI_STATUS_ERROR;
        *action = selected.output;
    } else {
        cgai_gameplay_example *example = &trace->examples[trace->count];
        const npc_corpus_provenance source = {world->family.id, world->variant, world->ticks};
        npc_encode(&observation, &session->memory, 1, &example->state);
        example->target = npc_teacher_action(&observation, &session->memory);
        trace->provenance[trace->count++] = source;
        *action = example->target;
    }
    return CGAI_STATUS_OK;
}

/** @brief Roll out a fixed initialized policy prefix followed by observation-limited recovery.
 * @param session Seeded immutable policy adapter, reset independently for this episode.
 * @param family Training family, restricted to item or hazard mechanics.
 * @param variant Family-local variant, zero or one.
 * @param trace Writable complete bounded trajectory, including unsuccessful terminals.
 * @return OK after terminal rollout, ERROR on malformed execution. */
static cgai_status recovery_rollout(npc_policy_session *session, const npc_family *family,
                                    uint32_t variant, npc_recovery_trace *trace) {
    npc_world world;
    if (!npc_world_init(&world, family, variant))
        return cgai_fail("could not initialize training-only recovery episode");
    npc_policy_session_reset(session);
    npc_recovery_trace generated = {0};
    while (world.terminal == NPC_RUNNING && world.ticks < NPC_MAX_TICKS) {
        uint32_t action = NPC_FALLBACK;
        if (!recovery_decide(session, &world, &generated, &action))
            return CGAI_STATUS_ERROR;
        (void)npc_world_step(&world, action);
    }
    if (world.terminal == NPC_RUNNING)
        return cgai_fail("recovery collection failed to classify a terminal");
    generated.terminal = world;
    *trace = generated;
    return CGAI_STATUS_OK;
}

/** @brief Append a complete and exactly replayed training-only recovery trajectory.
 * @param session Borrowed seeded policy adapter.
 * @param family Training-family provenance.
 * @param variant First or second pinned variant.
 * @param examples Writable canonical training records.
 * @param capacity Complete available record slots.
 * @param count Mutable actual record count.
 * @param provenance Optional parallel selected-step identifiers.
 * @return OK on admission or noncompletion rejection, ERROR otherwise. */
static cgai_status recovery_append(npc_policy_session *session, const npc_family *family,
                                   uint32_t variant, cgai_gameplay_example *examples,
                                   size_t capacity, size_t *count,
                                   npc_corpus_provenance *provenance) {
    npc_recovery_trace first;
    npc_recovery_trace replay;
    if (!recovery_rollout(session, family, variant, &first) ||
        !recovery_rollout(session, family, variant, &replay) ||
        memcmp(&first, &replay, sizeof(first)) != 0)
        return cgai_fail("initialized-policy recovery trajectory did not replay");
    if (first.terminal.terminal != NPC_SUCCESS || first.terminal.attempted_illegal != 0U ||
        first.terminal.executed_illegal != 0U)
        return CGAI_STATUS_OK;
    if (first.count > capacity - *count)
        return cgai_fail("verified NPC recovery records exceed the corpus cap");
    memcpy(examples + *count, first.examples, first.count * sizeof(first.examples[0]));
    if (provenance != NULL)
        memcpy(provenance + *count, first.provenance, first.count * sizeof(first.provenance[0]));
    *count += first.count;
    return CGAI_STATUS_OK;
}

/** @brief Traverse the frozen training-only item/hazard recovery schedule.
 * @param session Borrowed initialized policy adapter.
 * @param examples Writable canonical training records.
 * @param capacity Complete available record slots.
 * @param count Mutable actual record count.
 * @param provenance Optional parallel selected-step identifiers.
 * @return OK on complete bounded collection, ERROR otherwise. */
static cgai_status recovery_families(npc_policy_session *session, cgai_gameplay_example *examples,
                                     size_t capacity, size_t *count,
                                     npc_corpus_provenance *provenance) {
    for (uint32_t index = 0U; index < NPC_FAMILIES_PER_SPLIT; ++index) {
        npc_family family;
        if (!npc_family_get(NPC_TRAIN, index, &family))
            return cgai_fail("invalid training-only recovery family");
        if (family.mechanic == NPC_CUE)
            continue;
        for (uint32_t variant = 0U; variant < 2U; ++variant)
            if (!recovery_append(session, &family, variant, examples, capacity, count, provenance))
                return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

/** @brief Append only completed, exactly replayed recovery trajectories from training families.
 * @param examples Writable canonical records containing the verified base corpus.
 * @param capacity Complete available record slots.
 * @param count Mutable actual record count.
 * @param provenance Optional parallel selected-step identifiers.
 * @return OK on complete bounded collection, ERROR otherwise. */
static cgai_status collect_recovery(cgai_gameplay_example *examples, size_t capacity, size_t *count,
                                    npc_corpus_provenance *provenance) {
    const cgai_gameplay_config config = npc_policy_config();
    cgai_gameplay_model *initialized = cgai_gameplay_create(&config);
    npc_policy_session *session =
        initialized != NULL ? npc_policy_session_create(initialized, 1) : NULL;
    const cgai_status status =
        session != NULL ? recovery_families(session, examples, capacity, count, provenance)
                        : CGAI_STATUS_ERROR;
    npc_policy_session_destroy(session);
    cgai_gameplay_destroy(initialized);
    return status;
}

/** @brief Apply the preregistered four-field history mask to the same canonical records.
 * @param examples Writable canonical training updates.
 * @param count Actual complete-pass update count. */
static void history_disable(cgai_gameplay_example *examples, size_t count) {
    for (size_t index = 0U; index < count; ++index)
        for (size_t field = 8U; field < 12U; ++field)
            examples[index].state.values[field] = 0U;
}

/** @brief Generate the verified full-history canonical schedule and optional provenance.
 * @param examples Writable ordered records.
 * @param capacity Available record slots.
 * @param count Writable actual complete-pass count.
 * @param history Frozen history profile.
 * @param provenance Optional parallel selected-step source identifiers.
 * @return OK on complete verified publication, ERROR otherwise. */
static cgai_status records_generate(cgai_gameplay_example *examples, size_t capacity, size_t *count,
                                    int history, npc_corpus_provenance *provenance) {
    if (examples == NULL || count == NULL || capacity == 0U || capacity > NPC_RECORD_CAPACITY)
        return cgai_fail("invalid bounded NPC training corpus destination");
    size_t generated = 0U;
    if (!npc_corpus_generate_ex(examples, capacity, &generated, 1, provenance))
        return cgai_fail("verified base NPC corpus generation failed");
    if (!collect_recovery(examples, capacity, &generated, provenance))
        return CGAI_STATUS_ERROR;
    if (!npc_corpus_validate(examples, generated, 1))
        return cgai_fail("NPC generated recovery corpus failed the duplicate target audit");
    if (!history)
        history_disable(examples, generated);
    *count = generated;
    return CGAI_STATUS_OK;
}

/** @brief Generate one frozen corpus then apply the preregistered history ablation.
 * @param examples Writable ordered canonical records.
 * @param capacity Complete available record slots, at most8192.
 * @param count Writable actual complete-pass record count.
 * @param history Nonzero keeps history; zero masks fields8 through11.
 * @return OK on verified generation, ERROR otherwise. */
cgai_status npc_records_collect(cgai_gameplay_example *examples, size_t capacity, size_t *count,
                                int history) {
    return records_generate(examples, capacity, count, history, NULL);
}

/** @brief Write one deterministic selected record with all16 categorical fields.
 * @param stream Borrowed writable corpus stream.
 * @param index Canonical ordered record index.
 * @param example Borrowed complete record.
 * @param source Borrowed exact episode and selected-step source IDs.
 * @return Nonzero on complete formatted write. */
static int corpus_record_write(FILE *stream, size_t index, const cgai_gameplay_example *example,
                               const npc_corpus_provenance *source) {
    int valid = fprintf(stream, "example\t%zu\t%u\t%u\t%u\t1\t%u\t%u", index, source->family,
                        source->variant, source->step, example->task, example->target) >= 0;
    for (size_t field = 0U; field < NPC_POLICY_FIELDS && valid; ++field)
        valid = fprintf(stream, "\t%u", example->state.values[field]) >= 0;
    return valid && fputc('\n', stream) != EOF;
}

/** Owned local corpus emission buffers, excluded from deployed inference ownership. */
typedef struct npc_corpus_buffers {
    cgai_gameplay_example examples[NPC_RECORD_CAPACITY];   /**< Canonical ordered updates. */
    npc_corpus_provenance provenance[NPC_RECORD_CAPACITY]; /**< Parallel exact source IDs. */
    size_t count; /**< Actual successful full-pass count. */
} npc_corpus_buffers;

/** @brief Emit metadata and every canonical selected record to a prepared stream.
 * @param stream Borrowed writable generated-data stream.
 * @param buffers Borrowed complete canonical schedule and source IDs.
 * @param history Frozen encoding profile.
 * @return Nonzero on complete formatted writes. */
static int corpus_write(FILE *stream, const npc_corpus_buffers *buffers, int history) {
    int valid = fprintf(stream,
                        "schema\tnpc-corpus-v1\nprofile\t%s\nrecords\t%zu\n"
                        "base_schedule\ttrain-24families-variants((familyindex/3)%%6)*8+0..7-"
                        "teacher-allsteps\nrecovery_schedule\tinitialized-seed42-itemhazard-"
                        "variants0..1-3policydecisions-then-verified-teacher-completion\n"
                        "record_columns\tindex,family,variant,step,verified_completion,task,target,"
                        "f0..f15\n",
                        history ? "history" : "memoryless", buffers->count) >= 0;
    for (size_t index = 0U; index < buffers->count && valid; ++index)
        valid = corpus_record_write(stream, index, &buffers->examples[index],
                                    &buffers->provenance[index]);
    return valid;
}

/** @brief Complete one generated corpus write and publish its actual update count.
 * @param stream Owned writable stream, always closed by this function.
 * @param buffers Borrowed complete ordered corpus and selected-step identifiers.
 * @param history Frozen encoding profile.
 * @return OK on complete write, close and count publication, ERROR otherwise. */
static cgai_status corpus_publish(FILE *stream, const npc_corpus_buffers *buffers, int history) {
    const int written = corpus_write(stream, buffers, history);
    const int closed = fclose(stream) == 0;
    return written && closed && printf("corpus_records\t%zu\n", buffers->count) >= 0
               ? CGAI_STATUS_OK
               : cgai_fail("incomplete generated NPC corpus");
}

/** @brief Emit the exact generated record order and bounded provenance schedule.
 * @param path Trusted destination under ignored build storage.
 * @param history Frozen encoding profile.
 * @return OK on complete generation and write, ERROR otherwise. */
cgai_status npc_records_write(const char *path, int history) {
    npc_corpus_buffers *buffers = calloc(1U, sizeof(*buffers));
    if (buffers == NULL)
        return cgai_fail("could not allocate bounded NPC corpus generation storage");
    cgai_status status = records_generate(buffers->examples, NPC_RECORD_CAPACITY, &buffers->count,
                                          history, buffers->provenance);
    FILE *stream = status ? fopen(path, "wb") : NULL;
    if (status && stream == NULL)
        status = cgai_fail("could not open canonical generated NPC corpus");
    if (stream != NULL)
        status = corpus_publish(stream, buffers, history);
    free(buffers);
    return status;
}
