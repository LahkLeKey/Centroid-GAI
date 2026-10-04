/** @file npc_evaluation.h @brief Independent closed-loop episode and composition measurements. */
#ifndef CGAI_NPC_EVALUATION_H
#define CGAI_NPC_EVALUATION_H
#include "npc_policy.h"
/** Independently evaluated controller count. */
#define NPC_ACTORS 7U
/** Complete benchmark call count per workload. */
#define NPC_BENCHMARK_SAMPLES 2048U
/** Untimed complete policy calls before each workload. */
#define NPC_BENCHMARK_WARMUP 256U
/** Complete outcomes of independent episodes, never individual-step trials. */
typedef struct npc_episode_metrics {
    size_t count;            /**< All attempted episodes, including failures. */
    size_t success;          /**< Authoritatively completed goals. */
    size_t survived;         /**< Episodes without death, including timeouts. */
    size_t deaths;           /**< Authoritative death terminals. */
    size_t timeouts;         /**< Authoritative budget exhaustion terminals. */
    size_t cost;             /**< Total consumed host decisions. */
    size_t blocked;          /**< Observed obstruction outcomes. */
    size_t recoveries;       /**< Subsequent successful moves after an observed obstruction. */
    size_t illegal_attempts; /**< Host-rejected proposals. */
    size_t illegal_executed; /**< Authoritative execution invariant violations. */
    size_t fallbacks;        /**< Safe-wait fallback proposals, each consuming a decision. */
} npc_episode_metrics;
/** Deterministic comparative outcome and fixed-observation neural diagnostics. */
typedef struct npc_evaluation {
    npc_episode_metrics family[NPC_FAMILIES_PER_SPLIT][NPC_ACTORS]; /**< Paired family blocks. */
    npc_episode_metrics total[NPC_ACTORS];       /**< Complete equal-sized family outcomes. */
    npc_episode_metrics mechanic[3][NPC_ACTORS]; /**< Principal mechanic outcome strata. */
    size_t composition_count;       /**< Pinned observed teacher states, both modules eligible. */
    double mean_loss;               /**< Full-mixture target negative log likelihood. */
    double module_weights[2];       /**< Mean learned outer routing shares. */
    double module_contributions[2]; /**< Mean selected-output posterior shares. */
    double module_loss[2];          /**< Module-only fixed-observation target losses. */
    double module_loss_change[2];   /**< Mean absolute per-observation restriction changes. */
} npc_evaluation;
/** Measured nearest-rank complete adapter-path timing. */
typedef struct npc_timing {
    size_t samples;    /**< Measured complete calls or32-call serial scheduling batches. */
    size_t sessions;   /**< Preallocated session count on one worker. */
    double p50_us;     /**< Median microseconds. */
    double p95_us;     /**< 95th percentile microseconds. */
    double p99_us;     /**< 99th percentile microseconds. */
    double maximum_us; /**< Maximum observed duration. */
} npc_timing;

/** @brief Run every controller in its own independently caused closed-loop history.
 * @param model Borrowed trained compatible immutable model.
 * @param memoryless Borrowed separately trained compatible history-disabled model.
 * @param split Development or final audit; training is rejected.
 * @param evaluation Writable complete report, unchanged on error.
 * @return OK after all episodes and composition samples, ERROR otherwise. */
cgai_status npc_evaluate(const cgai_gameplay_model *model, const cgai_gameplay_model *memoryless,
                         npc_split split, npc_evaluation *evaluation);
/** @brief Write compact complete-episode outcomes and pinned neural diagnostics.
 * @param path Trusted report destination.
 * @param split Development or final audit.
 * @param evaluation Borrowed complete measured report.
 * @return OK on complete write and close, ERROR otherwise. */
cgai_status npc_evaluation_write(const char *path, npc_split split,
                                 const npc_evaluation *evaluation);
/** @brief Load two compatible inference artifacts and evaluate an isolated split.
 * @param model_path Trained inference artifact.
 * @param memoryless_path Separately trained history-disabled inference artifact.
 * @param report_path New compact report destination.
 * @param split Development or final audit.
 * @return OK on complete measurement and publication, ERROR otherwise. */
cgai_status npc_evaluation_files(const char *model_path, const char *memoryless_path,
                                 const char *report_path, npc_split split);
/** @brief Write offline shape and complete requested-heap verification without corpus access.
 * @param model_path Inference artifact bound by the release wrapper.
 * @param report_path New compact verification destination.
 * @return OK only for compatible inference-only weights within both resource budgets. */
cgai_status npc_verify_file(const char *model_path, const char *report_path);
/** @brief Measure full adapter paths and32-NPC serial scheduling with preallocated sessions.
 * @param model_path Compatible inference-only artifact.
 * @param report_path New measured benchmark destination.
 * @param hardware Recorded reference-machine label.
 * @return OK on complete measured report, ERROR otherwise; failed timings are never estimates. */
cgai_status npc_benchmark_file(const char *model_path, const char *report_path,
                               const char *hardware);
#endif
