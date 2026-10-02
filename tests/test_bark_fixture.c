/** @file test_bark_fixture.c @brief Frozen bark source fixtures, coverage and split checks. */
#include "bark_fixture.h"
#include "test_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Track training-only coverage of every input spelling and output target.
 * @param scenario Borrowed valid independent request.
 * @return Bitset with six events, two dangers, three relationships, two settings and nine IDs. */
static uint64_t bark_test_vocabulary(const bark_fixture_case *scenario) {
    /* Step 1: Assign nonoverlapping bit domains to input dimensions and the output catalog. */
    return (UINT64_C(1) << (unsigned)scenario->request.state.event) |
           (UINT64_C(1) << (6U + (unsigned)scenario->request.state.danger)) |
           (UINT64_C(1) << (8U + (unsigned)scenario->request.state.relationship)) |
           (UINT64_C(1) << (11U + (unsigned)scenario->request.state.setting)) |
           (UINT64_C(1) << (13U + scenario->expected_id));
}

/** @brief Check one family assignment and compare its prompt with all preceding prompts.
 * @param scenario Borrowed generated scenario.
 * @return No value; an invariant violation terminates the executable. */
static void bark_test_case(const bark_fixture_case *scenario) {
    /* Step 1: Bind stable IDs, rule labels and versioned request defaults. */
    TEST_CHECK(scenario->family_id == scenario->id / 2U, "unstable family ID");
    TEST_CHECK(scenario->expected_id == bark_fixture_oracle(&scenario->request.state),
               "teacher label mismatch");
    TEST_CHECK(scenario->request.contract_version == CGAI_BARK_CONTRACT_VERSION,
               "request contract mismatch");
    TEST_CHECK(scenario->request.allowed_ids == CGAI_BARK_ALL_IDS, "catalog mask mismatch");
    TEST_CHECK(scenario->request.recent_id == CGAI_BARK_ABSTAIN, "recent line suppression enabled");
    bark_fixture_split expected = scenario->family_id % 6U == 0U   ? BARK_FIXTURE_DEVELOPMENT
                                  : scenario->family_id % 6U == 1U ? BARK_FIXTURE_TEST
                                                                   : BARK_FIXTURE_TRAINING;
    TEST_CHECK(scenario->split == expected, "family crossed the frozen split boundary");
    /* Step 2: Prevent exact prompt duplication anywhere in the three splits. */
    for (size_t earlier = 0U; earlier < scenario->id; ++earlier) {
        bark_fixture_case preceding;
        TEST_CHECK(bark_fixture_get(earlier, &preceding) == CGAI_STATUS_OK, "earlier case missing");
        TEST_CHECK(strcmp(scenario->prompt, preceding.prompt) != 0, "duplicate prompt");
    }
}

/** @brief Require complete setting families, exact split counts and training vocabulary.
 * @param counts Borrowed three-entry scenario counts.
 * @param family_settings Borrowed thirty-six-entry setting-presence masks.
 * @param vocabulary Training-only state/target vocabulary mask.
 * @return No value; an invariant violation terminates the executable. */
static void bark_test_coverage(const size_t counts[3],
                               const unsigned family_settings[BARK_FIXTURE_FAMILY_COUNT],
                               uint64_t vocabulary) {
    /* Step 1: Require both settings per family and every model spelling in training alone. */
    for (size_t family = 0U; family < BARK_FIXTURE_FAMILY_COUNT; ++family)
        TEST_CHECK(family_settings[family] == 3U, "family lost a setting variant");
    TEST_CHECK(vocabulary == (UINT64_C(1) << 22U) - UINT64_C(1), "training vocabulary incomplete");
    /* Step 2: Bind generated scenario counts to the frozen split contract. */
    for (unsigned split = 0U; split < 3U; ++split)
        TEST_CHECK(counts[split] == bark_fixture_count((bark_fixture_split)split),
                   "generated split count changed");
}

/** @brief Verify split counts, indivisible families and training-only vocabulary coverage.
 * @return No value; an invariant violation terminates the executable. */
static void bark_test_splits(void) {
    /* Step 1: Inspect every independent scenario without consulting held-out text for vocabulary.
     */
    size_t counts[3] = {0U};
    unsigned family_settings[BARK_FIXTURE_FAMILY_COUNT] = {0U};
    uint64_t vocabulary = 0U;
    for (size_t index = 0U; index < BARK_FIXTURE_CASE_COUNT; ++index) {
        bark_fixture_case scenario;
        TEST_CHECK(bark_fixture_get(index, &scenario) == CGAI_STATUS_OK, "scenario missing");
        TEST_CHECK(scenario.id == index, "scenario ID changed");
        bark_test_case(&scenario);
        counts[scenario.split]++;
        family_settings[scenario.family_id] |= 1U << (unsigned)scenario.request.state.setting;
        if (scenario.split == BARK_FIXTURE_TRAINING)
            vocabulary |= bark_test_vocabulary(&scenario);
    }
    /* Step 2: Verify all aggregate invariants after inspecting the complete enumeration. */
    bark_test_coverage(counts, family_settings, vocabulary);
}

/** @brief Reject invalid enum values, missing pointers and unsupported split values.
 * @return No value; an invariant violation terminates the executable. */
static void bark_test_invalid(void) {
    /* Step 1: Check each independent state dimension and missing state before oracle lookup. */
    cgai_bark_state bad_states[4] = {0};
    memset(&bad_states[0].event, 0x7f, sizeof(bad_states[0].event));
    memset(&bad_states[1].danger, 0x7f, sizeof(bad_states[1].danger));
    memset(&bad_states[2].relationship, 0x7f, sizeof(bad_states[2].relationship));
    memset(&bad_states[3].setting, 0x7f, sizeof(bad_states[3].setting));
    for (size_t index = 0U; index < sizeof(bad_states) / sizeof(bad_states[0]); ++index)
        TEST_CHECK(bark_fixture_oracle(&bad_states[index]) == UINT32_MAX, "invalid state admitted");
    TEST_CHECK(bark_fixture_oracle(NULL) == UINT32_MAX, "missing state admitted");
    /* Step 2: Failed generation preserves caller output and reports invalid bounds. */
    bark_fixture_case scenario = {0};
    scenario.id = 123U;
    TEST_CHECK(bark_fixture_get(BARK_FIXTURE_CASE_COUNT, &scenario) == CGAI_STATUS_ERROR,
               "out-of-range scenario admitted");
    TEST_CHECK(scenario.id == 123U, "failed generation changed output");
    TEST_CHECK(bark_fixture_get(0U, NULL) == CGAI_STATUS_ERROR, "missing output admitted");
    TEST_CHECK(bark_fixture_count((bark_fixture_split)3) == 0U, "invalid split counted");
    TEST_CHECK(bark_fixture_split_name((bark_fixture_split)3) == NULL, "invalid split named");
}

/** @brief Open one authored fixture under the supplied source directory.
 * @param directory Borrowed directory pathname.
 * @param filename Borrowed constant fixture basename.
 * @return Owned binary stream; failures terminate the executable. */
static FILE *bark_test_open(const char *directory, const char *filename) {
    /* Step 1: Bound the combined path before reading exact fixture bytes. */
    char path[1024];
    int written = snprintf(path, sizeof(path), "%s/%s", directory, filename);
    TEST_CHECK(written >= 0 && (size_t)written < sizeof(path), "fixture path too long");
    FILE *file = fopen(path, "rb");
    TEST_CHECK(file != NULL, "fixture file missing");
    return file;
}

/** @brief Compare one authored LF-terminated row against deterministic serialization.
 * @param file Borrowed open stream.
 * @param expected Borrowed NUL-terminated expected row.
 * @return No value; mismatch or truncation terminates the executable. */
static void bark_test_line(FILE *file, const char *expected) {
    /* Step 1: Reject missing, oversized and differently serialized source rows. */
    char line[1024];
    TEST_CHECK(fgets(line, sizeof(line), file) != NULL, "fixture row missing");
    TEST_CHECK(strcmp(line, expected) == 0, "authored row differs from generated contract");
}

/** @brief Require exact EOF after all expected rows and close an owned stream.
 * @param file Owned open stream, invalid after this call.
 * @return No value; trailing data or I/O failure terminates the executable. */
static void bark_test_close(FILE *file) {
    /* Step 1: Reject unexpected rows and read errors before releasing the stream. */
    char line[1024];
    TEST_CHECK(fgets(line, sizeof(line), file) == NULL, "unexpected fixture row");
    TEST_CHECK(feof(file) && !ferror(file), "fixture read error");
    TEST_CHECK(fclose(file) == 0, "fixture close error");
}

/** @brief Match every checked-in scenario against the C11 enumerator and frozen labels.
 * @param directory Borrowed source fixture directory.
 * @return No value; any source/runtime mismatch terminates the executable. */
static void bark_test_scenarios(const char *directory) {
    /* Step 1: Require the authored schema and reconstruct every stable source row. */
    FILE *file = bark_test_open(directory, "scenarios.tsv");
    bark_test_line(
        file, "id\tfamily\tsplit\tevent\tdanger\trelationship\tsetting\texpected_id\tprompt\n");
    for (size_t index = 0U; index < BARK_FIXTURE_CASE_COUNT; ++index) {
        bark_fixture_case scenario;
        char expected[1024];
        TEST_CHECK(bark_fixture_get(index, &scenario) == CGAI_STATUS_OK, "scenario missing");
        int written =
            snprintf(expected, sizeof(expected), "%zu\t%zu\t%s\t%s\t%s\t%s\t%s\t%u\t%s\n",
                     scenario.id, scenario.family_id, bark_fixture_split_name(scenario.split),
                     bark_fixture_event_name(scenario.request.state.event),
                     bark_fixture_danger_name(scenario.request.state.danger),
                     bark_fixture_relationship_name(scenario.request.state.relationship),
                     bark_fixture_setting_name(scenario.request.state.setting),
                     scenario.expected_id, scenario.prompt);
        TEST_CHECK(written >= 0 && (size_t)written < sizeof(expected), "scenario row too long");
        bark_test_line(file, expected);
    }
    /* Step 2: Require exactly seventy-two authored scenarios with no trailing data. */
    bark_test_close(file);
}

/** @brief Match authored stable catalog IDs, target spellings and resolved game content.
 * @param directory Borrowed source fixture directory.
 * @return No value; any source/runtime mismatch terminates the executable. */
static void bark_test_catalog(const char *directory) {
    /* Step 1: Resolve all catalog content through the public gameplay runtime. */
    FILE *file = bark_test_open(directory, "catalog.tsv");
    bark_test_line(file, "id\tname\ttext\n");
    for (unsigned index = 0U; index < CGAI_BARK_ID_COUNT; ++index) {
        char expected[512];
        const char *text = cgai_bark_catalog_text((cgai_bark_id)index);
        int written = snprintf(expected, sizeof(expected), "%u\t%s\t%s\n", index,
                               cgai_bark_id_name((cgai_bark_id)index), text == NULL ? "-" : text);
        TEST_CHECK(written >= 0 && (size_t)written < sizeof(expected), "catalog row too long");
        bark_test_line(file, expected);
    }
    /* Step 2: Require all nine IDs, including the empty abstention line. */
    bark_test_close(file);
}

/** @brief Run frozen source fixtures and independent family/vocabulary invariants.
 * @param argc Argument count; exactly the executable and source directory are required.
 * @param argv Borrowed argument array; argv[1] names data/gameplay/barks-v1.
 * @return EXIT_SUCCESS when all task fixture invariants hold. */
int main(int argc, char **argv) {
    /* Step 1: Check fixture structure and public rejection boundaries. */
    TEST_CHECK(argc == 2, "expected source fixture directory argument");
    bark_test_invalid();
    bark_test_splits();
    TEST_CHECK(bark_fixture_count(BARK_FIXTURE_TRAINING) == 48U, "training count accessor failed");
    TEST_CHECK(bark_fixture_count(BARK_FIXTURE_DEVELOPMENT) == 12U, "development accessor failed");
    TEST_CHECK(bark_fixture_count(BARK_FIXTURE_TEST) == 12U, "test count accessor failed");
    /* Step 2: Cross-check checked-in source rows against the executable representation. */
    bark_test_scenarios(argv[1]);
    bark_test_catalog(argv[1]);
    return EXIT_SUCCESS;
}
