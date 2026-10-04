/** @file test_npc_policy.c @brief NPC adapter isolation, record fixtures and runtime budgets.
 */
#include "gameplay/gameplay_internal.h"
#include "npc_records.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Explicit owned optimizer snapshot fixture. */
static const char second_path[] = "npc-policy-second.cgcheckpoint";
/** Inference-only test artifact. */
static const char model_path[] = "npc-policy-model.cggp";

/** @brief Construct one complete visible observation from pinned initial conditions.
 * @param family_index Development-family local index.
 * @param variant Family-local variant.
 * @return Complete public observation. */
static npc_observation visible(uint32_t family_index, uint32_t variant) {
    npc_family family;
    npc_world world;
    npc_observation observation;
    TEST_CHECK(npc_family_get(NPC_DEV, family_index, &family), "missing development family");
    TEST_CHECK(npc_world_init(&world, &family, variant), "invalid pinned world");
    npc_world_observe(&world, &observation);
    return observation;
}

/** @brief Verify all shape categories match the visible encoder and both memory budgets. */
static void shape_and_resources(void) {
    const cgai_gameplay_config config = npc_policy_config();
    uint32_t cards[NPC_FEATURE_COUNT];
    npc_cardinalities(cards);
    TEST_CHECK(memcmp(cards, config.cardinalities, sizeof(cards)) == 0,
               "policy and observable adapter categorical domains diverged");
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    cgai_gameplay_resources resources;
    size_t bytes = 0U;
    TEST_CHECK(model != NULL && npc_policy_resources(model, &resources, &bytes), cgai_last_error());
    TEST_CHECK(resources.model_bytes <= NPC_MODEL_LIMIT && bytes <= NPC_SESSION_LIMIT,
               "frozen NPC shape exceeds its bounded resource contract");
    TEST_CHECK(bytes == resources.session_bytes + sizeof(npc_policy_session),
               "persistent observed-history adapter was omitted from the per-NPC heap");
    cgai_gameplay_destroy(model);
}

/** @brief Require a failed public observation leaves both output and observed memory unchanged.
 * @param session Borrowed adapter with previously observed history.
 * @param observation Deliberately malformed public input. */
static void rejected_observation(npc_policy_session *session, npc_observation observation) {
    cgai_gameplay_result sentinel;
    memset(&sentinel, 0x3c, sizeof(sentinel));
    cgai_gameplay_result result = sentinel;
    const npc_memory memory = session->memory;
    TEST_CHECK(!npc_policy_decide(session, &observation, 3U, &result),
               "malformed NPC visible input was admitted");
    TEST_CHECK(memcmp(&result, &sentinel, sizeof(result)) == 0 &&
                   memcmp(&session->memory, &memory, sizeof(memory)) == 0,
               "failed NPC selection changed output or persistent history");
}

/** @brief Exercise independent cue histories, forced fallback and reset semantics. */
static void adapter_isolation(void) {
    const cgai_gameplay_config config = npc_policy_config();
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    npc_policy_session *first = npc_policy_session_create(model, 1);
    npc_policy_session *second = npc_policy_session_create(model, 1);
    npc_observation left = visible(2U, 0U);
    npc_observation right = visible(2U, 4U);
    cgai_gameplay_result result;
    TEST_CHECK(first != NULL && second != NULL, cgai_last_error());
    TEST_CHECK(npc_policy_decide(first, &left, 0U, &result) && result.output == NPC_FALLBACK &&
                   result.forward_passes == 0U,
               "forced fallback performed a neural forward");
    TEST_CHECK(npc_policy_decide(second, &right, 0U, &result) &&
                   first->memory.cue != second->memory.cue,
               "independent NPC adapters share route-cue memory");
    left.target_dx = 9;
    rejected_observation(first, left);
    left = visible(2U, 0U);
    left.allowed_actions |= UINT64_C(128);
    rejected_observation(first, left);
    npc_policy_session_reset(first);
    TEST_CHECK(first->memory.initialized == 0U && first->memory.cue == 0U &&
                   second->memory.cue != 0U,
               "NPC reset erased another actor's observed history");
    npc_policy_session_destroy(first);
    npc_policy_session_destroy(second);
    cgai_gameplay_destroy(model);
}

/** @brief Require history profiles preserve identical labels and record order. */
static void corpus_history_ablation(void) {
    cgai_gameplay_example *history = calloc(NPC_RECORD_CAPACITY, sizeof(*history));
    cgai_gameplay_example *memoryless = calloc(NPC_RECORD_CAPACITY, sizeof(*memoryless));
    size_t history_count = 0U;
    size_t memoryless_count = 0U;
    TEST_CHECK(history != NULL && memoryless != NULL, "could not allocate bounded test corpus");
    TEST_CHECK(npc_records_collect(history, NPC_RECORD_CAPACITY, &history_count, 1) &&
                   npc_records_collect(memoryless, NPC_RECORD_CAPACITY, &memoryless_count, 0),
               cgai_last_error());
    TEST_CHECK(history_count == memoryless_count && history_count <= NPC_RECORD_CAPACITY,
               "history comparison changed its actual record update budget");
    for (size_t index = 0U; index < history_count; ++index) {
        for (size_t field = 8U; field < 12U; ++field)
            history[index].state.values[field] = 0U;
        TEST_CHECK(memcmp(&history[index], &memoryless[index], sizeof(history[index])) == 0,
                   "history-disabled recipe changed non-history inputs, labels or record order");
    }
    free(history);
    free(memoryless);
}

/** @brief Reject continuation ownership as a deployed NPC inference session. */
static void optimizer_checkpoint_rejected(void) {
    cgai_gameplay_model *checkpoint = cgai_gameplay_checkpoint_load(second_path);
    TEST_CHECK(checkpoint != NULL, cgai_last_error());
    TEST_CHECK(npc_policy_session_create(checkpoint, 1) == NULL,
               "NPC inference session admitted resident optimizer state over its model budget");
    cgai_gameplay_destroy(checkpoint);
}

/** @brief Construct owned moments directly to test runtime memory admission. */
static cgai_gameplay_model *checkpoint_fixture(void) {
    const cgai_gameplay_config config = npc_policy_config();
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    const size_t count = model->parameter_count;
    TEST_CHECK(count > 0U && count <= CGAI_GAMEPLAY_MAX_PARAMETERS,
               "invalid NPC fixture parameter capacity");
    model->adam_first = calloc(count, sizeof(*model->adam_first));
    model->adam_second = calloc(count, sizeof(*model->adam_second));
    TEST_CHECK(model->adam_first != NULL && model->adam_second != NULL,
               "could not allocate NPC fixture moments");
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, second_path), cgai_last_error());
    return model;
}

/** @brief Verify inference export excludes resident optimizer ownership. */
static void runtime_artifact_roundtrip(void) {
    cgai_gameplay_model *checkpoint = checkpoint_fixture();
    optimizer_checkpoint_rejected();
    TEST_CHECK(cgai_gameplay_save(checkpoint, model_path), cgai_last_error());
    cgai_gameplay_destroy(checkpoint);
    cgai_gameplay_model *model = cgai_gameplay_load(model_path);
    cgai_gameplay_resources resources;
    size_t bytes = 0U;
    TEST_CHECK(model != NULL && npc_policy_resources(model, &resources, &bytes) &&
                   resources.optimizer_bytes == 0U,
               "deployed NPC weights retain optimizer state");
    cgai_gameplay_destroy(model);
}

/** @brief Run adapter, record-collection and inference-ownership checks.
 * @return Zero after every invariant passes. */
int main(void) {
    shape_and_resources();
    adapter_isolation();
    corpus_history_ablation();
    runtime_artifact_roundtrip();
    (void)remove(second_path);
    (void)remove(model_path);
    return 0;
}
