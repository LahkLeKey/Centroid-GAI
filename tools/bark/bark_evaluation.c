/** @file bark_evaluation.c @brief Frozen specialist quality and automatic promotion gates. */
#include "bark_tool.h"
#include "internal/bark_internal.h"
#include "internal/error.h"
#include <math.h>

static int valid_quality(const bark_quality *quality);

/** @brief Check a published selection against the exact host mask and repetition contract.
 * @param request Borrowed validated request.
 * @param result Borrowed successful selection.
 * @param quality Writable counters receiving each independent violation. */
static void check_host_result(const cgai_bark_request *request, const cgai_bark_result *result,
                              bark_quality *quality) {
    /* Step 1: Abstention is always admitted; unsupported IDs cannot be shifted safely. */
    const unsigned id = (unsigned)result->id;
    if (id >= CGAI_BARK_ID_COUNT ||
        (id != CGAI_BARK_ABSTAIN && (request->allowed_ids & (UINT64_C(1) << id)) == 0U) ||
        result->abstained != (id == CGAI_BARK_ABSTAIN))
        ++quality->mask_violations;
    /* Step 2: A nonzero recent ID is never a permissible spoken proposal. */
    if (request->recent_id != 0U && id == request->recent_id)
        ++quality->repeat_violations;
}

/** @brief Score one independent frozen target using an already allocated session.
 * @param session Borrowed exclusive specialist session.
 * @param scenario Borrowed frozen request and authored teacher target.
 * @param quality Writable private accumulated quality report.
 * @return OK after one complete target score, ERROR otherwise. */
static cgai_status score_scenario(cgai_bark_session *session, const bark_fixture_case *scenario,
                                  bark_quality *quality) {
    /* Step 1: Default requests admit every ID and therefore always perform one forward pass. */
    cgai_bark_result selected = {0};
    if (!cgai_bark_select(session, &scenario->request, &selected))
        return CGAI_STATUS_ERROR;
    if (selected.forward_passes != 1U || (unsigned)scenario->split >= 3U ||
        scenario->expected_id >= CGAI_BARK_ID_COUNT)
        return cgai_fail("invalid frozen bark scoring work or scenario");
    const double loss = cgai_neural_loss(session->model->network, session->workspace,
                                         session->model->catalog[scenario->expected_id]);
    if (!isfinite(loss) || loss < 0.0)
        return cgai_fail("nonfinite or negative frozen bark target loss");
    /* Step 2: Count exact teacher agreement including silence, retaining unconditional NLL. */
    bark_split_metrics *split = &quality->split[scenario->split];
    ++split->cases;
    split->correct += (uint32_t)selected.id == scenario->expected_id ? 1U : 0U;
    split->abstained += selected.id == CGAI_BARK_ABSTAIN ? 1U : 0U;
    split->cross_entropy += loss;
    check_host_result(&scenario->request, &selected, quality);
    return CGAI_STATUS_OK;
}

/** @brief Exercise every legal-mask/recent-ID pair over deterministically rotated scenarios.
 * @param session Borrowed exclusive preallocated session.
 * @param quality Writable private report receiving host decoding violations.
 * @return OK after all 4608 bounded requests, ERROR on preparation or inference failure. */
static cgai_status stress_host_decoding(cgai_bark_session *session, bark_quality *quality) {
    /* Step 1: Cover every nine-bit host mask, including an empty mask and silence-only masks. */
    for (uint64_t mask = 0U; mask <= CGAI_BARK_ALL_IDS; ++mask) {
        for (uint32_t recent = 0U; recent < CGAI_BARK_ID_COUNT; ++recent) {
            bark_fixture_case scenario;
            if (!bark_fixture_get((size_t)(mask * CGAI_BARK_ID_COUNT + recent) %
                                      BARK_FIXTURE_CASE_COUNT,
                                  &scenario))
                return cgai_fail("could not prepare frozen bark host stress scenario");
            /* Step 2: Keep the typed state fixed while changing only host permissions. */
            scenario.request.allowed_ids = mask;
            scenario.request.recent_id = recent;
            cgai_bark_result selected = {0};
            if (!cgai_bark_select(session, &scenario.request, &selected))
                return CGAI_STATUS_ERROR;
            check_host_result(&scenario.request, &selected, quality);
        }
    }
    return CGAI_STATUS_OK;
}

/** @brief Complete every frozen target and host stress request before calculating means.
 * @param session Borrowed exclusive preallocated session.
 * @param measured Writable private quality accumulator.
 * @return OK after complete means, ERROR with private partial accumulation otherwise. */
static cgai_status score_quality_requests(cgai_bark_session *session, bark_quality *measured) {
    /* Step 1: Score independent full-vocabulary target likelihoods on each fixed split. */
    for (size_t i = 0U; i < BARK_FIXTURE_CASE_COUNT; ++i) {
        bark_fixture_case scenario;
        if (!bark_fixture_get(i, &scenario) || !score_scenario(session, &scenario, measured))
            return CGAI_STATUS_ERROR;
    }
    /* Step 2: Only complete host checks and fixed nonempty splits can yield published means. */
    if (!stress_host_decoding(session, measured))
        return CGAI_STATUS_ERROR;
    for (size_t i = 0U; i < 3U; ++i)
        measured->split[i].cross_entropy /= (double)measured->split[i].cases;
    return valid_quality(measured) ? CGAI_STATUS_OK
                                   : cgai_fail("invalid complete frozen bark quality measurements");
}

/** @brief Score frozen splits and bounded-host decoding with one reusable private session.
 * @param model Borrowed immutable specialist model.
 * @param quality Writable complete report, unchanged on every failure.
 * @return OK after all requests and publication, ERROR otherwise. */
cgai_status bark_tool_quality(const cgai_bark_model *model, bark_quality *quality) {
    /* Step 1: Acquire private scratch and resources without publishing partial measurements. */
    cgai_error_clear();
    if (model == NULL || quality == NULL)
        return cgai_fail("bark model and quality report required");
    bark_quality measured = {0};
    if (!cgai_bark_get_resources(model, &measured.resources))
        return CGAI_STATUS_ERROR;
    cgai_bark_session *session = cgai_bark_session_create(model, 0U);
    if (session == NULL)
        return CGAI_STATUS_ERROR;
    /* Step 2: Score independent targets and host constraints, releasing scratch on every path. */
    const cgai_status status = score_quality_requests(session, &measured);
    cgai_bark_session_destroy(session);
    if (!status)
        return CGAI_STATUS_ERROR;
    /* Step 3: Publish complete split means only after every request succeeded. */
    *quality = measured;
    return CGAI_STATUS_OK;
}

/** @brief Validate raw split counts and finite target losses before deriving any ratios.
 * @param quality Borrowed complete measured report.
 * @return Nonzero only for three exact frozen split counts and valid raw counters. */
static int valid_quality(const bark_quality *quality) {
    /* Step 1: Reject missing reports and absent resident-resource measurements. */
    if (quality == NULL || quality->resources.model_bytes == 0U ||
        quality->resources.session_bytes == 0U ||
        !isfinite(quality->resources.neural.config.routing_temperature))
        return 0;
    /* Step 2: Inspect every split, including training, before promotion comparisons. */
    for (size_t i = 0U; i < 3U; ++i) {
        const bark_split_metrics *split = &quality->split[i];
        if (split->cases != bark_fixture_count((bark_fixture_split)i) ||
            split->correct > split->cases || split->abstained > split->cases ||
            !isfinite(split->cross_entropy) || split->cross_entropy < 0.0)
            return 0;
    }
    return 1;
}

/** @brief Validate the exact timed workload and finite monotonic latency percentiles.
 * @param performance Borrowed complete current-machine timing report.
 * @return Nonzero when raw timings and workload meet every version-one requirement. */
static int valid_performance(const bark_performance *performance) {
    /* Step 1: Require the same successful workload and one-worker session pool. */
    if (performance == NULL || performance->samples != BARK_BENCHMARK_SAMPLES ||
        performance->session_pool != BARK_SESSION_POOL)
        return 0;
    /* Step 2: Reject NaN, infinity, negative or out-of-order summaries before thresholds. */
    return isfinite(performance->p50_us) && isfinite(performance->p95_us) &&
           isfinite(performance->p99_us) && isfinite(performance->maximum_us) &&
           performance->p50_us >= 0.0 && performance->p50_us <= performance->p95_us &&
           performance->p95_us <= performance->p99_us &&
           performance->p99_us <= performance->maximum_us && performance->p95_us <= 500.0 &&
           performance->p99_us <= 1000.0;
}

/** @brief Require a held-out split to meet absolute accuracy and preserve incumbent accuracy.
 * @param before Borrowed valid incumbent split metrics.
 * @param after Borrowed valid candidate split metrics with the same frozen case count.
 * @return Nonzero at or above 95 percent exact teacher agreement without regression. */
static int valid_accuracy(const bark_split_metrics *before, const bark_split_metrics *after) {
    /* Step 1: Compare raw counts so a differing denominator cannot hide a regression. */
    return after->correct >= before->correct &&
           (double)after->correct / (double)after->cases >= 0.95;
}

/** @brief Gate frozen quality, improvement, requested heap and measured latency together.
 * @param before Borrowed incumbent quality on the exact frozen scenarios.
 * @param after Borrowed candidate quality on the same scenarios.
 * @param performance Borrowed candidate end-to-end current-machine measurements.
 * @return Nonzero exactly when every promotion condition is satisfied. */
int bark_tool_gate(const bark_quality *before, const bark_quality *after,
                   const bark_performance *performance) {
    /* Step 1: Validate all raw measurements before accessing derived accuracy or losses. */
    if (!valid_quality(before) || !valid_quality(after) || !valid_performance(performance))
        return 0;
    if (after->resources.model_bytes > BARK_MODEL_LIMIT ||
        after->resources.session_bytes > BARK_SESSION_LIMIT || after->mask_violations != 0U ||
        after->repeat_violations != 0U)
        return 0;
    /* Step 2: Require both held-out accuracies and a material development loss improvement. */
    const bark_split_metrics *before_dev = &before->split[BARK_FIXTURE_DEVELOPMENT];
    const bark_split_metrics *after_dev = &after->split[BARK_FIXTURE_DEVELOPMENT];
    const bark_split_metrics *before_test = &before->split[BARK_FIXTURE_TEST];
    const bark_split_metrics *after_test = &after->split[BARK_FIXTURE_TEST];
    return valid_accuracy(before_dev, after_dev) && valid_accuracy(before_test, after_test) &&
           before_dev->cross_entropy - after_dev->cross_entropy > BARK_MINIMUM_IMPROVEMENT &&
           after_test->cross_entropy - before_test->cross_entropy <= BARK_REGRESSION_TOLERANCE;
}
