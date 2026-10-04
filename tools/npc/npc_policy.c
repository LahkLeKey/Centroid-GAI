/** @file npc_policy.c @brief Neural NPC selection using only visible state and bounded memory. */
#include "npc_policy.h"
#include "internal/error.h"
#include <stdlib.h>
#include <string.h>

/** @brief Return the complete frozen public neural shape.
 * @return Validated categorical architecture with two learned centroid banks. */
cgai_gameplay_config npc_policy_config(void) {
    cgai_gameplay_config config = {0};
    const uint32_t cardinalities[NPC_POLICY_FIELDS] = {6U, 6U, 6U, 6U, 17U, 17U, 2U, 5U,
                                                       5U, 7U, 8U, 8U, 4U,  2U,  8U, 3U};
    config.seed = 42U;
    config.routing_temperature = 32.0;
    config.feature_count = NPC_POLICY_FIELDS;
    config.embedding_dimensions = 16U;
    config.hidden_dimensions = 48U;
    config.module_count = 2U;
    config.centroids_per_module = 8U;
    config.task_count = 1U;
    memcpy(config.cardinalities, cardinalities, sizeof(cardinalities));
    config.output_counts[0] = 7U;
    config.task_modules[0] = 3U;
    config.task_features[0] = UINT64_C(65535);
    return config;
}

/** @brief Compare explicit semantic fields without relying on structure padding.
 * @param model Borrowed live model.
 * @return Nonzero for the exact pinned pilot shape. */
int npc_policy_compatible(const cgai_gameplay_model *model) {
    const cgai_gameplay_config expected = npc_policy_config();
    cgai_gameplay_config actual = {0};
    return cgai_gameplay_get_config(model, &actual) && actual.seed == expected.seed &&
           actual.routing_temperature == expected.routing_temperature &&
           actual.feature_count == expected.feature_count &&
           actual.embedding_dimensions == expected.embedding_dimensions &&
           actual.hidden_dimensions == expected.hidden_dimensions &&
           actual.module_count == expected.module_count &&
           actual.centroids_per_module == expected.centroids_per_module &&
           actual.task_count == expected.task_count &&
           memcmp(actual.cardinalities, expected.cardinalities, sizeof(actual.cardinalities)) ==
               0 &&
           memcmp(actual.output_counts, expected.output_counts, sizeof(actual.output_counts)) ==
               0 &&
           memcmp(actual.task_modules, expected.task_modules, sizeof(actual.task_modules)) == 0 &&
           memcmp(actual.task_features, expected.task_features, sizeof(actual.task_features)) == 0;
}

/** @brief Count neural allocations and the persistent history adapter.
 * @param model Borrowed compatible model.
 * @param resources Writable numerical report.
 * @param session_bytes Writable complete adapter-plus-scratch requested heap.
 * @return OK after validation, ERROR otherwise. */
cgai_status npc_policy_resources(const cgai_gameplay_model *model,
                                 cgai_gameplay_resources *resources, size_t *session_bytes) {
    cgai_gameplay_resources inspected;
    if (resources == NULL || session_bytes == NULL || !npc_policy_compatible(model) ||
        !cgai_gameplay_get_resources(model, &inspected))
        return cgai_fail("NPC resources require a compatible complete model");
    if (inspected.session_bytes > NPC_SESSION_LIMIT - sizeof(npc_policy_session))
        return cgai_fail("NPC persistent adapter and scratch exceed the session budget");
    *session_bytes = inspected.session_bytes + sizeof(npc_policy_session);
    *resources = inspected;
    return CGAI_STATUS_OK;
}

/** @brief Require bounded deployment ownership before allocating an NPC adapter.
 * @param model Borrowed compatible inference model.
 * @return OK only for optimizer-free weights within both requested heap budgets. */
static cgai_status inference_resources(const cgai_gameplay_model *model) {
    cgai_gameplay_resources resources;
    size_t bytes = 0U;
    if (!npc_policy_resources(model, &resources, &bytes))
        return CGAI_STATUS_ERROR;
    return resources.optimizer_bytes == 0U && resources.model_bytes <= NPC_MODEL_LIMIT
               ? CGAI_STATUS_OK
               : cgai_fail(
                     "NPC sessions require bounded inference weights without optimizer state");
}

/** @brief Allocate a capped adapter before scheduling policy requests.
 * @param model Borrowed compatible immutable model.
 * @param history Nonzero enables explicit observed memory.
 * @return Owned independent adapter or NULL. */
npc_policy_session *npc_policy_session_create(const cgai_gameplay_model *model, int history) {
    if (!inference_resources(model))
        return NULL;
    npc_policy_session *session = calloc(1U, sizeof(*session));
    if (session == NULL) {
        (void)cgai_fail("could not allocate NPC policy adapter");
        return NULL;
    }
    session->neural = cgai_gameplay_session_create(model, NPC_SESSION_LIMIT - sizeof(*session));
    if (session->neural == NULL) {
        free(session);
        return NULL;
    }
    session->history = history != 0;
    npc_memory_reset(&session->memory);
    return session;
}

/** @brief Reset only the adapter's observed information between episodes.
 * @param session Borrowed live adapter or NULL. */
void npc_policy_session_reset(npc_policy_session *session) {
    if (session != NULL)
        npc_memory_reset(&session->memory);
}

/** @brief Release neural scratch and its persistent adapter shell.
 * @param session Owned adapter or NULL. */
void npc_policy_session_destroy(npc_policy_session *session) {
    if (session != NULL) {
        cgai_gameplay_session_destroy(session->neural);
        free(session);
    }
}

/** @brief Validate every public observation domain before changing session history.
 * @param observation Borrowed candidate visible observation.
 * @return Nonzero only for complete bounded visible domains and action permissions. */
static int observation_valid(const npc_observation *observation) {
    if (observation == NULL || observation->target_dx < -8 || observation->target_dx > 8 ||
        observation->target_dy < -8 || observation->target_dy > 8 || observation->inventory > 1U ||
        observation->cue > 4U || observation->last_action > 6U || observation->last_outcome > 7U ||
        observation->stage > 3U || observation->at_target > 1U ||
        observation->ticks > NPC_MAX_TICKS || observation->ticks_remaining > NPC_MAX_TICKS ||
        observation->mechanic > 2U || (observation->allowed_actions & UINT64_C(3)) != 3U ||
        (observation->allowed_actions & ~UINT64_C(127)) != 0U)
        return 0;
    for (size_t direction = 0U; direction < 4U; ++direction)
        if (observation->neighbors[direction] > 5U)
            return 0;
    return 1;
}

/** @brief Execute the complete bounded observable-history inference path.
 * @param session Exclusive live adapter.
 * @param observation Borrowed current visible observation.
 * @param modules Eligible low two module bits or zero for forced fallback.
 * @param result Writable complete validated proposal.
 * @return OK on publication, ERROR otherwise. */
cgai_status npc_policy_decide(npc_policy_session *session, const npc_observation *observation,
                              uint64_t modules, cgai_gameplay_result *result) {
    if (session == NULL || !observation_valid(observation) || result == NULL ||
        (modules & ~UINT64_C(3)) != 0U)
        return cgai_fail("invalid NPC policy observation or module permission");
    npc_memory updated = session->memory;
    npc_memory_observe(&updated, observation);
    cgai_gameplay_request request = {0};
    npc_encode(observation, &updated, session->history, &request.state);
    request.contract_version = CGAI_GAMEPLAY_CONTRACT_VERSION;
    request.allowed_outputs = npc_observation_actions(observation);
    request.allowed_modules = modules;
    cgai_gameplay_result selected;
    if (!cgai_gameplay_select(session->neural, &request, &selected))
        return CGAI_STATUS_ERROR;
    if (selected.output > 6U ||
        (selected.output != 0U &&
         (request.allowed_outputs & (UINT64_C(1) << selected.output)) == 0U) ||
        selected.abstained != (selected.output == 0U))
        return cgai_fail("NPC neural proposal violates the host action contract");
    session->memory = updated;
    *result = selected;
    return CGAI_STATUS_OK;
}
