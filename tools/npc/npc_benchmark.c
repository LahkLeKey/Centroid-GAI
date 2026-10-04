/** @file npc_benchmark.c @brief Measured complete NPC policy paths and serial scheduling. */
#ifndef _WIN32
/** Request the POSIX monotonic clock interface on Unix hosts. */
#define _POSIX_C_SOURCE 200809L
#endif
#include "internal/error.h"
#include "npc_evaluation.h"
#include "npc_teacher.h"
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#ifndef NPC_BUILD_FLAGS
/** Effective configuration flags supplied by CMake, or the standalone fallback. */
#define NPC_BUILD_FLAGS "release-compiler-defaults"
#endif
#if defined(__clang__)
/** Compiler identity recorded alongside reference-machine measurements. */
#define NPC_COMPILER "clang " __clang_version__
#elif defined(__GNUC__)
/** Compiler identity recorded alongside reference-machine measurements. */
#define NPC_COMPILER "gcc " __VERSION__
#elif defined(_MSC_VER)
/** Stringify an expanded compiler version token. */
#define NPC_STRINGIFY_INNER(value) #value
/** Expand and stringify the compiler version. */
#define NPC_STRINGIFY(value) NPC_STRINGIFY_INNER(value)
/** Compiler identity recorded alongside reference-machine measurements. */
#define NPC_COMPILER "msvc " NPC_STRINGIFY(_MSC_VER)
#else
/** Standalone fallback when the compiler supplies no recognized version macro. */
#define NPC_COMPILER "unknown-c11-compiler"
#endif

/** Owned prepared benchmark sessions and immutable observation inputs. */
typedef struct npc_benchmark_job {
    npc_policy_session *sessions[32];      /**< Preallocated independent adapters, one worker. */
    npc_observation observations[32];      /**< Previously extracted visible inputs. */
    double timings[NPC_BENCHMARK_SAMPLES]; /**< Raw measured durations, retained only locally. */
    double us_per_tick;                    /**< Fixed raw-clock conversion. */
} npc_benchmark_job;

/** @brief Establish the platform's monotonic raw-tick conversion.
 * @param job Writable private benchmark context.
 * @return OK for a valid clock, ERROR otherwise. */
static cgai_status clock_initialize(npc_benchmark_job *job) {
#ifdef _WIN32
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return cgai_fail("NPC benchmark monotonic clock is unavailable");
    job->us_per_tick = 1000000.0 / (double)frequency.QuadPart;
#else
    job->us_per_tick = 0.001;
#endif
    return isfinite(job->us_per_tick) && job->us_per_tick > 0.0
               ? CGAI_STATUS_OK
               : cgai_fail("NPC benchmark clock conversion is invalid");
}

/** @brief Read an integer monotonic clock without allocation or I/O.
 * @param ticks Writable raw clock value.
 * @return OK for a valid reading, ERROR otherwise. */
static cgai_status clock_read(uint64_t *ticks) {
#ifdef _WIN32
    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter) || counter.QuadPart < 0)
        return cgai_fail("NPC benchmark clock reading failed");
    *ticks = (uint64_t)counter.QuadPart;
#else
    struct timespec counter;
    if (clock_gettime(CLOCK_MONOTONIC, &counter) != 0 || counter.tv_sec < 0 ||
        counter.tv_nsec < 0 || counter.tv_nsec >= 1000000000L ||
        (uint64_t)counter.tv_sec > (UINT64_MAX - (uint64_t)counter.tv_nsec) / UINT64_C(1000000000))
        return cgai_fail("NPC benchmark clock reading failed or overflowed");
    *ticks = (uint64_t)counter.tv_sec * UINT64_C(1000000000) + (uint64_t)counter.tv_nsec;
#endif
    return CGAI_STATUS_OK;
}

/** @brief Release every benchmark adapter, including partially prepared contexts.
 * @param job Borrowed private benchmark owner. */
static void benchmark_destroy(npc_benchmark_job *job) {
    for (size_t index = 0U; index < 32U; ++index)
        npc_policy_session_destroy(job->sessions[index]);
}

/** @brief Capture one real training-world observation after a bounded teacher prefix.
 * @param index Deterministic workload index.
 * @param observation Writable visible-only benchmark input.
 * @return OK on complete preparation, ERROR otherwise. */
static cgai_status benchmark_observation(size_t index, npc_observation *observation) {
    npc_family family;
    npc_world world;
    npc_memory memory;
    if (!npc_family_get(NPC_TRAIN, (uint32_t)(index % NPC_FAMILIES_PER_SPLIT), &family) ||
        !npc_world_init(&world, &family, (uint32_t)index))
        return cgai_fail("could not prepare a training-world benchmark observation");
    npc_memory_reset(&memory);
    for (size_t step = 0U; step < index % 6U && world.terminal == NPC_RUNNING; ++step) {
        npc_world_observe(&world, observation);
        npc_memory_observe(&memory, observation);
        (void)npc_world_step(&world, npc_teacher_action(observation, &memory));
    }
    npc_world_observe(&world, observation);
    return CGAI_STATUS_OK;
}

/** @brief Prepare all adapters and observations before any measured work.
 * @param model Borrowed immutable inference model.
 * @param job Writable zero-initialized benchmark owner.
 * @return OK after complete preparation, ERROR otherwise. */
static cgai_status benchmark_prepare(const cgai_gameplay_model *model, npc_benchmark_job *job) {
    for (size_t index = 0U; index < 32U; ++index) {
        job->sessions[index] = npc_policy_session_create(model, 1);
        if (job->sessions[index] == NULL ||
            !benchmark_observation(index, &job->observations[index]))
            return cgai_fail("could not prepare the complete NPC benchmark session pool");
    }
    return clock_initialize(job);
}

/** @brief Measure real adapter updates with nonrepeating bounded replay stamps.
 * @param job Borrowed prepared benchmark context.
 * @param sample Frozen round-robin sample index.
 * @param call Index inside a single-call or serial batch.
 * @param mode Normal0, fallback1 or serial32 batch2.
 * @return OK on one complete policy call, ERROR otherwise. */
static cgai_status benchmark_call(npc_benchmark_job *job, size_t sample, size_t call, size_t mode) {
    const size_t index = mode == 2U ? call : sample % 4U;
    npc_observation observation = job->observations[(sample + call) % 32U];
    observation.ticks = (uint32_t)((mode == 2U ? sample : sample / 4U) % NPC_MAX_TICKS);
    observation.ticks_remaining = NPC_MAX_TICKS - observation.ticks;
    cgai_gameplay_result selected;
    if (!npc_policy_decide(job->sessions[index], &observation, mode == 1U ? 0U : 3U, &selected))
        return CGAI_STATUS_ERROR;
    if (selected.forward_passes != (mode == 1U ? 0U : 1U))
        return cgai_fail("NPC benchmark performed an unexpected inference workload");
    return CGAI_STATUS_OK;
}

/** @brief Execute one policy call or one32-NPC serial scheduling batch.
 * @param job Borrowed prepared benchmark context.
 * @param sample Recorded deterministic round-robin workload index.
 * @param mode Normal0, forced fallback1, or serial32 batch2.
 * @return OK on complete validated work, ERROR otherwise. */
static cgai_status benchmark_query(npc_benchmark_job *job, size_t sample, size_t mode) {
    const size_t calls = mode == 2U ? 32U : 1U;
    for (size_t call = 0U; call < calls; ++call)
        if (!benchmark_call(job, sample, call, mode))
            return CGAI_STATUS_ERROR;
    return CGAI_STATUS_OK;
}

/** @brief Capture all complete-call timings into already allocated storage.
 * @param job Borrowed warmed benchmark context.
 * @param mode Frozen workload mode.
 * @return OK on every measured sample, ERROR otherwise. */
static cgai_status benchmark_samples(npc_benchmark_job *job, size_t mode) {
    for (size_t index = 0U; index < NPC_BENCHMARK_SAMPLES; ++index) {
        uint64_t start = 0U;
        uint64_t finish = 0U;
        if (!clock_read(&start) || !benchmark_query(job, index, mode) || !clock_read(&finish) ||
            finish < start)
            return cgai_fail("incomplete or nonmonotonic NPC benchmark sample");
        job->timings[index] = (double)(finish - start) * job->us_per_tick;
        if (!isfinite(job->timings[index]) || job->timings[index] < 0.0)
            return cgai_fail("invalid measured NPC benchmark duration");
    }
    return CGAI_STATUS_OK;
}

/** @brief Compare two finite measured durations for nearest-rank percentiles.
 * @param left Borrowed first scalar.
 * @param right Borrowed second scalar.
 * @return Negative, zero or positive total ordering. */
static int timing_compare(const void *left, const void *right) {
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

/** @brief Measure a complete pinned workload after untimed warmup.
 * @param job Borrowed preallocated benchmark context.
 * @param mode Normal0, fallback1 or serial32 batch2.
 * @param timing Writable complete nearest-rank timing.
 * @return OK after all measured samples, ERROR otherwise. */
static cgai_status benchmark_measure(npc_benchmark_job *job, size_t mode, npc_timing *timing) {
    for (size_t index = 0U; index < NPC_BENCHMARK_WARMUP; ++index)
        if (!benchmark_query(job, index, mode))
            return CGAI_STATUS_ERROR;
    if (!benchmark_samples(job, mode))
        return CGAI_STATUS_ERROR;
    qsort(job->timings, NPC_BENCHMARK_SAMPLES, sizeof(job->timings[0]), timing_compare);
    const npc_timing measured = {NPC_BENCHMARK_SAMPLES,
                                 mode == 2U ? 32U : 4U,
                                 job->timings[(NPC_BENCHMARK_SAMPLES * 50U + 99U) / 100U - 1U],
                                 job->timings[(NPC_BENCHMARK_SAMPLES * 95U + 99U) / 100U - 1U],
                                 job->timings[(NPC_BENCHMARK_SAMPLES * 99U + 99U) / 100U - 1U],
                                 job->timings[NPC_BENCHMARK_SAMPLES - 1U]};
    *timing = measured;
    return CGAI_STATUS_OK;
}

/** @brief Write resource ownership and measured complete-path timings.
 * @param path Trusted new report destination.
 * @param hardware Caller-recorded reference-machine label.
 * @param resources Complete public inference ownership report.
 * @param session_bytes Neural scratch plus persistent adapter requested heap.
 * @param timing Three complete measured workloads.
 * @return OK on complete write and close, ERROR otherwise. */
static cgai_status benchmark_write(const char *path, const char *hardware,
                                   const cgai_gameplay_resources *resources, size_t session_bytes,
                                   const npc_timing timing[3]) {
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
        return cgai_fail("could not open measured NPC benchmark report");
    int valid =
        fprintf(stream,
                "schema\tnpc-benchmark-v1\nhardware\t%s\ncompiler\t%s\n"
                "build_flags\t%s\nstandard\tc11\nfp_rounding\t%d\nworkers\t1\n"
                "clock\tmonotonic-raw-ticks\nwarmup\t%u\nparameters\t%zu\n"
                "model_bytes\t%zu\noptimizer_bytes\t%zu\nneural_session_bytes\t%zu\n"
                "adapter_bytes\t%zu\nsession_bytes\t%zu\n"
                "allocator_overhead\texcluded-platform-dependent\n"
                "policy_stack_estimate_bytes\t%zu\n"
                "benchmark_stack_bytes\t%zu\nsimulator_timing\texcluded\n"
                "observation_workload\ttrain-first32-familyvariant-boundedteacher-prefix-replay-"
                "stamps-v1\n"
                "static_contract_bytes\t%zu\n",
                hardware, NPC_COMPILER, NPC_BUILD_FLAGS, fegetround(), NPC_BENCHMARK_WARMUP,
                resources->parameter_count, resources->model_bytes, resources->optimizer_bytes,
                resources->session_bytes, sizeof(npc_policy_session), session_bytes,
                sizeof(cgai_gameplay_request) + sizeof(cgai_gameplay_result) + sizeof(npc_memory),
                sizeof(npc_benchmark_job), sizeof(cgai_gameplay_config)) >= 0;
    const char *const names[3] = {"normal", "fallback", "serial32"};
    for (size_t mode = 0U; mode < 3U && valid; ++mode)
        valid =
            fprintf(stream, "timing\t%s\t%zu\t%zu\t1\t%.17g\t%.17g\t%.17g\t%.17g\n", names[mode],
                    timing[mode].samples, timing[mode].sessions, timing[mode].p50_us,
                    timing[mode].p95_us, timing[mode].p99_us, timing[mode].maximum_us) >= 0;
    const int closed = fclose(stream) == 0;
    return valid && closed ? CGAI_STATUS_OK : cgai_fail("incomplete measured NPC benchmark report");
}

/** @brief Inspect resources and perform the three complete pinned workloads.
 * @param model Borrowed inference-only compatible weights.
 * @param report_path New compact measurement destination.
 * @param hardware Recorded reference-machine label.
 * @param job Zero-initialized benchmark owner, released by the caller.
 * @return OK on complete measured publication, ERROR otherwise. */
static cgai_status benchmark_model(const cgai_gameplay_model *model, const char *report_path,
                                   const char *hardware, npc_benchmark_job *job) {
    npc_timing timing[3] = {0};
    cgai_gameplay_resources resources;
    size_t session_bytes = 0U;
    if (!npc_policy_resources(model, &resources, &session_bytes) ||
        resources.optimizer_bytes != 0U || !benchmark_prepare(model, job))
        return CGAI_STATUS_ERROR;
    for (size_t mode = 0U; mode < 3U; ++mode)
        if (!benchmark_measure(job, mode, &timing[mode]))
            return CGAI_STATUS_ERROR;
    return benchmark_write(report_path, hardware, &resources, session_bytes, timing);
}

/** @brief Measure full policy adapter paths on this declared machine.
 * @param model_path Inference-only compatible weights.
 * @param report_path New measured report destination.
 * @param hardware Recorded reference-machine label without TSV control characters.
 * @return OK after all workloads and report, ERROR otherwise. */
cgai_status npc_benchmark_file(const char *model_path, const char *report_path,
                               const char *hardware) {
    if (hardware == NULL || hardware[0] == '\0' || strpbrk(hardware, "\t\r\n") != NULL)
        return cgai_fail("NPC benchmark requires a valid declared hardware label");
    cgai_gameplay_model *model = cgai_gameplay_load(model_path);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    npc_benchmark_job job = {0};
    const cgai_status status = benchmark_model(model, report_path, hardware, &job);
    benchmark_destroy(&job);
    cgai_gameplay_destroy(model);
    return status;
}
