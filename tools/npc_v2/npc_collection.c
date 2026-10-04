/** @file npc_collection.c @brief Successful exact-replayed closed-loop correction collection. */
#include "npc_collection.h"
#include "internal/error.h"
#include "npc_corpus.h"
#include "npc_teacher.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Canonical integer record columns; full-history labels and actual actions are separate. */
static const char columns[] =
    "record_columns\tindex,round,family,variant,step,phase,verified_completion,task,target,"
    "executed,f0..f15\n";
/** One temporary complete trajectory, never partially admitted. */
typedef struct npc_trace {
    cgai_gameplay_example examples[NPC_MAX_TICKS]; /**< Observed teacher correction labels. */
    npc_collection_source sources[NPC_MAX_TICKS];  /**< Exact execution provenance. */
    npc_world terminal;                            /**< Complete authoritative replay result. */
    size_t count;                                  /**< Actual complete decision count. */
    int guarded; /**< Training-only correction before a proposed fatal transition. */
} npc_trace;
/** Compact actual corpus coverage, excluded from inference ownership. */
typedef struct npc_coverage {
    size_t policy_records;      /**< Labelled snapshot-caused prefix observations. */
    size_t recovery_records;    /**< Labelled teacher recovery observations. */
    size_t blocked_records;     /**< Records after actually observed obstruction outcomes. */
    size_t hazard_records;      /**< Records whose visible neighbors include danger. */
    size_t cue_history_records; /**< Records using an observed cue after it disappears. */
    uint64_t rotation_mask;     /**< Four observed variant rotations. */
    uint64_t target_mask;       /**< Seven teacher action IDs represented. */
    uint64_t family_mask;       /**< Training-only complete family identities represented. */
    uint64_t variant_mask;      /**< Forty-eight family-local sibling IDs represented. */
    uint64_t vocabulary[NPC_FEATURE_COUNT]; /**< Observed values per categorical field. */
} npc_coverage;
/** Bounded validation-only owner for complete authoritative trajectory replay. */
typedef struct npc_replay_state {
    npc_world world;      /**< Current independently reconstructed authoritative world. */
    npc_memory memory;    /**< Current independently reconstructed observable history. */
    size_t base_count;    /**< Exact canonical base siblings already started. */
    size_t policy_count;  /**< Ordered unique policy siblings already started. */
    uint32_t last_policy; /**< Most recently admitted policy sibling position. */
    uint32_t phase;       /**< Teacher base0, snapshot prefix1 or teacher recovery2. */
    size_t guarded_count; /**< Trajectories corrected before the maximum policy prefix. */
} npc_replay_state;

/** @brief Correct a fatal proposal only in the training host's verification fork.
 * @param session Borrowed immutable snapshot scratch.
 * @param world Borrowed host state, never passed to the policy.
 * @param observation Borrowed visible policy input.
 * @param source Writable actual action and monotonic phase.
 * @return Nonzero after proposal selection and speculative host verification. */
static int prefix_proposal(npc_policy_session *session, const npc_world *world,
                           const npc_observation *observation, npc_collection_source *source) {
    cgai_gameplay_result selected;
    if (!npc_policy_decide(session, observation, 3U, &selected))
        return 0;
    npc_world fork = *world;
    (void)npc_world_step(&fork, selected.output);
    if (fork.terminal == NPC_DEATH)
        source->phase = 2U;
    else
        source->executed = selected.output;
    return 1;
}

/** @brief Record an observable teacher label and choose the actual frozen-policy action.
 * @param session Borrowed training-only scratch and observed history.
 * @param world Borrowed authoritative host state.
 * @param trace Writable bounded trajectory.
 * @param round Frozen collection round.
 * @param prefix Actual snapshot-policy prefix, zero for teacher base.
 * @return Nonzero on complete label and action selection. */
static int trace_record(npc_policy_session *session, const npc_world *world, npc_trace *trace,
                        uint32_t round, uint32_t prefix) {
    npc_observation observation;
    npc_world_observe(world, &observation);
    npc_memory_observe(&session->memory, &observation);
    cgai_gameplay_example *example = &trace->examples[trace->count];
    npc_collection_source *source = &trace->sources[trace->count];
    npc_encode(&observation, &session->memory, 1, &example->state);
    example->target = npc_teacher_action(&observation, &session->memory);
    *source = (npc_collection_source){round,
                                      world->family.id,
                                      world->variant,
                                      world->ticks,
                                      prefix == 0U            ? 0U
                                      : world->ticks < prefix ? 1U
                                                              : 2U,
                                      example->target};
    if (trace->count != 0U && trace->sources[trace->count - 1U].phase == 2U)
        source->phase = 2U;
    if (source->phase == 1U) {
        if (!prefix_proposal(session, world, &observation, source))
            return 0;
        trace->guarded |= source->phase == 2U;
    }
    ++trace->count;
    return 1;
}

/** @brief Run one bounded complete snapshot-prefix and teacher-correction trajectory.
 * @param session Borrowed frozen model scratch, reset for this episode.
 * @param family Training-only family.
 * @param variant Frozen family-local sibling.
 * @param collection Borrowed frozen round metadata.
 * @param prefix Actual prefix, zero for base demonstrations.
 * @param trace Writable complete zero-initialized trajectory.
 * @return Nonzero after a classified authoritative terminal. */
static int trace_run(npc_policy_session *session, const npc_family *family, uint32_t variant,
                     const npc_collection *collection, uint32_t prefix, npc_trace *trace) {
    npc_world world;
    memset(trace, 0, sizeof(*trace));
    if (!npc_world_init(&world, family, variant))
        return 0;
    npc_policy_session_reset(session);
    while (world.terminal == NPC_RUNNING && trace->count < NPC_MAX_TICKS) {
        if (!trace_record(session, &world, trace, collection->round, prefix))
            return 0;
        (void)npc_world_step(&world, trace->sources[trace->count - 1U].executed);
    }
    trace->terminal = world;
    return world.terminal != NPC_RUNNING;
}

/** @brief Admit only a successful complete trajectory identical to an independent rerun.
 * @param collection Mutable bounded corpus owner.
 * @param session Borrowed immutable snapshot scratch.
 * @param family Training-only family.
 * @param variant Frozen family-local sibling.
 * @param prefix Actual snapshot prefix, zero for teacher base.
 * @return Nonzero after verified admission or counted policy rejection. */
static int trace_append(npc_collection *collection, npc_policy_session *session,
                        const npc_family *family, uint32_t variant, uint32_t prefix) {
    npc_trace first;
    npc_trace repeated;
    if (!trace_run(session, family, variant, collection, prefix, &first) ||
        !trace_run(session, family, variant, collection, prefix, &repeated) ||
        memcmp(&first, &repeated, sizeof(first)) != 0)
        return 0;
    if (first.terminal.terminal != NPC_SUCCESS || first.terminal.attempted_illegal != 0U ||
        first.terminal.executed_illegal != 0U) {
        ++collection->rejected_episodes;
        return prefix != 0U;
    }
    if (first.count > NPC_TRAINING_LIMIT - collection->count)
        return 0;
    memcpy(collection->examples + collection->count, first.examples,
           first.count * sizeof(first.examples[0]));
    memcpy(collection->sources + collection->count, first.sources,
           first.count * sizeof(first.sources[0]));
    collection->count += first.count;
    collection->base_episodes += prefix == 0U;
    collection->policy_episodes += prefix != 0U;
    collection->guarded_episodes += first.guarded != 0;
    return 1;
}

/** @brief Traverse the complete frozen eight-variant block for every training family.
 * @param collection Mutable bounded corpus owner.
 * @param session Borrowed immutable snapshot scratch.
 * @param prefix Actual prefix, zero for all base demonstrations.
 * @return Nonzero after all complete-family selected siblings are checked. */
static int collect_block(npc_collection *collection, npc_policy_session *session, uint32_t prefix) {
    for (uint32_t index = 0U; index < NPC_FAMILIES_PER_SPLIT; ++index) {
        npc_family family;
        if (!npc_family_get(NPC_TRAIN, index, &family))
            return 0;
        for (uint32_t sibling = 0U; sibling < 8U; ++sibling)
            if (!trace_append(collection, session, &family, collection->round * 8U + sibling,
                              prefix))
                return 0;
    }
    return 1;
}

/** @brief Collect a training-only owner using frozen optimizer-resident weights.
 * @param model Borrowed immutable checkpoint, never updated during collection.
 * @param round Frozen collection round.
 * @param prefix Frozen snapshot prefix.
 * @return Owned complete verified records, or NULL. */
static npc_collection *collection_generate(const cgai_gameplay_model *model, uint32_t round,
                                           uint32_t prefix) {
    const cgai_gameplay_progress progress = cgai_gameplay_get_progress(model);
    if (progress.epochs > NPC_EPOCH_LIMIT ||
        (round == 0U ? progress.epochs != 0U || progress.steps != 0U
                     : progress.epochs < round || progress.steps == 0U))
        return NULL;
    npc_collection *collection = calloc(1U, sizeof(*collection));
    if (collection == NULL)
        return NULL;
    collection->round = round;
    collection->prefix = prefix;
    collection->source_epochs = progress.epochs;
    collection->source_steps = progress.steps;
    npc_policy_session session = {cgai_gameplay_session_create(model, NPC_SESSION_LIMIT), {0}, 1};
    const int valid = session.neural != NULL && collect_block(collection, &session, 0U) &&
                      (prefix == 0U || collect_block(collection, &session, prefix)) &&
                      npc_corpus_validate(collection->examples, collection->count, 1);
    cgai_gameplay_session_destroy(session.neural);
    if (!valid) {
        free(collection);
        return NULL;
    }
    return collection;
}

/** @brief Write the strict scalar corpus metadata shared by parser and writer.
 * @param stream Borrowed writable binary stream.
 * @param collection Borrowed complete verified records.
 * @return Nonzero after every metadata field is written. */
static int write_metadata(FILE *stream, const npc_collection *collection) {
    return fprintf(stream,
                   "schema\tnpc-corpus-v2\nprofile\thistory\nround\t%u\nprefix\t%u\n"
                   "source_epochs\t%llu\nsource_steps\t%llu\nrecords\t%zu\n"
                   "base_episodes\t%zu\npolicy_episodes\t%zu\nrejected_episodes\t%zu\n"
                   "guarded_episodes\t%zu\n%s",
                   collection->round, collection->prefix,
                   (unsigned long long)collection->source_epochs,
                   (unsigned long long)collection->source_steps, collection->count,
                   collection->base_episodes, collection->policy_episodes,
                   collection->rejected_episodes, collection->guarded_episodes, columns) >= 0;
}

/** @brief Emit one complete canonical record with exact source and actual execution.
 * @param stream Borrowed writable stream.
 * @param collection Borrowed complete corpus.
 * @param index Canonical selected record index.
 * @return Nonzero after every column and newline are written. */
static int write_record(FILE *stream, const npc_collection *collection, size_t index) {
    const cgai_gameplay_example *example = &collection->examples[index];
    const npc_collection_source *source = &collection->sources[index];
    int valid = fprintf(stream, "example\t%zu\t%u\t%u\t%u\t%u\t%u\t1\t%u\t%u\t%u", index,
                        source->round, source->family, source->variant, source->step, source->phase,
                        example->task, example->target, source->executed) >= 0;
    for (size_t field = 0U; field < NPC_FEATURE_COUNT && valid; ++field)
        valid = fprintf(stream, "\t%u", example->state.values[field]) >= 0;
    return valid && fputc('\n', stream) != EOF;
}

/** @brief Publish canonical bytes only after complete verified collection.
 * @param path New generated local corpus path.
 * @param collection Borrowed complete verified corpus.
 * @return Nonzero on complete write and close. */
static int corpus_write(const char *path, const npc_collection *collection) {
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
        return 0;
    int valid = write_metadata(stream, collection);
    for (size_t index = 0U; index < collection->count && valid; ++index)
        valid = write_record(stream, collection, index);
    const int closed = fclose(stream) == 0;
    return valid && closed;
}

/** @brief Accumulate actual observed coverage for one admitted correction label.
 * @param coverage Writable compact counters.
 * @param collection Borrowed complete corpus.
 * @param index Canonical admitted record index. */
static void coverage_record(npc_coverage *coverage, const npc_collection *collection,
                            size_t index) {
    const cgai_gameplay_example *example = &collection->examples[index];
    const npc_collection_source *source = &collection->sources[index];
    coverage->policy_records += source->phase == 1U;
    coverage->recovery_records += source->phase == 2U;
    coverage->blocked_records += example->state.values[10] == NPC_BLOCKED;
    coverage->cue_history_records +=
        example->state.values[7] == 0U && example->state.values[8] != 0U;
    coverage->rotation_mask |= UINT64_C(1) << (source->variant % 4U);
    coverage->target_mask |= UINT64_C(1) << example->target;
    coverage->family_mask |= UINT64_C(1) << source->family;
    coverage->variant_mask |= UINT64_C(1) << source->variant;
    int danger = 0;
    for (size_t field = 0U; field < NPC_FEATURE_COUNT; ++field) {
        coverage->vocabulary[field] |= UINT64_C(1) << example->state.values[field];
        danger |= field < 4U && example->state.values[field] == NPC_DANGER;
    }
    coverage->hazard_records += danger != 0;
}

/** @brief Write actual counters and represented source identities.
 * @param stream Borrowed writable compact evidence stream.
 * @param collection Borrowed complete verified corpus.
 * @param coverage Borrowed measured coverage.
 * @return Nonzero after complete scalar writes. */
static int coverage_scalars(FILE *stream, const npc_collection *collection,
                            const npc_coverage *coverage) {
    return fprintf(
               stream,
               "schema\tnpc-coverage-v2\nround\t%u\nprefix\t%u\nsource_epochs\t%llu\n"
               "source_steps\t%llu\nrecords\t%zu\nbase_episodes\t%zu\npolicy_episodes\t%zu\n"
               "rejected_episodes\t%zu\nguarded_episodes\t%zu\npolicy_records\t%zu\n"
               "recovery_records\t%zu\n"
               "blocked_records\t%zu\nhazard_records\t%zu\ncue_history_records\t%zu\n"
               "rotation_mask\t%llu\ntarget_mask\t%llu\nfamily_mask\t%llu\n"
               "variant_mask\t%llu\nguard_reason\tfatal-proposal-training-verification-only\n"
               "provenance\ttraining-only-successful-exact-replay\n",
               collection->round, collection->prefix, (unsigned long long)collection->source_epochs,
               (unsigned long long)collection->source_steps, collection->count,
               collection->base_episodes, collection->policy_episodes,
               collection->rejected_episodes, collection->guarded_episodes,
               coverage->policy_records, coverage->recovery_records, coverage->blocked_records,
               coverage->hazard_records, coverage->cue_history_records,
               (unsigned long long)coverage->rotation_mask,
               (unsigned long long)coverage->target_mask, (unsigned long long)coverage->family_mask,
               (unsigned long long)coverage->variant_mask) >= 0;
}

/** @brief Publish only compact measured coverage, with no observations or trajectories.
 * @param path New evidence path.
 * @param collection Borrowed verified corpus.
 * @return Nonzero on complete measured write and close. */
static int coverage_write(const char *path, const npc_collection *collection) {
    npc_coverage coverage = {0};
    for (size_t index = 0U; index < collection->count; ++index)
        coverage_record(&coverage, collection, index);
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
        return 0;
    int valid = coverage_scalars(stream, collection, &coverage);
    for (size_t field = 0U; field < NPC_FEATURE_COUNT && valid; ++field)
        valid = fprintf(stream, "f%zu\t%llu\n", field,
                        (unsigned long long)coverage.vocabulary[field]) >= 0;
    const int closed = fclose(stream) == 0;
    return valid && closed;
}

/** @brief Freeze one compatible snapshot, collect, verify, and publish its bounded records.
 * @param checkpoint Frozen optimizer checkpoint.
 * @param corpus New generated corpus destination.
 * @param coverage New compact coverage destination.
 * @param round Frozen collection round, zero through five.
 * @param prefix Frozen four-times-round policy prefix.
 * @return OK after complete verified writes, ERROR otherwise. */
cgai_status npc_collection_write(const char *checkpoint, const char *corpus, const char *coverage,
                                 uint32_t round, uint32_t prefix) {
    if (checkpoint == NULL || round >= NPC_TRAINING_ROUNDS || prefix != round * 4U ||
        corpus == NULL || coverage == NULL || strcmp(corpus, coverage) == 0 ||
        strcmp(checkpoint, corpus) == 0 || strcmp(checkpoint, coverage) == 0)
        return cgai_fail("invalid frozen NPC v2 collection destinations or round");
    cgai_gameplay_model *model = cgai_gameplay_checkpoint_load(checkpoint);
    npc_collection *collection = model != NULL && npc_policy_compatible(model)
                                     ? collection_generate(model, round, prefix)
                                     : NULL;
    const int valid = collection != NULL && corpus_write(corpus, collection) &&
                      coverage_write(coverage, collection);
    if (valid)
        printf("corpus_records\t%zu\n", collection->count);
    free(collection);
    cgai_gameplay_destroy(model);
    return valid ? CGAI_STATUS_OK : cgai_fail("NPC v2 complete verified collection failed");
}

/** @brief Parse one canonical unsigned integer without signs, spaces or leading zeros.
 * @param text Borrowed token, terminated by delimiter.
 * @param delimiter Required exact delimiter.
 * @param value Writable parsed value.
 * @return Pointer after delimiter, or NULL. */
static const char *integer_read(const char *text, char delimiter, uint64_t *value) {
    if (text[0] < '0' || text[0] > '9' || (text[0] == '0' && text[1] != delimiter))
        return NULL;
    errno = 0;
    char *end = NULL;
    const unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || *end != delimiter)
        return NULL;
    *value = (uint64_t)parsed;
    return end + 1;
}

/** @brief Read an exact canonical scalar metadata line.
 * @param stream Borrowed readable stream.
 * @param name Exact expected field name.
 * @param value Writable unsigned result.
 * @return Nonzero for one exact expected scalar. */
static int scalar_read(FILE *stream, const char *name, uint64_t *value) {
    char line[128];
    const size_t length = strlen(name);
    if (fgets(line, sizeof(line), stream) == NULL || strncmp(line, name, length) != 0 ||
        line[length] != '\t')
        return 0;
    const char *end = integer_read(line + length + 1U, '\n', value);
    return end != NULL && *end == '\0';
}

/** @brief Read strict canonical round and source-progress metadata.
 * @param stream Borrowed readable stream.
 * @param collection Writable bounded corpus owner.
 * @return Nonzero after validated round, prefix, and source progress. */
static int metadata_identity(FILE *stream, npc_collection *collection) {
    char line[128];
    uint64_t round = 0U;
    uint64_t prefix = 0U;
    if (fgets(line, sizeof(line), stream) == NULL || strcmp(line, "schema\tnpc-corpus-v2\n") != 0 ||
        fgets(line, sizeof(line), stream) == NULL || strcmp(line, "profile\thistory\n") != 0 ||
        !scalar_read(stream, "round", &round) || !scalar_read(stream, "prefix", &prefix) ||
        !scalar_read(stream, "source_epochs", &collection->source_epochs) ||
        !scalar_read(stream, "source_steps", &collection->source_steps))
        return 0;
    if (round >= NPC_TRAINING_ROUNDS || prefix != round * 4U ||
        collection->source_epochs > NPC_EPOCH_LIMIT ||
        (round == 0U && (collection->source_epochs != 0U || collection->source_steps != 0U)) ||
        (round > 0U && (collection->source_epochs < round || collection->source_steps == 0U)))
        return 0;
    collection->round = (uint32_t)round;
    collection->prefix = (uint32_t)prefix;
    return 1;
}

/** @brief Read strict actual record and trajectory counts.
 * @param stream Borrowed readable stream.
 * @param collection Writable bounded owner.
 * @return Nonzero after all counts and columns are validated. */
static int metadata_counts(FILE *stream, npc_collection *collection) {
    const char *names[] = {"records", "base_episodes", "policy_episodes", "rejected_episodes",
                           "guarded_episodes"};
    uint64_t values[5];
    for (size_t index = 0U; index < 5U; ++index)
        if (!scalar_read(stream, names[index], &values[index]))
            return 0;
    if (values[0] == 0U || values[0] > NPC_TRAINING_LIMIT || values[1] != 192U ||
        values[2] > 192U || values[3] > 192U || values[4] > values[2] ||
        (collection->prefix == 0U ? values[2] != 0U || values[3] != 0U
                                  : values[2] + values[3] != 192U))
        return 0;
    collection->count = (size_t)values[0];
    collection->base_episodes = (size_t)values[1];
    collection->policy_episodes = (size_t)values[2];
    collection->rejected_episodes = (size_t)values[3];
    collection->guarded_episodes = (size_t)values[4];
    char line[256];
    return fgets(line, sizeof(line), stream) != NULL && strcmp(line, columns) == 0;
}

/** @brief Parse one exact canonical record before any trajectory is replayed.
 * @param stream Borrowed readable stream.
 * @param values Writable twenty-six bounded unsigned integer columns.
 * @return Nonzero on complete bounded field publication. */
static int record_values(FILE *stream, uint64_t values[26]) {
    char line[512];
    if (fgets(line, sizeof(line), stream) == NULL || strncmp(line, "example\t", 8U) != 0)
        return 0;
    const char *cursor = line + 8U;
    for (size_t field = 0U; field < 26U; ++field) {
        cursor = integer_read(cursor, field == 25U ? '\n' : '\t', &values[field]);
        if (cursor == NULL || values[field] > UINT32_MAX)
            return 0;
    }
    return *cursor == '\0';
}

/** @brief Publish one parsed canonical record with complete exact execution provenance.
 * @param stream Borrowed readable stream.
 * @param collection Writable owner with validated count.
 * @param index Required canonical ordered index.
 * @return Nonzero after every parsed field is validated and published. */
static int record_read(FILE *stream, npc_collection *collection, size_t index) {
    uint64_t values[26];
    if (!record_values(stream, values) || values[0] != index || values[1] != collection->round ||
        values[6] != 1U)
        return 0;
    collection->sources[index] =
        (npc_collection_source){(uint32_t)values[1], (uint32_t)values[2], (uint32_t)values[3],
                                (uint32_t)values[4], (uint32_t)values[5], (uint32_t)values[9]};
    collection->examples[index].task = (uint32_t)values[7];
    collection->examples[index].target = (uint32_t)values[8];
    for (size_t field = 0U; field < NPC_FEATURE_COUNT; ++field)
        collection->examples[index].state.values[field] = (uint32_t)values[10U + field];
    return 1;
}

/** @brief Initialize only a permitted current-world training-family trajectory.
 * @param collection Borrowed frozen round metadata.
 * @param source Borrowed first source identity.
 * @param world Writable host replay state.
 * @param memory Writable independent observed history.
 * @return Nonzero for a training-only selected sibling. */
static int trajectory_start(const npc_collection *collection, const npc_collection_source *source,
                            npc_world *world, npc_memory *memory) {
    npc_family family;
    if (source->step != 0U || source->family >= NPC_FAMILIES_PER_SPLIT ||
        source->variant / 8U != collection->round || source->phase > 2U ||
        !npc_family_get(NPC_TRAIN, source->family, &family) || family.id != source->family ||
        !npc_world_init(world, &family, source->variant))
        return 0;
    npc_memory_reset(memory);
    return 1;
}

/** @brief Recompute one observed categorical state and teacher target before host execution.
 * @param collection Borrowed verified-format corpus.
 * @param index Canonical record index.
 * @param world Mutable current authoritative replay state.
 * @param memory Mutable observation-only history.
 * @param phase Mutable monotonic teacher-base or policy-to-teacher trajectory phase.
 * @return Nonzero only for identical visible record and permitted actual action. */
static int trajectory_record(const npc_collection *collection, size_t index, npc_world *world,
                             npc_memory *memory, uint32_t *phase) {
    const npc_collection_source *source = &collection->sources[index];
    npc_observation observation;
    npc_world_observe(world, &observation);
    npc_memory_observe(memory, &observation);
    cgai_gameplay_example expected = {0};
    npc_encode(&observation, memory, 1, &expected.state);
    expected.target = npc_teacher_action(&observation, memory);
    if (world->terminal != NPC_RUNNING || source->family != world->family.id ||
        source->variant != world->variant || source->step != world->ticks || source->phase > 2U ||
        (*phase == 0U ? source->phase != 0U : source->phase < *phase) ||
        (source->phase == 1U && world->ticks >= collection->prefix) ||
        memcmp(&expected, &collection->examples[index], sizeof(expected)) != 0 ||
        source->executed > NPC_INTERACT ||
        (observation.allowed_actions & (UINT64_C(1) << source->executed)) == 0U ||
        (source->phase != 1U && source->executed != expected.target))
        return 0;
    *phase = source->phase;
    (void)npc_world_step(world, source->executed);
    return world->attempted_illegal == 0U && world->executed_illegal == 0U;
}

/** @brief Require each base sibling exactly once and strictly ordered unique policy siblings.
 * @param collection Borrowed frozen selected block.
 * @param source Borrowed first trajectory record.
 * @param base_count Previously admitted complete base episodes.
 * @param policy_count Previously admitted complete policy episodes.
 * @param last_policy Writable last policy sibling position.
 * @return Nonzero for the complete canonical generation order. */
static int trajectory_order(const npc_collection *collection, const npc_collection_source *source,
                            size_t base_count, size_t policy_count, uint32_t *last_policy) {
    if (source->phase == 0U)
        return policy_count == 0U && source->family == base_count / 8U &&
               source->variant == collection->round * 8U + base_count % 8U;
    const uint32_t position = source->family * 8U + source->variant % 8U;
    if (base_count != 192U || collection->prefix == 0U ||
        (policy_count != 0U && position <= *last_policy))
        return 0;
    *last_policy = position;
    return 1;
}

/** @brief Begin one exactly ordered complete trajectory with independent visible history.
 * @param collection Borrowed frozen canonical corpus.
 * @param source Borrowed first source record.
 * @param replay Mutable validation-only owner.
 * @param first Nonzero for the first corpus episode.
 * @return Nonzero after the previous episode succeeds and the next identity is admitted. */
static int replay_start(const npc_collection *collection, const npc_collection_source *source,
                        npc_replay_state *replay, int first) {
    if ((!first && replay->world.terminal != NPC_SUCCESS) ||
        !trajectory_order(collection, source, replay->base_count, replay->policy_count,
                          &replay->last_policy) ||
        !trajectory_start(collection, source, &replay->world, &replay->memory))
        return 0;
    replay->phase = source->phase == 0U ? 0U : 1U;
    replay->base_count += replay->phase == 0U;
    replay->policy_count += replay->phase != 0U;
    return 1;
}

/** @brief Verify all complete admitted trajectories and exact declared episode counts.
 * @param collection Borrowed canonical full-history owner.
 * @return Nonzero after every host trajectory succeeds and labels are conflict-free. */
static int trajectories_verify(const npc_collection *collection) {
    npc_replay_state replay = {0};
    for (size_t index = 0U; index < collection->count; ++index) {
        const npc_collection_source *source = &collection->sources[index];
        if (source->step == 0U && !replay_start(collection, source, &replay, index == 0U))
            return 0;
        replay.guarded_count +=
            replay.phase == 1U && source->phase == 2U && source->step < collection->prefix;
        if (!trajectory_record(collection, index, &replay.world, &replay.memory, &replay.phase))
            return 0;
    }
    return replay.world.terminal == NPC_SUCCESS && replay.base_count == collection->base_episodes &&
           replay.policy_count == collection->policy_episodes &&
           replay.guarded_count == collection->guarded_episodes &&
           npc_corpus_validate(collection->examples, collection->count, 1);
}

/** @brief Load and authoritatively verify a bounded strict current-world correction corpus.
 * @param path Existing canonical corpus path.
 * @return Owned verified full-history records, or NULL. */
npc_collection *npc_collection_load(const char *path) {
    FILE *stream = path != NULL ? fopen(path, "rb") : NULL;
    npc_collection *collection = stream != NULL ? calloc(1U, sizeof(*collection)) : NULL;
    int valid = collection != NULL && metadata_identity(stream, collection) &&
                metadata_counts(stream, collection);
    for (size_t index = 0U; valid && index < collection->count; ++index)
        valid = record_read(stream, collection, index);
    if (valid)
        valid = fgetc(stream) == EOF && !ferror(stream) && trajectories_verify(collection);
    if (stream != NULL && fclose(stream) != 0)
        valid = 0;
    if (!valid) {
        free(collection);
        (void)cgai_fail(
            "NPC v2 corpus requires canonical training-only successful replayed trajectories");
        return NULL;
    }
    return collection;
}

/** @brief Apply the declared history ablation after full observable-label verification.
 * @param collection Mutable private full-history record copy. */
void npc_collection_disable_history(npc_collection *collection) {
    for (size_t index = 0U; index < collection->count; ++index)
        for (size_t field = 8U; field < 12U; ++field)
            collection->examples[index].state.values[field] = 0U;
}

/** @brief Compare one next record with all previously admitted full-history targets.
 * @param examples Borrowed previously admitted records.
 * @param count Previous record count.
 * @param example Borrowed next correction.
 * @return Nonzero if the exact categorical observation has an incompatible target. */
static int schedule_conflict(const cgai_gameplay_example *examples, size_t count,
                             const cgai_gameplay_example *example) {
    for (size_t index = 0U; index < count; ++index)
        if (memcmp(&examples[index].state, &example->state, sizeof(example->state)) == 0 &&
            examples[index].target != example->target)
            return 1;
    return 0;
}

/** @brief Append one conflict-free canonical round to a bounded validation-only schedule.
 * @param collection Borrowed verified current-world corpus.
 * @param examples Mutable prior scheduled records.
 * @param count Mutable actual prior schedule count.
 * @param round Expected next round identity.
 * @return Nonzero after exact round and cross-round targets agree. */
static int schedule_append(const npc_collection *collection, cgai_gameplay_example *examples,
                           size_t *count, size_t round) {
    if (collection->round != round)
        return 0;
    for (size_t index = 0U; index < collection->count; ++index)
        if (schedule_conflict(examples, *count, &collection->examples[index]))
            return 0;
    memcpy(examples + *count, collection->examples, collection->count * sizeof(*examples));
    *count += collection->count;
    return 1;
}

/** @brief Audit all canonical labels across the same increasing frozen-round schedule.
 * @param paths Borrowed immutable full-history corpus paths.
 * @param count Number of complete rounds, one through six.
 * @return OK only when every host replay and full-history target agrees. */
cgai_status npc_collection_validate(const char *const *paths, size_t count) {
    if (paths == NULL || count == 0U || count > NPC_TRAINING_ROUNDS)
        return cgai_fail("invalid bounded NPC v2 corpus schedule");
    cgai_gameplay_example *examples = calloc(count * NPC_TRAINING_LIMIT, sizeof(*examples));
    size_t accumulated = 0U;
    int valid = examples != NULL;
    for (size_t round = 0U; round < count && valid; ++round) {
        npc_collection *collection = npc_collection_load(paths[round]);
        valid = collection != NULL && schedule_append(collection, examples, &accumulated, round);
        free(collection);
    }
    free(examples);
    return valid ? CGAI_STATUS_OK : cgai_fail("NPC v2 full-history schedule target conflict");
}
