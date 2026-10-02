/** @file test_bark.c @brief Typed bark contracts, resource caps and constrained inference. */
#include "internal/bark_internal.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Vocabulary covers each authored state category and every selectable catalog ID. */
static const char *const bark_vocabulary =
    "eventidle eventgreet eventthreat eventvictory eventdiscovery eventretreat "
    "dangerlow dangerhigh relationfriendly relationneutral relationhostile "
    "settingindoor settingoutdoor abstain greetwarm greetplain warnhostile "
    "threatcalm threaturgent victory discover retreat";
/** Test-local trusted artifact; each native test runs in its build directory. */
static const char *const bark_artifact = "test-bark-runtime.cgnn";

/** @brief Create a compact contract-shaped neural fixture without optimizer allocations.
 * @param vocabulary Borrowed authored vocabulary text.
 * @param window Context width for valid and deliberately invalid shape tests.
 * @return Owned neural model, released by the caller. */
static cgai_neural_model *bark_network(const char *vocabulary, size_t window) {
    /* Step 1: Keep every dense test forward small while retaining multi-expert routing. */
    cgai_neural_config config = cgai_neural_default_config();
    config.embedding_dimensions = 2U;
    config.hidden_dimensions = 3U;
    config.centroid_count = 3U;
    config.context_window = window;
    cgai_neural_model *network = cgai_neural_create(&config, vocabulary);
    TEST_CHECK(network != NULL, cgai_last_error());
    return network;
}

/** @brief Save and load a trusted typed fixture using the public ownership API.
 * @return Owned typed model, released after its sessions. */
static cgai_bark_model *bark_fixture(void) {
    /* Step 1: Exercise native artifact loading instead of constructing the opaque wrapper. */
    cgai_neural_model *network = bark_network(bark_vocabulary, 4U);
    TEST_CHECK(cgai_neural_save(network, bark_artifact) == CGAI_STATUS_OK, cgai_last_error());
    cgai_neural_destroy(network);
    cgai_bark_model *model = cgai_bark_load(bark_artifact);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(remove(bark_artifact) == 0, "could not remove bark fixture artifact");
    return model;
}

/** @brief Verify both lexical names and authored game content have stable catalog IDs. */
static void bark_catalog(void) {
    /* Step 1: Require all stable lexical targets and all nonzero authored lines. */
    for (uint32_t index = 0U; index < CGAI_BARK_ID_COUNT; ++index) {
        const cgai_bark_id id = (cgai_bark_id)index;
        TEST_CHECK(cgai_bark_id_name(id) != NULL, "catalog target missing");
        TEST_CHECK((cgai_bark_catalog_text(id) == NULL) == (index == 0U),
                   "catalog silence/content mapping changed");
    }
    /* Step 2: Spot-check contract spelling/content and invalid enum boundaries. */
    TEST_CHECK(strcmp(cgai_bark_id_name(CGAI_BARK_THREAT_URGENT), "threaturgent") == 0,
               "urgent target spelling changed");
    TEST_CHECK(strcmp(cgai_bark_catalog_text(CGAI_BARK_GREET_WARM), "Good to see you.") == 0,
               "friendly line changed");
    TEST_CHECK(cgai_bark_id_name((cgai_bark_id)-1) == NULL &&
                   cgai_bark_id_name(CGAI_BARK_ID_COUNT) == NULL &&
                   cgai_bark_catalog_text((cgai_bark_id)-1) == NULL,
               "invalid catalog ID was accepted");
}

/** @brief Reject malformed contract shape or missing vocabulary before publication.
 * @param vocabulary Borrowed candidate vocabulary.
 * @param window Candidate context width. */
static void bark_reject_artifact(const char *vocabulary, size_t window) {
    /* Step 1: The generic neural artifact is valid, but its typed contract is incomplete. */
    cgai_neural_model *network = bark_network(vocabulary, window);
    TEST_CHECK(cgai_neural_save(network, bark_artifact) == CGAI_STATUS_OK, cgai_last_error());
    cgai_neural_destroy(network);
    TEST_CHECK(cgai_bark_load(bark_artifact) == NULL, "unsupported bark contract was loaded");
    TEST_CHECK(remove(bark_artifact) == 0, "could not remove rejected bark artifact");
}

/** @brief Reject a network containing every state token but missing one authored output. */
static void bark_missing_catalog(void) {
    /* Step 1: Remove the final retreat target without changing any state spelling. */
    char vocabulary[512];
    const int written = snprintf(vocabulary, sizeof(vocabulary), "%s", bark_vocabulary);
    TEST_CHECK(written > 0 && (size_t)written < sizeof(vocabulary),
               "fixture vocabulary exceeds its buffer");
    char *suffix = strrchr(vocabulary, ' ');
    TEST_CHECK(suffix != NULL, "fixture vocabulary has no final target");
    *suffix = '\0';
    /* Step 2: A generic valid model still cannot satisfy the typed catalog contract. */
    bark_reject_artifact(vocabulary, 4U);
}

/** @brief Verify complete payload accounting and exact constructor memory caps.
 * @param model Borrowed immutable typed fixture. */
static void bark_resources(const cgai_bark_model *model) {
    /* Step 1: Check wrapper-inclusive model and session payload rather than weights alone. */
    cgai_bark_resources resources = {0};
    TEST_CHECK(cgai_bark_get_resources(model, &resources) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(resources.model_bytes == resources.neural.model_bytes + sizeof(*model) &&
                   resources.session_bytes ==
                       resources.neural.workspace_bytes + sizeof(cgai_bark_session) &&
                   resources.neural.optimizer_bytes == 0U,
               "bark resources omit owned wrappers or include optimizer allocations");
    TEST_CHECK(cgai_bark_session_create(model, resources.session_bytes - 1U) == NULL,
               "session accepted an insufficient complete heap cap");
    cgai_bark_session *session = cgai_bark_session_create(model, resources.session_bytes);
    TEST_CHECK(session != NULL, cgai_last_error());
    cgai_bark_session_destroy(session);
    /* Step 2: Missing ownership must preserve the inspection output and reject construction. */
    unsigned char before[sizeof(resources)];
    memcpy(before, &resources, sizeof(resources));
    TEST_CHECK(cgai_bark_get_resources(NULL, &resources) == CGAI_STATUS_ERROR,
               "null model inspection succeeded");
    TEST_CHECK(memcmp(before, &resources, sizeof(resources)) == 0,
               "failed resource inspection changed output");
    TEST_CHECK(cgai_bark_get_resources(model, NULL) == CGAI_STATUS_ERROR &&
                   cgai_bark_session_create(NULL, 0U) == NULL,
               "missing resource/session arguments were accepted");
}

/** @brief Require a malformed request to preserve every caller-owned result byte.
 * @param session Borrowed initialized session.
 * @param request Borrowed deliberately invalid request. */
static void bark_reject_request(cgai_bark_session *session, const cgai_bark_request *request) {
    /* Step 1: Compare the complete initialized result before and after the rejected call. */
    cgai_bark_result result;
    memset(&result, 0x5a, sizeof(result));
    unsigned char before[sizeof(result)];
    memcpy(before, &result, sizeof(result));
    TEST_CHECK(cgai_bark_select(session, request, &result) == CGAI_STATUS_ERROR,
               "invalid typed bark request was accepted");
    TEST_CHECK(memcmp(before, &result, sizeof(result)) == 0,
               "invalid bark request changed caller output");
}

/** @brief Reject protocol, state, legality-mask and recent-ID boundary violations.
 * @param session Borrowed initialized session. */
static void bark_bad_requests(cgai_bark_session *session) {
    /* Step 1: Start from the supported typed contract and vary independent boundaries. */
    cgai_bark_request requests[7];
    const cgai_bark_request valid =
        cgai_bark_default_request(CGAI_BARK_EVENT_GREET, CGAI_BARK_DANGER_LOW,
                                  CGAI_BARK_RELATION_FRIENDLY, CGAI_BARK_SETTING_INDOOR);
    for (size_t index = 0U; index < 7U; ++index)
        requests[index] = valid;
    requests[0].contract_version = 2U;
    requests[1].state.event = (cgai_bark_event)-1;
    requests[2].state.danger = (cgai_bark_danger)2;
    requests[3].state.relationship = (cgai_bark_relationship)3;
    requests[4].state.setting = (cgai_bark_setting)2;
    requests[5].allowed_ids |= UINT64_C(1) << 63U;
    requests[6].recent_id = UINT32_MAX;
    /* Step 2: Every invalid request leaves its output intact. */
    for (size_t index = 0U; index < 7U; ++index)
        bark_reject_request(session, &requests[index]);
}

/** @brief Reject missing selection ownership and arguments without publication.
 * @param session Borrowed initialized session. */
static void bark_null_requests(cgai_bark_session *session) {
    /* Step 1: Every missing argument must fail before reading state or touching output. */
    const cgai_bark_request valid =
        cgai_bark_default_request(CGAI_BARK_EVENT_IDLE, CGAI_BARK_DANGER_LOW,
                                  CGAI_BARK_RELATION_NEUTRAL, CGAI_BARK_SETTING_INDOOR);
    bark_reject_request(NULL, &valid);
    bark_reject_request(session, NULL);
    TEST_CHECK(cgai_bark_select(session, &valid, NULL) == CGAI_STATUS_ERROR,
               "missing bark result accepted");
}

/** @brief Give each expert identical finite logits with controlled catalog ordering.
 * @param model Borrowed mutable test fixture; no production model mutation is permitted.
 * @param first Most likely catalog ID.
 * @param second Next most likely ID, or first when no alternative is needed. */
static void bark_force_logits(cgai_bark_model *model, cgai_bark_id first, cgai_bark_id second) {
    /* Step 1: Suppress every ordinary token, then favor catalog choices in stable order. */
    cgai_neural_model *network = model->network;
    for (size_t row = 0U; row < network->config.centroid_count; ++row) {
        for (size_t token = 0U; token < network->vocabulary_size; ++token)
            network->logits[row * network->vocabulary_size + token] = -30.0;
        network->logits[row * network->vocabulary_size + model->catalog[second].value] = 20.0;
        network->logits[row * network->vocabulary_size + model->catalog[first].value] = 30.0;
    }
}

/** @brief Verify constrained argmax and exact typed input ordering in a single forward.
 * @param model Borrowed mutable test fixture.
 * @param session Borrowed session associated with the fixture. */
static void bark_constrained(cgai_bark_model *model, cgai_bark_session *session) {
    /* Step 1: A supplied illegal maximum must not displace the allowed second choice. */
    bark_force_logits(model, CGAI_BARK_GREET_WARM, CGAI_BARK_GREET_PLAIN);
    cgai_bark_request request =
        cgai_bark_default_request(CGAI_BARK_EVENT_THREAT, CGAI_BARK_DANGER_HIGH,
                                  CGAI_BARK_RELATION_HOSTILE, CGAI_BARK_SETTING_OUTDOOR);
    request.allowed_ids = UINT64_C(1) << CGAI_BARK_GREET_PLAIN;
    cgai_bark_result result = {0};
    TEST_CHECK(cgai_bark_select(session, &request, &result) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(result.id == CGAI_BARK_GREET_PLAIN && !result.abstained &&
                   result.text == cgai_bark_catalog_text(result.id) &&
                   result.forward_passes == 1U && isfinite(result.probability) &&
                   result.probability >= 0.0 && result.probability <= 1.0,
               "constrained selection ignored mask, work or output contract");
    TEST_CHECK(session->context[0].value == model->events[CGAI_BARK_EVENT_THREAT].value &&
                   session->context[1].value == model->dangers[CGAI_BARK_DANGER_HIGH].value &&
                   session->context[2].value ==
                       model->relationships[CGAI_BARK_RELATION_HOSTILE].value &&
                   session->context[3].value == model->settings[CGAI_BARK_SETTING_OUTDOOR].value,
               "typed observations did not preserve their exact ordered context");
    /* Step 2: A recent maximum is suppressed independently from the host legality mask. */
    request.allowed_ids = CGAI_BARK_ALL_IDS;
    request.recent_id = CGAI_BARK_GREET_WARM;
    TEST_CHECK(cgai_bark_select(session, &request, &result) == CGAI_STATUS_OK &&
                   result.id == CGAI_BARK_GREET_PLAIN && result.forward_passes == 1U,
               "recent-line suppression did not constrain selection");
}

/** @brief Verify empty and fully suppressed host domains produce zero-work silence.
 * @param session Borrowed initialized session. */
static void bark_silence(cgai_bark_session *session) {
    /* Step 1: No allowed spoken IDs means no model computation is necessary. */
    cgai_bark_request request =
        cgai_bark_default_request(CGAI_BARK_EVENT_IDLE, CGAI_BARK_DANGER_LOW,
                                  CGAI_BARK_RELATION_NEUTRAL, CGAI_BARK_SETTING_INDOOR);
    request.allowed_ids = 0U;
    cgai_bark_result result = {0};
    TEST_CHECK(cgai_bark_select(session, &request, &result) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(result.id == CGAI_BARK_ABSTAIN && result.text == NULL && result.abstained &&
                   result.forward_passes == 0U && result.probability == 1.0,
               "empty domain did not produce zero-work silence");
    /* Step 2: Suppressing the only spoken ID also produces silence without forwarding. */
    request.allowed_ids = UINT64_C(1) << CGAI_BARK_RETREAT;
    request.recent_id = CGAI_BARK_RETREAT;
    TEST_CHECK(cgai_bark_select(session, &request, &result) == CGAI_STATUS_OK &&
                   result.id == CGAI_BARK_ABSTAIN && result.forward_passes == 0U,
               "fully suppressed domain performed model work or emitted speech");
}

/** @brief Verify stable catalog-ID tie breaking rather than neural vocabulary order.
 * @param model Borrowed mutable test fixture.
 * @param session Borrowed initialized associated session. */
static void bark_tie(cgai_bark_model *model, cgai_bark_session *session) {
    /* Step 1: Give two legal outputs exactly equal logits across all experts. */
    bark_force_logits(model, CGAI_BARK_GREET_WARM, CGAI_BARK_GREET_PLAIN);
    cgai_neural_model *network = model->network;
    for (size_t row = 0U; row < network->config.centroid_count; ++row)
        network
            ->logits[row * network->vocabulary_size + model->catalog[CGAI_BARK_GREET_PLAIN].value] =
            30.0;
    cgai_bark_request request =
        cgai_bark_default_request(CGAI_BARK_EVENT_GREET, CGAI_BARK_DANGER_LOW,
                                  CGAI_BARK_RELATION_NEUTRAL, CGAI_BARK_SETTING_INDOOR);
    cgai_bark_result result = {0};
    /* Step 2: The smaller stable authored ID wins an exact likelihood tie. */
    TEST_CHECK(cgai_bark_select(session, &request, &result) == CGAI_STATUS_OK &&
                   result.id == CGAI_BARK_GREET_WARM,
               "catalog-ID tie did not select the lower authored ID");
}

/** @brief Verify failed forward work never publishes stale or nonfinite selections.
 * @param model Borrowed mutable test fixture.
 * @param session Borrowed initialized associated session. */
static void bark_numeric_failure(cgai_bark_model *model, cgai_bark_session *session) {
    /* Step 1: Inject a private-fixture failure after the trusted load validated finite weights. */
    const double previous = model->network->bias[0];
    model->network->bias[0] = NAN;
    const cgai_bark_request request =
        cgai_bark_default_request(CGAI_BARK_EVENT_GREET, CGAI_BARK_DANGER_LOW,
                                  CGAI_BARK_RELATION_FRIENDLY, CGAI_BARK_SETTING_INDOOR);
    bark_reject_request(session, &request);
    /* Step 2: Scratch is reusable once the fixture's immutable production condition is restored. */
    model->network->bias[0] = previous;
    cgai_bark_result result = {0};
    TEST_CHECK(cgai_bark_select(session, &request, &result) == CGAI_STATUS_OK &&
                   result.forward_passes == 1U,
               "numeric failure left selection scratch permanently unusable");
}

/** @brief Run typed catalog, loading, memory and constrained-generation contracts.
 * @return Zero after all assertions; test_fail terminates on failure. */
int main(void) {
    /* Step 1: Check independent authored and artifact boundaries before runtime ownership. */
    bark_catalog();
    bark_reject_artifact(bark_vocabulary, 3U);
    bark_reject_artifact("eventidle abstain greetwarm", 4U);
    bark_missing_catalog();
    TEST_CHECK(cgai_bark_load(NULL) == NULL, "null bark artifact path accepted");
    cgai_bark_model *model = bark_fixture();
    bark_resources(model);
    cgai_bark_session *session = cgai_bark_session_create(model, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    /* Step 2: Exercise reusable inference and only then release its borrowed model. */
    bark_bad_requests(session);
    bark_null_requests(session);
    bark_constrained(model, session);
    bark_silence(session);
    bark_tie(model, session);
    bark_numeric_failure(model, session);
    cgai_bark_session_destroy(session);
    cgai_bark_destroy(model);
    cgai_bark_session_destroy(NULL);
    cgai_bark_destroy(NULL);
    return 0;
}
