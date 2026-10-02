/** @file bark_benchmark.c @brief Monotonic complete-selection timings with preallocated scratch. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "bark_tool.h"
#include "internal/error.h"
#include <math.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/** Initialized monotonic-clock conversion, immutable throughout one benchmark. */
typedef struct bark_benchmark_clock {
    double microseconds_per_tick; /**< Positive finite conversion of raw monotonic ticks. */
} bark_benchmark_clock;
/** All fixtures, session owners and timings prepared before the measured workload. */
typedef struct bark_benchmark_job {
    cgai_bark_session *sessions[BARK_SESSION_POOL];       /**< Owned independent scratch owners. */
    bark_fixture_case scenarios[BARK_FIXTURE_CASE_COUNT]; /**< Complete frozen requests. */
    double timings[BARK_BENCHMARK_SAMPLES]; /**< Stack-local durations, never persisted raw. */
    bark_benchmark_clock clock;             /**< Fixed monotonic clock conversion. */
} bark_benchmark_job;

/** @brief Establish one monotonic raw-tick clock conversion before measurement.
 * @param clock Writable private clock configuration.
 * @return OK for a supported finite conversion, ERROR otherwise. */
static cgai_status benchmark_clock_init(bark_benchmark_clock *clock) {
    /* Step 1: Use each platform's monotonic high-resolution time source. */
#ifdef _WIN32
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return cgai_fail("bark benchmark performance clock is unavailable");
    clock->microseconds_per_tick = 1000000.0 / (double)frequency.QuadPart;
#else
    clock->microseconds_per_tick = 0.001;
#endif
    /* Step 2: A malformed conversion must never yield a plausible benchmark result. */
    return isfinite(clock->microseconds_per_tick) && clock->microseconds_per_tick > 0.0
               ? CGAI_STATUS_OK
               : cgai_fail("bark benchmark performance clock conversion is invalid");
}

/** @brief Read validated raw monotonic ticks without allocation or I/O.
 * @param ticks Writable raw tick count, unchanged on failure.
 * @return OK for a representable monotonic reading, ERROR otherwise. */
static cgai_status benchmark_clock_read(uint64_t *ticks) {
    /* Step 1: Retain integer ticks so short durations do not subtract large floating values. */
#ifdef _WIN32
    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter) || counter.QuadPart < 0)
        return cgai_fail("bark benchmark performance clock read failed");
    *ticks = (uint64_t)counter.QuadPart;
#else
    struct timespec counter;
    if (clock_gettime(CLOCK_MONOTONIC, &counter) != 0 || counter.tv_sec < 0 ||
        counter.tv_nsec < 0 || counter.tv_nsec >= 1000000000L ||
        (uint64_t)counter.tv_sec > (UINT64_MAX - (uint64_t)counter.tv_nsec) / UINT64_C(1000000000))
        return cgai_fail("bark benchmark monotonic clock read failed or overflowed");
    *ticks = (uint64_t)counter.tv_sec * UINT64_C(1000000000) + (uint64_t)counter.tv_nsec;
#endif
    return CGAI_STATUS_OK;
}

/** @brief Release every independently allocated session, including partial preparation.
 * @param job Borrowed private benchmark owner. */
static void benchmark_destroy(bark_benchmark_job *job) {
    /* Step 1: Every session is independent and NULL cleanup is supported. */
    for (size_t index = 0U; index < BARK_SESSION_POOL; ++index)
        cgai_bark_session_destroy(job->sessions[index]);
}

/** @brief Prepare bounded session owners, frozen requests and clock conversion.
 * @param model Borrowed immutable specialist.
 * @param job Writable zero-initialized private benchmark owner.
 * @return OK when all resources exist, ERROR otherwise; caller always releases sessions. */
static cgai_status benchmark_prepare(const cgai_bark_model *model, bark_benchmark_job *job) {
    /* Step 1: Allocate every worker's scratch before warm-up or timed selection. */
    for (size_t index = 0U; index < BARK_SESSION_POOL; ++index) {
        job->sessions[index] = cgai_bark_session_create(model, BARK_SESSION_LIMIT);
        if (job->sessions[index] == NULL)
            return CGAI_STATUS_ERROR;
    }
    /* Step 2: Preencode independent fixtures; timed selection consumes only typed enums. */
    for (size_t index = 0U; index < BARK_FIXTURE_CASE_COUNT; ++index) {
        if (!bark_fixture_get(index, &job->scenarios[index]))
            return cgai_fail("could not prepare bark benchmark scenarios");
    }
    return benchmark_clock_init(&job->clock);
}

/** @brief Warm the complete selector on the same round-robin sessions and scenario sequence.
 * @param job Borrowed prepared benchmark owner.
 * @return OK after exactly 128 successful single-forward requests, ERROR otherwise. */
static cgai_status benchmark_warm(bark_benchmark_job *job) {
    /* Step 1: Exercise real typed input mapping and output masking before collecting timings. */
    cgai_bark_result selected = {0};
    for (size_t index = 0U; index < 128U; ++index) {
        if (!cgai_bark_select(job->sessions[index % BARK_SESSION_POOL],
                              &job->scenarios[index % BARK_FIXTURE_CASE_COUNT].request, &selected))
            return CGAI_STATUS_ERROR;
        if (selected.forward_passes != 1U)
            return cgai_fail("bark benchmark warm-up did not perform one complete forward");
    }
    return CGAI_STATUS_OK;
}

/** @brief Measure one complete allocation-free request between validated monotonic reads.
 * @param job Borrowed prepared benchmark owner.
 * @param index Sample index below BARK_BENCHMARK_SAMPLES.
 * @return OK after publishing one private duration, ERROR for selection or clock failure. */
static cgai_status benchmark_sample(bark_benchmark_job *job, size_t index) {
    /* Step 1: Retain complete-selection scope, including typed input encoding and masking. */
    uint64_t begin = 0U;
    uint64_t end = 0U;
    cgai_bark_result selected = {0};
    if (!benchmark_clock_read(&begin) ||
        !cgai_bark_select(job->sessions[index % BARK_SESSION_POOL],
                          &job->scenarios[index % BARK_FIXTURE_CASE_COUNT].request, &selected) ||
        !benchmark_clock_read(&end))
        return CGAI_STATUS_ERROR;
    /* Step 2: Reject backward clocks and incomplete work instead of accepting optimistic data. */
    if (end < begin || selected.forward_passes != 1U)
        return cgai_fail("bark benchmark clock moved backward or selection work changed");
    const double elapsed = (double)(end - begin) * job->clock.microseconds_per_tick;
    if (!isfinite(elapsed) || elapsed < 0.0)
        return cgai_fail("bark benchmark duration is invalid");
    job->timings[index] = elapsed;
    return CGAI_STATUS_OK;
}

/** @brief Sort finite measured durations without subtracting doubles into an integer.
 * @param first Borrowed first duration.
 * @param second Borrowed second duration.
 * @return Negative, zero or positive according to numerical order. */
static int benchmark_compare(const void *first, const void *second) {
    /* Step 1: Durations have already been checked finite and nonnegative. */
    const double left = *(const double *)first;
    const double right = *(const double *)second;
    return (left > right) - (left < right);
}

/** @brief Sort complete measurements and retain fixed nearest-rank aggregate quantiles.
 * @param job Borrowed private successful benchmark owner.
 * @return Compact timing report containing no raw samples. */
static bark_performance benchmark_aggregate(bark_benchmark_job *job) {
    /* Step 1: Aggregation is outside the timed selector and retains only bounded summaries. */
    qsort(job->timings, BARK_BENCHMARK_SAMPLES, sizeof(job->timings[0]), benchmark_compare);
    return (bark_performance){
        .samples = BARK_BENCHMARK_SAMPLES,
        .session_pool = BARK_SESSION_POOL,
        .p50_us = job->timings[(BARK_BENCHMARK_SAMPLES * 50U + 99U) / 100U - 1U],
        .p95_us = job->timings[(BARK_BENCHMARK_SAMPLES * 95U + 99U) / 100U - 1U],
        .p99_us = job->timings[(BARK_BENCHMARK_SAMPLES * 99U + 99U) / 100U - 1U],
        .maximum_us = job->timings[BARK_BENCHMARK_SAMPLES - 1U]};
}

/** @brief Measure and publish a complete one-worker benchmark with fixed nearest-rank quantiles.
 * @param model Borrowed immutable specialist.
 * @param performance Writable result, unchanged on every error.
 * @return OK after all samples and cleanup, ERROR otherwise. */
cgai_status bark_tool_benchmark(const cgai_bark_model *model, bark_performance *performance) {
    /* Step 1: Keep every session and duration private until the whole workload succeeds. */
    cgai_error_clear();
    if (model == NULL || performance == NULL)
        return cgai_fail("bark benchmark requires a model and writable report");
    bark_benchmark_job job = {0};
    cgai_status status = benchmark_prepare(model, &job);
    if (status)
        status = benchmark_warm(&job);
    /* Step 2: One worker serves all four sessions; this measures no parallel contention. */
    for (size_t index = 0U; status && index < BARK_BENCHMARK_SAMPLES; ++index)
        status = benchmark_sample(&job, index);
    benchmark_destroy(&job);
    if (!status)
        return CGAI_STATUS_ERROR;
    /* Step 3: Aggregate only; allocator activity inside sorting is outside the timed region. */
    *performance = benchmark_aggregate(&job);
    return CGAI_STATUS_OK;
}
