/** @file life_context_io.c @brief Exact atomic context and Life continuation bundles. */
#include "life_context.h"
#include "life_io.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int token(FILE *file, char text[128]) {
    size_t length = 0U;
    int next;
    do {
        next = fgetc(file);
    } while (next != EOF && isspace((unsigned char)next));
    while (next != EOF && !isspace((unsigned char)next)) {
        if (length == 127U || next == 0)
            return 0;
        text[length++] = (char)next;
        next = fgetc(file);
    }
    text[length] = '\0';
    return length != 0U && !ferror(file);
}

static int label(FILE *file, const char *expected) {
    char value[128];
    return token(file, value) && strcmp(value, expected) == 0;
}

static int number(FILE *file, uint64_t limit, uint64_t *output) {
    char value[128], *end = NULL;
    if (!token(file, value) || value[0] < '0' || value[0] > '9')
        return 0;
    errno = 0;
    const unsigned long long parsed = strtoull(value, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > limit)
        return 0;
    *output = (uint64_t)parsed;
    return 1;
}

static int number32(FILE *file, uint32_t limit, uint32_t *output) {
    uint64_t parsed;
    if (!number(file, limit, &parsed))
        return 0;
    *output = (uint32_t)parsed;
    return 1;
}

static int bounded_real(FILE *file, double minimum, double maximum, double *output) {
    char value[128], *end = NULL;
    if (!token(file, value))
        return 0;
    const char *start = value[0] == '-' ? value + 1 : value;
    if (start[0] != '0' || start[1] != 'x')
        return 0;
    errno = 0;
    const double parsed = strtod(value, &end);
    if ((errno != 0 && errno != ERANGE) || *end != '\0' || !isfinite(parsed) || parsed < minimum ||
        parsed > maximum)
        return 0;
    *output = parsed;
    return 1;
}

static int real_number(FILE *file, double *output) { return bounded_real(file, 0.0, 1.0, output); }

static int hex_digit(int value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

static int write_bytes(FILE *file, const char *name, const char *text) {
    const size_t length = strlen(text);
    if (fprintf(file, "%s %zu\n", name, length) < 0)
        return 0;
    for (size_t i = 0U; i < length; ++i)
        if (fprintf(file, "%02x", (unsigned int)(unsigned char)text[i]) < 0)
            return 0;
    return fputc('\n', file) != EOF;
}

static int read_bytes(FILE *file, const char *name, char *text, uint32_t limit) {
    uint32_t length;
    if (!label(file, name) || !number32(file, limit, &length) || length == 0U)
        return 0;
    for (uint32_t i = 0U; i < length; ++i) {
        const int high = hex_digit(fgetc(file)), low = hex_digit(fgetc(file));
        if (high < 0 || low < 0 || (high == 0 && low == 0))
            return 0;
        text[i] = (char)(high * 16 + low);
    }
    text[length] = '\0';
    return fgetc(file) == '\n' && !ferror(file);
}

static int write_centroids(FILE *file, const life_context_record *record, uint32_t groups) {
    if (fputs("CENTROIDS\n", file) < 0)
        return 0;
    for (size_t group = 0U; group < groups; ++group) {
        for (size_t feature = 0U; feature < CGAI_LIFE_CONTEXT_FEATURES; ++feature)
            if (fprintf(file, "%a ", record->centroids[group][feature]) < 0)
                return 0;
        if (fputc('\n', file) == EOF)
            return 0;
    }
    return 1;
}

static int read_centroids(FILE *file, life_context_record *record, uint32_t groups) {
    if (!label(file, "CENTROIDS"))
        return 0;
    for (size_t group = 0U; group < groups; ++group)
        for (size_t feature = 0U; feature < CGAI_LIFE_CONTEXT_FEATURES; ++feature)
            if (!real_number(file, &record->centroids[group][feature]))
                return 0;
    return 1;
}

static int write_record(FILE *file, const life_context_record *record, uint32_t groups) {
    return fprintf(file,
                   "RECORD %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32
                   " %" PRIu32 " %" PRIu32 " %" PRIu64 "\n",
                   record->first_line, record->last_line, record->family, record->kind,
                   record->split, record->reviewed, record->eligible_mask, record->learned_mask,
                   record->content_hash) >= 0 &&
           write_bytes(file, "SOURCE", record->source) && write_bytes(file, "TEXT", record->text) &&
           write_centroids(file, record, groups);
}

static int read_record_fields(FILE *file, life_context_record *record, uint32_t group_mask) {
    return label(file, "RECORD") && number32(file, UINT32_MAX, &record->first_line) &&
           number32(file, UINT32_MAX, &record->last_line) &&
           number32(file, UINT32_MAX, &record->family) &&
           number32(file, CGAI_LIFE_CONTEXT_ACTIVITY, &record->kind) &&
           number32(file, CGAI_LIFE_CONTEXT_AUDIT, &record->split) &&
           number32(file, 1U, &record->reviewed) &&
           number32(file, group_mask, &record->eligible_mask) &&
           number32(file, group_mask, &record->learned_mask) &&
           number(file, UINT64_MAX, &record->content_hash);
}

static int read_record(FILE *file, life_context_record *record, uint32_t groups) {
    return read_record_fields(file, record, (1U << groups) - 1U) &&
           read_bytes(file, "SOURCE", record->source, CGAI_LIFE_CONTEXT_SOURCE_BYTES) &&
           read_bytes(file, "TEXT", record->text, CGAI_LIFE_CONTEXT_TEXT_BYTES) &&
           read_centroids(file, record, groups) && life_context_record_valid(record);
}

static int write_state(FILE *file, const cgai_life_context *owner) {
    if (fprintf(file,
                "LIFE_CONTEXT 3\nGROUPS %u %" PRIu32 "\nENCODER 1 %u\nSTATE %" PRIu32 " %" PRIu64
                " %" PRIu64 "\n",
                CGAI_LIFE_GROUPS, life_context_group_count(owner), CGAI_LIFE_CONTEXT_FEATURES,
                owner->count, owner->domain_updates, owner->contact_events) < 0)
        return 0;
    for (size_t group = 0U; group < life_context_group_count(owner); ++group)
        if (fprintf(file, "GROUP %" PRIu32 " %" PRIu64 "\n", owner->cursors[group],
                    owner->group_updates[group]) < 0)
            return 0;
    return 1;
}

static int read_state(FILE *file, cgai_life_context *owner, uint32_t *version) {
    uint64_t encoder, features;
    uint32_t capacity;
    if (!label(file, "LIFE_CONTEXT") || !number32(file, 3U, version) || *version == 0U)
        return 0;
    owner->run.config.group_count = CGAI_LIFE_DEFAULT_GROUPS;
    if (*version == 3U && (!label(file, "GROUPS") || !number32(file, CGAI_LIFE_GROUPS, &capacity) ||
                           capacity != CGAI_LIFE_GROUPS ||
                           !number32(file, capacity, &owner->run.config.group_count) ||
                           owner->run.config.group_count < CGAI_LIFE_MIN_GROUPS))
        return 0;
    if (!label(file, "ENCODER") || !number(file, 1U, &encoder) || encoder != 1U ||
        !number(file, CGAI_LIFE_CONTEXT_FEATURES, &features) ||
        features != CGAI_LIFE_CONTEXT_FEATURES || !label(file, "STATE"))
        return 0;
    return number32(file, CGAI_LIFE_CONTEXT_MAX_RECORDS, &owner->count) &&
           number(file, UINT64_MAX, &owner->domain_updates) &&
           number(file, UINT64_MAX, &owner->contact_events);
}

static int read_groups(FILE *file, cgai_life_context *owner) {
    for (size_t group = 0U; group < life_context_group_count(owner); ++group)
        if (!label(file, "GROUP") ||
            !number32(file, CGAI_LIFE_CONTEXT_MAX_RECORDS, &owner->cursors[group]) ||
            !number(file, UINT64_MAX, &owner->group_updates[group]))
            return 0;
    return 1;
}

static int write_vector(FILE *file, const double values[CGAI_LIFE_CONTEXT_FEATURES]) {
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
        if (fprintf(file, "%a ", values[i]) < 0)
            return 0;
    return fputc('\n', file) != EOF;
}

static int read_vector(FILE *file, double values[CGAI_LIFE_CONTEXT_FEATURES], double minimum) {
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
        if (!bounded_real(file, minimum, 1.0, &values[i]))
            return 0;
    return 1;
}

static int write_action_model(FILE *file, const life_context_action_model *model, size_t group,
                              size_t action) {
    return fprintf(file, "MODEL %zu %zu %" PRIu64 " %" PRIu64 "\n", group, action,
                   model->observations, model->steps) >= 0 &&
           write_vector(file, model->centroid) && write_vector(file, model->readout);
}

static uint32_t model_count(const life_context_choice_state *choice, uint32_t groups) {
    uint32_t count = 0U;
    for (size_t group = 0U; group < groups; ++group)
        for (size_t action = 0U; action < CGAI_LIFE_CONTEXT_ACTIONS; ++action)
            count += choice->models[group][action].observations != 0U ? 1U : 0U;
    return count;
}

static int write_models(FILE *file, const life_context_choice_state *choice, uint32_t groups) {
    if (fprintf(file, "MODELS %" PRIu32 "\n", model_count(choice, groups)) < 0)
        return 0;
    for (size_t group = 0U; group < groups; ++group)
        for (size_t action = 0U; action < CGAI_LIFE_CONTEXT_ACTIONS; ++action) {
            const life_context_action_model *model = &choice->models[group][action];
            if (model->observations != 0U && !write_action_model(file, model, group, action))
                return 0;
        }
    return 1;
}

static int read_action_model(FILE *file, life_context_choice_state *choice, uint32_t groups) {
    uint32_t group, action;
    if (!label(file, "MODEL") || !number32(file, groups - 1U, &group) ||
        !number32(file, CGAI_LIFE_CONTEXT_ACTIONS - 1U, &action))
        return 0;
    life_context_action_model *model = &choice->models[group][action];
    if (model->observations != 0U || !number(file, UINT64_MAX, &model->observations) ||
        model->observations == 0U || !number(file, UINT64_MAX, &model->steps))
        return 0;
    return read_vector(file, model->centroid, 0.0) && read_vector(file, model->readout, -1.0);
}

static int read_models(FILE *file, life_context_choice_state *choice, uint32_t groups) {
    uint32_t count;
    if (!label(file, "MODELS") || !number32(file, groups * CGAI_LIFE_CONTEXT_ACTIONS, &count))
        return 0;
    for (uint32_t i = 0U; i < count; ++i)
        if (!read_action_model(file, choice, groups))
            return 0;
    return 1;
}

static int write_pending(FILE *file, const life_context_choice_state *choice) {
    const cgai_life_context_choice *decision = &choice->decision;
    if (choice->pending == 0U)
        return 1;
    return fprintf(file,
                   "PENDING %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu32 " %" PRIu32
                   " %" PRIu32 " %" PRIu32 " %" PRIu32 " %a\n",
                   decision->token, decision->input_hash, decision->model_version,
                   decision->world_hash, decision->generation, decision->conflict_id,
                   decision->participant_mask, decision->action, decision->supported_groups,
                   decision->predicted_utility) >= 0 &&
           write_bytes(file, "INPUT", choice->input);
}

static int read_pending_fields(FILE *file, cgai_life_context_choice *decision,
                               uint32_t group_mask) {
    return label(file, "PENDING") && number(file, UINT64_MAX, &decision->token) &&
           number(file, UINT64_MAX, &decision->input_hash) &&
           number(file, UINT64_MAX, &decision->model_version) &&
           number(file, UINT64_MAX, &decision->world_hash) &&
           number32(file, UINT32_MAX, &decision->generation) &&
           number32(file, UINT32_MAX, &decision->conflict_id) &&
           number32(file, group_mask, &decision->participant_mask) &&
           number32(file, CGAI_LIFE_CONTEXT_ACTIONS - 1U, &decision->action) &&
           number32(file, group_mask, &decision->supported_groups) &&
           bounded_real(file, -1.0, 1.0, &decision->predicted_utility);
}

static int read_pending(FILE *file, life_context_choice_state *choice, uint32_t groups) {
    if (choice->pending == 0U)
        return 1;
    return read_pending_fields(file, &choice->decision, (1U << groups) - 1U) &&
           read_bytes(file, "INPUT", choice->input, CGAI_LIFE_CONTEXT_TEXT_BYTES) &&
           life_context_encode(choice->input, choice->features);
}

static int write_choice_state(FILE *file, const life_context_choice_state *choice,
                              uint32_t groups) {
    if (fprintf(file,
                "CHOICE_STATE %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu32 " %" PRIu32
                " %" PRIu32 "\nCHOICE_STEPS",
                choice->version, choice->decisions, choice->observations, choice->deferred,
                choice->consumed_generation, choice->consumed_mask, choice->pending) < 0)
        return 0;
    for (size_t group = 0U; group < groups; ++group)
        if (fprintf(file, " %" PRIu64, choice->group_steps[group]) < 0)
            return 0;
    return fputc('\n', file) != EOF && write_models(file, choice, groups) &&
           write_pending(file, choice);
}

static int read_choice_counters(FILE *file, life_context_choice_state *choice) {
    return label(file, "CHOICE_STATE") && number(file, UINT64_MAX, &choice->version) &&
           number(file, UINT64_MAX, &choice->decisions) &&
           number(file, UINT64_MAX, &choice->observations) &&
           number(file, UINT64_MAX, &choice->deferred) &&
           number32(file, UINT32_MAX, &choice->consumed_generation) &&
           number32(file, 15U, &choice->consumed_mask) && number32(file, 1U, &choice->pending);
}

static int read_choice_state(FILE *file, life_context_choice_state *choice, uint32_t groups) {
    if (!read_choice_counters(file, choice) || !label(file, "CHOICE_STEPS"))
        return 0;
    for (size_t group = 0U; group < groups; ++group)
        if (!number(file, UINT64_MAX, &choice->group_steps[group]))
            return 0;
    return read_models(file, choice, groups) && read_pending(file, choice, groups);
}

static int write_owner(FILE *file, const void *state) {
    const cgai_life_context *owner = (const cgai_life_context *)state;
    const uint32_t groups = life_context_group_count(owner);
    if (!write_state(file, owner))
        return 0;
    for (uint32_t i = 0U; i < owner->count; ++i)
        if (!write_record(file, &owner->records[i], groups))
            return 0;
    return write_choice_state(file, &owner->choice, groups) && fputs("LIFE\n", file) >= 0 &&
           life_snapshot_write(file, &owner->run) &&
           fprintf(file, "CONTEXT_HASH %" PRIu64 "\nEND_CONTEXT\n",
                   cgai_life_context_hash(owner)) >= 0;
}

static int trailing_space(FILE *file) {
    int next;
    while ((next = fgetc(file)) != EOF)
        if (!isspace((unsigned char)next))
            return 0;
    return !ferror(file);
}

static int allocate_records(cgai_life_context *owner) {
    if (owner->count == 0U)
        return 1;
    owner->records = (life_context_record *)calloc(owner->count, sizeof(*owner->records));
    return owner->records != NULL;
}

static int read_records(FILE *file, cgai_life_context *owner) {
    const uint32_t groups = life_context_group_count(owner);
    for (uint32_t i = 0U; i < owner->count; ++i)
        if (!read_record(file, &owner->records[i], groups))
            return 0;
    return 1;
}

static int read_owner(FILE *file, cgai_life_context *owner) {
    uint64_t expected_hash;
    uint32_t version;
    if (!read_state(file, owner, &version) || !read_groups(file, owner) ||
        !allocate_records(owner) || !read_records(file, owner))
        return 0;
    const uint32_t groups = life_context_group_count(owner);
    if (version >= 2U && !read_choice_state(file, &owner->choice, groups))
        return 0;
    if (!label(file, "LIFE") || !life_snapshot_read(file, &owner->run) ||
        life_context_group_count(owner) != groups)
        return 0;
    const uint64_t actual_hash =
        version == 1U ? life_context_legacy_hash(owner) : cgai_life_context_hash(owner);
    return label(file, "CONTEXT_HASH") && number(file, UINT64_MAX, &expected_hash) &&
           label(file, "END_CONTEXT") && trailing_space(file) && life_context_state_valid(owner) &&
           actual_hash == expected_hash;
}

cgai_life_status cgai_life_context_save(const cgai_life_context *owner, const char *path) {
    if (owner == NULL || path == NULL || path[0] == '\0')
        return CGAI_LIFE_INVALID_ARGUMENT;
    if (!life_context_state_valid(owner))
        return CGAI_LIFE_ENGINE_ERROR;
    return life_checkpoint_publish(path, owner, write_owner) ? CGAI_LIFE_OK : CGAI_LIFE_IO_ERROR;
}

static cgai_life_status load_candidate(const char *path, cgai_life_context *candidate) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return CGAI_LIFE_IO_ERROR;
    int ok = read_owner(file, candidate);
    if (fclose(file) != 0)
        ok = 0;
    return ok ? CGAI_LIFE_OK : CGAI_LIFE_IO_ERROR;
}

cgai_life_status cgai_life_context_load(cgai_life_context *owner, const char *path) {
    if (owner == NULL || path == NULL || path[0] == '\0')
        return CGAI_LIFE_INVALID_ARGUMENT;
    cgai_life_context *candidate = (cgai_life_context *)calloc(1U, sizeof(*candidate));
    if (candidate == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    const cgai_life_status status = load_candidate(path, candidate);
    if (status == CGAI_LIFE_OK) {
        life_run_destroy(&owner->run);
        free(owner->records);
        *owner = *candidate;
        free(candidate);
    } else {
        cgai_life_context_destroy(candidate);
    }
    return status;
}
