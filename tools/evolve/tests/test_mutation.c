/** @file test_mutation.c @brief Finite source-site and collision provenance acceptance. */
#include "mutation.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, "Source mutation check failed")

typedef struct test_source {
    char *bytes;
    size_t size;
} test_source;

typedef struct test_cursor {
    size_t source;
    size_t candidate;
} test_cursor;

static const uint32_t expected_bits[23] = {0U,  0U, 1U,  2U,  4U,  8U,  16U, 32U, 3U,  5U,  9U, 17U,
                                           33U, 6U, 10U, 18U, 34U, 12U, 20U, 36U, 24U, 40U, 48U};
static const char *expected_literals[4][3] = {{"0.03", "0.04", "0.05"},
                                              {"0.75", "1.0", "1.25"},
                                              {"0.25", "0.3", "0.35"},
                                              {"0.25", "0.3", "0.35"}};

static test_source read_source(const char *path) {
    test_source source = {0};
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    source.bytes = malloc(EVOLVE_MUTATION_MAX_SOURCE_BYTES + 2U);
    CHECK(source.bytes != NULL);
    source.size = fread(source.bytes, 1U, EVOLVE_MUTATION_MAX_SOURCE_BYTES + 1U, file);
    CHECK(source.size != 0U && source.size <= EVOLVE_MUTATION_MAX_SOURCE_BYTES && !ferror(file) &&
          feof(file));
    CHECK(fclose(file) == 0);
    source.bytes[source.size] = '\0';
    return source;
}

static uint64_t reference_checksum(const char *bytes, size_t size) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0U; i < size; ++i) {
        hash ^= (unsigned char)bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static test_source replace_text(const test_source *source, const char *old,
                                const char *replacement) {
    const char *location = strstr(source->bytes, old);
    CHECK(location != NULL);
    const size_t offset = (size_t)(location - source->bytes);
    const size_t old_size = strlen(old);
    const size_t new_size = strlen(replacement);
    test_source result = {NULL, source->size - old_size + new_size};
    result.bytes = malloc(result.size + 1U);
    CHECK(result.bytes != NULL);
    memcpy(result.bytes, source->bytes, offset);
    memcpy(result.bytes + offset, replacement, new_size);
    memcpy(result.bytes + offset + new_size, source->bytes + offset + old_size,
           source->size - offset - old_size + 1U);
    return result;
}

static test_source append_text(const test_source *source, const char *suffix) {
    const size_t length = strlen(suffix);
    test_source result = {malloc(source->size + length + 1U), source->size + length};
    CHECK(result.bytes != NULL);
    memcpy(result.bytes, source->bytes, source->size);
    memcpy(result.bytes + source->size, suffix, length + 1U);
    return result;
}

static evolve_mutation_source validate_source(const test_source *source) {
    evolve_mutation_source result;
    CHECK(evolve_mutation_validate(source->bytes, source->size, &result) == EVOLVE_MUTATION_OK);
    CHECK(result.bytes == source->bytes && result.size == source->size);
    CHECK(result.checksum == reference_checksum(source->bytes, source->size));
    return result;
}

static cgai_life_collision_event event_for(uint32_t action) {
    cgai_life_collision_event event = {0};
    event.generation = 1U;
    event.conflict_id = 2U;
    event.conflict_age = 1U;
    event.participant_mask = 3U;
    event.group_uids[0] = 1U;
    event.group_uids[1] = 2U;
    event.group_ancestry[0] = 1U;
    event.group_ancestry[1] = 2U;
    event.frontier_count = 6U;
    for (uint32_t i = 0U; i < event.frontier_count; ++i)
        event.frontier_cells[i] = 10U + i;
    event.selected_output = action;
    event.toggle_bits = expected_bits[action];
    event.legal_output_mask = UINT32_C(0x7fffff);
    event.source_world_hash = UINT64_C(0x12345678abcdef01);
    return event;
}

static uint32_t toggle_count(uint32_t bits) {
    uint32_t count = 0U;
    while (bits != 0U) {
        count += bits & 1U;
        bits >>= 1U;
    }
    return count;
}

static void check_edit(const evolve_mutation_source *source,
                       const evolve_mutation_candidate *candidate,
                       const evolve_mutation_edit *edit) {
    CHECK(edit->site < 4U && edit->before_choice < 3U && edit->after_choice < 3U);
    CHECK(edit->before_choice == source->profile.choices[edit->site]);
    CHECK(edit->before_choice != edit->after_choice);
    CHECK(edit->after_choice == candidate->profile.choices[edit->site]);
    CHECK(edit->source_offset == source->literal_offsets[edit->site]);
    CHECK(strcmp(edit->before_literal, expected_literals[edit->site][edit->before_choice]) == 0);
    CHECK(strcmp(edit->after_literal, expected_literals[edit->site][edit->after_choice]) == 0);
    CHECK(memcmp(source->bytes + edit->source_offset, edit->before_literal,
                 strlen(edit->before_literal)) == 0);
    CHECK(memcmp(candidate->bytes + edit->candidate_offset, edit->after_literal,
                 strlen(edit->after_literal)) == 0);
}

static void preserved_segment(const evolve_mutation_source *source,
                              const evolve_mutation_candidate *candidate,
                              const evolve_mutation_edit *edit, test_cursor *cursor) {
    CHECK(edit->source_offset >= cursor->source);
    const size_t length = edit->source_offset - cursor->source;
    CHECK(edit->candidate_offset == cursor->candidate + length);
    CHECK(memcmp(source->bytes + cursor->source, candidate->bytes + cursor->candidate, length) ==
          0);
    cursor->source = edit->source_offset + strlen(edit->before_literal);
    cursor->candidate = edit->candidate_offset + strlen(edit->after_literal);
}

static void all_other_bytes_preserved(const evolve_mutation_source *source,
                                      const evolve_mutation_candidate *candidate) {
    test_cursor cursor = {0U, 0U};
    CHECK(candidate->edit_count >= 1U && candidate->edit_count <= 2U);
    for (size_t i = 0U; i < candidate->edit_count; ++i) {
        check_edit(source, candidate, &candidate->edits[i]);
        preserved_segment(source, candidate, &candidate->edits[i], &cursor);
    }
    CHECK(candidate->size - cursor.candidate == source->size - cursor.source);
    CHECK(memcmp(source->bytes + cursor.source, candidate->bytes + cursor.candidate,
                 source->size - cursor.source) == 0);
    CHECK(candidate->bytes[candidate->size] == '\0');
    CHECK(candidate->checksum == reference_checksum(candidate->bytes, candidate->size));
    CHECK(candidate->source_checksum == source->checksum);
    if (candidate->edit_count == 2U)
        CHECK(candidate->edits[0].site != candidate->edits[1].site);
}

static void correct_profile(const evolve_mutation_candidate *candidate) {
    evolve_mutation_source parsed;
    CHECK(evolve_mutation_validate(candidate->bytes, candidate->size, &parsed) ==
          EVOLVE_MUTATION_OK);
    CHECK(memcmp(&parsed.profile, &candidate->profile, sizeof(parsed.profile)) == 0);
}

static void same_candidates(const evolve_mutation_candidate *first,
                            const evolve_mutation_candidate *second) {
    CHECK(first->size == second->size && first->checksum == second->checksum);
    CHECK(first->source_checksum == second->source_checksum);
    CHECK(first->edit_count == second->edit_count);
    CHECK(memcmp(first->bytes, second->bytes, first->size + 1U) == 0);
    CHECK(memcmp(first->edits, second->edits, sizeof(first->edits)) == 0);
    CHECK(memcmp(&first->profile, &second->profile, sizeof(first->profile)) == 0);
    CHECK(first->event_generation == second->event_generation);
    CHECK(first->event_conflict_id == second->event_conflict_id);
    CHECK(first->event_source_world_hash == second->event_source_world_hash);
}

static void check_no_edit(const evolve_mutation_source *source, uint32_t action) {
    const cgai_life_collision_event event = event_for(action);
    evolve_mutation_candidate output = {0};
    output.event_generation = 991U;
    const evolve_mutation_candidate before = output;
    CHECK(evolve_mutation_generate(source, &event, &output) == EVOLVE_MUTATION_NO_EDIT);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void check_action(const evolve_mutation_source *source, uint32_t action) {
    const cgai_life_collision_event event = event_for(action);
    evolve_mutation_candidate first = {0};
    evolve_mutation_candidate second = {0};
    CHECK(evolve_mutation_generate(source, &event, &first) == EVOLVE_MUTATION_OK);
    CHECK(evolve_mutation_generate(source, &event, &second) == EVOLVE_MUTATION_OK);
    CHECK(first.edit_count == toggle_count(event.toggle_bits));
    CHECK(first.event_generation == event.generation &&
          first.event_conflict_id == event.conflict_id);
    CHECK(first.event_source_world_hash == event.source_world_hash);
    all_other_bytes_preserved(source, &first);
    same_candidates(&first, &second);
    correct_profile(&first);
    evolve_mutation_destroy(&first);
    evolve_mutation_destroy(&second);
    CHECK(first.bytes == NULL && first.edit_count == 0U);
}

static void check_actions(const evolve_mutation_source *source) {
    check_no_edit(source, 0U);
    check_no_edit(source, 1U);
    for (uint32_t action = 2U; action < 23U; ++action)
        check_action(source, action);
}

static void expect_invalid_source(const test_source *source) {
    evolve_mutation_source output;
    memset(&output, 0x5a, sizeof(output));
    const evolve_mutation_source before = output;
    CHECK(evolve_mutation_validate(source->bytes, source->size, &output) ==
          EVOLVE_MUTATION_INVALID);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void reject_replacement(const test_source *source, const char *old,
                               const char *replacement) {
    const test_source changed = replace_text(source, old, replacement);
    expect_invalid_source(&changed);
    free(changed.bytes);
}

static void invalid_registered_statements(const test_source *source) {
    reject_replacement(source, "random_scalar(state, 0.04)", "random_scalar(state, 0.06)");
    reject_replacement(source, "model->outer[i] =", "model->outer[j] =");
    reject_replacement(source, "model->outer[i] =", "#define SITE model->outer[i] =");
    reject_replacement(source, "model->inner[i] = random_scalar(&state, 0.3);",
                       "model->inner[i] = random_scalar(&state, 0.3f);");
    reject_replacement(source, "model->outer[i] = random_scalar(&state, 0.3);",
                       "model->outer[i] = random_scalar(&state, 0.3 + 0.0);");
    const test_source duplicate =
        append_text(source, "\nmodel->outer[i] = random_scalar(&state, 0.3);\n");
    expect_invalid_source(&duplicate);
    free(duplicate.bytes);
}

static void quoted_and_comment_sites(const test_source *source) {
    const char *ignored =
        "\n/* model->outer[i] = random_scalar(&state, 0.3); */\n"
        "// model->inner[i] = random_scalar(&state, 0.3);\n"
        "const char *text = \"model->embeddings[index] = random_scalar(state, 0.04);\";\n"
        "const char quote = '\\'';\n";
    const test_source changed = append_text(source, ignored);
    const evolve_mutation_source parsed = validate_source(&changed);
    CHECK(parsed.profile.choices[0] == 1U && parsed.profile.choices[3] == 1U);
    check_action(&parsed, 8U);
    free(changed.bytes);
    reject_replacement(source, "model->outer[i] = random_scalar(&state, 0.3);",
                       "/* model->outer[i] = random_scalar(&state, 0.3); */");
    reject_replacement(source, "model->inner[i] = random_scalar(&state, 0.3);",
                       "\"model->inner[i] = random_scalar(&state, 0.3);\"");
}

static void invalid_lexical_text(const test_source *source) {
    const test_source comment = append_text(source, "\n/* unfinished");
    const test_source quote = append_text(source, "\n\"unfinished");
    const test_source splice = append_text(source, "\n// comment\\\n");
    const test_source number = replace_text(source, "model->outer[i] =", "7model->outer[i] =");
    expect_invalid_source(&comment);
    expect_invalid_source(&quote);
    expect_invalid_source(&splice);
    expect_invalid_source(&number);
    free(comment.bytes);
    free(quote.bytes);
    free(splice.bytes);
    free(number.bytes);
}

static void invalid_source_bounds(const test_source *source) {
    evolve_mutation_source output;
    CHECK(evolve_mutation_validate(NULL, 1U, &output) == EVOLVE_MUTATION_INVALID);
    CHECK(evolve_mutation_validate(source->bytes, 0U, &output) == EVOLVE_MUTATION_INVALID);
    CHECK(evolve_mutation_validate(source->bytes, EVOLVE_MUTATION_MAX_SOURCE_BYTES + 1U, &output) ==
          EVOLVE_MUTATION_INVALID);
    CHECK(evolve_mutation_validate(source->bytes, source->size, NULL) == EVOLVE_MUTATION_INVALID);
    test_source nul = append_text(source, "extra");
    nul.bytes[source->size] = '\0';
    expect_invalid_source(&nul);
    free(nul.bytes);
    CHECK(evolve_mutation_literal(4U, 0U) == NULL && evolve_mutation_literal(0U, 3U) == NULL);
}

static void exact_source_limit(const test_source *source) {
    test_source bounded = {malloc(EVOLVE_MUTATION_MAX_SOURCE_BYTES + 1U),
                           EVOLVE_MUTATION_MAX_SOURCE_BYTES};
    CHECK(bounded.bytes != NULL && source->size < bounded.size);
    memcpy(bounded.bytes, source->bytes, source->size);
    memset(bounded.bytes + source->size, ' ', bounded.size - source->size);
    bounded.bytes[bounded.size] = '\0';
    const evolve_mutation_source parsed = validate_source(&bounded);
    const cgai_life_collision_event event = event_for(8U);
    evolve_mutation_candidate output = {0};
    output.event_generation = 93U;
    const evolve_mutation_candidate before = output;
    CHECK(evolve_mutation_generate(&parsed, &event, &output) == EVOLVE_MUTATION_INVALID);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    free(bounded.bytes);
}

static void rejected_event(const evolve_mutation_source *source,
                           const cgai_life_collision_event *event) {
    evolve_mutation_candidate output = {0};
    output.event_generation = 77U;
    const evolve_mutation_candidate before = output;
    CHECK(evolve_mutation_generate(source, event, &output) == EVOLVE_MUTATION_INVALID);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void invalid_actions(const evolve_mutation_source *source) {
    cgai_life_collision_event event = event_for(8U);
    event.toggle_bits = 7U;
    rejected_event(source, &event);
    event = event_for(8U);
    event.selected_output = 23U;
    rejected_event(source, &event);
    event = event_for(8U);
    event.legal_output_mask = 3U;
    rejected_event(source, &event);
    rejected_event(source, NULL);
}

static void invalid_frontiers(const evolve_mutation_source *source) {
    cgai_life_collision_event event = event_for(8U);
    event.frontier_count = 7U;
    rejected_event(source, &event);
    event = event_for(8U);
    event.frontier_cells[0] = 1024U;
    rejected_event(source, &event);
    event = event_for(8U);
    event.frontier_cells[1] = event.frontier_cells[0];
    rejected_event(source, &event);
    event = event_for(8U);
    event.generation = 0U;
    rejected_event(source, &event);
}

static void stale_and_owned_outputs(const test_source *source) {
    test_source changed = append_text(source, "\n");
    evolve_mutation_source parsed = validate_source(&changed);
    const cgai_life_collision_event event = event_for(8U);
    evolve_mutation_candidate candidate = {0};
    CHECK(evolve_mutation_generate(&parsed, &event, &candidate) == EVOLVE_MUTATION_OK);
    const evolve_mutation_candidate before = candidate;
    CHECK(evolve_mutation_generate(&parsed, &event, &candidate) == EVOLVE_MUTATION_INVALID);
    CHECK(memcmp(&candidate, &before, sizeof(candidate)) == 0);
    changed.bytes[changed.size - 1U] = ' ';
    rejected_event(&parsed, &event);
    CHECK(evolve_mutation_generate(NULL, &event, &(evolve_mutation_candidate){0}) ==
          EVOLVE_MUTATION_INVALID);
    CHECK(evolve_mutation_generate(&parsed, &event, NULL) == EVOLVE_MUTATION_INVALID);
    evolve_mutation_destroy(&candidate);
    free(changed.bytes);
    evolve_mutation_destroy(NULL);
}

static void accepted_parent(const evolve_mutation_source *source) {
    const cgai_life_collision_event event = event_for(8U);
    evolve_mutation_candidate parent = {0};
    evolve_mutation_candidate child = {0};
    CHECK(evolve_mutation_generate(source, &event, &parent) == EVOLVE_MUTATION_OK);
    const test_source parent_bytes = {parent.bytes, parent.size};
    const evolve_mutation_source parsed = validate_source(&parent_bytes);
    char *unchanged = malloc(parent.size + 1U);
    CHECK(unchanged != NULL);
    memcpy(unchanged, parent.bytes, parent.size + 1U);
    CHECK(evolve_mutation_generate(&parsed, &event, &child) == EVOLVE_MUTATION_OK);
    all_other_bytes_preserved(&parsed, &child);
    correct_profile(&child);
    CHECK(memcmp(unchanged, parent.bytes, parent.size + 1U) == 0);
    free(unchanged);
    evolve_mutation_destroy(&child);
    evolve_mutation_destroy(&parent);
}

static int edited_event(const cgai_life *owner, cgai_life_collision_event *output) {
    cgai_life_events batch;
    CHECK(cgai_life_get_events(owner, &batch) == CGAI_LIFE_OK);
    for (size_t i = 0U; i < batch.event_count; ++i) {
        if (batch.events[i].toggle_bits != 0U) {
            *output = batch.events[i];
            return 1;
        }
    }
    return 0;
}

static void actual_life_event(const evolve_mutation_source *source) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life *owner = NULL;
    cgai_life_collision_event event = {0};
    evolve_mutation_candidate candidate = {0};
    config.mode = CGAI_LIFE_TEACHER;
    config.enable_merges = 0U;
    CHECK(cgai_life_create(&config, &owner) == CGAI_LIFE_OK);
    for (uint32_t i = 0U; i < 16U && event.toggle_bits == 0U; ++i) {
        CHECK(cgai_life_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
        (void)edited_event(owner, &event);
    }
    CHECK(event.toggle_bits != 0U);
    CHECK(evolve_mutation_generate(source, &event, &candidate) == EVOLVE_MUTATION_OK);
    CHECK(candidate.edit_count == toggle_count(event.toggle_bits));
    all_other_bytes_preserved(source, &candidate);
    evolve_mutation_destroy(&candidate);
    cgai_life_destroy(owner);
}

static void all_catalog_parents(const test_source *source) {
    test_source changed =
        replace_text(source, "random_scalar(state, 0.04)", "random_scalar(state, 0.03)");
    test_source next =
        replace_text(&changed, "random_scalar(state, 1.0 /", "random_scalar(state, 1.25 /");
    free(changed.bytes);
    changed = replace_text(&next, "model->outer[i] = random_scalar(&state, 0.3);",
                           "model->outer[i] = random_scalar(&state, 0.35);");
    free(next.bytes);
    next = replace_text(&changed, "model->inner[i] = random_scalar(&state, 0.3);",
                        "model->inner[i] = random_scalar(&state, 0.25);");
    free(changed.bytes);
    const evolve_mutation_source parsed = validate_source(&next);
    CHECK(parsed.profile.choices[0] == 0U && parsed.profile.choices[1] == 2U);
    CHECK(parsed.profile.choices[2] == 2U && parsed.profile.choices[3] == 0U);
    check_actions(&parsed);
    free(next.bytes);
}

static void invalid_inputs(const test_source *source, const evolve_mutation_source *parsed) {
    invalid_registered_statements(source);
    quoted_and_comment_sites(source);
    invalid_lexical_text(source);
    invalid_source_bounds(source);
    invalid_actions(parsed);
    invalid_frontiers(parsed);
    stale_and_owned_outputs(source);
    exact_source_limit(source);
}

static void parent_unchanged(const test_source *source, const char *path) {
    const test_source original = read_source(path);
    CHECK(source->size == original.size);
    CHECK(memcmp(source->bytes, original.bytes, source->size + 1U) == 0);
    free(original.bytes);
}

static void selected_alternative(const evolve_mutation_source *source,
                                 const cgai_life_collision_event *event, uint32_t action) {
    evolve_mutation_candidate candidate = {0};
    CHECK(evolve_mutation_select(source, event, action, &candidate) == EVOLVE_MUTATION_OK);
    CHECK(evolve_mutation_profile_action(&candidate.profile) == action);
    CHECK(candidate.edit_count == toggle_count(candidate.domain_frontier_bits));
    CHECK(candidate.edit_count >= 1U && candidate.edit_count <= 2U);
    CHECK((candidate.domain_frontier_bits & ~((1U << event->frontier_count) - 1U)) == 0U);
    all_other_bytes_preserved(source, &candidate);
    evolve_mutation_source parsed;
    CHECK(evolve_mutation_validate(candidate.bytes, candidate.size, &parsed) == EVOLVE_MUTATION_OK);
    CHECK(evolve_mutation_profile_action(&parsed.profile) == action);
    evolve_mutation_destroy(&candidate);
}

static void check_alternatives(const evolve_mutation_source *source,
                               const cgai_life_collision_event *event) {
    uint32_t actions[EVOLVE_MUTATION_MAX_ALTERNATIVES], fallback;
    size_t count;
    CHECK(evolve_mutation_alternatives(source, event, actions, &count, &fallback) ==
          EVOLVE_MUTATION_OK);
    CHECK(count == 2U || count == 4U);
    uint32_t fallback_seen = 0U;
    for (size_t i = 0U; i < count; ++i) {
        CHECK(actions[i] < EVOLVE_MUTATION_PROFILE_ACTIONS);
        fallback_seen += actions[i] == fallback ? 1U : 0U;
        for (size_t j = 0U; j < i; ++j)
            CHECK(actions[i] != actions[j]);
        selected_alternative(source, event, actions[i]);
    }
    CHECK(fallback_seen == 1U);
}

static void complete_profile_choices(const evolve_mutation_source *source) {
    for (uint32_t output = 0U; output < 23U; ++output) {
        const cgai_life_collision_event event = event_for(output);
        check_alternatives(source, &event);
    }
}

static void domain_ignores_cell_teacher(const evolve_mutation_source *source) {
    const cgai_life_collision_event original = event_for(0U);
    uint32_t first[4], second[4], fallback_first, fallback_second;
    size_t count_first, count_second;
    CHECK(evolve_mutation_alternatives(source, &original, first, &count_first, &fallback_first) ==
          EVOLVE_MUTATION_OK);
    cgai_life_collision_event changed = event_for(22U);
    changed.teacher_target_valid = 1U;
    changed.teacher_target = 3U;
    CHECK(evolve_mutation_alternatives(source, &changed, second, &count_second, &fallback_second) ==
          EVOLVE_MUTATION_OK);
    CHECK(count_first == count_second && fallback_first == fallback_second);
    CHECK(memcmp(first, second, sizeof(first)) == 0);
    CHECK(original.toggle_bits == 0U && changed.toggle_bits != 0U);
}

static int latest_noop_contact(cgai_life_context *owner, cgai_life_collision_event *event,
                               uint32_t *index) {
    cgai_life_events events;
    CHECK(cgai_life_context_get_events(owner, &events) == CGAI_LIFE_OK);
    for (uint32_t i = 0U; i < events.event_count; ++i)
        if (events.events[i].toggle_bits == 0U) {
            *event = events.events[i];
            *index = i;
            return 1;
        }
    return 0;
}

static void authentic_noop_choice(cgai_life_context *owner, const evolve_mutation_source *source,
                                  const cgai_life_collision_event *event, uint32_t index) {
    const cgai_life_collision_event unchanged = *event;
    uint32_t actions[4], fallback;
    size_t count;
    CHECK(evolve_mutation_alternatives(source, event, actions, &count, &fallback) ==
          EVOLVE_MUTATION_OK);
    CHECK(count == 2U || count == 4U);
    cgai_life_context_choice choice;
    CHECK(cgai_life_context_choice_begin(owner, index, "native initialization embeddings_base",
                                         actions, count, fallback, &choice) == CGAI_LIFE_OK);
    CHECK(choice.participant_mask == event->participant_mask);
    CHECK(choice.generation == event->generation && choice.conflict_id == event->conflict_id);
    selected_alternative(source, event, choice.action);
    CHECK(memcmp(event, &unchanged, sizeof(unchanged)) == 0 && event->toggle_bits == 0U);
}

static void genuine_noop_contact(const evolve_mutation_source *source) {
    cgai_life_config config = cgai_life_config_default();
    config.enable_merges = 0U;
    config.training_epochs = 8U;
    cgai_life_context *owner = NULL;
    CHECK(cgai_life_context_create(&config, &owner) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_train_step(owner, 64U, NULL) == CGAI_LIFE_OK);
    cgai_life_collision_event event;
    uint32_t index = 0U;
    int found = latest_noop_contact(owner, &event, &index);
    for (uint32_t i = 0U; i < 32U && !found; ++i) {
        CHECK(cgai_life_context_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
        found = latest_noop_contact(owner, &event, &index);
    }
    CHECK(found);
    authentic_noop_choice(owner, source, &event, index);
    cgai_life_context_destroy(owner);
}

static void rejected_profile_choices(const evolve_mutation_source *source) {
    const cgai_life_collision_event event = event_for(8U);
    evolve_mutation_candidate output = {0};
    const uint32_t parent_action = evolve_mutation_profile_action(&source->profile);
    CHECK(evolve_mutation_select(source, &event, parent_action, &output) ==
          EVOLVE_MUTATION_INVALID);
    CHECK(evolve_mutation_select(source, &event, 81U, &output) == EVOLVE_MUTATION_INVALID);
    CHECK(output.bytes == NULL && output.size == 0U);
    CHECK(evolve_mutation_profile_action(NULL) == UINT32_MAX);
    evolve_mutation_profile invalid = source->profile;
    invalid.choices[0] = 3U;
    CHECK(evolve_mutation_profile_action(&invalid) == UINT32_MAX);
}

static void contact_profile_choices(const evolve_mutation_source *source) {
    complete_profile_choices(source);
    rejected_profile_choices(source);
    domain_ignores_cell_teacher(source);
    genuine_noop_contact(source);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    test_source source = read_source(argv[1]);
    const evolve_mutation_source parsed = validate_source(&source);
    for (size_t i = 0U; i < EVOLVE_MUTATION_SITES; ++i)
        CHECK(parsed.profile.choices[i] == 1U);
    check_actions(&parsed);
    contact_profile_choices(&parsed);
    invalid_inputs(&source, &parsed);
    accepted_parent(&parsed);
    all_catalog_parents(&source);
    actual_life_event(&parsed);
    CHECK(reference_checksum(source.bytes, source.size) == parsed.checksum);
    parent_unchanged(&source, argv[1]);
    free(source.bytes);
    puts("Finite C source mutations, exact provenance and committed Life event checks passed");
    return 0;
}
