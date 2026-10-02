/** @file bark_fixture.c @brief Reproducible family-separated bark task fixture. */
#include "bark_fixture.h"

#include <stdio.h>

/** Lowercase labels bind authored source fixtures to the state encoding. */
static const char *const bark_fixture_events[] = {"idle",    "greet",     "threat",
                                                  "victory", "discovery", "retreat"};
/** Lowercase labels for the bounded danger dimension. */
static const char *const bark_fixture_dangers[] = {"low", "high"};
/** Lowercase labels for the bounded relationship dimension. */
static const char *const bark_fixture_relationships[] = {"friendly", "neutral", "hostile"};
/** Lowercase labels for both setting variants of each family. */
static const char *const bark_fixture_settings[] = {"indoor", "outdoor"};
/** Stable names for the three independent task splits. */
static const char *const bark_fixture_splits[] = {"training", "development", "test"};

/** @brief Check every enumerated dimension before indexing authored tables.
 * @param state Borrowed state, which may be NULL.
 * @return Nonzero only when every field has a version-one value. */
static int bark_fixture_valid(const cgai_bark_state *state) {
    /* Step 1: Reject missing state and bound all four independently supplied fields. */
    return state != NULL && (unsigned)state->event < 6U && (unsigned)state->danger < 2U &&
           (unsigned)state->relationship < 3U && (unsigned)state->setting < 2U;
}

/** @brief Resolve the finite authored teacher rules without consulting model output.
 * @param state Borrowed bounded state, or NULL.
 * @return Stable authored catalog ID; UINT32_MAX rejects invalid state. */
uint32_t bark_fixture_oracle(const cgai_bark_state *state) {
    /* Step 1: Reject invalid state without reading beyond the authored output table. */
    static const uint32_t event_ids[] = {CGAI_BARK_ABSTAIN,     CGAI_BARK_GREET_PLAIN,
                                         CGAI_BARK_THREAT_CALM, CGAI_BARK_VICTORY,
                                         CGAI_BARK_DISCOVER,    CGAI_BARK_RETREAT};
    static const uint32_t greeting_ids[] = {CGAI_BARK_GREET_WARM, CGAI_BARK_GREET_PLAIN,
                                            CGAI_BARK_WARN_HOSTILE};
    if (!bark_fixture_valid(state))
        return UINT32_MAX;
    /* Step 2: Apply the two state-dependent rules, then the event-only rules. */
    if (state->event == CGAI_BARK_EVENT_GREET)
        return greeting_ids[state->relationship];
    if (state->event == CGAI_BARK_EVENT_THREAT && state->danger == CGAI_BARK_DANGER_HIGH)
        return CGAI_BARK_THREAT_URGENT;
    return event_ids[state->event];
}

/** @brief Assign a complete family to exactly one frozen split.
 * @param family_id Stable event/danger/relationship group, less than thirty-six.
 * @return Development for remainder zero, test for one, training for the others. */
static bark_fixture_split bark_fixture_family_split(size_t family_id) {
    /* Step 1: Keep both settings together and reserve two state combinations per event. */
    size_t remainder = family_id % 6U;
    return remainder == 0U ? BARK_FIXTURE_DEVELOPMENT
                           : (remainder == 1U ? BARK_FIXTURE_TEST : BARK_FIXTURE_TRAINING);
}

/** @brief Construct one independent scenario from its stable enumeration index.
 * @param index Stable index below seventy-two.
 * @param scenario Writable caller-owned result, unchanged on failure.
 * @return OK on publication; ERROR for invalid arguments or failed prompt encoding. */
cgai_status bark_fixture_get(size_t index, bark_fixture_case *scenario) {
    /* Step 1: Decode an index into the four stable state dimensions. */
    if (index >= BARK_FIXTURE_CASE_COUNT || scenario == NULL)
        return CGAI_STATUS_ERROR;
    bark_fixture_case value = {0};
    value.id = index;
    value.family_id = index / 2U;
    value.split = bark_fixture_family_split(value.family_id);
    value.request = cgai_bark_default_request((cgai_bark_event)((value.family_id / 6U) % 6U),
                                              (cgai_bark_danger)((value.family_id / 3U) % 2U),
                                              (cgai_bark_relationship)(value.family_id % 3U),
                                              (cgai_bark_setting)(index % 2U));
    /* Step 2: Admit the authored catalog, disable repetition suppression, and encode context. */
    value.expected_id = bark_fixture_oracle(&value.request.state);
    int written =
        snprintf(value.prompt, sizeof(value.prompt), "event%s danger%s relation%s setting%s",
                 bark_fixture_event_name(value.request.state.event),
                 bark_fixture_danger_name(value.request.state.danger),
                 bark_fixture_relationship_name(value.request.state.relationship),
                 bark_fixture_setting_name(value.request.state.setting));
    if (written < 0 || (size_t)written >= sizeof(value.prompt))
        return CGAI_STATUS_ERROR;
    *scenario = value;
    return CGAI_STATUS_OK;
}

/** @brief Return the immutable scenario count for a named split.
 * @param split Requested version-one split.
 * @return Fixed count, or zero for an unsupported split. */
size_t bark_fixture_count(bark_fixture_split split) {
    /* Step 1: Return the fixed family-separated scenario counts. */
    return split == BARK_FIXTURE_TRAINING                                    ? 48U
           : split == BARK_FIXTURE_DEVELOPMENT || split == BARK_FIXTURE_TEST ? 12U
                                                                             : 0U;
}

/** @brief Resolve a bounded split to its source-fixture label.
 * @param split Requested split.
 * @return Borrowed static spelling, or NULL for an invalid value. */
const char *bark_fixture_split_name(bark_fixture_split split) {
    /* Step 1: Return a borrowed label only for a supported split. */
    return (unsigned)split < 3U ? bark_fixture_splits[split] : NULL;
}

/** @brief Resolve a bounded event to its source-fixture label.
 * @param event Requested event.
 * @return Borrowed static spelling, or NULL for an invalid value. */
const char *bark_fixture_event_name(cgai_bark_event event) {
    /* Step 1: Return a borrowed label only for a supported event. */
    return (unsigned)event < 6U ? bark_fixture_events[event] : NULL;
}

/** @brief Resolve a bounded danger observation to its source-fixture label.
 * @param danger Requested danger observation.
 * @return Borrowed static spelling, or NULL for an invalid value. */
const char *bark_fixture_danger_name(cgai_bark_danger danger) {
    /* Step 1: Return a borrowed label only for a supported danger. */
    return (unsigned)danger < 2U ? bark_fixture_dangers[danger] : NULL;
}

/** @brief Resolve a bounded relationship observation to its source-fixture label.
 * @param relationship Requested relationship observation.
 * @return Borrowed static spelling, or NULL for an invalid value. */
const char *bark_fixture_relationship_name(cgai_bark_relationship relationship) {
    /* Step 1: Return a borrowed label only for a supported relationship. */
    return (unsigned)relationship < 3U ? bark_fixture_relationships[relationship] : NULL;
}

/** @brief Resolve a bounded setting observation to its source-fixture label.
 * @param setting Requested setting observation.
 * @return Borrowed static spelling, or NULL for an invalid value. */
const char *bark_fixture_setting_name(cgai_bark_setting setting) {
    /* Step 1: Return a borrowed label only for a supported setting. */
    return (unsigned)setting < 2U ? bark_fixture_settings[setting] : NULL;
}
