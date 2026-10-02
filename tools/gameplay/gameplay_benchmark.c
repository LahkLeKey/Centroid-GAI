/** @file gameplay_benchmark.c @brief Monotonic complete-selection timings with preallocated
 * scratch. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "gameplay_evaluation.h"
#include "internal/error.h"
#include <math.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/** Initialized monotonic-clock conversion, immutable throughout one benchmark. */
typedef struct gameplay_benchmark_clock {
    double microseconds_per_tick; /**< Positive finite conversion of raw monotonic ticks. */
} gameplay_benchmark_clock;
/** All fixtures, session owners and timings prepared before the measured workload. */
typedef struct gameplay_benchmark_job {
    cgai_gameplay_session
        *sessions[GAMEPLAY_SESSION_POOL]; /**< Owned independent scratch owners. */
    cgai_gameplay_request requests[GAMEPLAY_FIXTURE_CASE_COUNT]
                                  [GAMEPLAY_TASK_COUNT]; /**< Shared observations. */
    uint32_t workload;                          /**< Bark, intent or both sequential heads. */
    double timings[GAMEPLAY_BENCHMARK_SAMPLES]; /**< Stack-local durations, never persisted raw. */
    gameplay_benchmark_clock clock;             /**< Fixed monotonic clock conversion. */
} gameplay_benchmark_job;

/** @brief Establish one monotonic raw-tick clock conversion before measurement.
 * @param clock Writable private clock configuration.
 * @return OK for a supported finite conversion, ERROR otherwise. */
static cgai_status benchmark_clock_init(gameplay_benchmark_clock *clock) {
    /* Step 1: Use each platform's monotonic high-resolution time source. */
#ifdef _WIN32
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return cgai_fail("gameplay benchmark performance clock is unavailable");
    clock->microseconds_per_tick = 1000000.0 / (double)frequency.QuadPart;
#else
    clock->microseconds_per_tick = 0.001;
#endif
    /* Step 2: A malformed conversion must never yield a plausible benchmark result. */
    return isfinite(clock->microseconds_per_tick) && clock->microseconds_per_tick > 0.0
               ? CGAI_STATUS_OK
               : cgai_fail("gameplay benchmark performance clock conversion is invalid");
}

/** @brief Read validated raw monotonic ticks without allocation or I/O.
 * @param ticks Writable raw tick count, unchanged on failure.
 * @return OK for a representable monotonic reading, ERROR otherwise. */
static cgai_status benchmark_clock_read(uint64_t *ticks) {
    /* Step 1: Retain integer ticks so short durations do not subtract large floating values. */
#ifdef _WIN32
    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter) || counter.QuadPart < 0)
        return cgai_fail("gameplay benchmark performance clock read failed");
    *ticks = (uint64_t)counter.QuadPart;
#else
    struct timespec counter;
    if (clock_gettime(CLOCK_MONOTONIC, &counter) != 0 || counter.tv_sec < 0 ||
        counter.tv_nsec < 0 || counter.tv_nsec >= 1000000000L ||
        (uint64_t)counter.tv_sec > (UINT64_MAX - (uint64_t)counter.tv_nsec) / UINT64_C(1000000000))
        return cgai_fail("gameplay benchmark monotonic clock read failed or overflowed");
    *ticks = (uint64_t)counter.tv_sec * UINT64_C(1000000000) + (uint64_t)counter.tv_nsec;
#endif
    return CGAI_STATUS_OK;
}

/** @brief Release every independently allocated session, including partial preparation.
 * @param job Borrowed private benchmark owner. */
static void benchmark_destroy(gameplay_benchmark_job *job) {
    /* Step 1: Every session is independent and NULL cleanup is supported. */
    for (size_t index = 0U; index < GAMEPLAY_SESSION_POOL; ++index)
        cgai_gameplay_session_destroy(job->sessions[index]);
}

/** @brief Prepare complete typed queries for both heads against each shared frozen observation.
 * @param model Borrowed immutable aligned bundle.
 * @param job Private workload owner with writable request storage.
 * @return OK after all requests are initialized or ERROR. */
static cgai_status benchmark_requests(const cgai_gameplay_model *model,
                                      gameplay_benchmark_job *job) {
    /* Step 1: Preencode both task requests against each shared frozen observation. */
    for (size_t index = 0U; index < GAMEPLAY_FIXTURE_CASE_COUNT; ++index) {
        gameplay_fixture_case scenario;
        if (!gameplay_fixture_get(index, &scenario))
            return cgai_fail("could not prepare gameplay benchmark scenarios");
        for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task)
            if (!cgai_gameplay_default_request(model, task, &scenario.example.state,
                                               &job->requests[index][task]))
                return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

/** @brief Prepare every scratch owner, complete request and immutable monotonic clock.
 * @param model Borrowed immutable aligned bundle.
 * @param job Private zero-initialized workload owner.
 * @return OK after preparation or ERROR; caller releases every session. */
static cgai_status benchmark_prepare(const cgai_gameplay_model *model,
                                     gameplay_benchmark_job *job) {
    /* Step 1: Allocate every independent session before any timing. */
    for (size_t index = 0U; index < GAMEPLAY_SESSION_POOL; ++index) {
        job->sessions[index] = cgai_gameplay_session_create(model, GAMEPLAY_SESSION_LIMIT);
        if (job->sessions[index] == NULL)
            return CGAI_STATUS_ERROR;
    }
    /* Step 2: Establish complete typed queries and one immutable clock conversion. */
    return benchmark_requests(model, job) && benchmark_clock_init(&job->clock) ? CGAI_STATUS_OK
                                                                               : CGAI_STATUS_ERROR;
}

/** @brief Execute one complete single-head or sequential paired workload query.
 * @param job Borrowed prepared benchmark owner.
 * @param index Workload sample index used for deterministic state and session rotation.
 * @return OK after the expected complete forwards or ERROR. */
static cgai_status benchmark_query(gameplay_benchmark_job *job, size_t index) {
    /* Step 1: Single-head workloads use their own frozen task observations. */
    const gameplay_task_descriptor *task = gameplay_task_get(job->workload % GAMEPLAY_TASK_COUNT);
    const size_t scenario = job->workload < GAMEPLAY_TASK_COUNT
                                ? task->fixture_offset + index % task->fixture_count
                                : index % GAMEPLAY_FIXTURE_CASE_COUNT;
    const uint32_t begin = job->workload < GAMEPLAY_TASK_COUNT ? job->workload : 0U;
    const uint32_t end = job->workload < GAMEPLAY_TASK_COUNT ? begin + 1U : GAMEPLAY_TASK_COUNT;
    /* Step 2: Paired queries share the observation and scratch, making two complete forwards. */
    for (uint32_t head = begin; head < end; ++head) {
        cgai_gameplay_result selected = {0};
        if (!cgai_gameplay_select(job->sessions[index % GAMEPLAY_SESSION_POOL],
                                  &job->requests[scenario][head], &selected))
            return CGAI_STATUS_ERROR;
        if (selected.forward_passes != 1U || selected.active_modules != 2U ||
            selected.active_centroids != 32U)
            return cgai_fail("gameplay benchmark work changed");
    }
    return CGAI_STATUS_OK;
}

/** @brief Warm the exact selected workload outside its measurement window.
 * @param job Borrowed prepared workload owner.
 * @return OK after128 complete queries or ERROR. */
static cgai_status benchmark_warm(gameplay_benchmark_job *job) {
    /* Step 1: Each workload warms the same session schedule and complete query sequence. */
    for (size_t index = 0U; index < 128U; ++index)
        if (!benchmark_query(job, index))
            return CGAI_STATUS_ERROR;
    return CGAI_STATUS_OK;
}

/** @brief Measure one complete allocation-free request between validated monotonic reads.
 * @param job Borrowed prepared benchmark owner.
 * @param index Sample index below GAMEPLAY_BENCHMARK_SAMPLES.
 * @return OK after publishing one private duration, ERROR for selection or clock failure. */
static cgai_status benchmark_sample(gameplay_benchmark_job *job, size_t index) {
    /* Step 1: Retain complete-selection scope, including typed input encoding and masking. */
    uint64_t begin = 0U;
    uint64_t end = 0U;
    if (!benchmark_clock_read(&begin) || !benchmark_query(job, index) ||
        !benchmark_clock_read(&end))
        return CGAI_STATUS_ERROR;
    /* Step 2: Reject backward clocks and incomplete work instead of accepting optimistic data. */
    if (end < begin)
        return cgai_fail("gameplay benchmark clock moved backward or selection work changed");
    const double elapsed = (double)(end - begin) * job->clock.microseconds_per_tick;
    if (!isfinite(elapsed) || elapsed < 0.0)
        return cgai_fail("gameplay benchmark duration is invalid");
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
static gameplay_timing benchmark_aggregate(gameplay_benchmark_job *job) {
    /* Step 1: Aggregation is outside the timed selector and retains only bounded summaries. */
    qsort(job->timings, GAMEPLAY_BENCHMARK_SAMPLES, sizeof(job->timings[0]), benchmark_compare);
    return (gameplay_timing){
        .samples = GAMEPLAY_BENCHMARK_SAMPLES,
        .session_pool = GAMEPLAY_SESSION_POOL,
        .p50_us = job->timings[(GAMEPLAY_BENCHMARK_SAMPLES * 50U + 99U) / 100U - 1U],
        .p95_us = job->timings[(GAMEPLAY_BENCHMARK_SAMPLES * 95U + 99U) / 100U - 1U],
        .p99_us = job->timings[(GAMEPLAY_BENCHMARK_SAMPLES * 99U + 99U) / 100U - 1U],
        .maximum_us = job->timings[GAMEPLAY_BENCHMARK_SAMPLES - 1U]};
}

/** @brief Measure and publish a complete one-worker benchmark with fixed nearest-rank quantiles.
 * @param model Borrowed immutable aligned bundle.
 * @param workload Bark zero, intent one or paired two.
 * @param timing Writable complete workload summary, unchanged on error.
 * @return OK after all samples and cleanup, ERROR otherwise. */
static cgai_status benchmark_workload(const cgai_gameplay_model *model, uint32_t workload,
                                      gameplay_timing *timing) {
    /* Step 1: Keep every session and duration private until the whole workload succeeds. */
    gameplay_benchmark_job job = {0};
    job.workload = workload;
    cgai_status status = benchmark_prepare(model, &job);
    if (status)
        status = benchmark_warm(&job);
    /* Step 2: One worker serves all four sessions; this measures no parallel contention. */
    for (size_t index = 0U; status && index < GAMEPLAY_BENCHMARK_SAMPLES; ++index)
        status = benchmark_sample(&job, index);
    benchmark_destroy(&job);
    if (!status)
        return CGAI_STATUS_ERROR;
    /* Step 3: Aggregate only; allocator activity inside sorting is outside the timed region. */
    *timing = benchmark_aggregate(&job);
    return CGAI_STATUS_OK;
}

/** @brief Publish all single-head and paired complete workload summaries.
 * @param model Borrowed immutable composed bundle.
 * @param performance Writable complete result, unchanged on failure.
 * @return OK after all three complete timing workloads or ERROR. */
cgai_status gameplay_tool_benchmark(const cgai_gameplay_model *model,
                                    gameplay_performance *performance) {
    /* Step 1: Keep every workload private until all successful timings exist. */
    if (model == NULL || performance == NULL)
        return cgai_fail("gameplay benchmark requires a model and complete result");
    gameplay_performance measured = {0};
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task)
        if (!benchmark_workload(model, task, &measured.task[task]))
            return CGAI_STATUS_ERROR;
    /* Step 2: Measure two sequential selects independently from the single-head workloads. */
    if (!benchmark_workload(model, GAMEPLAY_TASK_COUNT, &measured.paired))
        return CGAI_STATUS_ERROR;
    *performance = measured;
    return CGAI_STATUS_OK;
}
