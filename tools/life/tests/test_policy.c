/** @file test_policy.c @brief Restricted updates, exact replay and preservation-gated merges. */
#include "life_policy.h"
#include "test_utils.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static life_record fixture_record(uint32_t target, uint32_t modules) {
    life_record record = {0};
    for (size_t i = 0U; i < 6U; ++i) {
        record.state.values[i] = (uint32_t)(i * 7U);
        record.state.values[i + 6U] = (uint32_t)(i % 5U);
    }
    record.state.values[12] = 2U;
    record.state.values[13] = 3U;
    record.state.values[14] = 4U;
    record.state.values[15] = 2U;
    record.target = target;
    record.module_mask = modules;
    return record;
}

static life_policy_info inspect(const life_policy *policy) {
    life_policy_info info;
    TEST_CHECK(life_policy_inspect(policy, &info), cgai_last_error());
    return info;
}

static double loss(life_policy *policy, const life_record *record) {
    double value = 0.0;
    TEST_CHECK(life_policy_evaluate(policy, record, &value), cgai_last_error());
    TEST_CHECK(isfinite(value), "Life target loss is nonfinite");
    return value;
}

static void require_frozen_modules(const life_policy_info *before, const life_policy_info *after) {
    TEST_CHECK(after->shared_hash == before->shared_hash,
               "collision learning changed the shared encoder, embeddings or their moments");
    for (size_t module = 0U; module < LIFE_POLICY_MODULES; ++module) {
        const int involved = module < 2U;
        TEST_CHECK((after->module_hash[module] != before->module_hash[module]) == involved,
                   "pair restriction changed an uninvolved weight/moment slice");
        TEST_CHECK(after->module_steps[module] ==
                       before->module_steps[module] + (involved ? 30U : 0U),
                   "pair restriction advanced an uninvolved Adam clock");
    }
}

static void require_trained_selection(life_policy *policy, const life_record *record) {
    cgai_gameplay_result result;
    TEST_CHECK(life_policy_select(policy, &record->state, 3U, UINT64_C(1) << 22U, &result),
               cgai_last_error());
    TEST_CHECK(result.output == 22U && result.active_modules == 2U && result.active_centroids == 8U,
               "trained collision head did not select its legal target");
    TEST_CHECK(fabs(result.module_weights[0] + result.module_weights[1] - 1.0) < 1e-12,
               "mass-aware routing probabilities did not normalize");
}

static void restricted_updates(void) {
    life_policy *policy = life_policy_create(193U);
    TEST_CHECK(policy != NULL, cgai_last_error());
    const life_record initial = fixture_record(4U, 15U);
    TEST_CHECK(life_policy_train(policy, &initial, 1U, 4U), cgai_last_error());
    const life_policy_info before = inspect(policy);
    const life_record pair = fixture_record(22U, 3U);
    const double previous = loss(policy, &pair);
    TEST_CHECK(life_policy_train(policy, &pair, 1U, 30U), cgai_last_error());
    const life_policy_info after = inspect(policy);
    const double learned = loss(policy, &pair);
    printf("restricted collision NLL: %.6f -> %.6f\n", previous, learned);
    TEST_CHECK(learned < previous * 0.9,
               "collision learning did not substantially lower edit-target loss");
    require_frozen_modules(&before, &after);
    require_trained_selection(policy, &pair);
    life_policy_destroy(policy);
}

static void module_clocks(void) {
    life_policy *busy = life_policy_create(6U);
    life_policy *fresh = life_policy_create(6U);
    TEST_CHECK(busy != NULL && fresh != NULL, cgai_last_error());
    const life_record pair = fixture_record(7U, 3U);
    TEST_CHECK(life_policy_train(busy, &pair, 1U, 9U), cgai_last_error());
    const life_record singleton = fixture_record(18U, 8U);
    TEST_CHECK(life_policy_train(busy, &singleton, 1U, 1U), cgai_last_error());
    TEST_CHECK(life_policy_train(fresh, &singleton, 1U, 1U), cgai_last_error());
    const life_policy_info first = inspect(busy);
    const life_policy_info second = inspect(fresh);
    TEST_CHECK(first.module_hash[3] == second.module_hash[3] && first.module_steps[3] == 1U &&
                   second.module_steps[3] == 1U,
               "module Adam bias correction used the global training clock");
    life_policy_destroy(busy);
    life_policy_destroy(fresh);
}

static void compare_files(const char *first_path, const char *second_path) {
    FILE *first = fopen(first_path, "rb");
    FILE *second = fopen(second_path, "rb");
    TEST_CHECK(first != NULL && second != NULL, "could not open comparison checkpoints");
    int left, right;
    do {
        left = fgetc(first);
        right = fgetc(second);
        TEST_CHECK(left == right, "exact continuation checkpoint bytes differ");
    } while (left != EOF);
    TEST_CHECK(!ferror(first) && !ferror(second), "could not read comparison checkpoints");
    TEST_CHECK(fclose(first) == 0 && fclose(second) == 0, "could not close checkpoints");
}

static void checkpoint_trailing_content(const char *path) {
    FILE *malformed = fopen(path, "a");
    TEST_CHECK(malformed != NULL && fputs("trailing-data\n", malformed) >= 0 &&
                   fclose(malformed) == 0,
               "could not create malformed checkpoint");
    TEST_CHECK(life_policy_checkpoint_load(path) == NULL,
               "checkpoint parser accepted trailing content");
}

static void compare_continuation(life_policy *whole, life_policy *resumed, const char *first_path,
                                 const char *second_path) {
    TEST_CHECK(life_policy_hash(resumed) == life_policy_hash(whole),
               "checkpoint resumed Life training changed deterministic continuation");
    TEST_CHECK(life_policy_checkpoint_save(whole, first_path), cgai_last_error());
    TEST_CHECK(life_policy_checkpoint_save(resumed, second_path), cgai_last_error());
    compare_files(first_path, second_path);
    checkpoint_trailing_content(second_path);
    TEST_CHECK(remove(first_path) == 0 && remove(second_path) == 0,
               "could not clean test checkpoints");
}

static void replay_resume(void) {
    const char *first_path = "life-policy-first.checkpoint";
    const char *second_path = "life-policy-second.checkpoint";
    life_record records[3] = {fixture_record(2U, 3U), fixture_record(15U, 6U),
                              fixture_record(22U, 12U)};
    records[1].state.values[0] = 54U;
    records[2].state.values[15] = 4U;
    life_policy *whole = life_policy_create(29U);
    life_policy *segmented = life_policy_create(29U);
    TEST_CHECK(whole != NULL && segmented != NULL, cgai_last_error());
    TEST_CHECK(life_policy_train(whole, records, 3U, 5U), cgai_last_error());
    TEST_CHECK(life_policy_train(segmented, records, 3U, 3U), cgai_last_error());
    TEST_CHECK(life_policy_checkpoint_save(segmented, first_path), cgai_last_error());
    life_policy *resumed = life_policy_checkpoint_load(first_path);
    TEST_CHECK(resumed != NULL, cgai_last_error());
    TEST_CHECK(life_policy_hash(resumed) == life_policy_hash(segmented),
               "checkpoint load changed exact Life adapter state");
    TEST_CHECK(life_policy_train(resumed, records, 3U, 2U), cgai_last_error());
    compare_continuation(whole, resumed, first_path, second_path);
    life_policy_destroy(resumed);
    life_policy_destroy(segmented);
    life_policy_destroy(whole);
}

static void invalid_inputs(void) {
    life_policy *policy = life_policy_create(7U);
    TEST_CHECK(policy != NULL, cgai_last_error());
    const uint64_t before = life_policy_hash(policy);
    life_record records[2] = {fixture_record(7U, 3U), fixture_record(8U, 3U)};
    records[1].state.values[0] = 55U;
    TEST_CHECK(!life_policy_train(policy, records, 2U, 1U),
               "Life training accepted an invalid feature");
    TEST_CHECK(life_policy_hash(policy) == before,
               "invalid batch mutated weights, moments or ordering before full validation");
    records[1] = fixture_record(23U, 3U);
    TEST_CHECK(!life_policy_train(policy, records, 2U, 1U), "Life admitted an invalid edit ID");
    records[1] = fixture_record(8U, 16U);
    TEST_CHECK(!life_policy_train(policy, records, 2U, 1U), "Life admitted an invalid module");
    TEST_CHECK(!life_policy_train(policy, records, 1U, 0U), "Life admitted zero epochs");
    cgai_gameplay_result result;
    memset(&result, 0x5a, sizeof(result));
    cgai_gameplay_result unchanged = result;
    TEST_CHECK(!life_policy_select(policy, &records[0].state, 16U, 3U, &result),
               "Life inference admitted an invalid module");
    TEST_CHECK(memcmp(&result, &unchanged, sizeof(result)) == 0,
               "invalid inference published partial results");
    TEST_CHECK(life_policy_select(policy, &records[0].state, 0U, 3U, &result) &&
                   result.output == 0U && result.forward_passes == 0U && result.probability == 1.0,
               "empty module domain did not yield deterministic fallback");
    TEST_CHECK(life_policy_hash(policy) == before, "invalid operations changed Life state");
    life_policy_destroy(policy);
}

static void merged_checkpoint(life_policy *policy) {
    TEST_CHECK(life_policy_checkpoint_save(policy, "life-policy-merged.checkpoint"),
               cgai_last_error());
    life_policy *loaded = life_policy_checkpoint_load("life-policy-merged.checkpoint");
    TEST_CHECK(loaded != NULL && life_policy_hash(loaded) == life_policy_hash(policy),
               "merged active mask or routing masses did not survive checkpointing");
    TEST_CHECK(remove("life-policy-merged.checkpoint") == 0, "could not clean merged checkpoint");
    life_policy_destroy(loaded);
}

static void merge_preservation(void) {
    life_policy *policy = life_policy_create(83U);
    TEST_CHECK(policy != NULL, cgai_last_error());
    life_record records[2] = {fixture_record(7U, 1U), fixture_record(7U, 2U)};
    life_merge_report report;
    const uint64_t unsupported = life_policy_hash(policy);
    TEST_CHECK(life_policy_try_merge(policy, 3U, records, 1U, 4U, &report) && !report.accepted,
               "Life consolidation admitted missing parent evidence");
    TEST_CHECK(life_policy_hash(policy) == unsupported,
               "unsupported consolidation mutated original policy");
    TEST_CHECK(life_policy_train(policy, records, 2U, 50U), cgai_last_error());
    const life_policy_info before = inspect(policy);
    TEST_CHECK(life_policy_try_merge(policy, 3U, records, 2U, 8U, &report), cgai_last_error());
    TEST_CHECK(report.accepted && report.survivor == 0U && report.retired_mask == 2U,
               "preserved parent models did not consolidate into the lowest slot");
    const life_policy_info after = inspect(policy);
    TEST_CHECK(after.active_mask == 13U && after.module_mass[0] == 2.0 &&
                   after.module_mass[1] == 0.0,
               "accepted consolidation did not preserve total routing mass");
    TEST_CHECK(after.shared_hash == before.shared_hash &&
                   after.module_hash[1] == before.module_hash[1] &&
                   after.module_hash[2] == before.module_hash[2] &&
                   after.module_hash[3] == before.module_hash[3],
               "consolidation changed retired or unrelated weights/moments");
    TEST_CHECK(report.parent_agreement[0] == 1.0 && report.parent_agreement[1] == 1.0 &&
                   report.global_agreement == 1.0 && report.runtime_agreement == 1.0,
               "accepted consolidation failed its behavior-preservation gate");
    merged_checkpoint(policy);
    life_policy_destroy(policy);
}

static void conflicting_merge_rejected(void) {
    life_policy *policy = life_policy_create(22U);
    TEST_CHECK(policy != NULL, cgai_last_error());
    life_record records[2] = {fixture_record(2U, 1U), fixture_record(7U, 2U)};
    TEST_CHECK(life_policy_train(policy, records, 2U, 50U), cgai_last_error());
    const uint64_t before = life_policy_hash(policy);
    life_merge_report report;
    TEST_CHECK(life_policy_try_merge(policy, 3U, records, 2U, 20U, &report) && !report.accepted,
               "consolidation accepted incompatible parent behavior on identical states");
    TEST_CHECK(life_policy_hash(policy) == before,
               "rejected consolidation modified parameters, Adam state or metadata");
    life_policy_destroy(policy);
}

static int forbidden_argmax_fixture(uint64_t seed) {
    life_policy *policy = life_policy_create(seed);
    TEST_CHECK(policy != NULL, cgai_last_error());
    life_record records[2] = {fixture_record(22U, 1U), fixture_record(22U, 2U)};
    TEST_CHECK(life_policy_train(policy, records, 2U, 50U), cgai_last_error());
    for (size_t i = 0U; i < 2U; ++i) {
        records[i].target = 0U;
        records[i].state.values[4] = records[i].state.values[5] = 54U;
    }
    const uint64_t before = life_policy_hash(policy);
    life_merge_report report;
    TEST_CHECK(life_policy_try_merge(policy, 3U, records, 2U, 1U, &report), cgai_last_error());
    const int caught = report.parent_agreement[0] == 1.0 && report.parent_agreement[1] == 1.0 &&
                       report.global_agreement == 1.0 && report.runtime_agreement < 0.95;
    TEST_CHECK(!caught || (!report.accepted && life_policy_hash(policy) == before),
               "a merge preserved forbidden argmaxes but changed actual legal decisions");
    printf("forbidden argmax fixture seed=%" PRIu64 " runtime=%g caught=%d\n", seed,
           report.runtime_agreement, caught);
    life_policy_destroy(policy);
    return caught;
}

static void forbidden_argmax_merge(void) {
    TEST_CHECK(forbidden_argmax_fixture(1U),
               "forbidden-argmax fixture did not exercise the runtime preservation gate");
}

int main(void) {
    restricted_updates();
    module_clocks();
    replay_resume();
    invalid_inputs();
    merge_preservation();
    conflicting_merge_rejected();
    forbidden_argmax_merge();
    puts("Life policy: restricted Adam, learning, exact replay and preservation gates pass");
    return EXIT_SUCCESS;
}
