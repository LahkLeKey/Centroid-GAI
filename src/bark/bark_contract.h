/** @file bark_contract.h @brief Private typed, allocation-free NPC bark selection. */
#ifndef CGAI_BARK_CONTRACT_H
#define CGAI_BARK_CONTRACT_H
#include "neural/neural_contract.h"

/** Supported authored state and output contract. */
#define CGAI_BARK_CONTRACT_VERSION 1U
/** Bit mask containing every authored output, including abstention. */
#define CGAI_BARK_ALL_IDS UINT64_C(511)
/** Gameplay event supplied by the host. */
typedef enum cgai_bark_event {
    CGAI_BARK_EVENT_IDLE = 0,      /**< No event requiring speech. */
    CGAI_BARK_EVENT_GREET = 1,     /**< First contact. */
    CGAI_BARK_EVENT_THREAT = 2,    /**< Threat detected. */
    CGAI_BARK_EVENT_VICTORY = 3,   /**< Encounter completed. */
    CGAI_BARK_EVENT_DISCOVERY = 4, /**< Something discovered. */
    CGAI_BARK_EVENT_RETREAT = 5    /**< Withdrawal requested. */
} cgai_bark_event;
/** Bounded danger observation; interpretation belongs to the host. */
typedef enum cgai_bark_danger {
    CGAI_BARK_DANGER_LOW = 0, /**< Low danger. */
    CGAI_BARK_DANGER_HIGH = 1 /**< High danger. */
} cgai_bark_danger;
/** Relationship observation supplied by the host. */
typedef enum cgai_bark_relationship {
    CGAI_BARK_RELATION_FRIENDLY = 0, /**< Friendly. */
    CGAI_BARK_RELATION_NEUTRAL = 1,  /**< Neutral. */
    CGAI_BARK_RELATION_HOSTILE = 2   /**< Hostile. */
} cgai_bark_relationship;
/** Setting observation supplied by the host. */
typedef enum cgai_bark_setting {
    CGAI_BARK_SETTING_INDOOR = 0, /**< Indoor. */
    CGAI_BARK_SETTING_OUTDOOR = 1 /**< Outdoor. */
} cgai_bark_setting;
/** Stable authored output IDs; zero always means silence. */
typedef enum cgai_bark_id {
    CGAI_BARK_ABSTAIN = 0,       /**< Silence. */
    CGAI_BARK_GREET_WARM = 1,    /**< Friendly greeting. */
    CGAI_BARK_GREET_PLAIN = 2,   /**< Neutral greeting. */
    CGAI_BARK_WARN_HOSTILE = 3,  /**< Hostile greeting. */
    CGAI_BARK_THREAT_CALM = 4,   /**< Low-danger warning. */
    CGAI_BARK_THREAT_URGENT = 5, /**< High-danger warning. */
    CGAI_BARK_VICTORY = 6,       /**< Victory line. */
    CGAI_BARK_DISCOVER = 7,      /**< Discovery line. */
    CGAI_BARK_RETREAT = 8,       /**< Retreat line. */
    CGAI_BARK_ID_COUNT = 9       /**< Output count; not a selectable ID. */
} cgai_bark_id;
/** Four categorical observations, encoded in this exact order. */
typedef struct cgai_bark_state {
    cgai_bark_event event;               /**< Event observation. */
    cgai_bark_danger danger;             /**< Danger observation. */
    cgai_bark_relationship relationship; /**< Relationship observation. */
    cgai_bark_setting setting;           /**< Setting observation. */
} cgai_bark_state;
/** One host-validated request; no caller strings or retained pointers. */
typedef struct cgai_bark_request {
    uint64_t allowed_ids;      /**< Bits 0..8; zero permits only abstention. */
    uint32_t contract_version; /**< Must equal CGAI_BARK_CONTRACT_VERSION. */
    uint32_t recent_id;        /**< Latest line ID to suppress; zero means none. */
    cgai_bark_state state;     /**< Bounded categorical observations. */
} cgai_bark_request;
/** Published selection; likelihood is not factual confidence or a calibrated score. */
typedef struct cgai_bark_result {
    const char *text;      /**< Borrowed static authored line; NULL for abstention. */
    double probability;    /**< Unconditional model likelihood; forced silence reports one. */
    size_t forward_passes; /**< One attempted pass, or zero when only abstention is legal. */
    cgai_bark_id id;       /**< Highest-likelihood allowed catalog ID; ties use lower ID. */
    int abstained;         /**< Nonzero exactly when id is CGAI_BARK_ABSTAIN. */
} cgai_bark_result;
/** Owned wrapper and its immutable, weights-only neural model. */
typedef struct cgai_bark_model cgai_bark_model;
/** Exclusive reusable scratch borrowing a live immutable bark model. */
typedef struct cgai_bark_session cgai_bark_session;
/** Requested heap payload; excludes allocator overhead, stack and static catalog storage. */
typedef struct cgai_bark_resources {
    cgai_neural_resources neural; /**< Underlying model and dense forward work. */
    size_t model_bytes;           /**< Underlying model plus owned bark wrapper. */
    size_t session_bytes;         /**< Owned bark session and numerical workspace. */
} cgai_bark_resources;

/** @brief Return a request allowing every catalog ID and no recent-line suppression.
 * @param event Event observation.
 * @param danger Danger observation.
 * @param relationship Relationship observation.
 * @param setting Setting observation.
 * @return Value requiring no cleanup; selection validates every enum value. */
cgai_bark_request cgai_bark_default_request(cgai_bark_event event, cgai_bark_danger danger,
                                            cgai_bark_relationship relationship,
                                            cgai_bark_setting setting);
/** @brief Resolve one stable ID to its authored lexical model target.
 * @param id Catalog ID.
 * @return Borrowed static target spelling, or NULL for an invalid ID. */
const char *cgai_bark_id_name(cgai_bark_id id);
/** @brief Resolve one stable ID to authored game content.
 * @param id Catalog ID.
 * @return Borrowed static text; NULL for abstention or an invalid ID. */
const char *cgai_bark_catalog_text(cgai_bark_id id);
/** @brief Load owned weights and validate contract-one shape and vocabulary.
 * @param path Trusted .cgnn path produced on the same architecture.
 * @return Owned model, or NULL with a diagnostic; destroy after its sessions.
 * @note Requires four context slots and all state/catalog spellings. Release provenance and
 * scenario quality must be checked using the accompanying release manifest. */
cgai_bark_model *cgai_bark_load(const char *path);
/** @brief Release an owned model after every associated session has been destroyed.
 * @param model Owned handle, or NULL. */
void cgai_bark_destroy(cgai_bark_model *model);
/** @brief Inspect complete requested model and session heap payload without allocation.
 * @param model Borrowed initialized immutable model.
 * @param resources Writable result, unchanged on error.
 * @return OK on publication, ERROR otherwise. */
cgai_status cgai_bark_get_resources(const cgai_bark_model *model, cgai_bark_resources *resources);
/** @brief Allocate scratch once before scheduling selection work.
 * @param model Borrowed immutable model, which must outlive the session.
 * @param max_session_bytes Complete session heap cap; zero disables the cap.
 * @return Owned session, or NULL with a diagnostic. Separate sessions may share one model. */
cgai_bark_session *cgai_bark_session_create(const cgai_bark_model *model, size_t max_session_bytes);
/** @brief Release scratch without releasing the borrowed model.
 * @param session Owned session, or NULL. */
void cgai_bark_session_destroy(cgai_bark_session *session);
/** @brief Select an allowed catalog ID using at most one allocation-free forward pass.
 * @param session Exclusive session borrowing a live immutable model.
 * @param request Borrowed typed request; invalid contract/enums/mask/recent ID are rejected.
 * @param result Writable selection, unchanged on every error including numeric failure.
 * @return OK on publication, ERROR otherwise. Abstention is always permitted. The recent
 * nonzero ID is excluded. If this leaves only abstention, no model work is performed.
 * @note No allocation, tokenization or I/O occurs here. A pass is indivisible and bounds
 * computation rather than wall-clock latency. The host remains responsible for legality. */
cgai_status cgai_bark_select(cgai_bark_session *session, const cgai_bark_request *request,
                             cgai_bark_result *result);
#endif
