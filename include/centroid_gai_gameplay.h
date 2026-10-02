/** @file centroid_gai_gameplay.h @brief Composed centroid specialists for bounded gameplay tasks.
 */
#ifndef CENTROID_GAI_GAMEPLAY_H
#define CENTROID_GAI_GAMEPLAY_H
#include "centroid_gai.h"
#ifdef __cplusplus
extern "C" {
#endif

/** Maximum typed categorical feature count. */
#define CGAI_GAMEPLAY_MAX_FEATURES 16U
/** Maximum task heads in one bundle. */
#define CGAI_GAMEPLAY_MAX_TASKS 8U
/** Maximum specialist modules in one bundle. */
#define CGAI_GAMEPLAY_MAX_MODULES 8U
/** Maximum task-local output count, including fallback zero. */
#define CGAI_GAMEPLAY_MAX_OUTPUTS 64U
/** Version of the typed request and composed parameter contract. */
#define CGAI_GAMEPLAY_CONTRACT_VERSION 1U

/** Fixed bounded composition shape; unused array entries must be zero. */
typedef struct cgai_gameplay_config {
    uint64_t seed;               /**< Initialization and canonical training-shuffle seed. */
    double routing_temperature;  /**< Positive distance temperature, 0.01..100. */
    size_t feature_count;        /**< Active categorical fields, 1..16. */
    size_t embedding_dimensions; /**< Category embedding coordinates, 1..64. */
    size_t hidden_dimensions;    /**< Shared encoder coordinates, 1..64. */
    size_t module_count;         /**< Learned outer routing centroids, 1..8. */
    size_t centroids_per_module; /**< Inner experts per module, 1..32. */
    size_t task_count;           /**< Distinct typed output heads, 1..8. */
    uint32_t cardinalities[CGAI_GAMEPLAY_MAX_FEATURES]; /**< Values per feature, 1..64. */
    uint32_t output_counts[CGAI_GAMEPLAY_MAX_TASKS];    /**< Task-local IDs, 2..64. */
    uint64_t task_modules[CGAI_GAMEPLAY_MAX_TASKS];     /**< Nonempty eligible-module masks. */
    uint64_t task_features[CGAI_GAMEPLAY_MAX_TASKS];    /**< Nonempty relevant-feature masks. */
} cgai_gameplay_config;

/** Caller-owned typed observations; trailing unused entries must be zero. */
typedef struct cgai_gameplay_state {
    uint32_t values[CGAI_GAMEPLAY_MAX_FEATURES]; /**< Contract-ordered categorical values. */
} cgai_gameplay_state;
/** Independent task target; its full state is never inherited from another record. */
typedef struct cgai_gameplay_example {
    cgai_gameplay_state state; /**< Complete categorical observation. */
    uint32_t task;             /**< Requested head index. */
    uint32_t target;           /**< Task-local target ID, including fallback zero. */
} cgai_gameplay_example;
/** Complete-epoch AdamW continuation settings; zero decay retains ordinary Adam. */
typedef struct cgai_gameplay_training {
    size_t epochs;        /**< Additional full passes, 1..10000. */
    double learning_rate; /**< Positive finite rate, at most one. */
    double gradient_clip; /**< Positive finite global norm cap, at most1000. */
    double weight_decay;  /**< Finite decoupled parameter shrinkage, 0..1; zero disables. */
} cgai_gameplay_training;
/** Accumulated deterministic training progress. */
typedef struct cgai_gameplay_progress {
    uint64_t epochs; /**< Successful full passes. */
    uint64_t steps;  /**< Successful record updates. */
} cgai_gameplay_progress;
/** One host-owned bounded task query. */
typedef struct cgai_gameplay_request {
    cgai_gameplay_state state; /**< Complete observation. */
    uint64_t allowed_outputs;  /**< Legal task-local output bits; zero permits fallback only. */
    uint64_t allowed_modules;  /**< Eligible module bits; zero forces fallback. */
    uint32_t contract_version; /**< Must equal CGAI_GAMEPLAY_CONTRACT_VERSION. */
    uint32_t task;             /**< Requested output head. */
    uint32_t recent_output;    /**< Nonzero ID to suppress, or zero for none. */
} cgai_gameplay_request;
/** Complete validated proposal and routing diagnostics. */
typedef struct cgai_gameplay_result {
    double probability; /**< Unconditional task-head likelihood; forced fallback reports one. */
    double module_weights[CGAI_GAMEPLAY_MAX_MODULES];       /**< Outer routing probabilities. */
    double module_contributions[CGAI_GAMEPLAY_MAX_MODULES]; /**< Selected-ID posterior shares. */
    size_t forward_passes;   /**< One full forward or zero for forced fallback. */
    size_t active_modules;   /**< Evaluated eligible modules. */
    size_t active_centroids; /**< Evaluated inner experts. */
    uint32_t output;         /**< Task-local ID admitted by the host. */
    int abstained;           /**< Nonzero exactly for fallback ID zero. */
} cgai_gameplay_result;
/** Requested owned heap and conservative configured dense single-head work bounds. */
typedef struct cgai_gameplay_resources {
    size_t parameter_count;         /**< Trainable scalar doubles. */
    size_t parameter_bytes;         /**< Weight allocation bytes. */
    size_t optimizer_bytes;         /**< Adam allocations; zero for inference-only loads. */
    size_t model_bytes;             /**< Model owner, weights and resident optimizer. */
    size_t session_bytes;           /**< Complete reusable scratch allocation. */
    uint64_t encoder_multiply_adds; /**< Shared encoder terms per request. */
    uint64_t outer_coordinates;     /**< All configured module-centroid coordinates; upper bound. */
    uint64_t inner_coordinates;   /**< All configured internal centroid coordinates; upper bound. */
    uint64_t maximum_head_logits; /**< All configured experts times widest head; upper bound. */
    uint64_t maximum_head_multiply_adds; /**< Configured modules times hidden times widest head;
                                            upper bound. */
} cgai_gameplay_resources;
/** Owned composed network and continuation state. */
typedef struct cgai_gameplay_model cgai_gameplay_model;
/** Exclusive numerical scratch borrowing one immutable live model. */
typedef struct cgai_gameplay_session cgai_gameplay_session;

/** @brief Initialize one aligned shared encoder and hierarchical centroid modules.
 * @param config Borrowed complete shape; category total is at most1024.
 * @return Owned initialized network, or NULL with a diagnostic. */
cgai_gameplay_model *cgai_gameplay_create(const cgai_gameplay_config *config);
/** @brief Release an owned network after all its sessions are destroyed.
 * @param model Owned handle, or NULL. */
void cgai_gameplay_destroy(cgai_gameplay_model *model);
/** @brief Copy the fixed shape without allocation.
 * @param model Borrowed initialized model.
 * @param config Writable result, unchanged on error.
 * @return OK on publication, ERROR otherwise. */
cgai_status cgai_gameplay_get_config(const cgai_gameplay_model *model,
                                     cgai_gameplay_config *config);
/** @brief Count complete requested heap and configured dense work upper bounds without allocation.
 * @param model Borrowed initialized model.
 * @param resources Writable result, unchanged on error.
 * @return OK on publication, ERROR otherwise. Task/module exclusions can reduce actual work. */
cgai_status cgai_gameplay_get_resources(const cgai_gameplay_model *model,
                                        cgai_gameplay_resources *resources);
/** @brief Allocate capped reusable scratch before scheduling queries.
 * @param model Borrowed immutable model which must outlive the session.
 * @param max_session_bytes Complete requested heap cap; zero disables the cap.
 * @return Owned session, or NULL with a diagnostic. */
cgai_gameplay_session *cgai_gameplay_session_create(const cgai_gameplay_model *model,
                                                    size_t max_session_bytes);
/** @brief Release scratch without releasing its borrowed network.
 * @param session Owned handle, or NULL. */
void cgai_gameplay_session_destroy(cgai_gameplay_session *session);
/** @brief Return a request allowing all configured task outputs and modules.
 * @param model Borrowed initialized model.
 * @param task Requested valid head.
 * @param state Borrowed categorical observation.
 * @param request Writable result, unchanged on invalid arguments.
 * @return OK on publication, ERROR otherwise. */
cgai_status cgai_gameplay_default_request(const cgai_gameplay_model *model, uint32_t task,
                                          const cgai_gameplay_state *state,
                                          cgai_gameplay_request *request);
/** @brief Mix specialist and inner-centroid predictions and publish one legal task ID.
 * @param session Exclusive scratch borrowing an immutable model.
 * @param request Borrowed complete typed query.
 * @param result Writable proposal, unchanged on any error.
 * @return OK on publication, ERROR otherwise. No allocation, tokenization or I/O occurs.
 * Fallback zero is always legal. An empty eligible-module/output domain needs no forward. */
cgai_status cgai_gameplay_select(cgai_gameplay_session *session,
                                 const cgai_gameplay_request *request,
                                 cgai_gameplay_result *result);
/** @brief Score one unrestricted independent target on an existing session.
 * @param session Exclusive live scratch.
 * @param example Borrowed complete state, task and target.
 * @param loss Writable task-local negative log likelihood, unchanged on error.
 * @return OK on publication, ERROR otherwise; no parameter mutation or allocations. */
cgai_status cgai_gameplay_evaluate(cgai_gameplay_session *session,
                                   const cgai_gameplay_example *example, double *loss);
/** @brief Score a target using only explicitly admitted modules for controlled ablation.
 * @param session Exclusive live scratch.
 * @param example Borrowed complete independent target.
 * @param allowed_modules Nonempty eligible specialist bits.
 * @param loss Writable task-head negative log likelihood, unchanged on error.
 * @return OK on publication, ERROR otherwise; no allocation or parameter mutation. */
cgai_status cgai_gameplay_evaluate_modules(cgai_gameplay_session *session,
                                           const cgai_gameplay_example *example,
                                           uint64_t allowed_modules, double *loss);
/** @brief Continue joint clipped AdamW with canonical record shuffling.
 * @param model Mutable initialized network; requires exclusive access.
 * @param examples Borrowed ordered independent task records.
 * @param count Number of records, 1..10000.
 * @param training Borrowed additional-pass settings.
 * @return OK after all full passes, ERROR otherwise. Invalid records/settings preserve state;
 * numeric errors retain earlier successful updates and advanced shuffle state.
 * Exact successful replay requires the same recipe. */
cgai_status cgai_gameplay_train_continue(cgai_gameplay_model *model,
                                         const cgai_gameplay_example *examples, size_t count,
                                         const cgai_gameplay_training *training);
/** @brief Inspect completed continuation passes and updates.
 * @param model Borrowed initialized model, or NULL.
 * @return Progress value, zero for NULL. */
cgai_gameplay_progress cgai_gameplay_get_progress(const cgai_gameplay_model *model);
/** @brief Save a distinct version-one inference artifact with weights only.
 * @param model Borrowed immutable network.
 * @param path Trusted destination; existing contents replaced nonatomically.
 * @return OK after complete write/close, ERROR otherwise. Uses the C numeric locale. */
cgai_status cgai_gameplay_save(const cgai_gameplay_model *model, const char *path);
/** @brief Load bounded validated composed weights from the distinct gameplay format.
 * @param path Trusted artifact path.
 * @return Owned inference network, or NULL with a diagnostic. Uses the C numeric locale. */
cgai_gameplay_model *cgai_gameplay_load(const char *path);
/** @brief Save exact weights, Adam, counters and shuffle state as hexadecimal text.
 * @param model Borrowed immutable network.
 * @param path Trusted destination; existing contents replaced nonatomically.
 * @return OK after complete write/close, ERROR otherwise. Uses the C numeric locale. */
cgai_status cgai_gameplay_checkpoint_save(const cgai_gameplay_model *model, const char *path);
/** @brief Load exact bounded continuation state from gameplay checkpoint text.
 * @param path Trusted checkpoint path.
 * @return Owned network, or NULL with a diagnostic; malformed input is rejected. */
cgai_gameplay_model *cgai_gameplay_checkpoint_load(const char *path);
#ifdef __cplusplus
}
#endif
#endif
