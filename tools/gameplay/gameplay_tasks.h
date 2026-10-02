/** @file gameplay_tasks.h @brief Authored task registry and independent composed scenarios. */
#ifndef CGAI_GAMEPLAY_TASKS_H
#define CGAI_GAMEPLAY_TASKS_H
#include "centroid_gai_gameplay.h"

/** Supported authored task heads. */
#define GAMEPLAY_TASK_COUNT 2U
/** Aligned specialist modules, both eligible for both heads. */
#define GAMEPLAY_MODULE_COUNT 2U
/** Number of complete frozen requests across both tasks. */
#define GAMEPLAY_FIXTURE_CASE_COUNT 328U
/** Independent base training records before explicit bark invariance augmentation. */
#define GAMEPLAY_BASE_TRAINING_COUNT 240U
/** Balanced training schedule: four cycles of bark plus one full intent training split. */
#define GAMEPLAY_TRAINING_COUNT 384U
/** Original bark task ID. */
#define GAMEPLAY_TASK_BARK 0U
/** Bounded tactical intent task ID. */
#define GAMEPLAY_TASK_INTENT 1U

/** Fixed categorical feature order shared by every specialist. */
typedef enum gameplay_feature {
    GAMEPLAY_EVENT = 0,        /**< Idle, greet, threat, victory, discovery or retreat. */
    GAMEPLAY_DANGER = 1,       /**< Low or high. */
    GAMEPLAY_RELATIONSHIP = 2, /**< Friendly, neutral or hostile. */
    GAMEPLAY_SETTING = 3,      /**< Indoor or outdoor. */
    GAMEPLAY_HEALTH = 4,       /**< Critical or ready. */
    GAMEPLAY_DISTANCE = 5,     /**< Near or far. */
    GAMEPLAY_COVER = 6,        /**< Absent or present. */
    GAMEPLAY_OBJECTIVE = 7,    /**< Hold or advance. */
    GAMEPLAY_READINESS = 8     /**< Unavailable or ready. */
} gameplay_feature;
/** Families never cross their frozen split boundary. */
typedef enum gameplay_split {
    GAMEPLAY_TRAINING = 0,    /**< Authored training families. */
    GAMEPLAY_DEVELOPMENT = 1, /**< Frozen recipe-selection families. */
    GAMEPLAY_TEST = 2         /**< Frozen final quality families. */
} gameplay_split;
/** Registry entry with explicit task-local output and scenario domains. */
typedef struct gameplay_task_descriptor {
    const char *name;       /**< Stable task identifier. */
    size_t fixture_offset;  /**< First scenario in the shared enumeration. */
    size_t fixture_count;   /**< Complete independent scenario count. */
    size_t split_counts[3]; /**< Training, development and test case counts. */
    uint32_t output_count;  /**< Outputs including task-local fallback zero. */
} gameplay_task_descriptor;
/** One complete frozen independent task record. */
typedef struct gameplay_fixture_case {
    size_t id;                     /**< Stable global scenario index. */
    size_t family_id;              /**< Stable task-local family identifier. */
    gameplay_split split;          /**< Frozen family-level membership. */
    cgai_gameplay_example example; /**< Complete shared state and task-local teacher. */
} gameplay_fixture_case;
/** Outcome from a bounded authored transition simulator, separate from exact teacher scoring. */
typedef struct gameplay_simulation_result {
    uint32_t health_after;   /**< Critical or ready after four abstract transition ticks. */
    uint32_t distance_after; /**< Near or far after the transition. */
    int legal;               /**< Proposal satisfies current action preconditions. */
    int survived;            /**< Agent remains alive under the authored hazard rule. */
    int objective_success;   /**< Agent preserves hold or advances safely. */
} gameplay_simulation_result;

/** @brief Return the aligned two-module network shape and initialization seed.
 * @return Complete bounded version-one composition configuration. */
cgai_gameplay_config gameplay_fixture_config(void);
/** @brief Resolve one supported task registry entry.
 * @param task Requested task-local head ID.
 * @return Borrowed static descriptor, or NULL for unsupported tasks. */
const gameplay_task_descriptor *gameplay_task_get(uint32_t task);
/** @brief Resolve a module's stable source identity.
 * @param module Module ID, zero social or one tactical.
 * @return Borrowed static module name, or NULL. */
const char *gameplay_module_name(uint32_t module);
/** @brief Resolve one shared feature's name.
 * @param feature Contract-ordered feature index.
 * @return Borrowed static feature name, or NULL. */
const char *gameplay_feature_name(uint32_t feature);
/** @brief Resolve a categorical value to its authored source spelling.
 * @param feature Contract-ordered feature index.
 * @param value Bounded categorical value.
 * @return Borrowed static category name, or NULL. */
const char *gameplay_feature_value(uint32_t feature, uint32_t value);
/** @brief Resolve one task-local output name.
 * @param task Supported head ID.
 * @param output Supported task-local output ID.
 * @return Borrowed static output identifier, or NULL. */
const char *gameplay_output_name(uint32_t task, uint32_t output);
/** @brief Resolve authored bark text; intent outputs have no spoken line.
 * @param task Supported head ID.
 * @param output Supported task-local output ID.
 * @return Borrowed static line for spoken bark IDs, otherwise NULL. */
const char *gameplay_output_text(uint32_t task, uint32_t output);
/** @brief Resolve a split to its source spelling.
 * @param split Supported frozen split.
 * @return Borrowed static name, or NULL. */
const char *gameplay_split_name(gameplay_split split);
/** @brief Count frozen base requests for one task and split.
 * @param task Supported task head ID.
 * @param split Supported split ID.
 * @return Exact frozen count, or zero for invalid arguments. */
size_t gameplay_fixture_count(uint32_t task, gameplay_split split);
/** @brief Construct a complete independent frozen request.
 * @param index Global fixture index, less than328.
 * @param scenario Writable result, unchanged on invalid arguments.
 * @return OK on publication, ERROR otherwise. */
cgai_status gameplay_fixture_get(size_t index, gameplay_fixture_case *scenario);
/** @brief Apply the authored task teacher to one validated observation.
 * @param task Supported task head ID.
 * @param state Complete bounded shared observation.
 * @return Task-local teacher ID, or UINT32_MAX for invalid arguments. */
uint32_t gameplay_fixture_teacher(uint32_t task, const cgai_gameplay_state *state);
/** @brief Construct an unrestricted two-module host query from one frozen scenario.
 * @param scenario Borrowed complete supported fixture.
 * @param request Writable query, unchanged on invalid arguments.
 * @return OK on publication, ERROR otherwise. */
cgai_status gameplay_fixture_request(const gameplay_fixture_case *scenario,
                                     cgai_gameplay_request *request);
/** @brief Fill canonical training-only examples, balancing both task heads equally.
 * @param examples Caller-owned array of at least GAMEPLAY_TRAINING_COUNT records.
 * @param capacity Caller array capacity.
 * @param count Writable successful count, unchanged on failure.
 * @return OK after all384 independent records, ERROR on invalid arguments. */
cgai_status gameplay_fixture_training(cgai_gameplay_example *examples, size_t capacity,
                                      size_t *count);
/** @brief Change only the five bark-irrelevant observations to one exhaustive variant.
 * @param state Complete caller-owned bounded observation.
 * @param variant Five-bit context variant,0..31.
 * @return OK on mutation, ERROR on invalid state or variant. */
cgai_status gameplay_fixture_bark_context(cgai_gameplay_state *state, uint32_t variant);
/** @brief Run a bounded four-tick intent transition using action preconditions and hazard rules.
 * @param state Complete bounded observation.
 * @param output Proposed intent ID,0..6.
 * @param result Writable complete outcome, unchanged on invalid arguments.
 * @return OK on publication, ERROR otherwise. */
cgai_status gameplay_fixture_simulate(const cgai_gameplay_state *state, uint32_t output,
                                      gameplay_simulation_result *result);
#endif
