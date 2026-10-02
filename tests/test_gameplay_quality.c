/** @file test_gameplay_quality.c @brief Independent task, composition and latency rejection gates.
 */
#include "gameplay_evaluation.h"
#include "test_utils.h"
#include <math.h>

/** @brief Populate one task's frozen metrics and materially contributing module diagnostics.
 * @param quality Private zero-initialized fixture.
 * @param task Supported task index.
 * @param candidate Nonzero selects improved metrics. */
static void task_fixture(gameplay_quality *quality, uint32_t task, int candidate) {
    /* Step 1: Fix independent quality denominators and complete host stress counts. */
    const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
    quality->host_checks[task] =
        ((size_t)1U << descriptor->output_count) * descriptor->output_count * 4U +
        descriptor->fixture_count;
    quality->composition_cases[task] = descriptor->fixture_count;
    for (size_t split = 0U; split < 3U; ++split) {
        gameplay_split_metrics *metric = &quality->split[task][split];
        metric->cases = gameplay_fixture_count(task, (gameplay_split)split);
        metric->correct = metric->cases - (candidate ? 0U : 2U);
        metric->cross_entropy = candidate ? 0.5 : 1.0;
    }
    /* Step 2: Both specialists have substantial posterior shares and measurable ablations. */
    for (size_t module = 0U; module < GAMEPLAY_MODULE_COUNT; ++module) {
        quality->module_weights[task][module] = 0.5;
        quality->module_contributions[task][module] = 0.5;
        quality->ablation_cross_entropy[task][module] = 0.6;
        quality->ablation_change[task][module] = 0.1;
    }
}

/** @brief Construct complete internally consistent report data for isolated promotion tests.
 * @param candidate Nonzero selects improved perfect held-out metrics.
 * @return Complete finite quality fixture. */
static gameplay_quality quality_fixture(int candidate) {
    /* Step 1: Every report retains all exact workload counts and finite task-local means. */
    gameplay_quality quality = {0};
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task)
        task_fixture(&quality, task, candidate);
    return quality;
}

/** @brief Add complete bounded resources and supplementary execution workload counts.
 * @param quality Private valid task-metric fixture. */
static void complete_fixture(gameplay_quality *quality) {
    /* Step 1: All supplemental workloads complete without unsafe or unstable proposals. */
    quality->bark_invariance_cases = GAMEPLAY_BARK_INVARIANCE_CASES;
    quality->bark_invariance_correct = GAMEPLAY_BARK_INVARIANCE_CASES;
    quality->simulator_cases = GAMEPLAY_SIMULATOR_CASES;
    quality->simulator_legal = GAMEPLAY_SIMULATOR_CASES;
    quality->simulator_survived = GAMEPLAY_SIMULATOR_CASES;
    quality->simulator_objective_successes = GAMEPLAY_SIMULATOR_CASES;
    quality->simulator_stable = GAMEPLAY_SIMULATOR_CASES;
    /* Step 2: Resource data describes weights-only inference ownership. */
    quality->resources.parameter_count = 1000U;
    quality->resources.parameter_bytes = 1000U * sizeof(double);
    quality->resources.model_bytes = 131072U;
    quality->resources.session_bytes = 32768U;
}

/** @brief Construct a complete fabricated candidate or incumbent.
 * @param candidate Nonzero selects improving perfect metrics.
 * @return Internally valid complete fixture. */
static gameplay_quality full_fixture(int candidate) {
    /* Step 1: Separate frozen task construction from supplemental execution ownership data. */
    gameplay_quality quality = quality_fixture(candidate);
    complete_fixture(&quality);
    return quality;
}

/** @brief Construct all three exact finite ordered timing workloads without a hardware dependency.
 * @return Complete successful synthetic timing summaries. */
static gameplay_performance performance_fixture(void) {
    /* Step 1: Singles meet their ceilings; paired summaries retain their independently measured
     * scope. */
    gameplay_performance performance = {0};
    for (size_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task)
        performance.task[task] = (gameplay_timing){
            GAMEPLAY_BENCHMARK_SAMPLES, GAMEPLAY_SESSION_POOL, 50.0, 100.0, 150.0, 200.0};
    performance.paired = (gameplay_timing){
        GAMEPLAY_BENCHMARK_SAMPLES, GAMEPLAY_SESSION_POOL, 100.0, 200.0, 300.0, 400.0};
    return performance;
}

/** @brief Reject incomplete and nonfinite split metrics in either comparison report.
 * @param task Supported task ID.
 * @param split Frozen split ID. */
static void check_split(uint32_t task, size_t split) {
    /* Step 1: Derive bad metrics from the same exact frozen denominator. */
    gameplay_quality before = full_fixture(0), after = full_fixture(1);
    const gameplay_performance performance = performance_fixture();
    const size_t count = after.split[task][split].cases;
    const gameplay_split_metrics invalid[] = {{0U, 0U, 0U, 0.5},
                                              {count + 1U, count, 0U, 0.5},
                                              {count, count + 1U, 0U, 0.5},
                                              {count, count, count + 1U, 0.5},
                                              {count, count, 0U, NAN},
                                              {count, count, 0U, INFINITY},
                                              {count, count, 0U, -0.01}};
    /* Step 2: Neither report can hide invalid training or held-out raw measurements. */
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        after.split[task][split] = invalid[i];
        TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
                   "malformed candidate split accepted");
        after = full_fixture(1);
        before.split[task][split] = invalid[i];
        TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
                   "malformed incumbent split accepted");
        before = full_fixture(0);
    }
}

/** @brief Require independent absolute accuracy, development improvement and held-out
 * nonregression.
 * @param task Supported task ID. */
static void check_improvement(uint32_t task) {
    /* Step 1: A weak task cannot be hidden by the other head's perfect quality. */
    gameplay_quality before = full_fixture(0), after = full_fixture(1);
    const gameplay_performance performance = performance_fixture();
    after.split[task][GAMEPLAY_DEVELOPMENT].correct = 0U;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "poor development accuracy accepted");
    after = full_fixture(1);
    after.split[task][GAMEPLAY_DEVELOPMENT].cross_entropy =
        1.0 - GAMEPLAY_MINIMUM_IMPROVEMENT * 0.5;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "insufficient task improvement accepted");
    /* Step 2: Only the explicit tiny test-loss tolerance is accepted. */
    after = full_fixture(1);
    after.split[task][GAMEPLAY_TEST].cross_entropy = 1.0 + GAMEPLAY_REGRESSION_TOLERANCE * 0.5;
    TEST_CHECK(gameplay_tool_gate(&before, &after, &performance),
               "allowed task loss tolerance rejected");
    after.split[task][GAMEPLAY_TEST].cross_entropy = 1.0 + GAMEPLAY_REGRESSION_TOLERANCE * 2.0;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "task test loss regression accepted");
    after = full_fixture(1);
    before.split[task][GAMEPLAY_TEST].correct = before.split[task][GAMEPLAY_TEST].cases;
    --after.split[task][GAMEPLAY_TEST].correct;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "task accuracy regression accepted");
}

/** @brief Reject decorative, collapsed or nonfinite specialist contributions to each task.
 * @param task Supported task ID.
 * @param module Tested module ID. */
static void check_composition(uint32_t task, size_t module) {
    /* Step 1: Both outer routing and selected-ID posterior shares must be materially nonzero. */
    const gameplay_quality before = full_fixture(0);
    gameplay_quality after = full_fixture(1);
    const gameplay_performance performance = performance_fixture();
    after.module_weights[task][module] = 0.01;
    after.module_weights[task][1U - module] = 0.99;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "collapsed task router accepted");
    after = full_fixture(1);
    after.module_contributions[task][module] = 0.01;
    after.module_contributions[task][1U - module] = 0.99;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "ineffective task module accepted");
    /* Step 2: Numerically equivalent or nonfinite ablations do not establish composition. */
    after = full_fixture(1);
    after.ablation_change[task][module] = 0.0;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance), "decorative module accepted");
    after = full_fixture(1);
    after.ablation_cross_entropy[task][module] = NAN;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance), "nonfinite ablation accepted");
    after = full_fixture(1);
    --after.composition_cases[task];
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "incomplete composition workload accepted");
}

/** @brief Reject each missing host validation, unsafe execution or broken invariant counter.
 * @return No value; failed gates terminate the test. */
static void check_execution(void) {
    /* Step 1: Independently exercise every complete execution or constraint requirement. */
    const gameplay_quality before = full_fixture(0);
    const gameplay_performance performance = performance_fixture();
    gameplay_quality after = full_fixture(1);
    size_t *counts[] = {&after.bark_invariance_cases, &after.bark_invariance_correct,
                        &after.simulator_cases,       &after.simulator_legal,
                        &after.simulator_survived,    &after.simulator_objective_successes,
                        &after.simulator_stable,      &after.host_checks[0],
                        &after.host_checks[1]};
    for (size_t i = 0U; i < sizeof(counts) / sizeof(*counts); ++i) {
        --*counts[i];
        TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
                   "incomplete or unsafe execution accepted");
        ++*counts[i];
    }
    /* Step 2: One output, repetition or module exclusion violation always rejects promotion. */
    size_t *violations[] = {&after.mask_violations[0], &after.mask_violations[1],
                            &after.repeat_violations[0], &after.repeat_violations[1],
                            &after.module_violations};
    for (size_t i = 0U; i < sizeof(violations) / sizeof(*violations); ++i) {
        ++*violations[i];
        TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
                   "host constraint violation accepted");
        --*violations[i];
    }
}

/** @brief Reject resident resource overruns and invalid or optimizer-bearing inference reports.
 * @return No value; failed resource gates terminate the test. */
static void check_resources(void) {
    /* Step 1: Exact requested heap caps remain accepted. */
    const gameplay_quality before = full_fixture(0);
    gameplay_quality after = full_fixture(1);
    const gameplay_performance performance = performance_fixture();
    after.resources.model_bytes = GAMEPLAY_MODEL_LIMIT;
    after.resources.session_bytes = GAMEPLAY_SESSION_LIMIT;
    TEST_CHECK(gameplay_tool_gate(&before, &after, &performance),
               "exact gameplay resource caps rejected");
    /* Step 2: Test each independent malformed or excessive resident resource value. */
    after.resources.model_bytes = GAMEPLAY_MODEL_LIMIT + 1U;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "oversized composed model accepted");
    after = full_fixture(1);
    after.resources.session_bytes = GAMEPLAY_SESSION_LIMIT + 1U;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance), "oversized session accepted");
    after = full_fixture(1);
    after.resources.optimizer_bytes = 1U;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "resident training optimizer accepted");
    after = full_fixture(1);
    ++after.resources.parameter_bytes;
    TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
               "invalid parameter byte accounting accepted");
}

/** @brief Reject missing, nonfinite, unordered or excessive timings for each workload.
 * @param workload Single-head0/1 or paired2 workload. */
static void check_timings(size_t workload) {
    /* Step 1: Independent workload summaries cannot be replaced by another head's fast timing. */
    const gameplay_quality before = full_fixture(0), after = full_fixture(1);
    gameplay_performance performance = performance_fixture();
    gameplay_timing *timing =
        workload < GAMEPLAY_TASK_COUNT ? &performance.task[workload] : &performance.paired;
    const gameplay_timing original = *timing;
    const double multiplier = workload < GAMEPLAY_TASK_COUNT ? 1.0 : 2.0;
    const gameplay_timing invalid[] = {
        {0U, 4U, 1.0, 2.0, 3.0, 4.0},
        {2048U, 0U, 1.0, 2.0, 3.0, 4.0},
        {2048U, 4U, NAN, 2.0, 3.0, 4.0},
        {2048U, 4U, -1.0, 2.0, 3.0, 4.0},
        {2048U, 4U, 5.0, 2.0, 3.0, 4.0},
        {2048U, 4U, 1.0, 501.0 * multiplier, 900.0 * multiplier, 1100.0 * multiplier},
        {2048U, 4U, 1.0, 500.0 * multiplier, 1001.0 * multiplier, 1100.0 * multiplier},
        {2048U, 4U, 1.0, 2.0, 3.0, INFINITY}};
    /* Step 2: All malformed raw summaries reject promotion before thresholds can mask them. */
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        *timing = invalid[i];
        TEST_CHECK(!gameplay_tool_gate(&before, &after, &performance),
                   "invalid gameplay timing accepted");
    }
    *timing = original;
    TEST_CHECK(gameplay_tool_gate(&before, &after, &performance), "restored valid timing rejected");
}

/** @brief Execute each task's split and composition rejection cases independently.
 * @return No value; failed gates terminate the test. */
static void check_all_tasks(void) {
    /* Step 1: Every task's training and held-out splits are validated independently. */
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task) {
        for (size_t split = 0U; split < 3U; ++split)
            check_split(task, split);
        check_improvement(task);
        for (size_t module = 0U; module < GAMEPLAY_MODULE_COUNT; ++module)
            check_composition(task, module);
    }
}

/** @brief Execute isolated complete promotion-gate validation.
 * @return Zero after all meaningful acceptance and failure cases pass. */
int main(void) {
    /* Step 1: Establish a valid complete improving two-task candidate. */
    const gameplay_quality before = full_fixture(0), after = full_fixture(1);
    const gameplay_performance performance = performance_fixture();
    TEST_CHECK(gameplay_tool_gate(&before, &after, &performance),
               "valid composed candidate rejected");
    check_all_tasks();
    /* Step 2: Validate execution, ownership and every timed workload independently. */
    check_execution();
    check_resources();
    for (size_t workload = 0U; workload <= GAMEPLAY_TASK_COUNT; ++workload)
        check_timings(workload);
    return 0;
}
