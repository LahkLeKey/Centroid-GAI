/** @file bark_model.c @brief Authored bark catalog, contract validation and owned model loading. */
#include "internal/bark_internal.h"
#include "internal/error.h"
#include "internal/size_utils.h"
#include <stdlib.h>

/** Contract-one output spellings, with IDs independent from neural vocabulary order. */
static const char *const bark_names[CGAI_BARK_ID_COUNT] = {
    "abstain",      "greetwarm", "greetplain", "warnhostile", "threatcalm",
    "threaturgent", "victory",   "discover",   "retreat"};
/** Authored lines resolved only after a catalog ID has been selected. */
static const char *const bark_text[CGAI_BARK_ID_COUNT] = {
    NULL,          "Good to see you.", "Hello there.",        "Keep your distance.", "Stay alert.",
    "Take cover!", "We made it.",      "What have we found?", "Fall back!"};
/** Four categorical input axes; lexical spellings are part of contract one. */
static const char *const bark_events[6] = {"eventidle",    "eventgreet",     "eventthreat",
                                           "eventvictory", "eventdiscovery", "eventretreat"};
/** Danger spellings in enum order. */
static const char *const bark_dangers[2] = {"dangerlow", "dangerhigh"};
/** Relationship spellings in enum order. */
static const char *const bark_relationships[3] = {"relationfriendly", "relationneutral",
                                                  "relationhostile"};
/** Setting spellings in enum order. */
static const char *const bark_settings[2] = {"settingindoor", "settingoutdoor"};

/** @brief Return a value allowing every catalog ID without recent-line suppression.
 * @param event Event observation.
 * @param danger Danger observation.
 * @param relationship Relationship observation.
 * @param setting Setting observation.
 * @return Copied request; selection validates the supplied observations. */
cgai_bark_request cgai_bark_default_request(cgai_bark_event event, cgai_bark_danger danger,
                                            cgai_bark_relationship relationship,
                                            cgai_bark_setting setting) {
    /* Step 1: Preserve typed observations and set the complete version-one output domain. */
    return (cgai_bark_request){.allowed_ids = CGAI_BARK_ALL_IDS,
                               .contract_version = CGAI_BARK_CONTRACT_VERSION,
                               .recent_id = 0U,
                               .state = {event, danger, relationship, setting}};
}

/** @brief Resolve one catalog ID to its stable lexical target spelling.
 * @param id Catalog ID.
 * @return Borrowed static spelling, or NULL for an invalid ID. */
const char *cgai_bark_id_name(cgai_bark_id id) {
    /* Step 1: Reject negative and out-of-range enum values before indexing. */
    return (uint32_t)id < CGAI_BARK_ID_COUNT ? bark_names[(size_t)id] : NULL;
}

/** @brief Resolve one catalog ID to authored game content.
 * @param id Catalog ID.
 * @return Borrowed static line; NULL for silence or an invalid ID. */
const char *cgai_bark_catalog_text(cgai_bark_id id) {
    /* Step 1: Catalog membership protects the static content table. */
    return (uint32_t)id < CGAI_BARK_ID_COUNT ? bark_text[(size_t)id] : NULL;
}

/** @brief Map one complete authored spelling table into frozen neural IDs.
 * @param network Borrowed initialized network.
 * @param names Borrowed count-element static spelling table.
 * @param count Number of authored spellings.
 * @param ids Writable count-element token table.
 * @return OK after every spelling is found, ERROR otherwise. */
static cgai_status bind_tokens(const cgai_neural_model *network, const char *const *names,
                               size_t count, cgai_token_id *ids) {
    /* Step 1: Map exact authored names once; future requests need no string processing. */
    for (size_t index = 0U; index < count; ++index) {
        ids[index] = cgai_neural_lookup(network, names[index]);
        if (ids[index].value == CGAI_TOKEN_UNKNOWN)
            return cgai_fail("bark model lacks a required contract-one token");
    }
    return CGAI_STATUS_OK;
}

/** @brief Validate the context width and bind all state and target IDs.
 * @param model Borrowed mutable wrapper retaining its newly loaded network.
 * @return OK for a complete contract-one interface, ERROR otherwise. */
static cgai_status bind_contract(cgai_bark_model *model) {
    /* Step 1: This specialist requires four independent categorical observations. */
    const cgai_neural_model *network = model->network;
    if (network->config.context_window != 4U)
        return cgai_fail("bark model must use four context slots");
    if (!bind_tokens(network, bark_events, 6U, model->events) ||
        !bind_tokens(network, bark_dangers, 2U, model->dangers) ||
        !bind_tokens(network, bark_relationships, 3U, model->relationships) ||
        !bind_tokens(network, bark_settings, 2U, model->settings) ||
        !bind_tokens(network, bark_names, CGAI_BARK_ID_COUNT, model->catalog))
        return CGAI_STATUS_ERROR;
    /* Step 2: Input-only vocabulary suffixes must never be accepted as catalog targets. */
    for (size_t index = 0U; index < CGAI_BARK_ID_COUNT; ++index) {
        if (model->catalog[index].value >= network->output_size)
            return cgai_fail("bark catalog token is not a predictable model output");
    }
    return CGAI_STATUS_OK;
}

/** @brief Release an owned wrapper and its network after all sessions are destroyed.
 * @param model Owned handle, or NULL. */
void cgai_bark_destroy(cgai_bark_model *model) {
    /* Step 1: Accept incomplete-constructor cleanup and NULL owners. */
    if (model == NULL)
        return;
    /* Step 2: The wrapper alone owns the immutable network. */
    cgai_neural_destroy(model->network);
    free(model);
}

/** @brief Load trusted weights and validate their typed bark interface.
 * @param path Borrowed .cgnn path produced on this architecture.
 * @return Owned contract-one model, or NULL with a diagnostic. */
cgai_bark_model *cgai_bark_load(const char *path) {
    /* Step 1: Reject malformed native artifacts before allocating the specialist shell. */
    cgai_neural_model *network = cgai_neural_load(path);
    if (network == NULL)
        return NULL;
    cgai_bark_model *model = calloc(1U, sizeof(*model));
    if (model == NULL) {
        cgai_neural_destroy(network);
        cgai_fail("could not allocate bark model wrapper");
        return NULL;
    }
    /* Step 2: Bind the immutable lookup tables before publishing the owned model. */
    model->network = network;
    if (!bind_contract(model)) {
        cgai_bark_destroy(model);
        return NULL;
    }
    return model;
}

/** @brief Count complete session-owned requested heap payload without allocation.
 * @param model Borrowed initialized model, or NULL.
 * @return Requested payload bytes, or zero for invalid ownership or overflow. */
size_t cgai_bark_session_bytes(const cgai_bark_model *model) {
    /* Step 1: Require the model before measuring its numerical scratch. */
    if (model == NULL)
        return 0U;
    const size_t workspace = cgai_neural_workspace_bytes(model->network);
    size_t total = 0U;
    /* Step 2: Include all fixed token arrays in the separately allocated session owner. */
    return workspace != 0U && cgai_size_add(workspace, sizeof(cgai_bark_session), &total) ? total
                                                                                          : 0U;
}

/** @brief Inspect complete requested model/session ownership and underlying dense work.
 * @param model Borrowed immutable specialist model.
 * @param resources Writable result, unchanged on error.
 * @return OK after publication, ERROR otherwise. */
cgai_status cgai_bark_get_resources(const cgai_bark_model *model, cgai_bark_resources *resources) {
    /* Step 1: Populate a private report, preserving caller output on any error. */
    cgai_error_clear();
    if (model == NULL || resources == NULL)
        return cgai_fail("bark resource inspection requires a model and result");
    cgai_bark_resources result = {0};
    if (!cgai_neural_get_resources(model->network, &result.neural))
        return CGAI_STATUS_ERROR;
    /* Step 2: The wrapper token maps and session owner are part of each complete cap. */
    result.session_bytes = cgai_bark_session_bytes(model);
    if (result.session_bytes == 0U ||
        !cgai_size_add(result.neural.model_bytes, sizeof(*model), &result.model_bytes))
        return cgai_fail("bark resource inspection sizes overflow");
    *resources = result;
    return CGAI_STATUS_OK;
}
