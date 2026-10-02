/** @file bark_tool.h @brief Native bark training, scenario quality and latency reporting. */
#ifndef CGAI_BARK_TOOL_H
#define CGAI_BARK_TOOL_H
#include "bark_fixture.h"

/** Maximum requested resident model heap in the version-one profile. */
#define BARK_MODEL_LIMIT 262144U
/** Maximum requested heap for one selection session. */
#define BARK_SESSION_LIMIT 65536U
/** Timed complete selections after warm-up. */
#define BARK_BENCHMARK_SAMPLES 2048U
/** Active sessions served round-robin by one worker; not parallel worker concurrency. */
#define BARK_SESSION_POOL 4U
/** Minimum development loss improvement required for automatic promotion. */
#define BARK_MINIMUM_IMPROVEMENT 0.000001
/** Frozen held-out loss regression tolerance. */
#define BARK_REGRESSION_TOLERANCE 0.000000001

/** Accumulated independent request metrics for one frozen split. */
typedef struct bark_split_metrics {
    size_t cases;         /**< Scored independent requests. */
    size_t correct;       /**< Exact authored teacher IDs, including abstention. */
    size_t abstained;     /**< Returned silence IDs. */
    double cross_entropy; /**< Mean target negative log likelihood over full vocabulary. */
} bark_split_metrics;
/** Frozen scenario quality plus bounded-host decoding checks. */
typedef struct bark_quality {
    bark_split_metrics split[3];   /**< Training, development and test metrics. */
    size_t mask_violations;        /**< Disallowed or invalid catalog selections. */
    size_t repeat_violations;      /**< Repeated suppressed line selections. */
    cgai_bark_resources resources; /**< Requested immutable model and session heap. */
} bark_quality;
/** Monotonic end-to-end selection timings on the current machine/build. */
typedef struct bark_performance {
    size_t samples;      /**< Successful measured selections. */
    size_t session_pool; /**< Active round-robin sessions on one worker. */
    double p50_us;       /**< Nearest-rank median microseconds. */
    double p95_us;       /**< Nearest-rank 95th percentile microseconds. */
    double p99_us;       /**< Nearest-rank 99th percentile microseconds. */
    double maximum_us;   /**< Largest observed duration, not a worst-case guarantee. */
} bark_performance;

/** @brief Initialize the fixed small architecture using only training vocabulary.
 * @param path New checkpoint destination; existing contents are replaced.
 * @return OK on complete checkpoint write, ERROR otherwise. */
cgai_status bark_tool_init(const char *path);
/** @brief Continue independent supervised labels with preserved Adam and shuffle state.
 * @param input Borrowed checkpoint path.
 * @param output New checkpoint destination, different from input.
 * @param epochs Additional complete passes, 1..10000.
 * @param rate Finite positive Adam learning rate, at most one.
 * @return OK after complete training/save, ERROR otherwise. */
cgai_status bark_tool_step(const char *input, const char *output, size_t epochs, double rate);
/** @brief Recreate a checkpoint from its shape/seed and complete independent training recipe.
 * @param input Reference checkpoint whose counters determine the epoch count.
 * @param output New replay checkpoint destination.
 * @param rate Original Adam learning rate.
 * @return OK after replay save, ERROR otherwise; caller compares exact file bytes. */
cgai_status bark_tool_replay(const char *input, const char *output, double rate);
/** @brief Export weights only for the gameplay runtime.
 * @param input Borrowed continuation checkpoint.
 * @param output Inference artifact destination.
 * @return OK on complete export, ERROR otherwise. */
cgai_status bark_tool_export(const char *input, const char *output);
/** @brief Score frozen scenarios and host masks using reusable allocation-free inference.
 * @param model Borrowed immutable specialist.
 * @param quality Writable complete report, unchanged on error.
 * @return OK after publication, ERROR otherwise. */
cgai_status bark_tool_quality(const cgai_bark_model *model, bark_quality *quality);
/** @brief Measure one-worker round-robin selection latency with preallocated sessions.
 * @param model Borrowed immutable specialist.
 * @param performance Writable complete report, unchanged on error.
 * @return OK after complete timed workload, ERROR otherwise. */
cgai_status bark_tool_benchmark(const cgai_bark_model *model, bark_performance *performance);
/** @brief Check quality, nonregression, requested heap and measured latency together.
 * @param before Borrowed incumbent measurements on the same frozen scenarios.
 * @param after Borrowed candidate measurements.
 * @param performance Borrowed candidate timings on the named hardware/build.
 * @return Nonzero exactly when all promotion requirements pass. */
int bark_tool_gate(const bark_quality *before, const bark_quality *after,
                   const bark_performance *performance);
/** @brief Write compact candidate/incumbent quality and performance without raw traces.
 * @param candidate Borrowed weights-only candidate path.
 * @param incumbent Borrowed weights-only comparison path.
 * @param report New TSV report destination.
 * @param hardware Borrowed nonempty one-line hardware description, without tabs.
 * @return OK after complete report write, ERROR on preparation or I/O; gate outcome is in TSV. */
cgai_status bark_tool_report(const char *candidate, const char *incumbent, const char *report,
                             const char *hardware);
/** @brief Print frozen scenario quality for an existing published inference model.
 * @param path Borrowed trusted model path.
 * @return OK on passing absolute quality/resource gates, ERROR otherwise. */
cgai_status bark_tool_score(const char *path);
#endif
