/** @file gameplay_evaluation.h @brief Frozen composed-task quality and performance gates. */
#ifndef CGAI_GAMEPLAY_EVALUATION_H
#define CGAI_GAMEPLAY_EVALUATION_H
#include "gameplay_tasks.h"
/** Inference bundle requested heap ceiling. */
#define GAMEPLAY_MODEL_LIMIT 262144U
/** Reusable single-session requested heap ceiling. */
#define GAMEPLAY_SESSION_LIMIT 65536U
/** Complete measured single-head or paired-query samples. */
#define GAMEPLAY_BENCHMARK_SAMPLES 2048U
/** Sessions served round-robin by one worker. */
#define GAMEPLAY_SESSION_POOL 4U
/** Minimum per-task development loss improvement. */
#define GAMEPLAY_MINIMUM_IMPROVEMENT 0.000001
/** Maximum tolerated per-task held-out loss increase. */
#define GAMEPLAY_REGRESSION_TOLERANCE 0.000000001
/** Minimum average routing probability and selected-ID posterior for each module. */
#define GAMEPLAY_MINIMUM_MODULE_SHARE 0.05
/** Minimum mean absolute per-case ablation loss change for each module. */
#define GAMEPLAY_MINIMUM_ABLATION_CHANGE 0.000001
/** Complete bark invariance checks across all frozen states and five-bit contexts. */
#define GAMEPLAY_BARK_INVARIANCE_CASES 2304U
/** Intent simulation cases, including setting variants from all frozen splits. */
#define GAMEPLAY_SIMULATOR_CASES 256U
/** Independent scenario metrics for one task's split. */
typedef struct gameplay_split_metrics {
    size_t cases;         /**< Independent requests. */
    size_t correct;       /**< Exact task teacher agreement. */
    size_t abstained;     /**< Fallback proposals. */
    double cross_entropy; /**< Mean task-local target negative log likelihood. */
} gameplay_split_metrics;
/** Complete quality, host constraints, composition and bounded simulation outcomes. */
typedef struct gameplay_quality {
    gameplay_split_metrics split[GAMEPLAY_TASK_COUNT][3]; /**< Task-local frozen means. */
    size_t host_checks[GAMEPLAY_TASK_COUNT]; /**< Complete unrestricted and mask-stress checks. */
    size_t mask_violations[GAMEPLAY_TASK_COUNT];   /**< Invalid or forbidden output selections. */
    size_t repeat_violations[GAMEPLAY_TASK_COUNT]; /**< Suppressed nonzero output selections. */
    size_t module_violations; /**< Invalid routing weights or forbidden module evaluation. */
    size_t composition_cases[GAMEPLAY_TASK_COUNT]; /**< Unrestricted cases scored for mixing. */
    double module_weights[GAMEPLAY_TASK_COUNT][GAMEPLAY_MODULE_COUNT]; /**< Mean outer weights. */
    double module_contributions[GAMEPLAY_TASK_COUNT]
                               [GAMEPLAY_MODULE_COUNT]; /**< Posterior means. */
    double ablation_cross_entropy[GAMEPLAY_TASK_COUNT]
                                 [GAMEPLAY_MODULE_COUNT]; /**< Module-only NLL. */
    double ablation_change[GAMEPLAY_TASK_COUNT]
                          [GAMEPLAY_MODULE_COUNT]; /**< Mean absolute NLL change. */
    size_t bark_invariance_cases;                  /**< Exhaustive irrelevant-context checks. */
    size_t bark_invariance_correct;                /**< Exact invariant bark teacher outputs. */
    size_t simulator_cases;                        /**< Independently simulated intent proposals. */
    size_t simulator_legal;                        /**< Actions satisfy simulation preconditions. */
    size_t simulator_survived; /**< Agents survive bounded authored hazard transitions. */
    size_t simulator_objective_successes; /**< Safe objective outcomes. */
    size_t simulator_stable; /**< Same proposal under setting change and identical-state replay. */
    cgai_gameplay_resources resources; /**< Requested immutable weights and scratch heap. */
} gameplay_quality;
/** One exact measured workload's nearest-rank latency summaries. */
typedef struct gameplay_timing {
    size_t samples;      /**< Successful measured complete queries. */
    size_t session_pool; /**< Round-robin session count; one worker. */
    double p50_us;       /**< Median microseconds. */
    double p95_us;       /**< 95th percentile microseconds. */
    double p99_us;       /**< 99th percentile microseconds. */
    double maximum_us;   /**< Largest observed duration, not a deadline guarantee. */
} gameplay_timing;
/** Single-head tasks and two sequential head requests, separately timed. */
typedef struct gameplay_performance {
    gameplay_timing task[GAMEPLAY_TASK_COUNT]; /**< Bark and intent request timings. */
    gameplay_timing paired; /**< Two complete selects on the same shared observation. */
} gameplay_performance;
/** @brief Score all independent tasks, host permissions, invariance and simulator transitions.
 * @param model Borrowed immutable composed bundle.
 * @param quality Writable complete result, unchanged on error.
 * @return OK on complete publication, ERROR otherwise. */
cgai_status gameplay_tool_quality(const cgai_gameplay_model *model, gameplay_quality *quality);
/** @brief Measure preallocated typed single-head and paired workloads on this hardware.
 * @param model Borrowed immutable composed bundle.
 * @param performance Writable complete result, unchanged on failure.
 * @return OK after all three complete workloads, ERROR otherwise. */
cgai_status gameplay_tool_benchmark(const cgai_gameplay_model *model,
                                    gameplay_performance *performance);
/** @brief Apply absolute task, composition, simulator and resource requirements.
 * @param quality Borrowed complete candidate quality.
 * @return Nonzero exactly for a valid candidate meeting all nontiming absolute gates. */
int gameplay_tool_absolute_gate(const gameplay_quality *quality);
/** @brief Apply every task's quality nonregression and improvement plus measured latency caps.
 * @param before Borrowed complete incumbent report.
 * @param after Borrowed complete candidate report.
 * @param performance Borrowed candidate measured timings.
 * @return Nonzero exactly when all frozen requirements pass. */
int gameplay_tool_gate(const gameplay_quality *before, const gameplay_quality *after,
                       const gameplay_performance *performance);
#endif
