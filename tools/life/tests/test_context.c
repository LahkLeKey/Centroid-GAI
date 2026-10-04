/** @file test_context.c @brief Contact-gated native context memory and exact restart checks. */
#include "centroid_life.h"
#include "life_context.h"
#include "test_utils.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, "Native Life context check failed")
#define TEST_PATH_BYTES 4096U

static cgai_life_context *new_owner(uint32_t epochs) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life_context *owner = NULL;
    config.enable_merges = 0U;
    config.training_epochs = epochs;
    CHECK(cgai_life_context_create(&config, &owner) == CGAI_LIFE_OK);
    CHECK(owner != NULL);
    return owner;
}

static cgai_life_context_input source_record(void) {
    return (cgai_life_context_input){"src/contact.c",
                                     "centroid collision contact frontier",
                                     3U,
                                     7U,
                                     42U,
                                     CGAI_LIFE_CONTEXT_SOURCE,
                                     CGAI_LIFE_CONTEXT_TRAIN,
                                     1U,
                                     15U};
}

static void add_records(cgai_life_context *owner) {
    cgai_life_context_input input = source_record();
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    input.source = "notes/unreviewed.llm";
    input.text = "unreviewed hallucination context";
    input.kind = CGAI_LIFE_CONTEXT_LLM;
    input.reviewed = 0U;
    input.family = 43U;
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    input.source = "notes/development.txt";
    input.text = "development reserved material";
    input.split = CGAI_LIFE_CONTEXT_DEVELOPMENT;
    input.reviewed = 1U;
    input.family = 44U;
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    input.source = "notes/audit.txt";
    input.text = "audit independent evidence";
    input.kind = CGAI_LIFE_CONTEXT_ACTIVITY;
    input.split = CGAI_LIFE_CONTEXT_AUDIT;
    input.family = 45U;
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
}

static cgai_life_context_stats stats(const cgai_life_context *owner) {
    cgai_life_context_stats result;
    CHECK(cgai_life_context_get_stats(owner, &result) == CGAI_LIFE_OK);
    return result;
}

static void empty_query(const cgai_life_context *owner, const char *query) {
    cgai_life_context_result result;
    size_t count = 99U;
    CHECK(cgai_life_context_query(owner, query, &result, 1U, &count) == CGAI_LIFE_OK);
    CHECK(count == 0U);
}

static void no_contact(cgai_life_context *owner) {
    const cgai_life_context_stats before = stats(owner);
    uint32_t completed = 99U;
    life_world_clear(&owner->run.world, 42U);
    CHECK(cgai_life_context_train_step(owner, 1U, &completed) == CGAI_LIFE_OK);
    const cgai_life_context_stats after = stats(owner);
    CHECK(completed == 1U && after.generation == before.generation + 1U);
    CHECK(owner->run.last_frame.patch_count == 0U);
    CHECK(after.trained_records == 0U && after.domain_updates == before.domain_updates);
    CHECK(memcmp(after.group_updates, before.group_updates, sizeof(before.group_updates)) == 0);
    CHECK(memcmp(after.group_hashes, before.group_hashes, sizeof(before.group_hashes)) == 0);
    empty_query(owner, "centroid collision");
}

static void shared_block(cgai_life_context *owner, uint8_t mask) {
    for (int y = 10; y < 12; ++y)
        for (int x = 10; x < 12; ++x)
            CHECK(life_world_set(&owner->run.world, x, y, mask) == LIFE_OK);
}

static uint32_t physical_participants(const cgai_life_context *owner) {
    uint32_t mask = 0U;
    CHECK(owner->run.last_frame.patch_count != 0U);
    for (size_t i = 0U; i < owner->run.last_frame.patch_count; ++i)
        mask |= owner->run.last_frame.patches[i].module_mask;
    return mask;
}

static void check_slices(const cgai_life_context_stats *before,
                         const cgai_life_context_stats *after, uint32_t participants) {
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group) {
        if ((participants & (1U << group)) != 0U) {
            CHECK(after->group_updates[group] == before->group_updates[group] + 1U);
            CHECK(after->group_hashes[group] != before->group_hashes[group]);
        } else {
            CHECK(after->group_updates[group] == before->group_updates[group]);
            CHECK(after->group_hashes[group] == before->group_hashes[group]);
        }
    }
}

static void contact_learns(cgai_life_context *owner) {
    const cgai_life_context_stats before = stats(owner);
    shared_block(owner, 3U);
    CHECK(cgai_life_context_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    const cgai_life_context_stats after = stats(owner);
    const uint32_t participants = physical_participants(owner);
    CHECK(participants == 3U);
    CHECK(after.trained_records == 1U && after.domain_updates == 2U);
    CHECK(after.contact_events > before.contact_events);
    CHECK(owner->records[0].learned_mask == participants);
    for (size_t i = 1U; i < owner->count; ++i)
        CHECK(owner->records[i].learned_mask == 0U);
    check_slices(&before, &after, participants);
}

static void frozen_query(cgai_life_context *owner) {
    cgai_life_context_result result[2];
    size_t count = 99U;
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_query(owner, "CENTROID collision", result, 2U, &count) == CGAI_LIFE_OK);
    CHECK(count == 1U);
    CHECK(strcmp(result[0].source, "src/contact.c") == 0);
    CHECK(strcmp(result[0].text, "centroid collision contact frontier") == 0);
    CHECK(result[0].first_line == 3U && result[0].last_line == 7U);
    CHECK(result[0].kind == CGAI_LIFE_CONTEXT_SOURCE && result[0].learned_mask == 3U);
    CHECK(result[0].content_hash == owner->records[0].content_hash && result[0].score > 0.0);
    empty_query(owner, "hallucination");
    empty_query(owner, "development reserved");
    empty_query(owner, "audit evidence");
    empty_query(owner, "ultraviolet marmalade");
    CHECK(cgai_life_context_hash(owner) == before);
}

static void admissions(cgai_life_context *owner) {
    cgai_life_context_input input = source_record();
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_hash(owner) == before && stats(owner).records == 4U);
    input.text = "centroid collision contact changed version";
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    CHECK(stats(owner).records == 5U);
    CHECK(owner->records[4].content_hash != owner->records[0].content_hash);
    CHECK(owner->records[4].learned_mask == 0U);
}

static void rejected_input(cgai_life_context *owner, const cgai_life_context_input *input) {
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_add(owner, input) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_hash(owner) == before);
}

static void split_boundaries(cgai_life_context *owner) {
    cgai_life_context_input input = source_record();
    input.text = "heldout material from the same source family";
    input.split = CGAI_LIFE_CONTEXT_AUDIT;
    rejected_input(owner, &input);
    input = source_record();
    input.source = "copied-source.c";
    input.family = 99U;
    input.split = CGAI_LIFE_CONTEXT_DEVELOPMENT;
    rejected_input(owner, &input);
}

static void invalid_metadata(cgai_life_context *owner) {
    cgai_life_context_input input = source_record();
    input.first_line = 0U;
    rejected_input(owner, &input);
    input.first_line = 8U;
    rejected_input(owner, &input);
    input = source_record();
    /* Deliberate invalid public input exercises enum validation. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    input.kind = (cgai_life_context_kind)3;
    rejected_input(owner, &input);
    input = source_record();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    input.split = (cgai_life_context_split)3;
    rejected_input(owner, &input);
    input = source_record();
    input.reviewed = 2U;
    rejected_input(owner, &input);
    input.reviewed = 1U;
    input.eligible_mask = 0U;
    rejected_input(owner, &input);
    input.eligible_mask = 16U;
    rejected_input(owner, &input);
}

static void invalid_strings(cgai_life_context *owner) {
    cgai_life_context_input input = source_record();
    char large[CGAI_LIFE_CONTEXT_TEXT_BYTES + 2U];
    memset(large, 'x', sizeof(large) - 1U);
    large[sizeof(large) - 1U] = '\0';
    input.source = NULL;
    rejected_input(owner, &input);
    input.source = "";
    rejected_input(owner, &input);
    input.source = large;
    rejected_input(owner, &input);
    input = source_record();
    input.text = NULL;
    rejected_input(owner, &input);
    input.text = large;
    rejected_input(owner, &input);
    rejected_input(owner, NULL);
}

static void invalid_operations(cgai_life_context *owner) {
    const uint64_t before = cgai_life_context_hash(owner);
    uint32_t completed = 99U;
    cgai_life_context_stats untouched;
    unsigned char expected[sizeof(untouched)];
    memset(&untouched, 0x5a, sizeof(untouched));
    memcpy(expected, &untouched, sizeof(expected));
    CHECK(cgai_life_context_train_step(owner, 0U, &completed) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_train_step(owner, 65U, &completed) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(completed == 99U);
    CHECK(cgai_life_context_get_stats(NULL, &untouched) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(memcmp(expected, &untouched, sizeof(expected)) == 0);
    CHECK(cgai_life_context_get_stats(owner, NULL) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_save(owner, "") == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_load(owner, NULL) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_hash(owner) == before);
}

static void invalid_queries(cgai_life_context *owner) {
    cgai_life_context_result result;
    size_t count = 99U;
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_query(owner, "contact", &result, 0U, &count) ==
          CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_query(owner, "contact", &result, 17U, &count) ==
          CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_query(owner, NULL, &result, 1U, &count) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_query(owner, "contact", NULL, 1U, &count) ==
          CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_query(owner, "contact", &result, 1U, NULL) ==
          CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(count == 99U && cgai_life_context_hash(owner) == before);
}

static void invalid_configurations(void) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life_context *owner = NULL;
    CHECK(cgai_life_context_create(&config, &owner) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(owner == NULL);
    config.enable_merges = 0U;
    config.mode = CGAI_LIFE_TEACHER;
    CHECK(cgai_life_context_create(&config, &owner) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_create(NULL, &owner) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_create(&config, NULL) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_hash(NULL) == 0U);
}

static void zero_epochs(void) {
    cgai_life_context *owner = new_owner(0U);
    add_records(owner);
    life_world_clear(&owner->run.world, 42U);
    shared_block(owner, 3U);
    CHECK(cgai_life_context_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(physical_participants(owner) == 3U);
    CHECK(stats(owner).domain_updates == 0U && stats(owner).trained_records == 0U);
    empty_query(owner, "collision");
    cgai_life_context_destroy(owner);
}

static void checkpoint_path(char *output, const char *directory, const char *name) {
    const int written = snprintf(output, TEST_PATH_BYTES, "%s/%s", directory, name);
    CHECK(written > 0 && (size_t)written < TEST_PATH_BYTES);
}

static void continuation(const char *path) {
    cgai_life_context *whole = new_owner(1U);
    cgai_life_context *split = new_owner(1U);
    cgai_life_context *restored = new_owner(1U);
    add_records(whole);
    add_records(split);
    CHECK(cgai_life_context_train_step(whole, 36U, NULL) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_train_step(split, 30U, NULL) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_save(split, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_load(restored, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_hash(restored) == cgai_life_context_hash(split));
    CHECK(memcmp(restored->cursors, split->cursors, sizeof(split->cursors)) == 0);
    CHECK(cgai_life_context_train_step(restored, 6U, NULL) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_hash(restored) == cgai_life_context_hash(whole));
    CHECK(life_run_hash(&restored->run) == life_run_hash(&whole->run));
    cgai_life_context_destroy(whole);
    cgai_life_context_destroy(split);
    cgai_life_context_destroy(restored);
}

static void late_attributed_query(const cgai_life_context *owner) {
    cgai_life_context_result result;
    size_t count = 99U;
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_query(owner, "scheduler rehearsal", &result, 1U, &count) ==
          CGAI_LIFE_OK);
    CHECK(count == 1U && result.kind == CGAI_LIFE_CONTEXT_LLM);
    CHECK(strcmp(result.source, "notes/late.llm") == 0);
    CHECK(strcmp(result.text, "late llm injection scheduler rehearsal") == 0);
    CHECK(cgai_life_context_hash(owner) == before);
}

static void late_injection(void) {
    cgai_life_context *owner = new_owner(1U);
    cgai_life_context_input input = source_record();
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_train_step(owner, 64U, NULL) == CGAI_LIFE_OK);
    const cgai_life_context_stats before = stats(owner);
    input.source = "notes/late.llm";
    input.text = "late llm injection scheduler rehearsal";
    input.kind = CGAI_LIFE_CONTEXT_LLM;
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    CHECK(owner->records[1].learned_mask == 0U &&
          stats(owner).domain_updates == before.domain_updates);
    empty_query(owner, "scheduler rehearsal");
    CHECK(cgai_life_context_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    const cgai_life_context_stats after = stats(owner);
    const uint32_t participants = physical_participants(owner);
    CHECK(after.generation == 65U && owner->records[1].learned_mask == participants);
    CHECK(after.trained_records == 2U && after.contact_events > before.contact_events);
    check_slices(&before, &after, participants);
    late_attributed_query(owner);
    cgai_life_context_destroy(owner);
}

static void competing_sources(cgai_life_context *owner) {
    cgai_life_context_input input = source_record();
    char source[64];
    input.source = source;
    input.text = "scheduler rehearsal";
    for (unsigned int i = 0U; i < 17U; ++i) {
        const int length = snprintf(source, sizeof(source), "scheduler rehearsal %u", i);
        CHECK(length > 0 && (size_t)length < sizeof(source));
        CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    }
}

static void competing_notes(cgai_life_context *owner) {
    cgai_life_context_input input = source_record();
    input.kind = CGAI_LIFE_CONTEXT_LLM;
    input.source = "notes/assistant.llm";
    input.text = "scheduler rehearsal inferred proposal speculative alternative interpretation";
    input.family = 71U;
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    input.source = "notes/unreviewed.llm";
    input.text = "scheduler rehearsal unreviewed concealed material";
    input.family = 72U;
    input.reviewed = 0U;
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
    input.source = "notes/development.llm";
    input.text = "scheduler rehearsal development reserved evidence";
    input.family = 73U;
    input.reviewed = 1U;
    input.split = CGAI_LIFE_CONTEXT_DEVELOPMENT;
    CHECK(cgai_life_context_add(owner, &input) == CGAI_LIFE_OK);
}

static void kind_filtered_ranking(const cgai_life_context *owner) {
    cgai_life_context_result results[16];
    size_t count = 99U;
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_query(owner, "scheduler rehearsal", results, 16U, &count) ==
          CGAI_LIFE_OK);
    CHECK(count == 16U);
    for (size_t i = 0U; i < count; ++i)
        CHECK(results[i].kind == CGAI_LIFE_CONTEXT_SOURCE);
    const double source_score = results[0].score;
    CHECK(cgai_life_context_query_kind(owner, "scheduler rehearsal", CGAI_LIFE_CONTEXT_LLM, results,
                                       16U, &count) == CGAI_LIFE_OK);
    CHECK(count == 1U && results[0].kind == CGAI_LIFE_CONTEXT_LLM);
    CHECK(strcmp(results[0].source, "notes/assistant.llm") == 0);
    CHECK(strcmp(results[0].text,
                 "scheduler rehearsal inferred proposal speculative alternative interpretation") ==
          0);
    CHECK(results[0].score < source_score && results[0].learned_mask == 3U);
    CHECK(owner->records[18].learned_mask == 0U && owner->records[19].learned_mask == 0U);
    CHECK(cgai_life_context_query_kind(owner, "scheduler rehearsal", CGAI_LIFE_CONTEXT_ACTIVITY,
                                       results, 16U, &count) == CGAI_LIFE_OK);
    CHECK(count == 0U && cgai_life_context_hash(owner) == before);
}

static void invalid_kind_query(const cgai_life_context *owner) {
    cgai_life_context_result result;
    unsigned char expected[sizeof(result)];
    size_t count = 99U;
    const uint64_t before = cgai_life_context_hash(owner);
    memset(&result, 0x5a, sizeof(result));
    memcpy(expected, &result, sizeof(expected));
    /* Deliberate unsupported kind validates the public rejection contract. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const cgai_life_context_kind kind = (cgai_life_context_kind)3;
    CHECK(cgai_life_context_query_kind(owner, "scheduler rehearsal", kind, &result, 1U, &count) ==
          CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(count == 99U && memcmp(expected, &result, sizeof(expected)) == 0);
    CHECK(cgai_life_context_hash(owner) == before);
}

static void kind_query_checks(void) {
    cgai_life_context *owner = new_owner(1U);
    competing_sources(owner);
    competing_notes(owner);
    life_world_clear(&owner->run.world, 42U);
    shared_block(owner, 3U);
    CHECK(cgai_life_context_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(owner->count == 20U && physical_participants(owner) == 3U);
    kind_filtered_ranking(owner);
    invalid_kind_query(owner);
    cgai_life_context_destroy(owner);
}

static void copy_bytes(FILE *input, FILE *output) {
    unsigned char buffer[512];
    while (!feof(input)) {
        const size_t count = fread(buffer, 1U, sizeof(buffer), input);
        CHECK(!ferror(input));
        CHECK(fwrite(buffer, 1U, count, output) == count);
    }
}

static void copy_checkpoint(const char *source, const char *target, int corrupt) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(target, "wb");
    CHECK(input != NULL && output != NULL);
    const int first = fgetc(input);
    CHECK(first != EOF);
    CHECK(fputc(corrupt ? first ^ 1 : first, output) != EOF);
    copy_bytes(input, output);
    if (!corrupt)
        CHECK(fputs("unexpected trailing data\n", output) >= 0);
    CHECK(fclose(input) == 0);
    CHECK(fclose(output) == 0);
}

static void copy_through_marker(FILE *input, FILE *output, const char *marker) {
    char line[512];
    while (!feof(input)) {
        if (fgets(line, sizeof(line), input) == NULL)
            break;
        CHECK(!ferror(input));
        CHECK(fputs(line, output) >= 0);
        if (strcmp(line, marker) == 0)
            return;
    }
    CHECK(0);
}

static void changed_centroid(const char *source, const char *target) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(target, "wb");
    char previous[128];
    CHECK(input != NULL && output != NULL);
    copy_through_marker(input, output, "CENTROIDS\n");
    CHECK(fscanf(input, "%127s", previous) == 1);
    CHECK(fputs("0x1p+0", output) >= 0);
    copy_bytes(input, output);
    CHECK(fclose(input) == 0);
    CHECK(fclose(output) == 0);
}

static void truncated_life(const char *source, const char *target) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(target, "wb");
    CHECK(input != NULL && output != NULL);
    copy_through_marker(input, output, "LIFE\n");
    CHECK(fclose(input) == 0);
    CHECK(fclose(output) == 0);
}

static void embedded_nul(const char *source, const char *target, size_t prefix) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(target, "wb");
    char bytes[32];
    CHECK(input != NULL && output != NULL);
    CHECK(prefix < sizeof(bytes));
    CHECK(fread(bytes, 1U, prefix, input) == prefix);
    CHECK(fwrite(bytes, 1U, prefix, output) == prefix);
    CHECK(fwrite("\0junk", 1U, 5U, output) == 5U);
    copy_bytes(input, output);
    CHECK(fclose(input) == 0);
    CHECK(fclose(output) == 0);
}

static void unchanged_after_rejection(cgai_life_context *owner, const char *path,
                                      uint64_t expected) {
    CHECK(cgai_life_context_load(owner, path) == CGAI_LIFE_IO_ERROR);
    CHECK(cgai_life_context_hash(owner) == expected);
}

static void rejected_restore(cgai_life_context *owner, const char *path, const char *bad) {
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_save(owner, path) == CGAI_LIFE_OK);
    copy_checkpoint(path, bad, 1);
    unchanged_after_rejection(owner, bad, before);
    changed_centroid(path, bad);
    unchanged_after_rejection(owner, bad, before);
    truncated_life(path, bad);
    unchanged_after_rejection(owner, bad, before);
    copy_checkpoint(path, bad, 0);
    unchanged_after_rejection(owner, bad, before);
    embedded_nul(path, bad, strlen("LIFE_CONTEXT"));
    unchanged_after_rejection(owner, bad, before);
    embedded_nul(path, bad, strlen("LIFE_CONTEXT 1"));
    unchanged_after_rejection(owner, bad, before);
    CHECK(cgai_life_context_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
}

static void contact_checks(cgai_life_context *owner) {
    add_records(owner);
    empty_query(owner, "centroid collision");
    no_contact(owner);
    contact_learns(owner);
    frozen_query(owner);
    admissions(owner);
    split_boundaries(owner);
    invalid_metadata(owner);
    invalid_strings(owner);
    invalid_operations(owner);
    invalid_queries(owner);
}

int main(int argc, char **argv) {
    char path[TEST_PATH_BYTES];
    char bad[TEST_PATH_BYTES];
    CHECK(argc == 2);
    checkpoint_path(path, argv[1], "test-life-context.snapshot");
    checkpoint_path(bad, argv[1], "test-life-context.bad.snapshot");
    cgai_life_context *owner = new_owner(1U);
    invalid_configurations();
    contact_checks(owner);
    zero_epochs();
    late_injection();
    kind_query_checks();
    continuation(path);
    rejected_restore(owner, path, bad);
    cgai_life_context_destroy(owner);
    cgai_life_context_destroy(NULL);
    CHECK(remove(path) == 0);
    CHECK(remove(bad) == 0);
    puts("Native context contact gates, frozen lookup and continuation checks passed");
    return 0;
}
