/** @file bark_fixture.h @brief Authored deterministic bark scenarios and training splits. */
#ifndef CGAI_BARK_FIXTURE_H
#define CGAI_BARK_FIXTURE_H

#include "bark/bark_contract.h"

/** Number of complete, unique state requests in the version-one fixture. */
#define BARK_FIXTURE_CASE_COUNT 72U
/** Total scenario count used by task tooling. */
#define BARK_FIXTURE_TOTAL_CASES BARK_FIXTURE_CASE_COUNT
/** Number of state families, each containing both setting variants. */
#define BARK_FIXTURE_FAMILY_COUNT 36U

/** Frozen split membership; families never cross a split boundary. */
typedef enum bark_fixture_split {
    BARK_FIXTURE_TRAINING = 0,    /**< Forty-eight training scenarios. */
    BARK_FIXTURE_DEVELOPMENT = 1, /**< Twelve development scenarios. */
    BARK_FIXTURE_TEST = 2         /**< Twelve final test scenarios. */
} bark_fixture_split;

/** One caller-owned scenario; prompt storage lives inside this value. */
typedef struct bark_fixture_case {
    size_t id;                 /**< Stable scenario index, 0..71. */
    size_t family_id;          /**< Event/danger/relationship group, 0..35. */
    bark_fixture_split split;  /**< Frozen family-level split membership. */
    cgai_bark_request request; /**< Versioned state and all catalog IDs admitted. */
    uint32_t expected_id;      /**< Authored rule teacher result. */
    char prompt[128];          /**< Four normalized state words, including NUL. */
} bark_fixture_case;

/** @brief Construct a deterministic independent training or evaluation request.
 * @param index Stable scenario index, less than BARK_FIXTURE_CASE_COUNT.
 * @param scenario Borrowed writable result, unchanged on invalid arguments.
 * @return OK on success; ERROR for an invalid index or missing output. */
cgai_status bark_fixture_get(size_t index, bark_fixture_case *scenario);
/** @brief Count the scenarios assigned to a frozen split.
 * @param split Requested split value.
 * @return Forty-eight for training, twelve for development/test, or zero when invalid. */
size_t bark_fixture_count(bark_fixture_split split);
/** @brief Resolve the authored rule teacher for a bounded state.
 * @param state Borrowed state; NULL and out-of-range fields are rejected.
 * @return Stable catalog ID, or UINT32_MAX for invalid state. */
uint32_t bark_fixture_oracle(const cgai_bark_state *state);
/** @brief Obtain the stable textual split name used in source fixtures.
 * @param split Requested split value.
 * @return Borrowed static name, or NULL for an invalid split. */
const char *bark_fixture_split_name(bark_fixture_split split);
/** @brief Obtain an event name used in source fixtures.
 * @param event Requested event value.
 * @return Borrowed static lowercase name, or NULL for an invalid event. */
const char *bark_fixture_event_name(cgai_bark_event event);
/** @brief Obtain a danger name used in source fixtures.
 * @param danger Requested danger value.
 * @return Borrowed static lowercase name, or NULL for an invalid danger. */
const char *bark_fixture_danger_name(cgai_bark_danger danger);
/** @brief Obtain a relationship name used in source fixtures.
 * @param relationship Requested relationship value.
 * @return Borrowed static lowercase name, or NULL for an invalid relationship. */
const char *bark_fixture_relationship_name(cgai_bark_relationship relationship);
/** @brief Obtain a setting name used in source fixtures.
 * @param setting Requested setting value.
 * @return Borrowed static lowercase name, or NULL for an invalid setting. */
const char *bark_fixture_setting_name(cgai_bark_setting setting);

#endif
