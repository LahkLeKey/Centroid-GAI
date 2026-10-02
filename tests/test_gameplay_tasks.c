/** @file test_gameplay_tasks.c @brief Authored composed registries, split isolation and
 * transitions. */
#include "gameplay_tasks.h"
#include "test_utils.h"
#include <stdio.h>
#include <string.h>

/** @brief Open one bounded authored TSV pathname.
 * @param directory Borrowed source fixture directory.
 * @param name Borrowed fixture basename.
 * @return Owned binary file; failed prerequisites terminate the test. */
static FILE *open_fixture(const char *directory, const char *name) {
    /* Step 1: Build and validate the bounded source path before opening it. */
    char path[1024];
    const int size = snprintf(path, sizeof(path), "%s/%s", directory, name);
    TEST_CHECK(size >= 0 && (size_t)size < sizeof(path), "fixture pathname overflow");
    FILE *file = fopen(path, "rb");
    TEST_CHECK(file != NULL, "missing authored composed fixture");
    return file;
}

/** @brief Read exactly one expected LF-normalized authored row.
 * @param file Borrowed binary stream.
 * @param expected Expected complete row including newline. */
static void expect_row(FILE *file, const char *expected) {
    /* Step 1: Reject truncated, extra-column, changed-label and unexpected-CR rows alike. */
    char row[1024];
    TEST_CHECK(fgets(row, sizeof(row), file) != NULL, "authored fixture row missing");
    TEST_CHECK(strcmp(row, expected) == 0, "authored fixture differs from native contract");
}

/** @brief Serialize one generated independent scenario using source category names.
 * @param scenario Complete frozen fixture.
 * @param row Writable1024-byte expected-row storage. */
static void scenario_row(const gameplay_fixture_case *scenario, char row[1024]) {
    /* Step 1: Stable IDs and split names precede the nine complete shared observations. */
    int size = snprintf(row, 1024U, "%zu\t%zu\t%s", scenario->id, scenario->family_id,
                        gameplay_split_name(scenario->split));
    TEST_CHECK(size >= 0 && size < 1024, "scenario prefix overflow");
    size_t offset = (size_t)size;
    for (uint32_t field = 0U; field < 9U; ++field) {
        size = snprintf(row + offset, 1024U - offset, "\t%s",
                        gameplay_feature_value(field, scenario->example.state.values[field]));
        TEST_CHECK(size >= 0 && (size_t)size < 1024U - offset, "scenario category overflow");
        offset += (size_t)size;
    }
    /* Step 2: Bind the final source target to the same task-local catalog domain. */
    size = snprintf(row + offset, 1024U - offset, "\t%s\n",
                    gameplay_output_name(scenario->example.task, scenario->example.target));
    TEST_CHECK(size >= 0 && (size_t)size < 1024U - offset, "scenario target overflow");
}

/** @brief Compare one task's frozen scenarios and catalog with every authored byte.
 * @param directory Borrowed source directory.
 * @param task Supported task head ID. */
static void check_source(const char *directory, uint32_t task) {
    /* Step 1: All source rows retain complete state even when a head masks irrelevant features. */
    const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
    char name[64], row[1024];
    (void)snprintf(name, sizeof(name), "%s-scenarios.tsv", descriptor->name);
    FILE *file = open_fixture(directory, name);
    expect_row(file, "id\tfamily\tsplit\tevent\tdanger\trelationship\tsetting\thealth\tdistance\tco"
                     "ver\tobjective\treadiness\ttarget\n");
    for (size_t i = 0U; i < descriptor->fixture_count; ++i) {
        gameplay_fixture_case scenario;
        TEST_CHECK(gameplay_fixture_get(descriptor->fixture_offset + i, &scenario),
                   "scenario missing");
        scenario_row(&scenario, row);
        expect_row(file, row);
    }
    TEST_CHECK(fgetc(file) == EOF && !ferror(file), "unexpected extra scenario data");
    TEST_CHECK(fclose(file) == 0, "scenario source close failed");
}

/** @brief Bind authored task-local output names and line text to the native catalog.
 * @param directory Borrowed source directory.
 * @param task Supported task ID. */
static void check_catalog(const char *directory, uint32_t task) {
    /* Step 1: Fallback and intent text use an explicit hyphen rather than a trailing empty cell. */
    const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
    char name[64], row[1024];
    (void)snprintf(name, sizeof(name), "%s-catalog.tsv", descriptor->name);
    FILE *file = open_fixture(directory, name);
    expect_row(file, "output_id\tname\ttext\n");
    for (uint32_t output = 0U; output < descriptor->output_count; ++output) {
        const char *text = gameplay_output_text(task, output);
        (void)snprintf(row, sizeof(row), "%u\t%s\t%s\n", output, gameplay_output_name(task, output),
                       text == NULL ? "-" : text);
        expect_row(file, row);
    }
    TEST_CHECK(fgetc(file) == EOF && !ferror(file), "unexpected extra catalog data");
    TEST_CHECK(fclose(file) == 0, "catalog source close failed");
}

/** @brief Bind the authored registry's feature masks and split policy to the native descriptors.
 * @param directory Borrowed authored source directory. */
static void check_registry(const char *directory) {
    /* Step 1: Registry rows declare complete teacher and acceptance policy for each trained task.
     */
    FILE *file = open_fixture(directory, "tasks.tsv");
    expect_row(file, "task_id\tname\toutput_count\ttraining_cases\tdevelopment_cases\ttest_"
                     "cases\tminimum_development_accuracy\tminimum_test_accuracy\tteacher_"
                     "complete\tacceptance_complete\tcatalog\tscenarios\ttask_features\n");
    const cgai_gameplay_config config = gameplay_fixture_config();
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task) {
        const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
        char row[1024];
        (void)snprintf(
            row, sizeof(row),
            "%u\t%s\t%u\t%zu\t%zu\t%zu\t0.95\t0.95\t1\t1\t%s-catalog.tsv\t%s-scenarios.tsv\t%llu\n",
            task, descriptor->name, descriptor->output_count, descriptor->split_counts[0],
            descriptor->split_counts[1], descriptor->split_counts[2], descriptor->name,
            descriptor->name, (unsigned long long)config.task_features[task]);
        expect_row(file, row);
    }
    TEST_CHECK(fgetc(file) == EOF && !ferror(file), "unexpected extra registered task");
    TEST_CHECK(fclose(file) == 0, "task registry close failed");
}

/** @brief Bind both authored specialist identities to their supported task heads.
 * @param directory Borrowed authored source directory. */
static void check_modules(const char *directory) {
    /* Step 1: Both modules must materially support both heads rather than map one-to-one to tasks.
     */
    FILE *file = open_fixture(directory, "modules.tsv");
    expect_row(file, "module_id\tname\ttask_mask\n");
    for (uint32_t module = 0U; module < GAMEPLAY_MODULE_COUNT; ++module) {
        char row[1024];
        (void)snprintf(row, sizeof(row), "%u\t%s\t3\n", module, gameplay_module_name(module));
        expect_row(file, row);
    }
    TEST_CHECK(fgetc(file) == EOF && !ferror(file), "unexpected extra specialist module");
    TEST_CHECK(fclose(file) == 0, "module registry close failed");
}

/** @brief Reject duplicate independent states and verify each setting family remains indivisible.
 * @param scenario Complete frozen scenario. */
static void check_case(const gameplay_fixture_case *scenario) {
    /* Step 1: Every pair shares its family, split and exact teacher while retaining both settings.
     */
    gameplay_fixture_case paired;
    TEST_CHECK(gameplay_fixture_get(scenario->id ^ 1U, &paired),
               "setting family counterpart missing");
    TEST_CHECK(paired.family_id == scenario->family_id && paired.split == scenario->split &&
                   paired.example.target == scenario->example.target,
               "setting family crossed a split");
    TEST_CHECK(paired.example.state.values[GAMEPLAY_SETTING] !=
                   scenario->example.state.values[GAMEPLAY_SETTING],
               "setting variant missing");
    /* Step 2: Distinct heads can reuse state, but one head never duplicates a frozen prompt. */
    for (size_t i = 0U; i < scenario->id; ++i) {
        gameplay_fixture_case earlier;
        TEST_CHECK(gameplay_fixture_get(i, &earlier), "earlier frozen case missing");
        TEST_CHECK(earlier.example.task != scenario->example.task ||
                       memcmp(&earlier.example.state, &scenario->example.state,
                              sizeof(scenario->example.state)) != 0,
                   "duplicate independent task state");
    }
}

/** @brief Check completed exact denominators and training-only label coverage.
 * @param counts Complete independent task/split counts.
 * @param labels Complete task-local training output masks. */
static void check_split_totals(const size_t counts[GAMEPLAY_TASK_COUNT][3],
                               const uint64_t labels[GAMEPLAY_TASK_COUNT]) {
    /* Step 1: Every task's full catalog must be learnable using training families alone. */
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task) {
        const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
        TEST_CHECK(labels[task] == (UINT64_C(1) << descriptor->output_count) - 1U,
                   "training-only task output coverage incomplete");
        for (size_t split = 0U; split < 3U; ++split)
            TEST_CHECK(counts[task][split] == gameplay_fixture_count(task, (gameplay_split)split),
                       "frozen independent split count changed");
    }
}

/** @brief Verify all exact frozen split counts and training-only output coverage.
 * @note No value; failed invariants terminate the test. */
static void check_splits(void) {
    /* Step 1: Inspect every independent record before considering any balanced training repetition.
     */
    size_t counts[GAMEPLAY_TASK_COUNT][3] = {{0U}};
    uint64_t labels[GAMEPLAY_TASK_COUNT] = {0U};
    for (size_t i = 0U; i < GAMEPLAY_FIXTURE_CASE_COUNT; ++i) {
        gameplay_fixture_case scenario;
        TEST_CHECK(gameplay_fixture_get(i, &scenario), "frozen scenario missing");
        check_case(&scenario);
        ++counts[scenario.example.task][scenario.split];
        if (scenario.split == GAMEPLAY_TRAINING)
            labels[scenario.example.task] |= UINT64_C(1) << scenario.example.target;
    }
    /* Step 2: Every task's full catalog must be learnable using training families alone. */
    check_split_totals(counts, labels);
}

/** @brief Locate one complete balanced training record in the independent frozen enumeration.
 * @param example Borrowed training record.
 * @return Matching frozen global index; failed prerequisites terminate the test. */
static size_t training_origin(const cgai_gameplay_example *example) {
    /* Step 1: Exact complete state and task identity determine a unique base scenario. */
    for (size_t i = 0U; i < GAMEPLAY_FIXTURE_CASE_COUNT; ++i) {
        gameplay_fixture_case scenario;
        TEST_CHECK(gameplay_fixture_get(i, &scenario), "training origin scenario missing");
        if (scenario.example.task == example->task &&
            memcmp(&scenario.example.state, &example->state, sizeof(example->state)) == 0) {
            TEST_CHECK(scenario.split == GAMEPLAY_TRAINING &&
                           scenario.example.target == example->target,
                       "held-out scenario or changed target leaked into training");
            return i;
        }
    }
    TEST_CHECK(0, "unknown training record");
}

/** @brief Verify the explicit384-record balanced recipe and its exact repetition counts.
 * @note No value; a changed recipe terminates the test. */
static void check_training(void) {
    /* Step 1: Training schedule contains192 records per task and no held-out family. */
    cgai_gameplay_example examples[GAMEPLAY_TRAINING_COUNT];
    size_t count = 0U, seen[GAMEPLAY_FIXTURE_CASE_COUNT] = {0U}, per_task[2] = {0U};
    TEST_CHECK(gameplay_fixture_training(examples, GAMEPLAY_TRAINING_COUNT, &count),
               "training recipe failed");
    TEST_CHECK(count == GAMEPLAY_TRAINING_COUNT, "balanced training count changed");
    for (size_t i = 0U; i < count; ++i) {
        ++seen[training_origin(&examples[i])];
        ++per_task[examples[i].task];
    }
    TEST_CHECK(per_task[0] == 192U && per_task[1] == 192U, "joint recipe no longer task balanced");
    /* Step 2: Every bark training case occurs four times; every intent training case once. */
    for (size_t i = 0U; i < GAMEPLAY_FIXTURE_CASE_COUNT; ++i) {
        gameplay_fixture_case scenario;
        TEST_CHECK(gameplay_fixture_get(i, &scenario), "recipe scenario missing");
        const size_t expected = scenario.split != GAMEPLAY_TRAINING
                                    ? 0U
                                    : (scenario.example.task == GAMEPLAY_TASK_BARK ? 4U : 1U);
        TEST_CHECK(seen[i] == expected, "balanced training repetition changed");
    }
}

/** @brief Ensure every authored teacher is executable and survives the finite transition world.
 * @note No value; a bad authored target terminates the test. */
static void check_teacher_simulation(void) {
    /* Step 1: Independent simulator rules must admit every frozen intent teacher target. */
    for (size_t i = 72U; i < GAMEPLAY_FIXTURE_CASE_COUNT; ++i) {
        gameplay_fixture_case scenario;
        gameplay_simulation_result result;
        TEST_CHECK(gameplay_fixture_get(i, &scenario), "simulation fixture missing");
        TEST_CHECK(
            gameplay_fixture_simulate(&scenario.example.state, scenario.example.target, &result),
            "teacher transition simulation failed");
        TEST_CHECK(result.legal && result.survived && result.objective_success,
                   "authored teacher fails independent execution requirements");
    }
}

/** @brief Demonstrate independent legality, survival and objective checks for nonteacher actions.
 * @note No value; failed independent-world distinctions terminate the test. */
static void check_simulation_failures(void) {
    /* Step 1: A safe alternative retreat can execute even when the teacher prefers nearby cover. */
    cgai_gameplay_state state = {{2U, 1U, 1U, 0U, 1U, 0U, 1U, 1U, 1U}};
    gameplay_simulation_result result;
    TEST_CHECK(gameplay_fixture_teacher(1U, &state) == 4U, "cover teacher changed");
    TEST_CHECK(gameplay_fixture_simulate(&state, 6U, &result) && result.legal && result.survived &&
                   result.objective_success,
               "safe nonteacher action rejected by simulator");
    /* Step 2: Legal hold can fail survival in danger or fail a safe advance objective
     * independently. */
    state.values[GAMEPLAY_HEALTH] = 0U;
    TEST_CHECK(gameplay_fixture_simulate(&state, 2U, &result) && result.legal && !result.survived,
               "unsafe executable action survived four hazard ticks");
    state.values[GAMEPLAY_DANGER] = 0U;
    state.values[GAMEPLAY_DISTANCE] = 1U;
    TEST_CHECK(gameplay_fixture_simulate(&state, 2U, &result) && result.legal && result.survived &&
                   !result.objective_success,
               "safe stationary action completed advance objective");
    TEST_CHECK(gameplay_fixture_simulate(&state, 4U, &result) && !result.legal,
               "missing hazard action precondition accepted");
}

/** @brief Reject malformed fields and preserve result owners on invalid fixture requests.
 * @note No value; failed validation terminates the test. */
static void check_invalid(void) {
    /* Step 1: Every category and unused trailing field remains validated under task feature masks.
     */
    cgai_gameplay_config config = gameplay_fixture_config();
    for (size_t field = 0U; field < CGAI_GAMEPLAY_MAX_FEATURES; ++field) {
        cgai_gameplay_state state = {{0U}};
        state.values[field] = field < 9U ? config.cardinalities[field] : 1U;
        TEST_CHECK(gameplay_fixture_teacher(0U, &state) == UINT32_MAX,
                   "invalid shared observation admitted");
    }
    /* Step 2: Invalid bounds and incomplete recipe storage preserve caller outputs. */
    gameplay_fixture_case scenario = {0};
    scenario.id = 123U;
    TEST_CHECK(!gameplay_fixture_get(GAMEPLAY_FIXTURE_CASE_COUNT, &scenario) && scenario.id == 123U,
               "invalid fixture index mutated output");
    size_t count = 123U;
    cgai_gameplay_example example = {0};
    TEST_CHECK(!gameplay_fixture_training(&example, 1U, &count) && count == 123U,
               "insufficient recipe storage changed count");
    TEST_CHECK(gameplay_task_get(2U) == NULL && gameplay_output_name(1U, 7U) == NULL &&
                   gameplay_fixture_count(0U, (gameplay_split)3) == 0U,
               "invalid registry ID admitted");
}

/** @brief Execute frozen source, recipe and independent simulation invariants.
 * @param argc Must contain one source directory argument.
 * @param argv Source-directory argument.
 * @return Zero after all assertions pass. */
int main(int argc, char **argv) {
    /* Step 1: Validate authored source rows and exact native scenario semantics. */
    TEST_CHECK(argc == 2, "composed fixture source directory required");
    check_registry(argv[1]);
    check_modules(argv[1]);
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task) {
        check_source(argv[1], task);
        check_catalog(argv[1], task);
    }
    check_splits();
    check_training();
    /* Step 2: Validate the authored abstract world and failure preservation. */
    check_teacher_simulation();
    check_simulation_failures();
    check_invalid();
    return 0;
}
