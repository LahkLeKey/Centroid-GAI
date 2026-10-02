/** @file gameplay_evaluation.c @brief Per-task quality, neural composition and transition gates. */
#include "gameplay_evaluation.h"
#include "internal/error.h"
#include <math.h>

/** @brief Count the complete output-mask/recent/module-mask stress domain and base cases.
 * @param task Supported task descriptor.
 * @return Fixed complete host-check count. */
static size_t host_case_count(const gameplay_task_descriptor *task) {
    /* Step 1: Both module masks, including empty eligibility, cover the entire two-bit domain. */
    return ((size_t)1U << task->output_count) * task->output_count * 4U + task->fixture_count;
}

/** @brief Count admitted module bits in the version-one two-module profile.
 * @param mask Validated two-bit eligibility.
 * @return Active module count. */
static size_t active_modules(uint64_t mask) {
    /* Step 1: Explicit bounded bits avoid platform-specific population-count extensions. */
    return (size_t)(mask & 1U) + (size_t)((mask >> 1U) & 1U);
}

/** @brief Verify one published proposal's output and repetition permissions.
 * @param request Complete validated query.
 * @param result Published selection.
 * @param quality Private accumulated report. */
static void check_output(const cgai_gameplay_request *request, const cgai_gameplay_result *result,
                         gameplay_quality *quality) {
    /* Step 1: Fallback remains legal under every host mask; other IDs require an admitted bit. */
    const gameplay_task_descriptor *task = gameplay_task_get(request->task);
    ++quality->host_checks[request->task];
    if (result->output >= task->output_count ||
        (result->output != 0U &&
         (request->allowed_outputs & (UINT64_C(1) << result->output)) == 0U) ||
        result->abstained != (result->output == 0U))
        ++quality->mask_violations[request->task];
    /* Step 2: The recent nonzero output is never a legal repeated proposal. */
    if (request->recent_output != 0U && result->output == request->recent_output)
        ++quality->repeat_violations[request->task];
}

/** @brief Validate every reported routing scalar before normalization checks.
 * @param request Complete valid query.
 * @param result Published complete forward diagnostics.
 * @return Nonzero only for finite bounded eligible-module diagnostics. */
static int valid_module_scalars(const cgai_gameplay_request *request,
                                const cgai_gameplay_result *result) {
    /* Step 1: Excluded and unused module slots must have zero diagnostics. */
    if (!isfinite(result->probability) || result->probability < 0.0 || result->probability > 1.0)
        return 0;
    for (size_t i = 0U; i < CGAI_GAMEPLAY_MAX_MODULES; ++i) {
        const double weight = result->module_weights[i];
        const double share = result->module_contributions[i];
        if (!isfinite(weight) || !isfinite(share) || weight < 0.0 || weight > 1.0 || share < 0.0 ||
            share > 1.0)
            return 0;
        if ((request->allowed_modules & (UINT64_C(1) << i)) == 0U &&
            (weight != 0.0 || share != 0.0))
            return 0;
    }
    return 1;
}

/** @brief Verify module exclusions, complete work and normalized neural mixtures.
 * @param request Complete valid query.
 * @param result Published selection.
 * @param quality Private accumulated violation counter. */
static void check_modules(const cgai_gameplay_request *request, const cgai_gameplay_result *result,
                          gameplay_quality *quality) {
    /* Step 1: A forced fallback performs no work; normal queries evaluate every admitted expert. */
    if (!valid_module_scalars(request, result)) {
        ++quality->module_violations;
        return;
    }
    if (result->forward_passes == 0U) {
        if (result->output != 0U || result->active_modules != 0U ||
            result->active_centroids != 0U || result->probability != 1.0 ||
            result->module_weights[0] != 0.0 || result->module_weights[1] != 0.0 ||
            result->module_contributions[0] != 0.0 || result->module_contributions[1] != 0.0)
            ++quality->module_violations;
        return;
    }
    /* Step 2: Dense routing must normalize both probabilities and selected-output posteriors. */
    const double weights = result->module_weights[0] + result->module_weights[1];
    const double shares = result->module_contributions[0] + result->module_contributions[1];
    if (result->forward_passes != 1U ||
        result->active_modules != active_modules(request->allowed_modules) ||
        result->active_centroids != result->active_modules * 16U || fabs(weights - 1.0) > 1e-9 ||
        fabs(shares - 1.0) > 1e-9)
        ++quality->module_violations;
}

/** @brief Accumulate independent module-only target losses against the full mixture.
 * @param session Exclusive live scratch.
 * @param example Complete independent task target.
 * @param full_loss Valid finite full-mixture target loss.
 * @param quality Private accumulated diagnostics.
 * @return OK after both module-only evaluations, ERROR otherwise. */
static cgai_status score_ablations(cgai_gameplay_session *session,
                                   const cgai_gameplay_example *example, double full_loss,
                                   gameplay_quality *quality) {
    /* Step 1: A module-only ablation retains the same aligned shared encoder and task target. */
    for (size_t module = 0U; module < GAMEPLAY_MODULE_COUNT; ++module) {
        double loss = 0.0;
        if (!cgai_gameplay_evaluate_modules(session, example, UINT64_C(1) << module, &loss))
            return CGAI_STATUS_ERROR;
        if (!isfinite(loss) || loss < 0.0)
            return cgai_fail("invalid composed gameplay ablation loss");
        quality->ablation_cross_entropy[example->task][module] += loss;
        quality->ablation_change[example->task][module] += fabs(loss - full_loss);
    }
    return CGAI_STATUS_OK;
}

/** @brief Accumulate both specialists' contribution within one requested output domain.
 * @param request Complete unrestricted task query.
 * @param selected Published full forward diagnostics.
 * @param quality Private accumulated quality. */
static void score_composition(const cgai_gameplay_request *request,
                              const cgai_gameplay_result *selected, gameplay_quality *quality) {
    /* Step 1: Retain routing and selected-ID posterior before scratch is reused. */
    ++quality->composition_cases[request->task];
    for (size_t module = 0U; module < GAMEPLAY_MODULE_COUNT; ++module) {
        quality->module_weights[request->task][module] += selected->module_weights[module];
        quality->module_contributions[request->task][module] +=
            selected->module_contributions[module];
    }
}

/** @brief Score one full independent task request and its neural composition.
 * @param session Exclusive live scratch.
 * @param scenario Complete frozen task target.
 * @param quality Private accumulated quality report.
 * @return OK after complete score and ablations, ERROR otherwise. */
static cgai_status score_scenario(cgai_gameplay_session *session,
                                  const gameplay_fixture_case *scenario,
                                  gameplay_quality *quality) {
    /* Step 1: Score exact teacher agreement and stable unconditional task-local target loss. */
    cgai_gameplay_request request;
    cgai_gameplay_result selected = {0};
    double loss = 0.0;
    if (!gameplay_fixture_request(scenario, &request) ||
        !cgai_gameplay_select(session, &request, &selected) ||
        !cgai_gameplay_evaluate(session, &scenario->example, &loss))
        return CGAI_STATUS_ERROR;
    gameplay_split_metrics *split = &quality->split[scenario->example.task][scenario->split];
    ++split->cases;
    split->correct += selected.output == scenario->example.target ? 1U : 0U;
    split->abstained += selected.output == 0U ? 1U : 0U;
    split->cross_entropy += loss;
    check_output(&request, &selected, quality);
    check_modules(&request, &selected, quality);
    /* Step 2: Retain per-head routing and contribution means before scratch is reused. */
    score_composition(&request, &selected, quality);
    return score_ablations(session, &scenario->example, loss, quality);
}

/** @brief Execute one host repetition and module availability combination.
 * @param session Exclusive live scratch.
 * @param request Caller-owned complete query.
 * @param recent Current recent output.
 * @param modules Current eligible-module mask.
 * @param quality Private accumulated constraint report.
 * @return OK after one complete selection or ERROR. */
static cgai_status stress_query(cgai_gameplay_session *session, cgai_gameplay_request *request,
                                uint32_t recent, uint64_t modules, gameplay_quality *quality) {
    /* Step 1: Host repetition and module availability are independent permissions. */
    request->allowed_modules = modules;
    request->recent_output = recent;
    cgai_gameplay_result selected = {0};
    if (!cgai_gameplay_select(session, request, &selected))
        return CGAI_STATUS_ERROR;
    check_output(request, &selected, quality);
    check_modules(request, &selected, quality);
    return CGAI_STATUS_OK;
}

/** @brief Exercise one legal mask and every repeat/module eligibility combination.
 * @param session Exclusive live scratch.
 * @param task Supported task ID.
 * @param mask Current task-local output permissions.
 * @param quality Private accumulated violation report.
 * @return OK after complete bounded combinations, ERROR otherwise. */
static cgai_status stress_mask(cgai_gameplay_session *session, uint32_t task, uint64_t mask,
                               gameplay_quality *quality) {
    /* Step 1: Rotate state families while fully enumerating host control bits. */
    const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
    for (uint32_t recent = 0U; recent < descriptor->output_count; ++recent) {
        for (uint64_t modules = 0U; modules < 4U; ++modules) {
            gameplay_fixture_case scenario;
            cgai_gameplay_request request;
            const size_t offset = (size_t)(mask * descriptor->output_count + recent + modules);
            if (!gameplay_fixture_get(
                    descriptor->fixture_offset + offset % descriptor->fixture_count, &scenario) ||
                !gameplay_fixture_request(&scenario, &request))
                return CGAI_STATUS_ERROR;
            /* Step 2: Empty eligibility and fallback-only masks must remain valid host queries. */
            request.allowed_outputs = mask;
            if (!stress_query(session, &request, recent, modules, quality))
                return CGAI_STATUS_ERROR;
        }
    }
    return CGAI_STATUS_OK;
}

/** @brief Exercise every task-local output mask, recent ID and two-module permission mask.
 * @param session Exclusive live scratch.
 * @param quality Private accumulated violation report.
 * @return OK after the full fixed domain, ERROR otherwise. */
static cgai_status stress_host(cgai_gameplay_session *session, gameplay_quality *quality) {
    /* Step 1: Task heads preserve their distinct permission domains. */
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task) {
        const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
        const uint64_t count = UINT64_C(1) << descriptor->output_count;
        for (uint64_t mask = 0U; mask < count; ++mask)
            if (!stress_mask(session, task, mask, quality))
                return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

/** @brief Verify one frozen bark over every explicitly irrelevant tactical context.
 * @param session Exclusive live scratch.
 * @param scenario Private caller-owned bark scenario.
 * @param quality Private accumulated invariant counters.
 * @return OK after thirty-two independent variants, ERROR otherwise. */
static cgai_status invariant_scenario(cgai_gameplay_session *session,
                                      gameplay_fixture_case *scenario, gameplay_quality *quality) {
    /* Step 1: The task feature mask declares irrelevance without discarding state validation. */
    for (uint32_t variant = 0U; variant < 32U; ++variant) {
        cgai_gameplay_request request;
        cgai_gameplay_result selected = {0};
        if (!gameplay_fixture_bark_context(&scenario->example.state, variant) ||
            !gameplay_fixture_request(scenario, &request) ||
            !cgai_gameplay_select(session, &request, &selected))
            return CGAI_STATUS_ERROR;
        ++quality->bark_invariance_cases;
        quality->bark_invariance_correct += selected.output == scenario->example.target ? 1U : 0U;
    }
    return CGAI_STATUS_OK;
}

/** @brief Score abstract intent execution and deterministic replay under irrelevant setting
 * changes.
 * @param session Exclusive live scratch.
 * @param scenario Private caller-owned intent scenario.
 * @param quality Private accumulated transition counters.
 * @return OK after three selects and one bounded transition, ERROR otherwise. */
static cgai_status simulate_scenario(cgai_gameplay_session *session,
                                     gameplay_fixture_case *scenario, gameplay_quality *quality) {
    /* Step 1: Apply the actual neural proposal to independent precondition and hazard rules. */
    cgai_gameplay_request request;
    cgai_gameplay_result selected = {0}, repeated = {0}, variant = {0};
    gameplay_simulation_result outcome;
    if (!gameplay_fixture_request(scenario, &request) ||
        !cgai_gameplay_select(session, &request, &selected) ||
        !cgai_gameplay_select(session, &request, &repeated) ||
        !gameplay_fixture_simulate(&request.state, selected.output, &outcome))
        return CGAI_STATUS_ERROR;
    /* Step 2: Setting is irrelevant to intent; deterministic replay must preserve the proposal. */
    request.state.values[GAMEPLAY_SETTING] ^= 1U;
    if (!cgai_gameplay_select(session, &request, &variant))
        return CGAI_STATUS_ERROR;
    ++quality->simulator_cases;
    quality->simulator_legal += outcome.legal ? 1U : 0U;
    quality->simulator_survived += outcome.survived ? 1U : 0U;
    quality->simulator_objective_successes += outcome.objective_success ? 1U : 0U;
    quality->simulator_stable +=
        selected.output == repeated.output && selected.output == variant.output ? 1U : 0U;
    return CGAI_STATUS_OK;
}

/** @brief Reduce complete private accumulations to fixed per-task means.
 * @param quality Complete private accumulator. */
static void quality_means(gameplay_quality *quality) {
    /* Step 1: Every fixed split and task is nonempty in the authored profile. */
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task) {
        for (size_t split = 0U; split < 3U; ++split)
            quality->split[task][split].cross_entropy /= (double)quality->split[task][split].cases;
        /* Step 2: Both specialist contributions are measured in the same requested output head. */
        for (size_t module = 0U; module < GAMEPLAY_MODULE_COUNT; ++module) {
            const double count = (double)quality->composition_cases[task];
            quality->module_weights[task][module] /= count;
            quality->module_contributions[task][module] /= count;
            quality->ablation_cross_entropy[task][module] /= count;
            quality->ablation_change[task][module] /= count;
        }
    }
}

/** @brief Score all frozen requests and supplementary invariant/simulator checks.
 * @param session Exclusive preallocated scratch.
 * @param quality Private zero-initialized accumulator.
 * @return OK after every complete workload, ERROR otherwise. */
static cgai_status quality_requests(cgai_gameplay_session *session, gameplay_quality *quality) {
    /* Step 1: Every independent scenario receives task scoring plus its supplemental workload. */
    for (size_t i = 0U; i < GAMEPLAY_FIXTURE_CASE_COUNT; ++i) {
        gameplay_fixture_case scenario;
        if (!gameplay_fixture_get(i, &scenario) || !score_scenario(session, &scenario, quality))
            return CGAI_STATUS_ERROR;
        const cgai_status status = scenario.example.task == GAMEPLAY_TASK_BARK
                                       ? invariant_scenario(session, &scenario, quality)
                                       : simulate_scenario(session, &scenario, quality);
        if (!status)
            return CGAI_STATUS_ERROR;
    }
    /* Step 2: Complete all host-mask and module-exclusion checks before reducing means. */
    if (!stress_host(session, quality))
        return CGAI_STATUS_ERROR;
    quality_means(quality);
    return CGAI_STATUS_OK;
}

/** @brief Produce one complete immutable-model quality report.
 * @param model Borrowed aligned bundle.
 * @param quality Writable result, unchanged on every error.
 * @return OK on complete publication or ERROR. */
cgai_status gameplay_tool_quality(const cgai_gameplay_model *model, gameplay_quality *quality) {
    /* Step 1: Prepare private counters and scratch so callers never observe partial metrics. */
    if (model == NULL || quality == NULL)
        return cgai_fail("composed gameplay model and quality result required");
    gameplay_quality measured = {0};
    if (!cgai_gameplay_get_resources(model, &measured.resources))
        return CGAI_STATUS_ERROR;
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, 0U);
    if (session == NULL)
        return CGAI_STATUS_ERROR;
    /* Step 2: Release scratch after complete workloads on both success and failure. */
    const cgai_status status = quality_requests(session, &measured);
    cgai_gameplay_session_destroy(session);
    if (!status)
        return CGAI_STATUS_ERROR;
    *quality = measured;
    return CGAI_STATUS_OK;
}

/** @brief Validate exact split counts and finite target means for one task.
 * @param task Supported task index.
 * @param quality Borrowed complete quality.
 * @return Nonzero for internally valid split metrics. */
static int valid_task_splits(uint32_t task, const gameplay_quality *quality) {
    /* Step 1: Exact denominators prevent malformed training or held-out counts hiding regressions.
     */
    for (size_t split = 0U; split < 3U; ++split) {
        const gameplay_split_metrics *metric = &quality->split[task][split];
        if (metric->cases != gameplay_fixture_count(task, (gameplay_split)split) ||
            metric->correct > metric->cases || metric->abstained > metric->cases ||
            !isfinite(metric->cross_entropy) || metric->cross_entropy < 0.0)
            return 0;
    }
    return 1;
}

/** @brief Validate complete composition means and their frozen workload counts.
 * @param task Supported task index.
 * @param quality Borrowed complete quality.
 * @return Nonzero for finite bounded diagnostic means. */
static int valid_composition(uint32_t task, const gameplay_quality *quality) {
    /* Step 1: Require complete independent states and host-permission stress counts. */
    const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
    if (quality->composition_cases[task] != descriptor->fixture_count ||
        quality->host_checks[task] != host_case_count(descriptor))
        return 0;
    /* Step 2: Reject all nonfinite routing, posterior and ablation means. */
    for (size_t module = 0U; module < GAMEPLAY_MODULE_COUNT; ++module) {
        const double weight = quality->module_weights[task][module];
        const double share = quality->module_contributions[task][module];
        const double loss = quality->ablation_cross_entropy[task][module];
        const double change = quality->ablation_change[task][module];
        if (!isfinite(weight) || !isfinite(share) || !isfinite(loss) || !isfinite(change) ||
            weight < 0.0 || weight > 1.0 || share < 0.0 || share > 1.0 || loss < 0.0 ||
            change < 0.0)
            return 0;
    }
    return fabs(quality->module_weights[task][0] + quality->module_weights[task][1] - 1.0) <=
               1e-9 &&
           fabs(quality->module_contributions[task][0] + quality->module_contributions[task][1] -
                1.0) <= 1e-9;
}

/** @brief Validate complete raw quality before deriving any acceptance ratios.
 * @param quality Borrowed complete report.
 * @return Nonzero for valid workload counts, finite metrics and nonempty resources. */
static int valid_quality(const gameplay_quality *quality) {
    /* Step 1: Resource and supplemental workload counters must describe the exact profile. */
    if (quality == NULL || quality->resources.model_bytes == 0U ||
        quality->resources.session_bytes == 0U || quality->resources.parameter_count == 0U ||
        quality->resources.parameter_count > SIZE_MAX / sizeof(double) ||
        quality->resources.parameter_bytes != quality->resources.parameter_count * sizeof(double) ||
        quality->resources.optimizer_bytes != 0U ||
        quality->resources.model_bytes <= quality->resources.parameter_bytes ||
        quality->bark_invariance_cases != GAMEPLAY_BARK_INVARIANCE_CASES ||
        quality->bark_invariance_correct > quality->bark_invariance_cases ||
        quality->simulator_cases != GAMEPLAY_SIMULATOR_CASES ||
        quality->simulator_legal > quality->simulator_cases ||
        quality->simulator_survived > quality->simulator_legal ||
        quality->simulator_objective_successes > quality->simulator_survived ||
        quality->simulator_stable > quality->simulator_cases)
        return 0;
    /* Step 2: Validate every task, including training and all neural composition diagnostics. */
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task)
        if (!valid_task_splits(task, quality) || !valid_composition(task, quality))
            return 0;
    return 1;
}

/** @brief Require two materially contributing modules for one requested task head.
 * @param task Supported task index.
 * @param quality Valid candidate report.
 * @return Nonzero when both modules contribute and affect the same head's prediction. */
static int material_composition(uint32_t task, const gameplay_quality *quality) {
    /* Step 1: An unused router bank or numerically equivalent ablation cannot establish
     * composition. */
    for (size_t module = 0U; module < GAMEPLAY_MODULE_COUNT; ++module)
        if (quality->module_weights[task][module] < GAMEPLAY_MINIMUM_MODULE_SHARE ||
            quality->module_contributions[task][module] < GAMEPLAY_MINIMUM_MODULE_SHARE ||
            quality->ablation_change[task][module] < GAMEPLAY_MINIMUM_ABLATION_CHANGE)
            return 0;
    return 1;
}

/** @brief Apply absolute candidate task accuracy and every host and composition gate.
 * @param quality Complete candidate measurements.
 * @return Nonzero exactly when all nontiming acceptance requirements pass. */
int gameplay_tool_absolute_gate(const gameplay_quality *quality) {
    /* Step 1: Full invariance and executable safe simulator behavior are independent quality gates.
     */
    if (!valid_quality(quality) || quality->resources.model_bytes > GAMEPLAY_MODEL_LIMIT ||
        quality->resources.session_bytes > GAMEPLAY_SESSION_LIMIT ||
        quality->module_violations != 0U ||
        quality->bark_invariance_correct != quality->bark_invariance_cases ||
        quality->simulator_legal != quality->simulator_cases ||
        quality->simulator_survived != quality->simulator_cases ||
        quality->simulator_objective_successes != quality->simulator_cases ||
        quality->simulator_stable != quality->simulator_cases)
        return 0;
    /* Step 2: Every task must independently pass held-out accuracy and neural materiality. */
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task) {
        if (quality->mask_violations[task] != 0U || quality->repeat_violations[task] != 0U ||
            !material_composition(task, quality))
            return 0;
        for (size_t split = GAMEPLAY_DEVELOPMENT; split <= GAMEPLAY_TEST; ++split)
            if ((double)quality->split[task][split].correct /
                    (double)quality->split[task][split].cases <
                0.95)
                return 0;
    }
    return 1;
}

/** @brief Validate a complete timed workload against its explicit latency ceilings.
 * @param timing Borrowed workload timings.
 * @param multiplier One for single-head requests, two for paired sequential requests.
 * @return Nonzero for finite ordered timing summaries meeting the declared profile. */
static int valid_timing(const gameplay_timing *timing, double multiplier) {
    /* Step 1: Fix successful sample count and session pool before comparing quantiles. */
    return timing->samples == GAMEPLAY_BENCHMARK_SAMPLES &&
           timing->session_pool == GAMEPLAY_SESSION_POOL && isfinite(timing->p50_us) &&
           isfinite(timing->p95_us) && isfinite(timing->p99_us) && isfinite(timing->maximum_us) &&
           timing->p50_us >= 0.0 && timing->p50_us <= timing->p95_us &&
           timing->p95_us <= timing->p99_us && timing->p99_us <= timing->maximum_us &&
           timing->p95_us <= 500.0 * multiplier && timing->p99_us <= 1000.0 * multiplier;
}

/** @brief Require independent per-task held-out improvement without accuracy or test loss
 * regression.
 * @param task Supported head index.
 * @param before Complete valid incumbent report.
 * @param after Complete valid candidate report.
 * @return Nonzero for all task-local relative quality requirements. */
static int task_improves(uint32_t task, const gameplay_quality *before,
                         const gameplay_quality *after) {
    /* Step 1: Held-out task counts are already validated, so raw correct counts are comparable. */
    const gameplay_split_metrics *before_dev = &before->split[task][GAMEPLAY_DEVELOPMENT];
    const gameplay_split_metrics *after_dev = &after->split[task][GAMEPLAY_DEVELOPMENT];
    const gameplay_split_metrics *before_test = &before->split[task][GAMEPLAY_TEST];
    const gameplay_split_metrics *after_test = &after->split[task][GAMEPLAY_TEST];
    return after_dev->correct >= before_dev->correct &&
           after_test->correct >= before_test->correct &&
           before_dev->cross_entropy - after_dev->cross_entropy > GAMEPLAY_MINIMUM_IMPROVEMENT &&
           after_test->cross_entropy - before_test->cross_entropy <= GAMEPLAY_REGRESSION_TOLERANCE;
}

/** @brief Gate every task, neural composition, bounded execution and measured workload together.
 * @param before Complete incumbent measurements.
 * @param after Complete candidate measurements.
 * @param performance Candidate timings on named hardware and build.
 * @return Nonzero exactly when every acceptance requirement passes. */
int gameplay_tool_gate(const gameplay_quality *before, const gameplay_quality *after,
                       const gameplay_performance *performance) {
    /* Step 1: Invalid or incomplete incumbent and candidate data cannot pass relative comparisons.
     */
    if (!valid_quality(before) || !gameplay_tool_absolute_gate(after) || performance == NULL ||
        !valid_timing(&performance->paired, 2.0))
        return 0;
    /* Step 2: Promotion requires both tasks to improve and meet their single-request timing caps.
     */
    for (uint32_t task = 0U; task < GAMEPLAY_TASK_COUNT; ++task)
        if (!task_improves(task, before, after) || !valid_timing(&performance->task[task], 1.0))
            return 0;
    return 1;
}
