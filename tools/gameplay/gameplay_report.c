/** @file gameplay_report.c @brief Complete compact task, composition and performance evidence. */
#include "gameplay_tool.h"
#include "internal/error.h"
#include <stdio.h>
#include <string.h>

/** Expand compiler identity before stringification. */
#define GAMEPLAY_STRINGIFY(value) GAMEPLAY_STRINGIFY_LITERAL(value)
/** Stringify the expanded compiler identity. */
#define GAMEPLAY_STRINGIFY_LITERAL(value) #value

/** @brief Identify the actual compiler used for the measured native build.
 * @return Borrowed static compiler/version text. */
static const char *compiler_name(void) {
    /* Step1: Prefer an actual compiler over its compatibility macros. */
#if defined(__clang__)
    return "clang " __clang_version__ " C11";
#elif defined(_MSC_VER)
    return "msvc " GAMEPLAY_STRINGIFY(_MSC_FULL_VER) " C11";
#elif defined(__GNUC__)
    return "gcc " __VERSION__ " C11";
#else
    return "C11 compiler";
#endif
}

/** @brief Serialize one task/split's independent target metrics.
 * @param stream Borrowed writable stream.
 * @param phase Stable before/after spelling.
 * @param task Stable task name.
 * @param split Stable split name.
 * @param metrics Borrowed complete measured split.
 * @return Nonzero on all formatted writes. */
static int write_split(FILE *stream, const char *phase, const char *task, const char *split,
                       const gameplay_split_metrics *metrics) {
    /* Step1: Full task-head likelihood is reported without legal-output renormalization. */
    return fprintf(stream,
                   "%s_%s_%s_cases\t%zu\n%s_%s_%s_correct\t%zu\n"
                   "%s_%s_%s_abstained\t%zu\n%s_%s_%s_cross_entropy\t%.17g\n",
                   phase, task, split, metrics->cases, phase, task, split, metrics->correct, phase,
                   task, split, metrics->abstained, phase, task, split,
                   metrics->cross_entropy) >= 0;
}

/** @brief Serialize controlled same-task module routing and ablation evidence.
 * @param stream Borrowed writable stream.
 * @param phase Stable before/after spelling.
 * @param name Stable task name.
 * @param task Task index.
 * @param quality Borrowed complete measured report.
 * @return Nonzero on all formatted writes. */
static int write_modules(FILE *stream, const char *phase, const char *name, size_t task,
                         const gameplay_quality *quality) {
    /* Step1: Each admitted bank's weight and selected-output posterior are independently visible.
     */
    for (size_t module = 0U; module < GAMEPLAY_MODULE_COUNT; ++module)
        if (fprintf(stream,
                    "%s_%s_module%zu_weight\t%.17g\n%s_%s_module%zu_contribution\t%.17g\n"
                    "%s_%s_module%zu_ablation_cross_entropy\t%.17g\n"
                    "%s_%s_module%zu_ablation_change\t%.17g\n",
                    phase, name, module, quality->module_weights[task][module], phase, name, module,
                    quality->module_contributions[task][module], phase, name, module,
                    quality->ablation_cross_entropy[task][module], phase, name, module,
                    quality->ablation_change[task][module]) < 0)
            return 0;
    return 1;
}

/** @brief Serialize all splits and decoder/composition checks for one task.
 * @param stream Borrowed writable stream.
 * @param phase Stable before/after spelling.
 * @param task Task index.
 * @param quality Borrowed complete report.
 * @return Nonzero on every formatted write. */
static int write_task(FILE *stream, const char *phase, size_t task,
                      const gameplay_quality *quality) {
    /* Step1: Preserve per-task quality rather than pooling task accuracies. */
    const gameplay_task_descriptor *descriptor = gameplay_task_get((uint32_t)task);
    for (size_t split = 0U; split < 3U; ++split)
        if (!write_split(stream, phase, descriptor->name,
                         gameplay_split_name((gameplay_split)split), &quality->split[task][split]))
            return 0;
    /* Step2: Host checks are exhaustive over every legal output mask, recent ID and module mask. */
    return fprintf(stream,
                   "%s_%s_host_checks\t%zu\n%s_%s_mask_violations\t%zu\n"
                   "%s_%s_repeat_violations\t%zu\n%s_%s_composition_cases\t%zu\n",
                   phase, descriptor->name, quality->host_checks[task], phase, descriptor->name,
                   quality->mask_violations[task], phase, descriptor->name,
                   quality->repeat_violations[task], phase, descriptor->name,
                   quality->composition_cases[task]) >= 0 &&
           write_modules(stream, phase, descriptor->name, task, quality);
}

/** @brief Serialize complete invariant-context and bounded transition outcomes.
 * @param stream Borrowed writable stream.
 * @param phase Stable before/after spelling.
 * @param quality Borrowed complete report.
 * @return Nonzero on every formatted write. */
static int write_simulator(FILE *stream, const char *phase, const gameplay_quality *quality) {
    /* Step1: Structural counts make incomplete scenario loops detectable. */
    return fprintf(stream,
                   "%s_module_violations\t%zu\n%s_bark_invariance_cases\t%zu\n"
                   "%s_bark_invariance_correct\t%zu\n%s_simulator_cases\t%zu\n"
                   "%s_simulator_legal\t%zu\n%s_simulator_survived\t%zu\n"
                   "%s_simulator_objective_successes\t%zu\n%s_simulator_stable\t%zu\n",
                   phase, quality->module_violations, phase, quality->bark_invariance_cases, phase,
                   quality->bark_invariance_correct, phase, quality->simulator_cases, phase,
                   quality->simulator_legal, phase, quality->simulator_survived, phase,
                   quality->simulator_objective_successes, phase, quality->simulator_stable) >= 0;
}

/** @brief Serialize all task domains and global independent outcome checks.
 * @param stream Borrowed writable stream.
 * @param phase Stable before/after spelling.
 * @param quality Borrowed complete report.
 * @return Nonzero on every formatted write. */
static int write_quality(FILE *stream, const char *phase, const gameplay_quality *quality) {
    /* Step1: No task may hide another task's failure in a combined average. */
    for (size_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task)
        if (!write_task(stream, phase, task, quality))
            return 0;
    return write_simulator(stream, phase, quality);
}

/** @brief Serialize complete owned resource payload and dense-work accounting.
 * @param stream Borrowed writable stream.
 * @param resources Borrowed inspected candidate resources.
 * @return Nonzero on complete formatted write. */
static int write_resources(FILE *stream, const cgai_gameplay_resources *resources) {
    /* Step1: Inference optimizer bytes must be zero and all routing layers are counted. */
    return fprintf(stream,
                   "model_bytes\t%zu\nsession_bytes\t%zu\nparameter_bytes\t%zu\n"
                   "parameter_count\t%zu\noptimizer_bytes\t%zu\nencoder_multiply_adds\t%llu\n"
                   "outer_coordinates\t%llu\ninner_coordinates\t%llu\nmaximum_head_logits\t%llu\n"
                   "maximum_head_multiply_adds\t%llu\n",
                   resources->model_bytes, resources->session_bytes, resources->parameter_bytes,
                   resources->parameter_count, resources->optimizer_bytes,
                   (unsigned long long)resources->encoder_multiply_adds,
                   (unsigned long long)resources->outer_coordinates,
                   (unsigned long long)resources->inner_coordinates,
                   (unsigned long long)resources->maximum_head_logits,
                   (unsigned long long)resources->maximum_head_multiply_adds) >= 0;
}

/** @brief Serialize one separately timed complete-query workload.
 * @param stream Borrowed writable stream.
 * @param name Stable task or paired workload name.
 * @param timing Borrowed complete monotonic timings.
 * @param passes Full task forwards performed by each measured sample.
 * @return Nonzero on complete formatted write. */
static int write_timing(FILE *stream, const char *name, const gameplay_timing *timing,
                        unsigned passes) {
    /* Step1: A paired sample contains two complete requests, not an assumed shared-encoder cache.
     */
    return fprintf(stream,
                   "%s_sample_count\t%zu\n%s_session_pool\t%zu\n%s_workers\t1\n"
                   "%s_forward_passes_per_sample\t%u\n%s_p50_us\t%.17g\n"
                   "%s_p95_us\t%.17g\n%s_p99_us\t%.17g\n%s_maximum_us\t%.17g\n",
                   name, timing->samples, name, timing->session_pool, name, name, passes, name,
                   timing->p50_us, name, timing->p95_us, name, timing->p99_us, name,
                   timing->maximum_us) >= 0;
}

/** @brief Serialize the exact profile limits and bounded synthetic scope.
 * @param stream Borrowed writable stream.
 * @param hardware Borrowed validated one-line machine label.
 * @param passed Complete native promotion decision.
 * @return Nonzero on every formatted write. */
static int write_policy(FILE *stream, const char *hardware, int passed) {
    /* Step1: These hardware-specific timings do not imply a universal frame deadline. */
    return fprintf(
               stream,
               "version\t1\ncontract_version\t1\nscope\tbounded-synthetic-gameplay-v1\n"
               "hardware\t%s\ncompiler\t%s\npromotion_passed\t%d\n"
               "task_count\t2\nmodule_count\t2\nmodel_limit_bytes\t262144\n"
               "session_limit_bytes\t65536\np95_limit_us\t500\np99_limit_us\t1000\n"
               "paired_p95_limit_us\t1000\npaired_p99_limit_us\t2000\n"
               "minimum_development_improvement\t0.000001\ntest_regression_tolerance\t0.000000001\n"
               "minimum_module_share\t0.05\nminimum_ablation_change\t0.000001\n"
               "minimum_bark_accuracy\t0.95\nminimum_intent_accuracy\t0.95\n",
               hardware, compiler_name(), passed) >= 0;
}

/** @brief Save every complete native measurement and close on all paths.
 * @param path New compact report path.
 * @param hardware Borrowed validated hardware label.
 * @param before Borrowed complete incumbent quality.
 * @param after Borrowed complete candidate quality.
 * @param performance Borrowed complete candidate workloads.
 * @return OK after complete write/close, ERROR otherwise. */
static cgai_status save_report(const char *path, const char *hardware,
                               const gameplay_quality *before, const gameplay_quality *after,
                               const gameplay_performance *performance) {
    /* Step1: Open only after every quality and timing workload succeeded. */
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
        return cgai_fail("cannot open composed gameplay report");
    const int written =
        write_policy(stream, hardware, gameplay_tool_gate(before, after, performance)) &&
        write_quality(stream, "before", before) && write_quality(stream, "after", after) &&
        write_resources(stream, &after->resources) &&
        write_timing(stream, "bark", &performance->task[0], 1U) &&
        write_timing(stream, "intent", &performance->task[1], 1U) &&
        write_timing(stream, "paired", &performance->paired, 2U);
    /* Step2: I/O failure never produces a valid promotion report. */
    const int closed = fclose(stream) == 0;
    return written && closed ? CGAI_STATUS_OK : cgai_fail("cannot save complete gameplay report");
}

/** @brief Measure complete candidate/incumbent quality and current-machine performance.
 * @param candidate Candidate inference weights.
 * @param incumbent Incumbent inference weights.
 * @param report New compact report destination.
 * @param hardware Bounded one-line machine label.
 * @return OK after complete report write, ERROR otherwise; failed gates are recorded. */
cgai_status gameplay_tool_report(const char *candidate, const char *incumbent, const char *report,
                                 const char *hardware) {
    /* Step1: Prevent ambiguous metadata rows before loading either bundle. */
    if (report == NULL || hardware == NULL || hardware[0] == '\0' || strlen(hardware) > 512U ||
        strpbrk(hardware, "\t\r\n") != NULL)
        return cgai_fail("gameplay report requires a bounded one-line hardware label");
    cgai_gameplay_model *before_model = cgai_gameplay_load(incumbent);
    cgai_gameplay_model *after_model = cgai_gameplay_load(candidate);
    gameplay_quality before = {0}, after = {0};
    gameplay_performance performance = {0};
    /* Step2: Timing and outcome checks use separate preallocated exclusive sessions. */
    cgai_status status = before_model != NULL && after_model != NULL &&
                                 gameplay_tool_quality(before_model, &before) &&
                                 gameplay_tool_quality(after_model, &after) &&
                                 gameplay_tool_benchmark(after_model, &performance)
                             ? CGAI_STATUS_OK
                             : CGAI_STATUS_ERROR;
    if (status)
        status = save_report(report, hardware, &before, &after, &performance);
    cgai_gameplay_destroy(before_model);
    cgai_gameplay_destroy(after_model);
    return status;
}

/** @brief Score an accepted bundle's complete frozen nonperformance quality.
 * @param path Trusted inference artifact path.
 * @return OK for passing absolute task/simulator/composition/resource gates, ERROR otherwise. */
cgai_status gameplay_tool_score(const char *path) {
    /* Step1: Score every task and independent transition before printing aggregate evidence. */
    cgai_gameplay_model *model = cgai_gameplay_load(path);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    gameplay_quality quality = {0};
    cgai_status status = gameplay_tool_quality(model, &quality);
    cgai_gameplay_destroy(model);
    /* Step2: This operation never modifies any model or accepted pointer. */
    if (status && !write_quality(stdout, "after", &quality))
        status = cgai_fail("cannot print composed quality report");
    if (status && !gameplay_tool_absolute_gate(&quality))
        status = cgai_fail("composed model fails frozen task, simulator or mixing gates");
    return status;
}
