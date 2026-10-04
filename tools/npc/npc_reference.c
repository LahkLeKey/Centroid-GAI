/** @file npc_reference.c @brief Validate comparator observations and score complete host episodes.
 */
#include "npc_reference.h"
#include "internal/error.h"
#include "npc_teacher.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Artifact identities and diagnostics belong to the selected immutable world profile. */
typedef struct npc_reference_profile {
    const char *capture_schema;
    const char *report_schema;
    const char *capture_validation;
    const char *invalid_arguments;
    const char *open_failure;
    const char *capture_failure;
} npc_reference_profile;

#if NPC_REFERENCE_PROFILE == 2
static const npc_reference_profile profile = {
    "npc-reference-capture-v2",
    "npc-reference-evaluation-v2",
    "authoritative-native-replay-v2",
    "invalid NPC v2 comparator replay arguments",
    "could not open NPC v2 comparator capture",
    "NPC v2 comparator capture is incomplete or differs from observed history"};
#elif NPC_REFERENCE_PROFILE == 3
static const npc_reference_profile profile = {
    "npc-reference-capture-v3",
    "npc-reference-evaluation-v3",
    "authoritative-native-replay-v3",
    "invalid NPC v3 comparator replay arguments",
    "could not open NPC v3 comparator capture",
    "NPC v3 comparator capture is incomplete or differs from observed history"};
#else
#error NPC reference replay requires an explicit supported profile
#endif

/** Complete bounded comparator evidence, including unsuccessful episodes. */
typedef struct npc_reference_job {
    FILE *capture; /**< Borrowed canonical raw capture stream. */
    int generate;  /**< Development planner-template mode. */
    npc_episode_metrics family[NPC_FAMILIES_PER_SPLIT]; /**< Complete independent family blocks. */
    npc_episode_metrics total;                          /**< Every attempted episode. */
    npc_episode_metrics mechanic[3];                    /**< Balanced principal mechanic strata. */
} npc_reference_job;

/** Independent host episode and comparator-owned observed memory. */
typedef struct npc_reference_episode {
    npc_world world;             /**< Authoritative host state, never passed to a comparator. */
    npc_memory memory;           /**< This actor's bounded observable history. */
    npc_episode_metrics metrics; /**< Complete outcome counters. */
    int pending;                 /**< Observed obstruction awaiting a successful recovery move. */
} npc_reference_episode;

/** @brief Require a canonical lowercase SHA256 descriptor identity.
 * @param text Borrowed proposed identity.
 * @return Nonzero only for exactly64 lowercase hexadecimal characters. */
static int reference_hash(const char *text) {
    if (text == NULL || strlen(text) != 64U)
        return 0;
    for (size_t index = 0U; index < 64U; ++index)
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f')))
            return 0;
    return 1;
}

/** @brief Consume one exact metadata line with canonical LF bytes.
 * @param stream Borrowed capture stream.
 * @param key Required metadata key.
 * @param value Required pinned value.
 * @return Nonzero only for the complete expected line. */
static int reference_line(FILE *stream, const char *key, const char *value) {
    char actual[256];
    char expected[256];
    const int length = snprintf(expected, sizeof(expected), "%s\t%s\n", key, value);
    return length > 0 && (size_t)length < sizeof(expected) &&
           fgets(actual, (int)sizeof(actual), stream) != NULL && strcmp(actual, expected) == 0;
}

/** @brief Write or verify metadata before reading any decision rows.
 * @param job Mutable complete reference operation.
 * @param split Development or audit partition.
 * @param identity Pinned comparator-descriptor SHA256.
 * @return Nonzero for an intact canonical header. */
static int reference_header(npc_reference_job *job, npc_split split, const char *identity) {
    const char *partition = split == NPC_DEV ? "development" : "audit";
    if (job->generate)
        return fprintf(job->capture,
                       "schema\t%s\nsplit\t%s\n"
                       "comparator_sha256\t%s\n",
                       profile.capture_schema, partition, identity) >= 0;
    return reference_line(job->capture, "schema", profile.capture_schema) &&
           reference_line(job->capture, "split", partition) &&
           reference_line(job->capture, "comparator_sha256", identity);
}

/** @brief Parse one canonical unsigned decimal field without signs or leading zeros.
 * @param cursor Mutable current position in the bounded row.
 * @param final Nonzero requires LF and end of row, otherwise a tab.
 * @param value Writable bounded integer.
 * @return Nonzero only for the exact required field syntax. */
static int reference_integer(char **cursor, int final, uint32_t *value) {
    if (**cursor < '0' || **cursor > '9' ||
        (**cursor == '0' && (*cursor)[1] >= '0' && (*cursor)[1] <= '9'))
        return 0;
    errno = 0;
    char *end = NULL;
    const unsigned long parsed = strtoul(*cursor, &end, 10);
    if (errno != 0 || parsed > UINT32_MAX ||
        (final ? end[0] != '\n' || end[1] != '\0' : end[0] != '\t'))
        return 0;
    *value = (uint32_t)parsed;
    *cursor = end + 1;
    return 1;
}

/** @brief Match every captured categorical field against the actor's own visible history.
 * @param cursor Position following the proposal field.
 * @param state Freshly encoded authoritative observation and bounded memory.
 * @return Nonzero only for all16 exact fields and canonical end of row. */
static int reference_state(char *cursor, const cgai_gameplay_state *state) {
    for (size_t field = 0U; field < NPC_FEATURE_COUNT; ++field) {
        uint32_t value = 0U;
        if (!reference_integer(&cursor, field + 1U == NPC_FEATURE_COUNT, &value) ||
            value != state->values[field])
            return 0;
    }
    return 1;
}

/** @brief Validate decision provenance and current observation before admitting its proposal.
 * @param stream Borrowed raw capture input.
 * @param world Authoritative provenance and expected pre-action decision index.
 * @param state This comparator's exact observed-history encoding.
 * @param action Writable captured proposal.
 * @return Nonzero only for an exact complete decision record. */
static int reference_read(FILE *stream, const npc_world *world, const cgai_gameplay_state *state,
                          uint32_t *action) {
    char line[256];
    uint32_t fields[4] = {0};
    if (fgets(line, (int)sizeof(line), stream) == NULL || strncmp(line, "decision\t", 9U) != 0)
        return 0;
    char *cursor = line + 9U;
    for (size_t field = 0U; field < 4U; ++field)
        if (!reference_integer(&cursor, 0, &fields[field]))
            return 0;
    if (fields[0] != world->family.id || fields[1] != world->variant || fields[2] != world->ticks ||
        fields[3] > NPC_INTERACT || !reference_state(cursor, state))
        return 0;
    *action = fields[3];
    return 1;
}

/** @brief Write one observation-limited planner decision with complete selected-step provenance.
 * @param stream Borrowed raw capture output.
 * @param world Authoritative provenance, excluded from the controller's inputs.
 * @param state Exact observed-history encoding.
 * @param action Planner proposal from the same observation and memory.
 * @return Nonzero after the entire canonical row is written. */
static int reference_write(FILE *stream, const npc_world *world, const cgai_gameplay_state *state,
                           uint32_t action) {
    if (fprintf(stream, "decision\t%u\t%u\t%u\t%u", world->family.id, world->variant, world->ticks,
                action) < 0)
        return 0;
    for (size_t field = 0U; field < NPC_FEATURE_COUNT; ++field)
        if (fprintf(stream, "\t%u", state->values[field]) < 0)
            return 0;
    return fputc('\n', stream) != EOF;
}

/** @brief Select or replay one proposal using only the current observation and actor memory.
 * @param job Borrowed input/output mode and capture stream.
 * @param episode This actor's independently caused host state and bounded memory.
 * @param action Writable complete proposal.
 * @return Nonzero for a verified proposal or complete template write. */
static int reference_action(npc_reference_job *job, npc_reference_episode *episode,
                            uint32_t *action) {
    npc_observation observation;
    cgai_gameplay_state state;
    npc_world_observe(&episode->world, &observation);
    npc_memory_observe(&episode->memory, &observation);
    npc_encode(&observation, &episode->memory, 1, &state);
    if (!job->generate)
        return reference_read(job->capture, &episode->world, &state, action);
    *action = npc_teacher_action(&observation, &episode->memory);
    return reference_write(job->capture, &episode->world, &state, *action);
}

/** @brief Score one authoritative transition including rejected proposals and fallback waits.
 * @param episode Mutable complete host episode.
 * @param action Captured in-domain proposal; permissions are enforced by the host. */
static void reference_step(npc_reference_episode *episode, uint32_t action) {
    episode->metrics.fallbacks += action == NPC_FALLBACK ? 1U : 0U;
    const npc_outcome outcome = npc_world_step(&episode->world, action);
    if (outcome == NPC_BLOCKED) {
        ++episode->metrics.blocked;
        episode->pending = 1;
    } else if (outcome == NPC_MOVED && episode->pending) {
        ++episode->metrics.recoveries;
        episode->pending = 0;
    }
}

/** @brief Record every terminal in the denominator, including death and timeout.
 * @param episode Completed authoritative host episode. */
static void reference_terminal(npc_reference_episode *episode) {
    const npc_world *world = &episode->world;
    npc_episode_metrics *metrics = &episode->metrics;
    metrics->count = 1U;
    metrics->success = world->terminal == NPC_SUCCESS ? 1U : 0U;
    metrics->survived = world->terminal != NPC_DEATH ? 1U : 0U;
    metrics->deaths = world->terminal == NPC_DEATH ? 1U : 0U;
    metrics->timeouts = world->terminal == NPC_TIMEOUT ? 1U : 0U;
    metrics->cost = world->ticks;
    metrics->illegal_attempts = world->attempted_illegal;
    metrics->illegal_executed = world->executed_illegal;
}

/** @brief Accumulate whole episodes without treating related decisions as independent trials.
 * @param target Writable complete metric accumulator.
 * @param source Complete episode or family metric value. */
static void reference_add(npc_episode_metrics *target, const npc_episode_metrics *source) {
    target->count += source->count;
    target->success += source->success;
    target->survived += source->survived;
    target->deaths += source->deaths;
    target->timeouts += source->timeouts;
    target->cost += source->cost;
    target->blocked += source->blocked;
    target->recoveries += source->recoveries;
    target->illegal_attempts += source->illegal_attempts;
    target->illegal_executed += source->illegal_executed;
    target->fallbacks += source->fallbacks;
}

/** @brief Replay one independently reset comparator episode to its declared terminal.
 * @param job Mutable complete reference operation.
 * @param family Pinned family provenance.
 * @param variant Family-local seed and rotation sibling.
 * @param metrics Writable complete episode result.
 * @return Nonzero only for a full, observation-validated terminal rollout. */
static int reference_episode(npc_reference_job *job, const npc_family *family, uint32_t variant,
                             npc_episode_metrics *metrics) {
    npc_reference_episode episode = {0};
    if (!npc_world_init(&episode.world, family, variant))
        return 0;
    npc_memory_reset(&episode.memory);
    while (episode.world.terminal == NPC_RUNNING && episode.world.ticks < NPC_MAX_TICKS) {
        uint32_t action = NPC_FALLBACK;
        if (!reference_action(job, &episode, &action))
            return 0;
        reference_step(&episode, action);
    }
    if (episode.world.terminal == NPC_RUNNING)
        return 0;
    reference_terminal(&episode);
    *metrics = episode.metrics;
    return 1;
}

/** @brief Replay every declared family and variant, in canonical provenance order.
 * @param job Mutable bounded family outcomes and capture stream.
 * @param split Development or sealed audit partition.
 * @return Nonzero only after all independent episodes validate. */
static int reference_all(npc_reference_job *job, npc_split split) {
    for (uint32_t index = 0U; index < NPC_FAMILIES_PER_SPLIT; ++index) {
        npc_family family;
        if (!npc_family_get(split, index, &family))
            return 0;
        for (uint32_t variant = 0U; variant < NPC_VARIANTS_PER_FAMILY; ++variant) {
            npc_episode_metrics episode;
            if (!reference_episode(job, &family, variant, &episode))
                return 0;
            reference_add(&job->family[index], &episode);
            reference_add(&job->total, &episode);
            reference_add(&job->mechanic[family.mechanic], &episode);
        }
    }
    return job->generate ? !ferror(job->capture)
                         : fgetc(job->capture) == EOF && !ferror(job->capture);
}

/** @brief Write all11 complete episode counters in the shared evaluation ordering.
 * @param stream Borrowed compact output stream.
 * @param metrics Complete family or total counters.
 * @return Nonzero after the full metric row is written. */
static int reference_metrics(FILE *stream, const npc_episode_metrics *metrics) {
    return fprintf(stream, "\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\n",
                   metrics->count, metrics->success, metrics->survived, metrics->deaths,
                   metrics->timeouts, metrics->cost, metrics->blocked, metrics->recoveries,
                   metrics->illegal_attempts, metrics->illegal_executed, metrics->fallbacks) >= 0;
}

/** @brief Publish complete family blocks and principal mechanic strata.
 * @param stream Borrowed compact outcome output.
 * @param job Complete authoritative replay evidence.
 * @param split Development or sealed audit partition.
 * @return Nonzero only after every family and aggregate row is written. */
static int reference_outcomes(FILE *stream, const npc_reference_job *job, npc_split split) {
    for (uint32_t index = 0U; index < NPC_FAMILIES_PER_SPLIT; ++index) {
        npc_family family;
        if (!npc_family_get(split, index, &family) ||
            fprintf(stream, "family\t%u\treference", family.id) < 0 ||
            !reference_metrics(stream, &job->family[index]))
            return 0;
    }
    if (fprintf(stream, "summary\treference") < 0 || !reference_metrics(stream, &job->total))
        return 0;
    const char *const names[3] = {"item", "hazard", "cue"};
    for (size_t mechanic = 0U; mechanic < 3U; ++mechanic)
        if (fprintf(stream, "mechanic\t%s\treference", names[mechanic]) < 0 ||
            !reference_metrics(stream, &job->mechanic[mechanic]))
            return 0;
    return 1;
}

/** @brief Publish a compact report only after the entire raw capture passes host replay.
 * @param path New compact report destination.
 * @param job Complete host-replayed comparator evidence.
 * @param split Development or sealed audit partition.
 * @param identity Pinned comparator descriptor SHA256.
 * @return Nonzero after complete write and close. */
static int reference_report(const char *path, const npc_reference_job *job, npc_split split,
                            const char *identity) {
    FILE *stream = fopen(path, "wbx");
    if (stream == NULL)
        return 0;
    const int valid =
        fprintf(stream,
                "schema\t%s\nsplit\t%s\n"
                "comparator_sha256\t%s\nfamily_count\t%u\n"
                "variants_per_family\t%u\nweighting\tequal-family\n"
                "capture_validation\t%s\n",
                profile.report_schema, split == NPC_DEV ? "development" : "audit", identity,
                NPC_FAMILIES_PER_SPLIT, NPC_VARIANTS_PER_FAMILY, profile.capture_validation) >= 0 &&
        reference_outcomes(stream, job, split);
    const int closed = fclose(stream) == 0;
    return valid && closed;
}

/** @brief Validate bounded operation arguments before opening any artifact.
 * @param capture Raw capture path.
 * @param report New compact report path.
 * @param split Development or sealed audit.
 * @param identity Pinned comparator descriptor SHA256.
 * @return Nonzero for distinct nonempty paths, valid partition and canonical identity. */
static int reference_arguments(const char *capture, const char *report, npc_split split,
                               const char *identity) {
    return capture != NULL && report != NULL && capture[0] != '\0' && report[0] != '\0' &&
           strcmp(capture, report) != 0 && (split == NPC_DEV || split == NPC_AUDIT) &&
           reference_hash(identity);
}

/** @brief Run one bounded canonical capture operation and release its stream on all paths.
 * @param capture Raw capture path.
 * @param report New compact report path.
 * @param split Development or sealed audit.
 * @param identity Pinned comparator descriptor SHA256.
 * @param generate Nonzero emits a development-only planner template.
 * @return OK after complete host replay and report publication, ERROR otherwise. */
static cgai_status reference_run(const char *capture, const char *report, npc_split split,
                                 const char *identity, int generate) {
    if (!reference_arguments(capture, report, split, identity) || (generate && split != NPC_DEV))
        return cgai_fail(profile.invalid_arguments);
    npc_reference_job job = {0};
    job.generate = generate;
    job.capture = fopen(capture, generate ? "wbx" : "rb");
    if (job.capture == NULL)
        return cgai_fail(profile.open_failure);
    const int valid = reference_header(&job, split, identity) && reference_all(&job, split);
    const int closed = fclose(job.capture) == 0;
    return valid && closed && reference_report(report, &job, split, identity)
               ? CGAI_STATUS_OK
               : cgai_fail(profile.capture_failure);
}

cgai_status npc_reference_files(const char *capture_path, const char *report_path, npc_split split,
                                const char *comparator_sha) {
    return reference_run(capture_path, report_path, split, comparator_sha, 0);
}

cgai_status npc_reference_template(const char *capture_path, const char *report_path,
                                   npc_split split, const char *comparator_sha) {
    return reference_run(capture_path, report_path, split, comparator_sha, 1);
}
