/** @file life_context.c @brief Native source reconstruction prototypes fitted at Life contacts. */
#include "life_context.h"
#include "life_events.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CONTEXT_HASH_START UINT64_C(14695981039346656037)
#define CONTEXT_HASH_PRIME UINT64_C(1099511628211)
#define CONTEXT_FIT_BATCH 64U
#define CONTEXT_ENCOUNTER_GENERATIONS 32U
#define CONTEXT_GROUP_MASK ((1U << CGAI_LIFE_GROUPS) - 1U)
#define CONTEXT_LAST_START (UINT32_MAX - 9U)
#define CONTEXT_QUERY_RESULTS 16U

uint32_t life_context_group_count(const cgai_life_context *owner) {
    return owner == NULL                         ? 0U
           : owner->run.config.group_count == 0U ? CGAI_LIFE_DEFAULT_GROUPS
                                                 : owner->run.config.group_count;
}

uint32_t life_context_group_mask(const cgai_life_context *owner) {
    const uint32_t count = life_context_group_count(owner);
    return count >= CGAI_LIFE_MIN_GROUPS && count <= CGAI_LIFE_GROUPS ? (1U << count) - 1U : 0U;
}

typedef struct context_token {
    const char *text;
    size_t size;
} context_token;

static uint64_t hash_bytes(uint64_t hash, const void *bytes, size_t size) {
    const unsigned char *data = bytes;
    for (size_t i = 0U; i < size; ++i) {
        hash ^= data[i];
        hash *= CONTEXT_HASH_PRIME;
    }
    return hash;
}

uint64_t life_context_bytes_hash(const void *bytes, size_t size) {
    return bytes == NULL && size != 0U ? 0U : hash_bytes(CONTEXT_HASH_START, bytes, size);
}

static uint64_t hash_word(uint64_t hash, uint64_t value) {
    for (size_t i = 0U; i < 8U; ++i) {
        const unsigned char byte = (unsigned char)(value & 255U);
        hash = hash_bytes(hash, &byte, 1U);
        value >>= 8U;
    }
    return hash;
}

static uint64_t hash_string(uint64_t hash, const char *text) {
    const size_t size = strlen(text);
    return hash_bytes(hash_word(hash, size), text, size);
}

static uint64_t hash_double(uint64_t hash, double value) {
    uint64_t bits = 0U;
    _Static_assert(sizeof(double) == sizeof(bits), "Context hashes require 64-bit doubles");
    memcpy(&bits, &value, sizeof(bits));
    return hash_word(hash, bits);
}

static int text_length(const char *text, size_t limit, size_t *size) {
    size_t length = 0U;
    if (text == NULL)
        return 0;
    while (length <= limit && text[length] != '\0')
        ++length;
    if (length == 0U || length > limit)
        return 0;
    *size = length;
    return 1;
}

static unsigned char ascii_lower(unsigned char value) {
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static int ascii_word(unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_';
}

static int next_token(const char **cursor, context_token *token) {
    while (**cursor != '\0' && !ascii_word((unsigned char)**cursor))
        ++*cursor;
    token->text = *cursor;
    while (ascii_word((unsigned char)**cursor))
        ++*cursor;
    token->size = (size_t)(*cursor - token->text);
    return token->size != 0U;
}

static int tokens_equal(const context_token *first, const context_token *second) {
    if (first->size != second->size)
        return 0;
    for (size_t i = 0U; i < first->size; ++i)
        if (ascii_lower((unsigned char)first->text[i]) !=
            ascii_lower((unsigned char)second->text[i]))
            return 0;
    return 1;
}

static int token_is_word(const context_token *token, const char *word) {
    const context_token candidate = {word, strlen(word)};
    return tokens_equal(token, &candidate);
}

static int token_has_letter(const context_token *token) {
    for (size_t i = 0U; i < token->size; ++i) {
        const unsigned char value = ascii_lower((unsigned char)token->text[i]);
        if (value >= 'a' && value <= 'z')
            return 1;
    }
    return 0;
}

static int meaningful_token(const context_token *token) {
    static const char *const ignored[] = {
        "the",  "and",  "for",   "with", "that",  "this",   "from",  "into",  "are",   "was",
        "were", "will", "would", "can",  "could", "should", "what",  "where", "which", "when",
        "how",  "does", "have",  "has",  "its",   "our",    "you",   "your",  "not",   "all",
        "any",  "but",  "than",  "then", "these", "those",  "about", "please"};
    if (token->size < 3U || !token_has_letter(token))
        return 0;
    for (size_t i = 0U; i < sizeof(ignored) / sizeof(ignored[0]); ++i)
        if (token_is_word(token, ignored[i]))
            return 0;
    return 1;
}

static uint64_t token_hash(const context_token *token) {
    uint64_t hash = CONTEXT_HASH_START;
    for (size_t i = 0U; i < token->size; ++i) {
        const unsigned char value = ascii_lower((unsigned char)token->text[i]);
        hash = hash_bytes(hash, &value, 1U);
    }
    return hash;
}

static int next_piece(const context_token *token, size_t *offset, context_token *piece) {
    const size_t start = *offset;
    while (*offset < token->size && token->text[*offset] != '_')
        ++*offset;
    piece->text = token->text + start;
    piece->size = *offset - start;
    if (*offset < token->size)
        ++*offset;
    return start < token->size;
}

static void token_features(const context_token *token, double weight,
                           double features[CGAI_LIFE_CONTEXT_FEATURES]) {
    context_token piece;
    size_t offset = 0U;
    if (meaningful_token(token))
        features[token_hash(token) % CGAI_LIFE_CONTEXT_FEATURES] += weight;
    if (memchr(token->text, '_', token->size) == NULL)
        return;
    while (next_piece(token, &offset, &piece))
        if (meaningful_token(&piece))
            features[token_hash(&piece) % CGAI_LIFE_CONTEXT_FEATURES] += weight;
}

static void text_features(const char *text, double weight,
                          double features[CGAI_LIFE_CONTEXT_FEATURES]) {
    context_token token;
    while (next_token(&text, &token))
        token_features(&token, weight, features);
}

static void normalize_features(double features[CGAI_LIFE_CONTEXT_FEATURES]) {
    double squared = 0.0;
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
        squared += features[i] * features[i];
    if (squared != 0.0) {
        const double norm = sqrt(squared);
        for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
            features[i] /= norm;
    }
}

int life_context_encode(const char *text, double features[CGAI_LIFE_CONTEXT_FEATURES]) {
    size_t size = 0U;
    if (features == NULL || !text_length(text, CGAI_LIFE_CONTEXT_TEXT_BYTES, &size))
        return 0;
    memset(features, 0, CGAI_LIFE_CONTEXT_FEATURES * sizeof(*features));
    text_features(text, 1.0, features);
    normalize_features(features);
    return 1;
}

static void record_features(const life_context_record *record,
                            double features[CGAI_LIFE_CONTEXT_FEATURES]) {
    memset(features, 0, CGAI_LIFE_CONTEXT_FEATURES * sizeof(*features));
    text_features(record->source, 2.0, features);
    text_features(record->text, 1.0, features);
    normalize_features(features);
}

static int token_in_token(const context_token *wanted, const context_token *found) {
    context_token piece;
    size_t offset = 0U;
    if (tokens_equal(wanted, found))
        return 1;
    while (next_piece(found, &offset, &piece))
        if (tokens_equal(wanted, &piece))
            return 1;
    return 0;
}

static int token_in_text(const context_token *wanted, const char *text) {
    context_token found;
    while (next_token(&text, &found))
        if (token_in_token(wanted, &found))
            return 1;
    return 0;
}

static int lexical_overlap(const char *query, const life_context_record *record) {
    context_token token;
    while (next_token(&query, &token))
        if (meaningful_token(&token) &&
            (token_in_text(&token, record->source) || token_in_text(&token, record->text)))
            return 1;
    return 0;
}

static int record_metadata_valid(const life_context_record *record) {
    size_t source_size = 0U, text_size = 0U;
    return record != NULL &&
           text_length(record->source, CGAI_LIFE_CONTEXT_SOURCE_BYTES, &source_size) &&
           text_length(record->text, CGAI_LIFE_CONTEXT_TEXT_BYTES, &text_size) &&
           record->kind <= CGAI_LIFE_CONTEXT_ACTIVITY && record->split <= CGAI_LIFE_CONTEXT_AUDIT &&
           record->reviewed <= 1U && record->eligible_mask != 0U &&
           (record->eligible_mask & ~CONTEXT_GROUP_MASK) == 0U &&
           record->last_line >= record->first_line &&
           ((record->first_line == 0U) == (record->last_line == 0U)) &&
           record->content_hash == life_context_bytes_hash(record->text, text_size);
}

static int learned_metadata_valid(const life_context_record *record) {
    return (record->learned_mask & ~record->eligible_mask) == 0U &&
           (record->learned_mask == 0U ||
            (record->reviewed == 1U && record->split == CGAI_LIFE_CONTEXT_TRAIN));
}

static int prototype_valid(const life_context_record *record, size_t group,
                           const double features[CGAI_LIFE_CONTEXT_FEATURES]) {
    const int learned = (record->learned_mask & (1U << group)) != 0U;
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i) {
        const double expected = learned ? features[i] : 0.0;
        if (!isfinite(record->centroids[group][i]) || record->centroids[group][i] != expected)
            return 0;
    }
    return 1;
}

int life_context_record_valid(const life_context_record *record) {
    double features[CGAI_LIFE_CONTEXT_FEATURES];
    if (!record_metadata_valid(record) || !learned_metadata_valid(record))
        return 0;
    record_features(record, features);
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i)
        if (!prototype_valid(record, i, features))
            return 0;
    return 1;
}

static int same_admission(const life_context_record *first, const life_context_record *second) {
    return first->first_line == second->first_line && first->last_line == second->last_line &&
           first->family == second->family && first->kind == second->kind &&
           first->split == second->split && first->reviewed == second->reviewed &&
           first->eligible_mask == second->eligible_mask &&
           strcmp(first->source, second->source) == 0 && strcmp(first->text, second->text) == 0;
}

static int incompatible_split(const life_context_record *first, const life_context_record *second) {
    return first->split != second->split &&
           ((first->family != 0U && first->family == second->family) ||
            (first->content_hash == second->content_hash &&
             strcmp(first->text, second->text) == 0));
}

static int records_compatible(const cgai_life_context *owner) {
    for (uint32_t i = 0U; i < owner->count; ++i)
        for (uint32_t j = 0U; j < i; ++j)
            if (same_admission(&owner->records[i], &owner->records[j]) ||
                incompatible_split(&owner->records[i], &owner->records[j]))
                return 0;
    return 1;
}

static int group_state_valid(const cgai_life_context *owner, size_t group) {
    uint64_t learned = 0U;
    if ((owner->count == 0U && owner->cursors[group] != 0U) ||
        (owner->count != 0U && owner->cursors[group] >= owner->count))
        return 0;
    for (uint32_t i = 0U; i < owner->count; ++i)
        learned += (owner->records[i].learned_mask & (1U << group)) != 0U ? 1U : 0U;
    return learned == owner->group_updates[group];
}

static int record_groups_valid(const cgai_life_context *owner, const life_context_record *record) {
    const double zero[CGAI_LIFE_CONTEXT_FEATURES] = {0};
    if ((record->eligible_mask & ~life_context_group_mask(owner)) != 0U)
        return 0;
    for (size_t group = life_context_group_count(owner); group < CGAI_LIFE_GROUPS; ++group)
        if (memcmp(record->centroids[group], zero, sizeof(zero)) != 0)
            return 0;
    return 1;
}

static int unused_groups_valid(const cgai_life_context *owner) {
    for (size_t group = life_context_group_count(owner); group < CGAI_LIFE_GROUPS; ++group)
        if (owner->cursors[group] != 0U || owner->group_updates[group] != 0U)
            return 0;
    return 1;
}

static int context_recipe_valid(const cgai_life_context *owner) {
    return owner->run.policy != NULL && owner->run.config.seed <= UINT32_MAX &&
           owner->run.config.scenario <= 3U && owner->run.config.training_epochs <= 64U &&
           owner->run.config.mode == LIFE_MODE_LEARNED && owner->run.config.enable_merges == 0 &&
           life_context_group_mask(owner) != 0U &&
           owner->run.world.group_count == life_context_group_count(owner) &&
           life_policy_group_count(owner->run.policy) == life_context_group_count(owner) &&
           life_policy_active_mask(owner->run.policy) == life_context_group_mask(owner);
}

static int contact_state_valid(const cgai_life_context *owner) {
    return owner->contact_events <= (uint64_t)owner->run.world.tick * LIFE_MAX_PATCHES &&
           owner->domain_updates <=
               owner->contact_events *
                   (uint64_t)(CONTEXT_FIT_BATCH * life_context_group_count(owner)) &&
           (owner->run.config.training_epochs != 0U || owner->domain_updates == 0U);
}

int life_context_state_valid(const cgai_life_context *owner) {
    uint64_t total = 0U;
    if (owner == NULL || owner->count > CGAI_LIFE_CONTEXT_MAX_RECORDS ||
        (owner->count != 0U && owner->records == NULL) || !context_recipe_valid(owner) ||
        !contact_state_valid(owner) || !unused_groups_valid(owner))
        return 0;
    for (uint32_t i = 0U; i < owner->count; ++i)
        if (!life_context_record_valid(&owner->records[i]) ||
            !record_groups_valid(owner, &owner->records[i]))
            return 0;
    for (size_t group = 0U; group < life_context_group_count(owner); ++group) {
        if (!group_state_valid(owner, group))
            return 0;
        total += owner->group_updates[group];
    }
    return total == owner->domain_updates && records_compatible(owner) &&
           life_context_choice_valid(owner);
}

static int context_config_valid(const cgai_life_config *config) {
    return config != NULL && config->seed <= UINT32_MAX && config->scenario <= 3U &&
           config->training_epochs <= CGAI_LIFE_MAX_TRAINING_EPOCHS &&
           config->mode == CGAI_LIFE_LEARNED && config->enable_merges == 0U &&
           (config->group_count == 0U || (config->group_count >= CGAI_LIFE_MIN_GROUPS &&
                                          config->group_count <= CGAI_LIFE_GROUPS));
}

cgai_life_status cgai_life_context_create(const cgai_life_config *config,
                                          cgai_life_context **output) {
    cgai_life_context *candidate;
    life_run_config recipe;
    if (!context_config_valid(config) || output == NULL || *output != NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    candidate = calloc(1U, sizeof(*candidate));
    if (candidate == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    recipe = (life_run_config){
        config->seed,       config->scenario, config->training_epochs, LIFE_MODE_LEARNED, 0,
        config->group_count};
    if (!life_run_init(&candidate->run, &recipe)) {
        cgai_life_context_destroy(candidate);
        return CGAI_LIFE_OUT_OF_MEMORY;
    }
    *output = candidate;
    return CGAI_LIFE_OK;
}

void cgai_life_context_destroy(cgai_life_context *owner) {
    if (owner != NULL) {
        life_run_destroy(&owner->run);
        free(owner->records);
        free(owner);
    }
}

static int input_record(const cgai_life_context_input *input, life_context_record *record) {
    size_t source_size = 0U, text_size = 0U;
    if (input == NULL ||
        !text_length(input->source, CGAI_LIFE_CONTEXT_SOURCE_BYTES, &source_size) ||
        !text_length(input->text, CGAI_LIFE_CONTEXT_TEXT_BYTES, &text_size))
        return 0;
    memset(record, 0, sizeof(*record));
    memcpy(record->source, input->source, source_size);
    memcpy(record->text, input->text, text_size);
    record->first_line = input->first_line;
    record->last_line = input->last_line;
    record->family = input->family;
    record->kind = (uint32_t)input->kind;
    record->split = (uint32_t)input->split;
    record->reviewed = input->reviewed;
    record->eligible_mask = input->eligible_mask;
    record->content_hash = life_context_bytes_hash(record->text, text_size);
    return record_metadata_valid(record);
}

static cgai_life_status append_record(cgai_life_context *owner, const life_context_record *record) {
    life_context_record *storage;
    if (owner->count == CGAI_LIFE_CONTEXT_MAX_RECORDS)
        return CGAI_LIFE_LIMIT_REACHED;
    storage = realloc(owner->records, ((size_t)owner->count + 1U) * sizeof(*storage));
    if (storage == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    owner->records = storage;
    owner->records[owner->count] = *record;
    ++owner->count;
    return CGAI_LIFE_OK;
}

cgai_life_status cgai_life_context_add_record(cgai_life_context *owner,
                                              const cgai_life_context_input *input,
                                              uint32_t *record_id) {
    life_context_record record;
    if (owner == NULL || record_id == NULL || !input_record(input, &record) ||
        !record_groups_valid(owner, &record))
        return CGAI_LIFE_INVALID_ARGUMENT;
    for (uint32_t i = 0U; i < owner->count; ++i) {
        if (same_admission(&record, &owner->records[i])) {
            *record_id = i + 1U;
            return CGAI_LIFE_OK;
        }
        if (incompatible_split(&record, &owner->records[i]))
            return CGAI_LIFE_INVALID_ARGUMENT;
    }
    const cgai_life_status status = append_record(owner, &record);
    if (status == CGAI_LIFE_OK)
        *record_id = owner->count;
    return status;
}

cgai_life_status cgai_life_context_add(cgai_life_context *owner,
                                       const cgai_life_context_input *input) {
    uint32_t record_id;
    return cgai_life_context_add_record(owner, input, &record_id);
}

static int record_needs_fit(const life_context_record *record, uint32_t bit) {
    return record->reviewed == 1U && record->split == CGAI_LIFE_CONTEXT_TRAIN &&
           (record->eligible_mask & bit) != 0U && (record->learned_mask & bit) == 0U;
}

static void fit_group(cgai_life_context *owner, size_t group) {
    const uint32_t bit = 1U << group;
    uint32_t scanned = 0U, fitted = 0U;
    while (scanned < owner->count && fitted < CONTEXT_FIT_BATCH) {
        life_context_record *record = &owner->records[owner->cursors[group]];
        owner->cursors[group] = (owner->cursors[group] + 1U) % owner->count;
        ++scanned;
        if (record_needs_fit(record, bit)) {
            record_features(record, record->centroids[group]);
            record->learned_mask |= bit;
            ++fitted;
        }
    }
    owner->group_updates[group] += fitted;
    owner->domain_updates += fitted;
}

static void fit_contacts(cgai_life_context *owner, const cgai_life_events *events) {
    owner->contact_events += events->event_count;
    if (owner->run.config.training_epochs == 0U)
        return;
    for (size_t event = 0U; event < events->event_count; ++event)
        for (size_t group = 0U; group < life_context_group_count(owner); ++group)
            if ((events->events[event].participant_mask & (1U << group)) != 0U)
                fit_group(owner, group);
}

static int contacts_counters_valid(const cgai_life_context *owner, const cgai_life_events *events) {
    const uint64_t maximum = (uint64_t)events->event_count * CONTEXT_FIT_BATCH;
    if (events->event_count > UINT64_MAX - owner->contact_events ||
        maximum * life_context_group_count(owner) > UINT64_MAX - owner->domain_updates)
        return 0;
    for (size_t group = 0U; group < life_context_group_count(owner); ++group)
        if (maximum > UINT64_MAX - owner->group_updates[group])
            return 0;
    return 1;
}

static int renew_encounter(life_run *candidate) {
    life_world renewed;
    if (candidate->world.tick == 0U || candidate->world.tick % CONTEXT_ENCOUNTER_GENERATIONS != 0U)
        return 1;
    /* Placement depends only on the fixed recipe and generation phase. Late records
     * encounter the same authored fixtures, without injecting labels into cells. */
    if (life_world_init_groups(&renewed, (uint32_t)candidate->config.seed,
                               candidate->config.scenario,
                               candidate->config.group_count) != LIFE_OK)
        return 0;
    renewed.tick = candidate->world.tick;
    renewed.stats = candidate->world.stats;
    memcpy(renewed.entities, candidate->world.entities, sizeof(renewed.entities));
    renewed.next_uid = candidate->world.next_uid;
    renewed.next_conflict_id = candidate->world.next_conflict_id;
    candidate->world = renewed;
    return 1;
}

static cgai_life_status tick_candidate(const cgai_life_context *owner, life_run *candidate,
                                       cgai_life_events *events) {
    life_run source = owner->run;
    if (!renew_encounter(candidate))
        return CGAI_LIFE_ENGINE_ERROR;
    source.world = candidate->world;
    if (!life_run_tick(candidate))
        return CGAI_LIFE_ENGINE_ERROR;
    life_events_capture(&source, candidate, 0, events);
    return CGAI_LIFE_OK;
}

static cgai_life_status train_candidate(const cgai_life_context *owner, life_run *candidate,
                                        cgai_life_events *events) {
    *candidate = owner->run;
    if (candidate->world.tick > CONTEXT_LAST_START)
        return CGAI_LIFE_LIMIT_REACHED;
    candidate->policy = life_policy_clone(owner->run.policy);
    if (candidate->policy == NULL)
        return CGAI_LIFE_OUT_OF_MEMORY;
    const cgai_life_status status = tick_candidate(owner, candidate, events);
    if (status != CGAI_LIFE_OK) {
        life_run_destroy(candidate);
        return status;
    }
    return CGAI_LIFE_OK;
}

static cgai_life_status train_generation(cgai_life_context *owner) {
    life_run candidate;
    cgai_life_events events;
    const cgai_life_status status = train_candidate(owner, &candidate, &events);
    if (status != CGAI_LIFE_OK)
        return status;
    if (!contacts_counters_valid(owner, &events)) {
        life_run_destroy(&candidate);
        return CGAI_LIFE_LIMIT_REACHED;
    }
    fit_contacts(owner, &events);
    life_run_destroy(&owner->run);
    owner->run = candidate;
    owner->events = events;
    return CGAI_LIFE_OK;
}

cgai_life_status cgai_life_context_train_step(cgai_life_context *owner, uint32_t generations,
                                              uint32_t *completed) {
    cgai_life_status status = CGAI_LIFE_OK;
    uint32_t committed = 0U;
    if (owner == NULL || owner->choice.pending != 0U || generations == 0U ||
        generations > CGAI_LIFE_MAX_STEP_GENERATIONS)
        return CGAI_LIFE_INVALID_ARGUMENT;
    while (committed < generations) {
        status = train_generation(owner);
        if (status != CGAI_LIFE_OK)
            break;
        ++committed;
    }
    if (completed != NULL)
        *completed = committed;
    return status;
}

static double prototype_score(const double features[CGAI_LIFE_CONTEXT_FEATURES],
                              const double centroid[CGAI_LIFE_CONTEXT_FEATURES]) {
    double score = 0.0;
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
        score += features[i] * centroid[i];
    return score > 1.0 ? 1.0 : score;
}

static double record_score(const life_context_record *record,
                           const double features[CGAI_LIFE_CONTEXT_FEATURES]) {
    double score = 0.0;
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        if ((record->learned_mask & (1U << group)) != 0U) {
            const double candidate = prototype_score(features, record->centroids[group]);
            if (candidate > score)
                score = candidate;
        }
    return score;
}

static void result_from_record(const life_context_record *record, uint32_t id, double score,
                               cgai_life_context_result *result) {
    memset(result, 0, sizeof(*result));
    memcpy(result->source, record->source, sizeof(result->source));
    memcpy(result->text, record->text, sizeof(result->text));
    result->first_line = record->first_line;
    result->last_line = record->last_line;
    result->record_id = id;
    result->kind = (cgai_life_context_kind)record->kind;
    result->learned_mask = record->learned_mask;
    result->content_hash = record->content_hash;
    result->score = score;
}

static void ranked_insert(cgai_life_context_result *results, size_t capacity, size_t *count,
                          const life_context_record *record, uint32_t id, double score) {
    size_t position = 0U;
    while (position < *count && results[position].score >= score)
        ++position;
    if (position >= capacity)
        return;
    if (*count < capacity)
        ++*count;
    for (size_t i = *count - 1U; i > position; --i)
        results[i] = results[i - 1U];
    result_from_record(record, id, score, &results[position]);
}

static void query_records(const cgai_life_context *owner, const char *query,
                          const double features[CGAI_LIFE_CONTEXT_FEATURES], uint32_t kind,
                          cgai_life_context_result *results, size_t capacity, size_t *count) {
    for (uint32_t i = 0U; i < owner->count; ++i) {
        const life_context_record *record = &owner->records[i];
        if ((kind == UINT32_MAX || record->kind == kind) && record->learned_mask != 0U &&
            record->reviewed == 1U && record->split == CGAI_LIFE_CONTEXT_TRAIN &&
            lexical_overlap(query, record))
            ranked_insert(results, capacity, count, record, i + 1U, record_score(record, features));
    }
}

static cgai_life_status context_query(const cgai_life_context *owner, const char *query,
                                      uint32_t kind, cgai_life_context_result *results,
                                      size_t capacity, size_t *count) {
    double features[CGAI_LIFE_CONTEXT_FEATURES];
    cgai_life_context_result selected[CONTEXT_QUERY_RESULTS];
    size_t selected_count = 0U;
    if (owner == NULL || results == NULL || count == NULL || capacity == 0U ||
        capacity > CONTEXT_QUERY_RESULTS || !life_context_encode(query, features))
        return CGAI_LIFE_INVALID_ARGUMENT;
    query_records(owner, query, features, kind, selected, capacity, &selected_count);
    memcpy(results, selected, selected_count * sizeof(*selected));
    *count = selected_count;
    return CGAI_LIFE_OK;
}

cgai_life_status cgai_life_context_query(const cgai_life_context *owner, const char *query,
                                         cgai_life_context_result *results, size_t capacity,
                                         size_t *count) {
    return context_query(owner, query, UINT32_MAX, results, capacity, count);
}

cgai_life_status cgai_life_context_query_kind(const cgai_life_context *owner, const char *query,
                                              cgai_life_context_kind kind,
                                              cgai_life_context_result *results, size_t capacity,
                                              size_t *count) {
    if ((uint32_t)kind > CGAI_LIFE_CONTEXT_ACTIVITY)
        return CGAI_LIFE_INVALID_ARGUMENT;
    return context_query(owner, query, (uint32_t)kind, results, capacity, count);
}

static uint64_t record_source_hash(uint64_t hash, const life_context_record *record) {
    hash = hash_string(hash, record->source);
    hash = hash_string(hash, record->text);
    hash = hash_word(hash, record->first_line);
    hash = hash_word(hash, record->last_line);
    hash = hash_word(hash, record->family);
    hash = hash_word(hash, record->kind);
    hash = hash_word(hash, record->split);
    hash = hash_word(hash, record->reviewed);
    hash = hash_word(hash, record->eligible_mask);
    return hash_word(hash, record->content_hash);
}

static uint64_t source_hash(const cgai_life_context *owner) {
    uint64_t hash = hash_word(CONTEXT_HASH_START, owner->count);
    for (uint32_t i = 0U; i < owner->count; ++i)
        hash = record_source_hash(hash, &owner->records[i]);
    return hash;
}

static uint64_t group_hash(const cgai_life_context *owner, size_t group) {
    uint64_t hash = hash_word(CONTEXT_HASH_START, owner->group_updates[group]);
    hash = hash_word(hash, owner->cursors[group]);
    for (uint32_t i = 0U; i < owner->count; ++i) {
        const life_context_record *record = &owner->records[i];
        if ((record->learned_mask & (1U << group)) != 0U) {
            hash = hash_word(hash, i + 1U);
            hash = hash_word(hash, record->content_hash);
            for (size_t coordinate = 0U; coordinate < CGAI_LIFE_CONTEXT_FEATURES; ++coordinate)
                hash = hash_double(hash, record->centroids[group][coordinate]);
        }
    }
    return hash;
}

static void context_group_stats(const cgai_life_context *owner, cgai_life_context_stats *stats) {
    for (size_t group = 0U; group < life_context_group_count(owner); ++group) {
        stats->group_updates[group] = owner->group_updates[group];
        stats->group_hashes[group] = group_hash(owner, group);
    }
}

cgai_life_status cgai_life_context_get_stats(const cgai_life_context *owner,
                                             cgai_life_context_stats *output) {
    cgai_life_context_stats result = {0};
    if (owner == NULL || output == NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    result.group_count = life_context_group_count(owner);
    result.records = owner->count;
    result.generation = owner->run.world.tick;
    result.domain_updates = owner->domain_updates;
    result.contact_events = owner->contact_events;
    result.source_hash = source_hash(owner);
    for (uint32_t i = 0U; i < owner->count; ++i)
        result.trained_records += owner->records[i].learned_mask != 0U ? 1U : 0U;
    result.deferred_records = result.records - result.trained_records;
    context_group_stats(owner, &result);
    *output = result;
    return CGAI_LIFE_OK;
}

static uint64_t record_state_hash(uint64_t hash, const life_context_record *record,
                                  uint32_t groups) {
    hash = record_source_hash(hash, record);
    hash = hash_word(hash, record->learned_mask);
    for (size_t group = 0U; group < groups; ++group)
        for (size_t coordinate = 0U; coordinate < CGAI_LIFE_CONTEXT_FEATURES; ++coordinate)
            hash = hash_double(hash, record->centroids[group][coordinate]);
    return hash;
}

uint64_t life_context_legacy_hash(const cgai_life_context *owner) {
    uint64_t hash;
    if (owner == NULL)
        return 0U;
    hash = hash_word(CONTEXT_HASH_START, life_run_hash(&owner->run));
    hash = hash_word(hash, owner->count);
    hash = hash_word(hash, owner->domain_updates);
    hash = hash_word(hash, owner->contact_events);
    for (size_t group = 0U; group < life_context_group_count(owner); ++group) {
        hash = hash_word(hash, owner->cursors[group]);
        hash = hash_word(hash, owner->group_updates[group]);
    }
    for (uint32_t i = 0U; i < owner->count; ++i)
        hash = record_state_hash(hash, &owner->records[i], life_context_group_count(owner));
    return hash;
}

uint64_t cgai_life_context_hash(const cgai_life_context *owner) {
    return owner == NULL
               ? 0U
               : hash_word(life_context_legacy_hash(owner), life_context_choice_hash(owner));
}
