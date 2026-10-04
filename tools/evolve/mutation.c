/** @file mutation.c @brief Exact finite literal substitution without working-tree writes. */
#include "mutation.h"
#include <stdlib.h>
#include <string.h>

typedef struct mutation_catalog {
    const char *prefix;
    const char *suffix;
    const char *literals[EVOLVE_MUTATION_CHOICES];
} mutation_catalog;

static const mutation_catalog catalogs[EVOLVE_MUTATION_SITES] = {
    {"model->embeddings[index] = random_scalar(state, ", ");", {"0.03", "0.04", "0.05"}},
    {"model->encoder[i] = random_scalar(state, ",
     " / sqrt((double)model->input_count));",
     {"0.75", "1.0", "1.25"}},
    {"model->outer[i] = random_scalar(&state, ", ");", {"0.25", "0.3", "0.35"}},
    {"model->inner[i] = random_scalar(&state, ", ");", {"0.25", "0.3", "0.35"}}};

static const uint32_t action_bits[23] = {0U,  0U, 1U,  2U,  4U,  8U,  16U, 32U, 3U,  5U,  9U, 17U,
                                         33U, 6U, 10U, 18U, 34U, 12U, 20U, 36U, 24U, 40U, 48U};

typedef struct mutation_scanner {
    const char *bytes;
    size_t size;
    size_t cursor;
    int valid;
} mutation_scanner;

typedef struct mutation_cursor {
    size_t source;
    size_t candidate;
} mutation_cursor;

const char *evolve_mutation_literal(uint32_t site, uint32_t choice) {
    return site < EVOLVE_MUTATION_SITES && choice < EVOLVE_MUTATION_CHOICES
               ? catalogs[site].literals[choice]
               : NULL;
}

static int identifier_start(char value) {
    return value == '_' || (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

static int identifier_part(char value) {
    return identifier_start(value) || (value >= '0' && value <= '9');
}

static int bytes_match(const char *bytes, size_t size, size_t offset, const char *text) {
    const size_t length = strlen(text);
    return offset <= size && length <= size - offset && memcmp(bytes + offset, text, length) == 0;
}

static int lexical_bytes_valid(const char *bytes, size_t size) {
    for (size_t i = 0U; i < size; ++i) {
        if (bytes[i] == '\0')
            return 0;
        if (bytes[i] == '\\' && i + 1U < size && (bytes[i + 1U] == '\n' || bytes[i + 1U] == '\r'))
            return 0;
        if (i + 2U < size && bytes[i] == '?' && bytes[i + 1U] == '?' && bytes[i + 2U] == '/')
            return 0;
    }
    return 1;
}

static void skip_quote(mutation_scanner *scanner) {
    const char quote = scanner->bytes[scanner->cursor++];
    while (scanner->cursor < scanner->size) {
        const char value = scanner->bytes[scanner->cursor++];
        if (value == quote)
            return;
        if (value == '\n' || value == '\r')
            break;
        if (value == '\\') {
            if (scanner->cursor == scanner->size)
                break;
            ++scanner->cursor;
        }
    }
    scanner->valid = 0;
}

static void skip_line_comment(mutation_scanner *scanner) {
    scanner->cursor += 2U;
    while (scanner->cursor < scanner->size && scanner->bytes[scanner->cursor] != '\n')
        ++scanner->cursor;
}

static void skip_directive(mutation_scanner *scanner) {
    while (scanner->cursor < scanner->size && scanner->bytes[scanner->cursor] != '\n')
        ++scanner->cursor;
}

static void skip_block_comment(mutation_scanner *scanner) {
    scanner->cursor += 2U;
    while (scanner->cursor + 1U < scanner->size) {
        if (scanner->bytes[scanner->cursor] == '*' && scanner->bytes[scanner->cursor + 1U] == '/') {
            scanner->cursor += 2U;
            return;
        }
        ++scanner->cursor;
    }
    scanner->valid = 0;
}

static int skip_comment(mutation_scanner *scanner) {
    if (scanner->bytes[scanner->cursor] == '/' && scanner->cursor + 1U < scanner->size) {
        const char next = scanner->bytes[scanner->cursor + 1U];
        if (next == '/') {
            skip_line_comment(scanner);
            return 1;
        }
        if (next == '*') {
            skip_block_comment(scanner);
            return 1;
        }
    }
    return 0;
}

static int skip_noncode(mutation_scanner *scanner) {
    const char value = scanner->bytes[scanner->cursor];
    if (value == '"' || value == '\'') {
        skip_quote(scanner);
        return 1;
    }
    if (value == '#') {
        skip_directive(scanner);
        return 1;
    }
    return skip_comment(scanner);
}

static int number_start(const mutation_scanner *scanner) {
    const char value = scanner->bytes[scanner->cursor];
    return (value >= '0' && value <= '9') ||
           (value == '.' && scanner->cursor + 1U < scanner->size &&
            scanner->bytes[scanner->cursor + 1U] >= '0' &&
            scanner->bytes[scanner->cursor + 1U] <= '9');
}

static void skip_number(mutation_scanner *scanner) {
    char previous = scanner->bytes[scanner->cursor++];
    while (scanner->cursor < scanner->size) {
        const char value = scanner->bytes[scanner->cursor];
        if (!identifier_part(value) && value != '.' &&
            !((value == '+' || value == '-') &&
              (previous == 'e' || previous == 'E' || previous == 'p' || previous == 'P')))
            return;
        previous = value;
        ++scanner->cursor;
    }
}

static int next_identifier(mutation_scanner *scanner, size_t *offset) {
    while (scanner->cursor < scanner->size && scanner->valid) {
        if (skip_noncode(scanner))
            continue;
        if (number_start(scanner)) {
            skip_number(scanner);
            continue;
        }
        if (identifier_start(scanner->bytes[scanner->cursor])) {
            *offset = scanner->cursor++;
            while (scanner->cursor < scanner->size &&
                   identifier_part(scanner->bytes[scanner->cursor]))
                ++scanner->cursor;
            return 1;
        }
        ++scanner->cursor;
    }
    return 0;
}

static int record_literal(evolve_mutation_source *source, size_t offset, uint32_t site) {
    const mutation_catalog *catalog = &catalogs[site];
    const size_t literal_offset = offset + strlen(catalog->prefix);
    for (uint32_t choice = 0U; choice < EVOLVE_MUTATION_CHOICES; ++choice) {
        const size_t length = strlen(catalog->literals[choice]);
        if (bytes_match(source->bytes, source->size, literal_offset, catalog->literals[choice]) &&
            bytes_match(source->bytes, source->size, literal_offset + length, catalog->suffix)) {
            source->profile.choices[site] = choice;
            source->literal_offsets[site] = literal_offset;
            source->literal_lengths[site] = (uint32_t)length;
            return 1;
        }
    }
    return 0;
}

static int scan_anchor(evolve_mutation_source *source, size_t offset, uint32_t *seen) {
    for (uint32_t site = 0U; site < EVOLVE_MUTATION_SITES; ++site) {
        const uint32_t bit = UINT32_C(1) << site;
        if (!bytes_match(source->bytes, source->size, offset, catalogs[site].prefix))
            continue;
        if ((*seen & bit) != 0U || !record_literal(source, offset, site))
            return 0;
        *seen |= bit;
    }
    return 1;
}

static int scan_source(evolve_mutation_source *source) {
    mutation_scanner scanner = {source->bytes, source->size, 0U, 1};
    uint32_t seen = 0U;
    size_t offset = 0U;
    while (next_identifier(&scanner, &offset))
        if (!scan_anchor(source, offset, &seen))
            return 0;
    return scanner.valid && seen == (UINT32_C(1) << EVOLVE_MUTATION_SITES) - 1U;
}

static uint64_t checksum_bytes(const char *bytes, size_t size) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0U; i < size; ++i)
        hash = (hash ^ (uint8_t)bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

evolve_mutation_status evolve_mutation_validate(const char *bytes, size_t size,
                                                evolve_mutation_source *output) {
    evolve_mutation_source source = {0};
    if (bytes == NULL || output == NULL || size == 0U || size > EVOLVE_MUTATION_MAX_SOURCE_BYTES ||
        !lexical_bytes_valid(bytes, size))
        return EVOLVE_MUTATION_INVALID;
    source.bytes = bytes;
    source.size = size;
    if (!scan_source(&source))
        return EVOLVE_MUTATION_INVALID;
    source.checksum = checksum_bytes(bytes, size);
    memcpy(output, &source, sizeof(source));
    return EVOLVE_MUTATION_OK;
}

static int source_unchanged(const evolve_mutation_source *source, evolve_mutation_source *checked) {
    if (source == NULL ||
        evolve_mutation_validate(source->bytes, source->size, checked) != EVOLVE_MUTATION_OK)
        return 0;
    return source->checksum == checked->checksum &&
           memcmp(&source->profile, &checked->profile, sizeof(source->profile)) == 0 &&
           memcmp(source->literal_offsets, checked->literal_offsets,
                  sizeof(source->literal_offsets)) == 0 &&
           memcmp(source->literal_lengths, checked->literal_lengths,
                  sizeof(source->literal_lengths)) == 0;
}

static uint32_t legal_actions(uint32_t count) {
    const uint32_t cells = (UINT32_C(1) << count) - 1U;
    uint32_t allowed = 0U;
    for (uint32_t i = 0U; i < 23U; ++i)
        if ((action_bits[i] & ~cells) == 0U)
            allowed |= UINT32_C(1) << i;
    return allowed;
}

static int valid_event(const cgai_life_collision_event *event) {
    if (event == NULL || event->generation == 0U || event->conflict_id == 0U ||
        event->participant_mask == 0U || event->participant_mask >= (1U << CGAI_LIFE_GROUPS) ||
        event->frontier_count > CGAI_LIFE_MAX_FRONTIER_CELLS || event->selected_output >= 23U)
        return 0;
    if (event->legal_output_mask != legal_actions(event->frontier_count) ||
        (event->legal_output_mask & (UINT32_C(1) << event->selected_output)) == 0U ||
        event->toggle_bits != action_bits[event->selected_output])
        return 0;
    for (size_t i = 0U; i < event->frontier_count; ++i)
        if (event->frontier_cells[i] >= CGAI_LIFE_WIDTH * CGAI_LIFE_HEIGHT ||
            (i != 0U && event->frontier_cells[i - 1U] >= event->frontier_cells[i]))
            return 0;
    return 1;
}

static uint64_t checksum_word(uint64_t hash, uint64_t value) {
    for (uint32_t i = 0U; i < 8U; ++i) {
        hash = (hash ^ (value & UINT64_C(255))) * UINT64_C(1099511628211);
        value >>= 8U;
    }
    return hash;
}

static uint32_t distinct_site(uint64_t hash, uint32_t used) {
    uint32_t site = (uint32_t)(hash % EVOLVE_MUTATION_SITES);
    while ((used & (UINT32_C(1) << site)) != 0U)
        site = (site + 1U) % EVOLVE_MUTATION_SITES;
    return site;
}

static uint64_t mix_selection(uint64_t hash) {
    hash = (hash ^ (hash >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    hash = (hash ^ (hash >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return hash ^ (hash >> 31U);
}

static void record_edit(const evolve_mutation_source *source, uint64_t hash, uint32_t site,
                        evolve_mutation_candidate *candidate) {
    evolve_mutation_edit *edit = &candidate->edits[candidate->edit_count++];
    const uint32_t before = source->profile.choices[site];
    const uint32_t after = (before + 1U + (uint32_t)((hash >> 32U) % 2U)) % EVOLVE_MUTATION_CHOICES;
    edit->site = site;
    edit->before_choice = before;
    edit->after_choice = after;
    edit->source_offset = source->literal_offsets[site];
    memcpy(edit->before_literal, catalogs[site].literals[before],
           strlen(catalogs[site].literals[before]) + 1U);
    memcpy(edit->after_literal, catalogs[site].literals[after],
           strlen(catalogs[site].literals[after]) + 1U);
    candidate->profile.choices[site] = after;
}

static void sort_edits(evolve_mutation_candidate *candidate) {
    if (candidate->edit_count == 2U &&
        candidate->edits[0].source_offset > candidate->edits[1].source_offset) {
        evolve_mutation_edit temporary;
        memcpy(&temporary, &candidate->edits[0], sizeof(temporary));
        memcpy(&candidate->edits[0], &candidate->edits[1], sizeof(temporary));
        memcpy(&candidate->edits[1], &temporary, sizeof(temporary));
    }
}

static void map_edits(const evolve_mutation_source *source, const cgai_life_collision_event *event,
                      evolve_mutation_candidate *candidate) {
    const uint64_t seed = checksum_word(source->checksum, event->source_world_hash);
    uint32_t used = 0U;
    memcpy(&candidate->profile, &source->profile, sizeof(candidate->profile));
    candidate->domain_frontier_bits = event->toggle_bits;
    for (uint32_t i = 0U; i < event->frontier_count; ++i) {
        if ((event->toggle_bits & (UINT32_C(1) << i)) != 0U) {
            const uint64_t hash =
                mix_selection(checksum_word(checksum_word(seed, event->frontier_cells[i]), i));
            const uint32_t site = distinct_site(hash, used);
            record_edit(source, hash, site, candidate);
            used |= UINT32_C(1) << site;
        }
    }
    sort_edits(candidate);
}

static uint64_t contact_seed(const cgai_life_collision_event *event) {
    return mix_selection(checksum_word(checksum_word(event->source_world_hash, event->generation),
                                       event->conflict_id));
}

static uint32_t domain_frontier_bits(const cgai_life_collision_event *event, uint64_t seed) {
    const uint32_t first = (uint32_t)(seed % event->frontier_count);
    uint32_t bits = UINT32_C(1) << first;
    if (event->frontier_count > 1U && ((seed >> 32U) & 1U) != 0U) {
        uint32_t second = (uint32_t)((seed >> 16U) % (event->frontier_count - 1U));
        if (second >= first)
            ++second;
        bits |= UINT32_C(1) << second;
    }
    return bits;
}

static void map_domain_edits(const evolve_mutation_source *source,
                             const cgai_life_collision_event *event,
                             evolve_mutation_candidate *candidate) {
    const uint64_t seed = contact_seed(event);
    uint32_t used = 0U;
    candidate->profile = source->profile;
    candidate->domain_frontier_bits = domain_frontier_bits(event, seed);
    for (uint32_t i = 0U; i < event->frontier_count; ++i)
        if ((candidate->domain_frontier_bits & (UINT32_C(1) << i)) != 0U) {
            const uint64_t hash =
                mix_selection(checksum_word(checksum_word(seed, event->frontier_cells[i]), i));
            const uint32_t site = distinct_site(hash, used);
            record_edit(source, hash, site, candidate);
            used |= UINT32_C(1) << site;
        }
    sort_edits(candidate);
}

static size_t candidate_size(const evolve_mutation_source *source,
                             const evolve_mutation_candidate *candidate) {
    size_t size = source->size;
    for (size_t i = 0U; i < candidate->edit_count; ++i)
        size = size - strlen(candidate->edits[i].before_literal) +
               strlen(candidate->edits[i].after_literal);
    return size;
}

static void append_edit(const evolve_mutation_source *source, evolve_mutation_candidate *candidate,
                        size_t index, mutation_cursor *cursor) {
    evolve_mutation_edit *edit = &candidate->edits[index];
    const size_t prefix = edit->source_offset - cursor->source;
    const size_t after = strlen(edit->after_literal);
    memcpy(candidate->bytes + cursor->candidate, source->bytes + cursor->source, prefix);
    cursor->candidate += prefix;
    edit->candidate_offset = cursor->candidate;
    memcpy(candidate->bytes + cursor->candidate, edit->after_literal, after);
    cursor->candidate += after;
    cursor->source = edit->source_offset + strlen(edit->before_literal);
}

static evolve_mutation_status assemble_candidate(const evolve_mutation_source *source,
                                                 evolve_mutation_candidate *candidate) {
    mutation_cursor cursor = {0U, 0U};
    candidate->size = candidate_size(source, candidate);
    if (candidate->size > EVOLVE_MUTATION_MAX_SOURCE_BYTES)
        return EVOLVE_MUTATION_INVALID;
    candidate->bytes = malloc(candidate->size + 1U);
    if (candidate->bytes == NULL)
        return EVOLVE_MUTATION_OUT_OF_MEMORY;
    for (size_t i = 0U; i < candidate->edit_count; ++i)
        append_edit(source, candidate, i, &cursor);
    memcpy(candidate->bytes + cursor.candidate, source->bytes + cursor.source,
           source->size - cursor.source);
    candidate->bytes[candidate->size] = '\0';
    candidate->checksum = checksum_bytes(candidate->bytes, candidate->size);
    return EVOLVE_MUTATION_OK;
}

evolve_mutation_status evolve_mutation_generate(const evolve_mutation_source *source,
                                                const cgai_life_collision_event *event,
                                                evolve_mutation_candidate *output) {
    evolve_mutation_source checked;
    evolve_mutation_candidate candidate = {0};
    if (output == NULL || output->bytes != NULL || !source_unchanged(source, &checked) ||
        !valid_event(event))
        return EVOLVE_MUTATION_INVALID;
    if (event->toggle_bits == 0U)
        return EVOLVE_MUTATION_NO_EDIT;
    candidate.source_checksum = checked.checksum;
    candidate.event_generation = event->generation;
    candidate.event_conflict_id = event->conflict_id;
    candidate.event_source_world_hash = event->source_world_hash;
    map_edits(&checked, event, &candidate);
    const evolve_mutation_status status = assemble_candidate(&checked, &candidate);
    if (status != EVOLVE_MUTATION_OK)
        return status;
    memcpy(output, &candidate, sizeof(candidate));
    return EVOLVE_MUTATION_OK;
}

uint32_t evolve_mutation_profile_action(const evolve_mutation_profile *profile) {
    uint32_t action = 0U, factor = 1U;
    if (profile == NULL)
        return UINT32_MAX;
    for (size_t site = 0U; site < EVOLVE_MUTATION_SITES; ++site) {
        if (profile->choices[site] >= EVOLVE_MUTATION_CHOICES)
            return UINT32_MAX;
        action += factor * profile->choices[site];
        factor *= EVOLVE_MUTATION_CHOICES;
    }
    return action;
}

static void enumerate_profiles(const evolve_mutation_source *source,
                               const evolve_mutation_candidate *mapped, uint32_t *actions) {
    const uint32_t count = UINT32_C(1) << mapped->edit_count;
    for (uint32_t alternative = 0U; alternative < count; ++alternative) {
        evolve_mutation_profile profile = source->profile;
        for (uint32_t edit = 0U; edit < mapped->edit_count; ++edit) {
            const uint32_t site = mapped->edits[edit].site;
            const uint32_t delta = 1U + ((alternative >> edit) & 1U);
            profile.choices[site] = (profile.choices[site] + delta) % EVOLVE_MUTATION_CHOICES;
        }
        actions[alternative] = evolve_mutation_profile_action(&profile);
    }
}

evolve_mutation_status evolve_mutation_alternatives(
    const evolve_mutation_source *source, const cgai_life_collision_event *event,
    uint32_t actions[EVOLVE_MUTATION_MAX_ALTERNATIVES], size_t *count, uint32_t *fallback) {
    evolve_mutation_source checked;
    evolve_mutation_candidate mapped = {0};
    uint32_t alternatives[EVOLVE_MUTATION_MAX_ALTERNATIVES] = {0};
    if (actions == NULL || count == NULL || fallback == NULL ||
        !source_unchanged(source, &checked) || !valid_event(event))
        return EVOLVE_MUTATION_INVALID;
    if (event->frontier_count == 0U)
        return EVOLVE_MUTATION_NO_EDIT;
    map_domain_edits(&checked, event, &mapped);
    enumerate_profiles(&checked, &mapped, alternatives);
    memcpy(actions, alternatives, sizeof(alternatives));
    *count = (size_t)1U << mapped.edit_count;
    *fallback = evolve_mutation_profile_action(&mapped.profile);
    return EVOLVE_MUTATION_OK;
}

static int select_profile(evolve_mutation_candidate *candidate, uint32_t action) {
    evolve_mutation_profile profile;
    for (size_t site = 0U; site < EVOLVE_MUTATION_SITES; ++site) {
        profile.choices[site] = action % EVOLVE_MUTATION_CHOICES;
        action /= EVOLVE_MUTATION_CHOICES;
    }
    for (size_t i = 0U; i < candidate->edit_count; ++i) {
        evolve_mutation_edit *edit = &candidate->edits[i];
        edit->after_choice = profile.choices[edit->site];
        memcpy(edit->after_literal, catalogs[edit->site].literals[edit->after_choice],
               strlen(catalogs[edit->site].literals[edit->after_choice]) + 1U);
    }
    candidate->profile = profile;
    return action == 0U;
}

static int allowed_profile(uint32_t action, const uint32_t *actions, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (actions[i] == action)
            return 1;
    return 0;
}

static void prepare_selected_candidate(const evolve_mutation_source *source,
                                       const cgai_life_collision_event *event, uint32_t action,
                                       evolve_mutation_candidate *candidate) {
    map_domain_edits(source, event, candidate);
    (void)select_profile(candidate, action);
    candidate->source_checksum = source->checksum;
    candidate->event_generation = event->generation;
    candidate->event_conflict_id = event->conflict_id;
    candidate->event_source_world_hash = event->source_world_hash;
}

evolve_mutation_status evolve_mutation_select(const evolve_mutation_source *source,
                                              const cgai_life_collision_event *event,
                                              uint32_t action, evolve_mutation_candidate *output) {
    uint32_t actions[EVOLVE_MUTATION_MAX_ALTERNATIVES], fallback;
    size_t count;
    if (output == NULL || output->bytes != NULL)
        return EVOLVE_MUTATION_INVALID;
    const evolve_mutation_status alternatives =
        evolve_mutation_alternatives(source, event, actions, &count, &fallback);
    if (alternatives != EVOLVE_MUTATION_OK)
        return alternatives;
    if (!allowed_profile(action, actions, count))
        return EVOLVE_MUTATION_INVALID;
    evolve_mutation_candidate candidate = {0};
    prepare_selected_candidate(source, event, action, &candidate);
    const evolve_mutation_status status = assemble_candidate(source, &candidate);
    if (status == EVOLVE_MUTATION_OK)
        memcpy(output, &candidate, sizeof(candidate));
    return status;
}

void evolve_mutation_destroy(evolve_mutation_candidate *candidate) {
    if (candidate != NULL) {
        free(candidate->bytes);
        memset(candidate, 0, sizeof(*candidate));
    }
}
