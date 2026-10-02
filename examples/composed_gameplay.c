/** @file composed_gameplay.c @brief Seeded host validates bark and intent from one composed bundle.
 */
#include "centroid_gai_gameplay.h"
#include "gameplay_tasks.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Require the authored task domains before resolving host catalogs or controllers.
 * @param model Borrowed loaded inference bundle.
 * @return Nonzero for the exact demonstrated shape and category/head contract. */
static int host_compatible(const cgai_gameplay_model *model) {
    /* Step1: Shape and domain validation prevents unrelated valid bundles reaching host tables. */
    cgai_gameplay_config actual = {0};
    const cgai_gameplay_config expected = gameplay_fixture_config();
    if (!cgai_gameplay_get_config(model, &actual))
        return 0;
    return actual.feature_count == expected.feature_count &&
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

/** @brief Advance a demo-local seeded observation stream without global state.
 * @param state Borrowed mutable unsigned state.
 * @return Next deterministic unsigned word. */
static uint64_t host_random(uint64_t *state) {
    /* Step1: Unsigned wraparound provides portable seeded sample selection. */
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state;
}

/** @brief Parse one unsigned seed or bounded simulation step count.
 * @param text Borrowed command token.
 * @param value Writable unsigned scalar.
 * @return Nonzero for a complete unsigned decimal token. */
static int host_number(const char *text, uint64_t *value) {
    /* Step1: Validate syntax before any artifact or scene changes. */
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    char *end = NULL;
    const unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > UINT64_MAX)
        return 0;
    *value = (uint64_t)parsed;
    return 1;
}

/** @brief Restrict authored bark IDs to the host's current event family.
 * @param state Borrowed validated observation.
 * @return Spoken-ID mask; the runtime always permits fallback zero. */
static uint64_t host_barks(const cgai_gameplay_state *state) {
    /* Step1: The host's legal content domain is independent from the neural preference. */
    const uint64_t masks[] = {0U, 14U, 48U, 64U, 128U, 256U};
    return masks[state->values[GAMEPLAY_EVENT]];
}

/** @brief Admit only intent IDs whose preconditions the host controller can execute.
 * @param state Borrowed validated observation.
 * @return Task-local legal intent bits. */
static uint64_t host_intents(const cgai_gameplay_state *state) {
    /* Step1: Idle hosts perform no controller work; unavailable actors may only wait. */
    if (state->values[GAMEPLAY_EVENT] == 0U)
        return 1U;
    if (state->values[GAMEPLAY_READINESS] == 0U)
        return UINT64_C(3);
    uint64_t mask = UINT64_C(15) | (UINT64_C(1) << 6U);
    /* Step2: Cover and engagement are admitted only when their observed preconditions hold. */
    if (state->values[GAMEPLAY_DANGER] != 0U && state->values[GAMEPLAY_COVER] != 0U)
        mask |= UINT64_C(1) << 4U;
    if (state->values[GAMEPLAY_DANGER] != 0U && state->values[GAMEPLAY_HEALTH] != 0U &&
        state->values[GAMEPLAY_DISTANCE] == 0U)
        mask |= UINT64_C(1) << 5U;
    return mask;
}

/** @brief Prepare a host-validated request for one head in the shared bundle.
 * @param model Borrowed live immutable bundle.
 * @param state Borrowed complete state.
 * @param task Requested task.
 * @param recent Last spoken bark ID; zero for no suppression.
 * @param request Writable query.
 * @return Nonzero on complete query preparation. */
static int host_request(const cgai_gameplay_model *model, const cgai_gameplay_state *state,
                        uint32_t task, uint32_t recent, cgai_gameplay_request *request) {
    /* Step1: Bind explicit task-local permissions without textual prompt construction. */
    if (!cgai_gameplay_default_request(model, task, state, request))
        return 0;
    request->allowed_outputs = task == GAMEPLAY_TASK_BARK ? host_barks(state) : host_intents(state);
    request->recent_output = task == GAMEPLAY_TASK_BARK ? recent : 0U;
    return 1;
}

/** @brief Resolve one legal neural proposal and print its actual module contributions.
 * @param session Borrowed exclusive scratch.
 * @param request Borrowed validated typed request.
 * @param result Writable complete proposal.
 * @return Nonzero on successful legal selection. */
static int host_select(cgai_gameplay_session *session, const cgai_gameplay_request *request,
                       cgai_gameplay_result *result) {
    /* Step1: One composed forward uses learned routing across both banks for this task head. */
    if (!cgai_gameplay_select(session, request, result))
        return 0;
    const char *line = gameplay_output_text(request->task, result->output);
    printf("  %s=%s passes=%zu modules=%.3f/%.3f%s%s\n", gameplay_task_get(request->task)->name,
           gameplay_output_name(request->task, result->output), result->forward_passes,
           result->module_contributions[0], result->module_contributions[1],
           line != NULL ? " line=" : "", line != NULL ? line : "");
    return 1;
}

/** @brief Feed a seeded scenario through both heads and validate controller effects.
 * @param model Borrowed immutable composed bundle.
 * @param session Borrowed exclusive numerical scratch.
 * @param state Borrowed complete observation.
 * @param recent Borrowed mutable last spoken bark ID.
 * @return Nonzero when all proposals and host simulation checks succeed. */
static int host_tick(const cgai_gameplay_model *model, cgai_gameplay_session *session,
                     const cgai_gameplay_state *state, uint32_t *recent) {
    /* Step1: Both typed domains share the same bundle and reusable session allocation. */
    cgai_gameplay_request bark = {0}, intent = {0};
    cgai_gameplay_result line = {0}, action = {0};
    if (!host_request(model, state, GAMEPLAY_TASK_BARK, *recent, &bark) ||
        !host_request(model, state, GAMEPLAY_TASK_INTENT, 0U, &intent) ||
        !host_select(session, &bark, &line) || !host_select(session, &intent, &action))
        return 0;
    if (!line.abstained)
        *recent = line.output;
    /* Step2: The host controller checks preconditions and applies bounded authored transitions. */
    gameplay_simulation_result applied = {0};
    if (!gameplay_fixture_simulate(state, action.output, &applied) || !applied.legal ||
        !applied.survived || !applied.objective_success)
        return 0;
    printf("  controller health=%u distance=%u survived=%d objective=%d\n", applied.health_after,
           applied.distance_after, applied.survived, applied.objective_success);
    return 1;
}

/** @brief Run bounded deterministic scene observations outside request timing.
 * @param model Borrowed immutable live bundle.
 * @param session Borrowed exclusive scratch.
 * @param seed Local deterministic observation stream seed.
 * @param steps Number of host scene ticks,1..64.
 * @return Nonzero on every validated tick. */
static int host_run(const cgai_gameplay_model *model, cgai_gameplay_session *session, uint64_t seed,
                    uint64_t steps) {
    /* Step1: Each abstract scene is constructed from a bounded authored intent observation. */
    uint32_t recent = 0U;
    for (uint64_t tick = 0U; tick < steps; ++tick) {
        gameplay_fixture_case scenario = {0};
        const size_t sample = 72U + (size_t)((host_random(&seed) >> 32U) % 256U);
        if (!gameplay_fixture_get(sample, &scenario))
            return 0;
        printf(
            "tick=%llu event=%s danger=%s\n", (unsigned long long)tick,
            gameplay_feature_value(GAMEPLAY_EVENT, scenario.example.state.values[GAMEPLAY_EVENT]),
            gameplay_feature_value(GAMEPLAY_DANGER,
                                   scenario.example.state.values[GAMEPLAY_DANGER]));
        if (!host_tick(model, session, &scenario.example.state, &recent))
            return 0;
    }
    return 1;
}

/** @brief Load a capped bundle and run a seeded host demonstration of both specialist heads.
 * @param argc Model, optional decimal seed and optional step count.
 * @param argv Borrowed command arguments.
 * @return Zero on success, two for usage, one for artifact/runtime/host failure. */
int main(int argc, char **argv) {
    /* Step1: Parse bounded host options before loading any model. */
    uint64_t seed = 42U, steps = 8U;
    if (argc < 2 || argc > 4 || (argc >= 3 && !host_number(argv[2], &seed)) ||
        (argc == 4 && (!host_number(argv[3], &steps) || steps == 0U || steps > 64U))) {
        fprintf(stderr, "usage: cgai_composed_demo MODEL [SEED] [STEPS]\n");
        return 2;
    }
    cgai_gameplay_model *model = cgai_gameplay_load(argv[1]);
    cgai_gameplay_resources resources = {0};
    if (model == NULL || !host_compatible(model) ||
        !cgai_gameplay_get_resources(model, &resources) || resources.model_bytes > 262144U) {
        cgai_gameplay_destroy(model);
        fprintf(stderr, "compatible bounded gameplay bundle required: %s\n", cgai_last_error());
        return 1;
    }
    /* Step2: Prepare reusable capped scratch before issuing any typed gameplay requests. */
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, 65536U);
    const int passed = session != NULL && host_run(model, session, seed, steps);
    cgai_gameplay_session_destroy(session);
    cgai_gameplay_destroy(model);
    return passed ? 0 : 1;
}
