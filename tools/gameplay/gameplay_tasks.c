/** @file gameplay_tasks.c @brief Stable task descriptors, family splits and authored transitions.
 */
#include "gameplay_tasks.h"
#include <string.h>

/** Bounded cardinalities bind every shared field to its authored categories. */
static const uint32_t cardinalities[] = {6U, 2U, 3U, 2U, 2U, 2U, 2U, 2U, 2U};
/** Stable task registry; independent states remain separate from the balanced recipe. */
static const gameplay_task_descriptor tasks[] = {{"bark", 0U, 72U, {48U, 12U, 12U}, 9U},
                                                 {"intent", 72U, 256U, {192U, 32U, 32U}, 7U}};
/** Stable shared feature names. */
static const char *const feature_names[] = {"event",   "danger",    "relationship",
                                            "setting", "health",    "distance",
                                            "cover",   "objective", "readiness"};
/** Categories in exact source-contract order. */
static const char *const feature_values[9][6] = {
    {"idle", "greet", "threat", "victory", "discovery", "retreat"},
    {"low", "high"},
    {"friendly", "neutral", "hostile"},
    {"indoor", "outdoor"},
    {"critical", "ready"},
    {"near", "far"},
    {"absent", "present"},
    {"hold", "advance"},
    {"unavailable", "ready"}};
/** Task-local output names, always including fallback zero. */
static const char *const output_names[2][9] = {
    {"abstain", "greetwarm", "greetplain", "warnhostile", "threatcalm", "threaturgent", "victory",
     "discover", "retreat"},
    {"abstain", "wait", "hold", "approach", "seekcover", "engage", "retreat"}};
/** Authored spoken lines; intent IDs propose host actions instead. */
static const char *const bark_lines[] = {
    NULL,          "Good to see you.", "Hello there.",        "Keep your distance.", "Stay alert.",
    "Take cover!", "We made it.",      "What have we found?", "Fall back!"};

/** @brief Validate all used categories and reject nonzero trailing observations.
 * @param state Borrowed complete shared state.
 * @return Nonzero only for the exact version-one category domain. */
static int valid_state(const cgai_gameplay_state *state) {
    /* Step 1: Bound every category before teacher or simulator indexing. */
    if (state == NULL)
        return 0;
    for (size_t i = 0U; i < 9U; ++i)
        if (state->values[i] >= cardinalities[i])
            return 0;
    /* Step 2: Require the same canonical trailing-state representation as the runtime. */
    for (size_t i = 9U; i < CGAI_GAMEPLAY_MAX_FEATURES; ++i)
        if (state->values[i] != 0U)
            return 0;
    return 1;
}

/** @brief Return the complete fixed aligned shape.
 * @return Initialized version-one two-task configuration. */
cgai_gameplay_config gameplay_fixture_config(void) {
    /* Step 1: Initialize every unused bounded shape entry to its canonical zero. */
    cgai_gameplay_config config = {0};
    config.seed = 42U;
    config.routing_temperature = 4.0;
    config.feature_count = 9U;
    config.embedding_dimensions = 32U;
    config.hidden_dimensions = 64U;
    config.module_count = 2U;
    config.centroids_per_module = 16U;
    config.task_count = 2U;
    /* Step 2: Align both eligible specialist banks and declare task-relevant input slots. */
    memcpy(config.cardinalities, cardinalities, sizeof(cardinalities));
    config.output_counts[0] = 9U;
    config.output_counts[1] = 7U;
    config.task_modules[0] = 3U;
    config.task_modules[1] = 3U;
    config.task_features[0] = 15U;
    config.task_features[1] = 499U;
    return config;
}

/** @brief Resolve a bounded authored task.
 * @param task Supported task ID.
 * @return Borrowed descriptor or NULL. */
const gameplay_task_descriptor *gameplay_task_get(uint32_t task) {
    /* Step 1: Stable head IDs directly index the frozen descriptor registry. */
    return task < GAMEPLAY_TASK_COUNT ? &tasks[task] : NULL;
}

/** @brief Resolve a bounded specialist identity.
 * @param module Supported module ID.
 * @return Borrowed static name or NULL. */
const char *gameplay_module_name(uint32_t module) {
    /* Step 1: Both identities are metadata; eligibility is explicit in the shape. */
    static const char *const names[] = {"social", "tactical"};
    return module < GAMEPLAY_MODULE_COUNT ? names[module] : NULL;
}

/** @brief Resolve the contract-ordered feature name.
 * @param feature Shared feature index.
 * @return Borrowed name or NULL. */
const char *gameplay_feature_name(uint32_t feature) {
    /* Step 1: The feature domain is fixed by the authored profile. */
    return feature < 9U ? feature_names[feature] : NULL;
}

/** @brief Resolve a category's stable source spelling.
 * @param feature Shared feature index.
 * @param value Category index.
 * @return Borrowed category name or NULL. */
const char *gameplay_feature_value(uint32_t feature, uint32_t value) {
    /* Step 1: Validate both indices before looking up the table. */
    return feature < 9U && value < cardinalities[feature] ? feature_values[feature][value] : NULL;
}

/** @brief Resolve a task-local catalog identifier.
 * @param task Supported task ID.
 * @param output Task-local output ID.
 * @return Borrowed output name or NULL. */
const char *gameplay_output_name(uint32_t task, uint32_t output) {
    /* Step 1: Each task retains its own distinct output domain. */
    const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
    return descriptor != NULL && output < descriptor->output_count ? output_names[task][output]
                                                                   : NULL;
}

/** @brief Resolve only authored spoken bark content.
 * @param task Supported task ID.
 * @param output Task-local output ID.
 * @return Borrowed line or NULL for fallback, intents and invalid IDs. */
const char *gameplay_output_text(uint32_t task, uint32_t output) {
    /* Step 1: Gameplay intent execution remains a host responsibility. */
    return task == GAMEPLAY_TASK_BARK && output < 9U ? bark_lines[output] : NULL;
}

/** @brief Resolve a frozen split's source spelling.
 * @param split Requested split.
 * @return Borrowed spelling or NULL. */
const char *gameplay_split_name(gameplay_split split) {
    /* Step 1: Stable indices bind scenario files to quality report fields. */
    static const char *const names[] = {"training", "development", "test"};
    return (unsigned)split < 3U ? names[split] : NULL;
}

/** @brief Count one task's independent frozen scenarios.
 * @param task Supported task head.
 * @param split Supported split.
 * @return Frozen count or zero. */
size_t gameplay_fixture_count(uint32_t task, gameplay_split split) {
    /* Step 1: Balanced training repetition never changes independent split counts. */
    const gameplay_task_descriptor *descriptor = gameplay_task_get(task);
    return descriptor != NULL && (unsigned)split < 3U ? descriptor->split_counts[split] : 0U;
}

/** @brief Apply the original authored bark teacher with unchanged IDs.
 * @param state Validated shared observations.
 * @return Frozen bark target. */
static uint32_t bark_teacher(const cgai_gameplay_state *state) {
    /* Step 1: Only the original four categorical slots are relevant to this head. */
    static const uint32_t event_ids[] = {0U, 2U, 4U, 6U, 7U, 8U};
    if (state->values[GAMEPLAY_EVENT] == 1U)
        return 1U + state->values[GAMEPLAY_RELATIONSHIP];
    if (state->values[GAMEPLAY_EVENT] == 2U && state->values[GAMEPLAY_DANGER] == 1U)
        return 5U;
    return event_ids[state->values[GAMEPLAY_EVENT]];
}

/** @brief Apply the bounded authored tactical priority rules.
 * @param state Validated shared observations.
 * @return Task-local intent target. */
static uint32_t intent_teacher(const cgai_gameplay_state *state) {
    /* Step 1: Idle has no proposed action; unavailable agents defer to their host. */
    if (state->values[GAMEPLAY_EVENT] == 0U)
        return 0U;
    if (state->values[GAMEPLAY_READINESS] == 0U)
        return 1U;
    /* Step 2: Apply hazard priorities before objective progress. */
    if (state->values[GAMEPLAY_DANGER] == 1U && state->values[GAMEPLAY_HEALTH] == 0U)
        return 6U;
    if (state->values[GAMEPLAY_DANGER] == 1U && state->values[GAMEPLAY_COVER] == 1U)
        return 4U;
    if (state->values[GAMEPLAY_DANGER] == 1U && state->values[GAMEPLAY_DISTANCE] == 0U)
        return 5U;
    return state->values[GAMEPLAY_OBJECTIVE] == 1U ? 3U : 2U;
}

/** @brief Resolve one validated independent teacher target.
 * @param task Supported task ID.
 * @param state Complete bounded shared observation.
 * @return Task-local target or UINT32_MAX. */
uint32_t gameplay_fixture_teacher(uint32_t task, const cgai_gameplay_state *state) {
    /* Step 1: Reject invalid state before applying any task-specific rule. */
    if (!valid_state(state) || task >= GAMEPLAY_TASK_COUNT)
        return UINT32_MAX;
    return task == GAMEPLAY_TASK_BARK ? bark_teacher(state) : intent_teacher(state);
}

/** @brief Assign remainder zero to development, one to test and all others to training.
 * @param remainder Authored family partition key.
 * @return Frozen split. */
static gameplay_split family_split(size_t remainder) {
    /* Step 1: Settings remain in the same split because only the family selects membership. */
    return remainder == 0U ? GAMEPLAY_DEVELOPMENT
                           : (remainder == 1U ? GAMEPLAY_TEST : GAMEPLAY_TRAINING);
}

/** @brief Decode one original bark state and canonical irrelevant observations.
 * @param index Bark-local scenario index.
 * @param scenario Private zero-initialized result. */
static void bark_case(size_t index, gameplay_fixture_case *scenario) {
    /* Step 1: Preserve all original seventy-two states and their family-level partitions. */
    scenario->family_id = index / 2U;
    scenario->split = family_split(scenario->family_id % 6U);
    scenario->example.task = GAMEPLAY_TASK_BARK;
    scenario->example.state.values[GAMEPLAY_EVENT] = (uint32_t)(scenario->family_id / 6U);
    scenario->example.state.values[GAMEPLAY_DANGER] = (uint32_t)((scenario->family_id / 3U) % 2U);
    scenario->example.state.values[GAMEPLAY_RELATIONSHIP] = (uint32_t)(scenario->family_id % 3U);
    scenario->example.state.values[GAMEPLAY_SETTING] = (uint32_t)(index % 2U);
    /* Step 2: Canonical tactical observations do not affect the bark head's masked encoder. */
    scenario->example.state.values[GAMEPLAY_HEALTH] = 1U;
    scenario->example.state.values[GAMEPLAY_DISTANCE] = 1U;
    scenario->example.state.values[GAMEPLAY_READINESS] = 1U;
}

/** @brief Decode one tactical family and its setting variant.
 * @param index Intent-local scenario index.
 * @param scenario Private zero-initialized result. */
static void intent_case(size_t index, gameplay_fixture_case *scenario) {
    /* Step 1: Event and six binary categories define one128-family domain. */
    static const uint32_t binary_features[] = {GAMEPLAY_DANGER,    GAMEPLAY_HEALTH,
                                               GAMEPLAY_DISTANCE,  GAMEPLAY_COVER,
                                               GAMEPLAY_OBJECTIVE, GAMEPLAY_READINESS};
    scenario->family_id = index / 2U;
    const size_t bits = scenario->family_id % 64U;
    scenario->split = family_split((bits % 8U) ^ (bits / 8U));
    scenario->example.task = GAMEPLAY_TASK_INTENT;
    scenario->example.state.values[GAMEPLAY_EVENT] = scenario->family_id < 64U ? 0U : 2U;
    scenario->example.state.values[GAMEPLAY_RELATIONSHIP] = 1U;
    scenario->example.state.values[GAMEPLAY_SETTING] = (uint32_t)(index % 2U);
    /* Step 2: Enumerate binary fields least-significant-bit first in contract order. */
    for (size_t i = 0U; i < 6U; ++i)
        scenario->example.state.values[binary_features[i]] = (uint32_t)((bits >> i) & 1U);
}

/** @brief Construct one independent frozen scenario.
 * @param index Stable global index.
 * @param scenario Writable caller-owned result.
 * @return OK on publication or ERROR with unchanged result. */
cgai_status gameplay_fixture_get(size_t index, gameplay_fixture_case *scenario) {
    /* Step 1: Decode into private state before publishing any caller-visible value. */
    if (index >= GAMEPLAY_FIXTURE_CASE_COUNT || scenario == NULL)
        return CGAI_STATUS_ERROR;
    gameplay_fixture_case value = {0};
    value.id = index;
    if (index < 72U)
        bark_case(index, &value);
    else
        intent_case(index - 72U, &value);
    /* Step 2: Resolve the frozen teacher after complete category construction. */
    value.example.target = gameplay_fixture_teacher(value.example.task, &value.example.state);
    if (value.example.target == UINT32_MAX)
        return CGAI_STATUS_ERROR;
    *scenario = value;
    return CGAI_STATUS_OK;
}

/** @brief Construct one complete unrestricted host query from a validated fixture.
 * @param scenario Borrowed complete fixture.
 * @param request Writable caller-owned query.
 * @return OK on publication, ERROR otherwise. */
cgai_status gameplay_fixture_request(const gameplay_fixture_case *scenario,
                                     cgai_gameplay_request *request) {
    /* Step 1: Bound the task and categories before shifting its output count. */
    if (scenario == NULL || request == NULL || !valid_state(&scenario->example.state))
        return CGAI_STATUS_ERROR;
    const gameplay_task_descriptor *task = gameplay_task_get(scenario->example.task);
    if (task == NULL || scenario->example.target >= task->output_count)
        return CGAI_STATUS_ERROR;
    /* Step 2: Admit both aligned specialist banks and every task-local output. */
    const cgai_gameplay_request value = {.state = scenario->example.state,
                                         .allowed_outputs =
                                             (UINT64_C(1) << task->output_count) - 1U,
                                         .allowed_modules = 3U,
                                         .contract_version = CGAI_GAMEPLAY_CONTRACT_VERSION,
                                         .task = scenario->example.task,
                                         .recent_output = 0U};
    *request = value;
    return CGAI_STATUS_OK;
}

/** @brief Append one task's training split in frozen enumeration order.
 * @param task Task head index.
 * @param examples Writable adequately sized caller array.
 * @param count Writable running record count.
 * @return OK after complete task records or ERROR. */
static cgai_status append_training(uint32_t task, cgai_gameplay_example *examples, size_t *count) {
    /* Step 1: Fixture construction is total over this fixed internal descriptor domain. */
    const gameplay_task_descriptor *descriptor = &tasks[task];
    for (size_t i = 0U; i < descriptor->fixture_count; ++i) {
        gameplay_fixture_case scenario;
        if (!gameplay_fixture_get(descriptor->fixture_offset + i, &scenario))
            return CGAI_STATUS_ERROR;
        if (scenario.split == GAMEPLAY_TRAINING)
            examples[(*count)++] = scenario.example;
    }
    return CGAI_STATUS_OK;
}

/** @brief Fill the explicit balanced independent training schedule.
 * @param examples Caller-owned array.
 * @param capacity Available record slots.
 * @param count Writable successful record count.
 * @return OK after384 records or ERROR on invalid arguments. */
cgai_status gameplay_fixture_training(cgai_gameplay_example *examples, size_t capacity,
                                      size_t *count) {
    /* Step 1: Validate all external bounds before writing any training record. */
    if (examples == NULL || count == NULL || capacity < GAMEPLAY_TRAINING_COUNT)
        return CGAI_STATUS_ERROR;
    size_t written = 0U;
    /* Step 2: Four bark cycles and one intent cycle provide192 records for each head. */
    for (size_t cycle = 0U; cycle < 4U; ++cycle)
        if (!append_training(GAMEPLAY_TASK_BARK, examples, &written))
            return CGAI_STATUS_ERROR;
    if (!append_training(GAMEPLAY_TASK_INTENT, examples, &written))
        return CGAI_STATUS_ERROR;
    *count = written;
    return written == GAMEPLAY_TRAINING_COUNT ? CGAI_STATUS_OK : CGAI_STATUS_ERROR;
}

/** @brief Expand only explicitly irrelevant bark fields into a bounded context variant.
 * @param state Caller-owned complete bounded state.
 * @param variant Five-bit variant.
 * @return OK on mutation or ERROR before any mutation. */
cgai_status gameplay_fixture_bark_context(cgai_gameplay_state *state, uint32_t variant) {
    /* Step 1: Validate the entire shared observation even for task-omitted input slots. */
    if (!valid_state(state) || variant >= 32U)
        return CGAI_STATUS_ERROR;
    for (size_t i = GAMEPLAY_HEALTH; i <= GAMEPLAY_READINESS; ++i)
        state->values[i] = (variant >> (i - GAMEPLAY_HEALTH)) & 1U;
    return CGAI_STATUS_OK;
}

/** @brief Check action preconditions independently of the teacher's priority ordering.
 * @param state Validated current observation.
 * @param output Validated task-local intent.
 * @return Nonzero when a host could execute this abstract action. */
static int simulation_legal(const cgai_gameplay_state *state, uint32_t output) {
    /* Step 1: Idle and unavailable agents admit only their explicit no-op or wait actions. */
    if (state->values[GAMEPLAY_EVENT] == 0U)
        return output == 0U;
    if (state->values[GAMEPLAY_READINESS] == 0U)
        return output == 1U;
    /* Step 2: Remaining action preconditions follow the bounded navigation and combat world. */
    switch (output) {
    case 2U:
        return 1;
    case 3U:
        return state->values[GAMEPLAY_OBJECTIVE] == 1U;
    case 4U:
        return state->values[GAMEPLAY_DANGER] == 1U && state->values[GAMEPLAY_COVER] == 1U;
    case 5U:
        return state->values[GAMEPLAY_DANGER] == 1U && state->values[GAMEPLAY_DISTANCE] == 0U;
    case 6U:
        return state->values[GAMEPLAY_DANGER] == 1U;
    default:
        return 0;
    }
}

/** @brief Apply bounded hazard exposure over four abstract simulation ticks.
 * @param state Validated current observation.
 * @param output Proposed task-local action.
 * @return Nonzero for surviving the authored finite hazard transition. */
static int simulation_survival(const cgai_gameplay_state *state, uint32_t output) {
    /* Step 1: Idle and unavailable host-managed waits expose no actionable hazard. */
    if (state->values[GAMEPLAY_EVENT] == 0U || state->values[GAMEPLAY_READINESS] == 0U ||
        state->values[GAMEPLAY_DANGER] == 0U)
        return 1;
    /* Step 2: Retreat exits danger; cover and engagement protect healthy agents. */
    if (output == 6U)
        return 1;
    if (state->values[GAMEPLAY_HEALTH] == 0U)
        return 0;
    return output == 4U || output == 5U ||
           (state->values[GAMEPLAY_DISTANCE] == 1U && state->values[GAMEPLAY_COVER] == 0U);
}

/** @brief Advance four authored hazard ticks and finish a bounded movement action.
 * @param state Validated original observation.
 * @param output Validated task-local proposal.
 * @param result Private outcome with action legality already checked. */
static void simulation_ticks(const cgai_gameplay_state *state, uint32_t output,
                             gameplay_simulation_result *result) {
    /* Step 1: Healthy agents have four abstract hit points, critical agents have one. */
    uint32_t remaining = state->values[GAMEPLAY_HEALTH] == 1U ? 4U : 1U;
    const uint32_t exposure = simulation_survival(state, output) ? 0U : 1U;
    for (size_t tick = 0U; tick < 4U; ++tick)
        if (remaining > 0U && exposure != 0U)
            --remaining;
    /* Step 2: Actions complete their bounded movement after the four-tick hazard interval. */
    result->survived = result->legal && remaining != 0U;
    result->health_after = remaining > 1U ? 1U : 0U;
    if (output == 3U || output == 5U)
        result->distance_after = 0U;
    if (output == 6U)
        result->distance_after = 1U;
}

/** @brief Apply one independently checked action to a bounded four-tick abstract transition.
 * @param state Complete bounded state.
 * @param output Proposed task-local intent.
 * @param result Writable complete outcome.
 * @return OK on publication or ERROR on invalid input. */
cgai_status gameplay_fixture_simulate(const cgai_gameplay_state *state, uint32_t output,
                                      gameplay_simulation_result *result) {
    /* Step 1: Reject invalid observations before calculating executable action preconditions. */
    if (!valid_state(state) || output >= 7U || result == NULL)
        return CGAI_STATUS_ERROR;
    gameplay_simulation_result measured = {0};
    measured.health_after = state->values[GAMEPLAY_HEALTH];
    measured.distance_after = state->values[GAMEPLAY_DISTANCE];
    measured.legal = simulation_legal(state, output);
    simulation_ticks(state, output, &measured);
    /* Step 2: Safe defensive actions preserve the objective; advance requires closing distance. */
    measured.objective_success =
        measured.survived &&
        (state->values[GAMEPLAY_EVENT] == 0U || state->values[GAMEPLAY_READINESS] == 0U ||
         state->values[GAMEPLAY_DANGER] == 1U || state->values[GAMEPLAY_OBJECTIVE] == 0U ||
         measured.distance_after == 0U);
    if (!measured.survived)
        measured.health_after = 0U;
    *result = measured;
    return CGAI_STATUS_OK;
}
