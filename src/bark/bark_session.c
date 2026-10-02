/** @file bark_session.c @brief Single-forward typed NPC proposals with legal-output masking. */
#include "internal/bark_internal.h"
#include "internal/error.h"
#include <math.h>
#include <stdlib.h>

/** @brief Release scratch without changing the borrowed model.
 * @param session Owned session, or NULL. */
void cgai_bark_session_destroy(cgai_bark_session *session) {
    /* Step 1: Permit constructor cleanup without a completed owner. */
    if (session == NULL)
        return;
    /* Step 2: Individual numeric slices borrow the workspace storage allocation. */
    cgai_neural_workspace_destroy(session->workspace);
    free(session);
}

/** @brief Allocate complete selection scratch once within the caller's byte cap.
 * @param model Borrowed immutable model, which outlives the session.
 * @param max_session_bytes Complete payload cap; zero disables the cap.
 * @return Owned session, or NULL with a diagnostic. */
cgai_bark_session *cgai_bark_session_create(const cgai_bark_model *model,
                                            size_t max_session_bytes) {
    /* Step 1: Apply the cap to the complete owner and all numerical allocations. */
    cgai_error_clear();
    const size_t bytes = cgai_bark_session_bytes(model);
    if (bytes == 0U || (max_session_bytes != 0U && bytes > max_session_bytes)) {
        cgai_fail("bark session exceeds its memory cap or has an invalid model");
        return NULL;
    }
    cgai_bark_session *session = calloc(1U, sizeof(*session));
    if (session == NULL) {
        cgai_fail("could not allocate bark selection session");
        return NULL;
    }
    /* Step 2: Retain the immutable model and allocate scratch before any request runs. */
    session->model = model;
    session->workspace = cgai_neural_workspace_create(model->network);
    if (session->workspace == NULL) {
        cgai_bark_session_destroy(session);
        return NULL;
    }
    return session;
}

/** @brief Validate typed observations without reading vocabulary or changing scratch.
 * @param request Borrowed non-NULL request.
 * @return One for the complete supported contract, zero otherwise. */
static int request_valid(const cgai_bark_request *request) {
    /* Step 1: Unsigned comparisons also reject negative cast enum values. */
    return request->contract_version == CGAI_BARK_CONTRACT_VERSION &&
           (uint32_t)request->state.event <= CGAI_BARK_EVENT_RETREAT &&
           (uint32_t)request->state.danger <= CGAI_BARK_DANGER_HIGH &&
           (uint32_t)request->state.relationship <= CGAI_BARK_RELATION_HOSTILE &&
           (uint32_t)request->state.setting <= CGAI_BARK_SETTING_OUTDOOR &&
           (request->allowed_ids & ~CGAI_BARK_ALL_IDS) == 0U &&
           request->recent_id < CGAI_BARK_ID_COUNT;
}

/** @brief Convert validated enum indices into four exact ordered state token IDs.
 * @param session Borrowed exclusive initialized session.
 * @param state Borrowed validated categorical observations. */
static void prepare_context(cgai_bark_session *session, const cgai_bark_state *state) {
    /* Step 1: Lookups use immutable tables populated once when the model was loaded. */
    session->context[0] = session->model->events[state->event];
    session->context[1] = session->model->dangers[state->danger];
    session->context[2] = session->model->relationships[state->relationship];
    session->context[3] = session->model->settings[state->setting];
}

/** @brief Select the highest-likelihood admitted catalog target from one completed forward.
 * @param session Borrowed session retaining successful forward scratch.
 * @param mask Legal catalog mask containing abstention.
 * @param result Writable private selection; caller publishes only on success.
 * @return OK for a finite best likelihood, ERROR on numeric failure. */
static cgai_status select_catalog(const cgai_bark_session *session, uint64_t mask,
                                  cgai_bark_result *result) {
    /* Step 1: Compare log likelihoods, preserving order even when probabilities underflow. */
    double best = HUGE_VAL;
    for (size_t index = 0U; index < CGAI_BARK_ID_COUNT; ++index) {
        if ((mask & (UINT64_C(1) << index)) == 0U)
            continue;
        const double loss = cgai_neural_loss(session->model->network, session->workspace,
                                             session->model->catalog[index]);
        if (!isfinite(loss))
            return cgai_fail("bark selection produced a nonfinite likelihood");
        if (loss < best) {
            best = loss;
            result->id = (cgai_bark_id)index;
        }
    }
    /* Step 2: Resolve content after masking, retaining unconditional model likelihood. */
    result->probability = exp(-best);
    result->text = cgai_bark_catalog_text(result->id);
    result->abstained = result->id == CGAI_BARK_ABSTAIN;
    return CGAI_STATUS_OK;
}

/** @brief Select one legal authored line using at most one allocation-free model forward.
 * @param session Exclusive live session retaining an immutable model.
 * @param request Borrowed contract-one typed request.
 * @param result Writable output, unchanged on every error.
 * @return OK on publication, ERROR on invalid arguments or nonfinite network work. */
cgai_status cgai_bark_select(cgai_bark_session *session, const cgai_bark_request *request,
                             cgai_bark_result *result) {
    /* Step 1: Reject malformed requests before changing scratch or caller output. */
    cgai_error_clear();
    if (session == NULL || request == NULL || result == NULL || !request_valid(request))
        return cgai_fail("invalid bark selection arguments or contract");
    uint64_t mask = request->allowed_ids | UINT64_C(1);
    if (request->recent_id != 0U)
        mask &= ~(UINT64_C(1) << request->recent_id);
    cgai_bark_result selected = {.text = NULL,
                                 .probability = 1.0,
                                 .forward_passes = 0U,
                                 .id = CGAI_BARK_ABSTAIN,
                                 .abstained = 1};
    /* Step 2: Silence needs no network work when every spoken output is excluded. */
    if (mask != UINT64_C(1)) {
        prepare_context(session, &request->state);
        selected.forward_passes = 1U;
        if (!cgai_neural_forward(session->model->network, session->context, session->workspace) ||
            !select_catalog(session, mask, &selected))
            return CGAI_STATUS_ERROR;
    }
    /* Step 3: Publish a complete finite validated catalog selection atomically to the caller. */
    *result = selected;
    return CGAI_STATUS_OK;
}
