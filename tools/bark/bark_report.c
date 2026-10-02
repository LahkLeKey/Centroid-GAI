/** @file bark_report.c @brief Compact bounded-task quality and hardware latency reports. */
#include "bark_tool.h"
#include "internal/error.h"
#include <stdio.h>
#include <string.h>

/** Expand one compiler macro before stringifying its exact version. */
#define BARK_STRINGIFY_VALUE(value) BARK_STRINGIFY_LITERAL(value)
/** Convert one expanded compiler version token to static text. */
#define BARK_STRINGIFY_LITERAL(value) #value

/** @brief Serialize one split with no per-request text or raw trace data.
 * @param stream Borrowed writable report stream.
 * @param prefix Stable before/after split name.
 * @param metrics Borrowed independent request metrics.
 * @return Nonzero on successful formatted write. */
static int write_split(FILE *stream, const char *prefix, const bark_split_metrics *metrics) {
    /* Step 1: Preserve enough precision to compare the frozen target loss. */
    return fprintf(stream,
                   "%s_cases\t%zu\n%s_correct\t%zu\n%s_abstained\t%zu\n"
                   "%s_cross_entropy\t%.17g\n",
                   prefix, metrics->cases, prefix, metrics->correct, prefix, metrics->abstained,
                   prefix, metrics->cross_entropy) >= 0;
}

/** @brief Serialize all three frozen split totals.
 * @param stream Borrowed writable stream.
 * @param before Borrowed incumbent quality.
 * @param after Borrowed candidate quality.
 * @return Nonzero on all successful writes. */
static int write_quality(FILE *stream, const bark_quality *before, const bark_quality *after) {
    /* Step 1: Keep every held-out split separate from training measurements. */
    const char *before_names[] = {"before_training", "before_development", "before_test"};
    const char *after_names[] = {"after_training", "after_development", "after_test"};
    for (size_t index = 0U; index < 3U; ++index)
        if (!write_split(stream, before_names[index], &before->split[index]) ||
            !write_split(stream, after_names[index], &after->split[index]))
            return 0;
    /* Step 2: Decoder checks exercise all legal-mask/recent-ID combinations. */
    return fprintf(stream, "mask_violations\t%zu\nrepeat_violations\t%zu\n", after->mask_violations,
                   after->repeat_violations) >= 0;
}

/** @brief Serialize requested heap and measured complete-selection latency.
 * @param stream Borrowed writable stream.
 * @param quality Borrowed candidate resources.
 * @param performance Borrowed measured workload.
 * @return Nonzero on successful formatted write. */
static int write_performance(FILE *stream, const bark_quality *quality,
                             const bark_performance *performance) {
    /* Step 1: Include resident optimizer bytes so accidental checkpoint deployment is visible. */
    return fprintf(
               stream,
               "model_bytes\t%zu\nsession_bytes\t%zu\nparameter_bytes\t%zu\n"
               "parameter_count\t%zu\nvocabulary_size\t%zu\noptimizer_bytes\t%zu\n"
               "sample_count\t%zu\nsession_pool\t%zu\nworkers\t1\n"
               "p50_us\t%.17g\np95_us\t%.17g\np99_us\t%.17g\nmaximum_us\t%.17g\n",
               quality->resources.model_bytes, quality->resources.session_bytes,
               quality->resources.neural.parameter_bytes, quality->resources.neural.parameter_count,
               quality->resources.neural.vocabulary_size, quality->resources.neural.optimizer_bytes,
               performance->samples, performance->session_pool, performance->p50_us,
               performance->p95_us, performance->p99_us, performance->maximum_us) >= 0;
}

/** @brief Identify the compiler family and version of the measured C11 executable.
 * @return Borrowed static version string. */
static const char *report_compiler(void) {
    /* Step 1: Prefer Clang's identity when its compatibility macros also name another compiler. */
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(_MSC_VER)
    return "msvc " BARK_STRINGIFY_VALUE(_MSC_FULL_VER) " C11";
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#else
    return "C11 compiler";
#endif
}

/** @brief Write the exact profile limits and bounded scope alongside the gate decision.
 * @param stream Borrowed writable stream.
 * @param hardware Borrowed validated machine description.
 * @param passed Promotion gate decision.
 * @return Nonzero on complete formatted write. */
static int write_policy(FILE *stream, const char *hardware, int passed) {
    /* Step 1: A hardware-specific timing result does not claim a frame deadline. */
    return fprintf(stream,
                   "version\t1\ncontract_version\t1\nscope\tbounded-synthetic-bark-v1\n"
                   "hardware\t%s\ncompiler\t%s\npromotion_passed\t%d\n"
                   "model_limit_bytes\t262144\nsession_limit_bytes\t65536\n"
                   "p95_limit_us\t500\np99_limit_us\t1000\nminimum_accuracy\t0.95\n"
                   "minimum_development_improvement\t0.000001\n"
                   "test_regression_tolerance\t0.000000001\n",
                   hardware, report_compiler(), passed) >= 0;
}

/** @brief Save compact quality/performance fields and close on every path.
 * @param path New TSV destination.
 * @param hardware Borrowed one-line machine description.
 * @param before Borrowed incumbent measurements.
 * @param after Borrowed candidate measurements.
 * @param performance Borrowed complete-selection timings.
 * @return OK after complete write/close, ERROR otherwise. */
static cgai_status save_report(const char *path, const char *hardware, const bark_quality *before,
                               const bark_quality *after, const bark_performance *performance) {
    /* Step 1: Open only after all measurement operations succeeded. */
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
        return cgai_fail("cannot open bark report destination");
    const int passed = bark_tool_gate(before, after, performance);
    const int written = write_policy(stream, hardware, passed) &&
                        write_quality(stream, before, after) &&
                        write_performance(stream, after, performance);
    /* Step 2: Close even when a formatted write fails. */
    const int closed = fclose(stream) == 0;
    return written && closed ? CGAI_STATUS_OK : cgai_fail("cannot write complete bark report");
}

/** @brief Measure a candidate against the incumbent before publishing a compact report.
 * @param candidate Borrowed candidate inference path.
 * @param incumbent Borrowed incumbent inference path.
 * @param report New report destination.
 * @param hardware Borrowed nonempty one-line hardware label, at most 512 bytes.
 * @return OK after complete report save, ERROR otherwise; decision is recorded in the report. */
cgai_status bark_tool_report(const char *candidate, const char *incumbent, const char *report,
                             const char *hardware) {
    /* Step 1: Reject labels that could introduce ambiguous metadata rows. */
    if (report == NULL || hardware == NULL || hardware[0] == '\0' || strlen(hardware) > 512U ||
        strpbrk(hardware, "\t\r\n") != NULL)
        return cgai_fail("bark report requires a bounded one-line hardware description");
    cgai_bark_model *after_model = cgai_bark_load(candidate);
    cgai_bark_model *before_model = cgai_bark_load(incumbent);
    bark_quality before = {0}, after = {0};
    bark_performance performance = {0};
    /* Step 2: Quality scoring and timing use independent reusable scratch. */
    cgai_status status = after_model != NULL && before_model != NULL &&
                                 bark_tool_quality(before_model, &before) &&
                                 bark_tool_quality(after_model, &after) &&
                                 bark_tool_benchmark(after_model, &performance)
                             ? CGAI_STATUS_OK
                             : CGAI_STATUS_ERROR;
    if (status)
        status = save_report(report, hardware, &before, &after, &performance);
    cgai_bark_destroy(before_model);
    cgai_bark_destroy(after_model);
    return status;
}

/** @brief Check absolute frozen scenario quality for an existing model.
 * @param quality Borrowed complete measurements.
 * @return Nonzero when held-out accuracy, decoder checks and heap fit the contract. */
static int absolute_quality(const bark_quality *quality) {
    /* Step 1: All twelve requests in each held-out split must satisfy the 95-percent floor. */
    return quality->split[BARK_FIXTURE_DEVELOPMENT].correct == 12U &&
           quality->split[BARK_FIXTURE_TEST].correct == 12U && quality->mask_violations == 0U &&
           quality->repeat_violations == 0U && quality->resources.model_bytes <= BARK_MODEL_LIMIT &&
           quality->resources.session_bytes <= BARK_SESSION_LIMIT;
}

/** @brief Print frozen scenario quality for an existing published inference model.
 * @param path Borrowed trusted inference artifact path.
 * @return OK on passing absolute quality/resource gates, ERROR otherwise. */
cgai_status bark_tool_score(const char *path) {
    /* Step 1: Load weights and exercise every independent frozen request. */
    cgai_bark_model *model = cgai_bark_load(path);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    bark_quality quality = {0};
    cgai_status status = bark_tool_quality(model, &quality);
    cgai_bark_destroy(model);
    /* Step 2: Print aggregate metrics only; verification does not mutate the release. */
    if (status && (!write_split(stdout, "training", &quality.split[0]) ||
                   !write_split(stdout, "development", &quality.split[1]) ||
                   !write_split(stdout, "test", &quality.split[2])))
        status = cgai_fail("cannot print bark quality metrics");
    if (status && !absolute_quality(&quality))
        status = cgai_fail("bark model fails frozen scenario or resource gates");
    return status;
}
