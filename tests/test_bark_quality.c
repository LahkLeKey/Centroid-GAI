/** @file test_bark_quality.c @brief Frozen quality, resource and latency promotion checks. */
#include "bark_tool.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>

/** @brief Construct internally consistent measurements for each exact frozen split.
 * @param candidate Nonzero selects improving perfect candidate metrics.
 * @return Complete fabricated quality report for isolated gate tests. */
static bark_quality quality_fixture(int candidate) {
    /* Step 1: Populate all raw counters, including the training split and abstention. */
    bark_quality quality = {0};
    for (size_t i = 0U; i < 3U; ++i) {
        quality.split[i].cases = bark_fixture_count((bark_fixture_split)i);
        quality.split[i].correct = quality.split[i].cases - (candidate ? 0U : 2U);
        quality.split[i].abstained = i == BARK_FIXTURE_TRAINING ? 8U : 2U;
        quality.split[i].cross_entropy = candidate ? 0.5 : 1.0;
    }
    /* Step 2: Supply nonempty bounded resident payload measurements. */
    quality.resources.model_bytes = 131072U;
    quality.resources.session_bytes = 32768U;
    return quality;
}

/** @brief Construct a finite ordered successful benchmark on the required workload.
 * @return Complete fabricated timings requiring no runtime or hardware dependency. */
static bark_performance performance_fixture(void) {
    /* Step 1: Keep all quantiles ordered below the absolute version-one ceilings. */
    const bark_performance performance = {
        BARK_BENCHMARK_SAMPLES, BARK_SESSION_POOL, 50.0, 100.0, 150.0, 200.0};
    return performance;
}

/** @brief Reject malformed raw counts and every nonfinite or negative loss in both reports.
 * @param index Frozen split index tested independently. */
static void check_invalid_split(size_t index) {
    /* Step 1: Derive malformed values from the exact nonempty frozen denominator. */
    bark_quality before = quality_fixture(0);
    bark_quality after = quality_fixture(1);
    const bark_performance performance = performance_fixture();
    const size_t count = before.split[index].cases;
    const bark_split_metrics invalid[] = {{0U, 0U, 0U, 0.5},
                                          {count + 1U, count, 0U, 0.5},
                                          {count, count + 1U, 0U, 0.5},
                                          {count, count, count + 1U, 0.5},
                                          {count, count, 0U, NAN},
                                          {count, count, 0U, INFINITY},
                                          {count, count, 0U, -0.01}};
    /* Step 2: Invalid incumbent or candidate data must never hide behind good held-out metrics. */
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        after.split[index] = invalid[i];
        TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
                   "malformed candidate split passed promotion");
        after = quality_fixture(1);
        before.split[index] = invalid[i];
        TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
                   "malformed incumbent split passed promotion");
        before = quality_fixture(0);
    }
}

/** @brief Reject insufficient held-out improvement, accuracy or tolerated test regression. */
static void check_heldout_gates(void) {
    /* Step 1: Accuracy must meet the absolute floor on both independent held-out splits. */
    bark_quality before = quality_fixture(0);
    bark_quality after = quality_fixture(1);
    const bark_performance performance = performance_fixture();
    for (size_t split = BARK_FIXTURE_DEVELOPMENT; split <= BARK_FIXTURE_TEST; ++split) {
        after.split[split].correct = 11U;
        TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
                   "held-out accuracy below 95 percent passed promotion");
        after = quality_fixture(1);
    }
    /* Step 2: A sub-threshold development loss decrease cannot pass the improvement gate. */
    after.split[BARK_FIXTURE_DEVELOPMENT].cross_entropy =
        before.split[BARK_FIXTURE_DEVELOPMENT].cross_entropy - BARK_MINIMUM_IMPROVEMENT * 0.5;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
               "insufficient development improvement passed promotion");
    after = quality_fixture(1);
    /* Step 3: Preserve the explicit tiny numerical tolerance without accepting larger regressions.
     */
    after.split[BARK_FIXTURE_TEST].cross_entropy =
        before.split[BARK_FIXTURE_TEST].cross_entropy + BARK_REGRESSION_TOLERANCE * 0.5;
    TEST_CHECK(bark_tool_gate(&before, &after, &performance),
               "allowed test-loss tolerance was rejected");
    after.split[BARK_FIXTURE_TEST].cross_entropy =
        before.split[BARK_FIXTURE_TEST].cross_entropy + BARK_REGRESSION_TOLERANCE * 2.0;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
               "test-loss regression passed promotion");
}

/** @brief Reject host violations and oversized or absent resident memory measurements. */
static void check_resource_gates(void) {
    /* Step 1: Exactly bounded requested payloads remain admissible. */
    const bark_quality before = quality_fixture(0);
    bark_quality after = quality_fixture(1);
    const bark_performance performance = performance_fixture();
    after.resources.model_bytes = BARK_MODEL_LIMIT;
    after.resources.session_bytes = BARK_SESSION_LIMIT;
    TEST_CHECK(bark_tool_gate(&before, &after, &performance), "exact memory caps were rejected");
    /* Step 2: Reject any amount above either cap and each independent decoding violation. */
    after.resources.model_bytes = BARK_MODEL_LIMIT + 1U;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance), "oversized model passed promotion");
    after = quality_fixture(1);
    after.resources.session_bytes = BARK_SESSION_LIMIT + 1U;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
               "oversized session passed promotion");
    after = quality_fixture(1);
    after.mask_violations = 1U;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance), "mask violation passed promotion");
    after = quality_fixture(1);
    after.repeat_violations = 1U;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance), "repeat violation passed promotion");
}

/** @brief Reject absent resident resource measurements in either comparison report. */
static void check_missing_resources(void) {
    /* Step 1: Both quality owners need actual nonempty resource measurements. */
    bark_quality before = quality_fixture(0);
    bark_quality after = quality_fixture(1);
    const bark_performance performance = performance_fixture();
    after.resources.model_bytes = 0U;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance), "missing candidate model accepted");
    after = quality_fixture(1);
    after.resources.session_bytes = 0U;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
               "missing candidate scratch accepted");
    after = quality_fixture(1);
    before.resources.model_bytes = 0U;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance), "missing incumbent model accepted");
    before = quality_fixture(0);
    before.resources.session_bytes = 0U;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
               "missing incumbent scratch accepted");
}

/** @brief Reject nonfinite architecture scalars hidden inside either resource report. */
static void check_nonfinite_resources(void) {
    /* Step 1: All measured doubles must be finite, including copied resource configuration. */
    bark_quality before = quality_fixture(0);
    bark_quality after = quality_fixture(1);
    const bark_performance performance = performance_fixture();
    after.resources.neural.config.routing_temperature = NAN;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
               "nonfinite candidate resource scalar accepted");
    after = quality_fixture(1);
    before.resources.neural.config.routing_temperature = INFINITY;
    TEST_CHECK(!bark_tool_gate(&before, &after, &performance),
               "nonfinite incumbent resource scalar accepted");
}

/** @brief Reject changed workloads, invalid summaries and slow candidate timing. */
static void check_performance_gates(void) {
    /* Step 1: Cover workload changes, each nonfinite field, ordering and absolute ceilings. */
    const bark_quality before = quality_fixture(0);
    const bark_quality after = quality_fixture(1);
    const bark_performance invalid[] = {
        {0U, 4U, 50.0, 100.0, 150.0, 200.0},       {2047U, 4U, 50.0, 100.0, 150.0, 200.0},
        {2048U, 0U, 50.0, 100.0, 150.0, 200.0},    {2048U, 3U, 50.0, 100.0, 150.0, 200.0},
        {2048U, 4U, NAN, 100.0, 150.0, 200.0},     {2048U, 4U, 50.0, NAN, 150.0, 200.0},
        {2048U, 4U, 50.0, 100.0, NAN, 200.0},      {2048U, 4U, 50.0, 100.0, 150.0, NAN},
        {2048U, 4U, 50.0, 100.0, 150.0, INFINITY}, {2048U, 4U, -1.0, 100.0, 150.0, 200.0},
        {2048U, 4U, 101.0, 100.0, 150.0, 200.0},   {2048U, 4U, 50.0, 151.0, 150.0, 200.0},
        {2048U, 4U, 50.0, 100.0, 201.0, 200.0},    {2048U, 4U, 50.0, 501.0, 600.0, 700.0},
        {2048U, 4U, 50.0, 500.0, 1001.0, 1100.0}};
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(*invalid); ++i)
        TEST_CHECK(!bark_tool_gate(&before, &after, &invalid[i]),
                   "invalid or slow benchmark passed promotion");
    /* Step 2: Exact quantile ceilings pass without silently constraining measured maximum. */
    const bark_performance boundary = {2048U, 4U, 50.0, 500.0, 1000.0, 2000.0};
    TEST_CHECK(bark_tool_gate(&before, &after, &boundary), "exact latency ceilings were rejected");
}

/** @brief Check complete valid promotion and rejected missing pointers without side effects. */
static void check_valid_and_null(void) {
    /* Step 1: A materially improving perfect candidate meets every gate. */
    const bark_quality before = quality_fixture(0);
    const bark_quality after = quality_fixture(1);
    const bark_performance performance = performance_fixture();
    TEST_CHECK(bark_tool_gate(&before, &after, &performance), "valid candidate failed promotion");
    /* Step 2: Missing reports must return rejection rather than dereferencing missing state. */
    TEST_CHECK(!bark_tool_gate(NULL, &after, &performance), "missing incumbent passed promotion");
    TEST_CHECK(!bark_tool_gate(&before, NULL, &performance), "missing candidate passed promotion");
    TEST_CHECK(!bark_tool_gate(&before, &after, NULL), "missing benchmark passed promotion");
    bark_quality sentinel = after;
    TEST_CHECK(bark_tool_quality(NULL, &sentinel) == CGAI_STATUS_ERROR,
               "missing quality model was accepted");
    TEST_CHECK(sentinel.split[0].cases == after.split[0].cases &&
                   sentinel.split[0].cross_entropy == after.split[0].cross_entropy,
               "failed quality scoring changed the caller report");
}

/** @brief Run fabricated promotion checks independently of local benchmark variability.
 * @return Zero after every gate threshold and malformed-data check passes. */
int main(void) {
    /* Step 1: Check full valid promotion and every split's raw-count integrity. */
    check_valid_and_null();
    for (size_t i = 0U; i < 3U; ++i)
        check_invalid_split(i);
    /* Step 2: Require held-out improvement, legal host output, memory and timing simultaneously. */
    check_heldout_gates();
    check_resource_gates();
    check_missing_resources();
    check_nonfinite_resources();
    check_performance_gates();
    puts("Bark quality promotion checks passed.");
    return 0;
}
