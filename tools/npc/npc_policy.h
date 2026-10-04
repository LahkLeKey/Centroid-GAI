/** @file npc_policy.h @brief Bounded observable-history adapter for the NPC centroid pilot. */
#ifndef CGAI_NPC_POLICY_H
#define CGAI_NPC_POLICY_H
#include "gameplay/gameplay_contract.h"
#include "npc_world.h"

/** Frozen categorical field count for the pilot. */
#define NPC_POLICY_FIELDS 16U
/** Inference owner and weights budget, excluding allocator overhead. */
#define NPC_MODEL_LIMIT 262144U
/** Per-NPC numerical scratch and persistent adapter budget. */
#define NPC_SESSION_LIMIT 65536U
/** Frozen maximum complete training epochs. */
#define NPC_EPOCH_LIMIT 10000U
/** Runtime adapter owning scratch and explicit observed memory. */
typedef struct npc_policy_session {
    cgai_gameplay_session *neural; /**< Exclusive scratch borrowing one immutable model. */
    npc_memory memory;             /**< Previously observed information only. */
    int history;                   /**< Nonzero enables the frozen history encoding. */
} npc_policy_session;

/** @brief Return the frozen D16/H48/M2/K8 seven-action architecture.
 * @return Complete configuration with seed42 and routing temperature32. */
cgai_gameplay_config npc_policy_config(void);
/** @brief Require every shape and seed field of the frozen pilot architecture.
 * @param model Borrowed live model.
 * @return Nonzero only for the complete compatible configuration. */
int npc_policy_compatible(const cgai_gameplay_model *model);
/** @brief Allocate numerical scratch and initialize independent observable memory.
 * @param model Borrowed compatible immutable model, which must outlive the adapter.
 * @param history Zero disables history at both encoding and inference.
 * @return Owned adapter, or NULL on incompatible shape or excessive heap. */
npc_policy_session *npc_policy_session_create(const cgai_gameplay_model *model, int history);
/** @brief Reset an existing adapter before another episode.
 * @param session Borrowed live adapter, or NULL. */
void npc_policy_session_reset(npc_policy_session *session);
/** @brief Release one adapter without releasing its borrowed model.
 * @param session Owned adapter, or NULL. */
void npc_policy_session_destroy(npc_policy_session *session);
/** @brief Update observed memory, encode, select, and validate one admitted proposal.
 * @param session Exclusive live adapter.
 * @param observation Current visible state after the previous authoritative transition.
 * @param modules Eligible module bits; zero requests forced fallback.
 * @param result Writable published proposal, unchanged on error.
 * @return OK on complete validation, ERROR otherwise; no allocation or I/O. */
cgai_status npc_policy_decide(npc_policy_session *session, const npc_observation *observation,
                              uint64_t modules, cgai_gameplay_result *result);
/** @brief Inspect persistent adapter plus public neural requested heap.
 * @param model Borrowed compatible model.
 * @param resources Writable public numerical resource report.
 * @param session_bytes Writable complete per-NPC requested heap.
 * @return OK after complete publication, ERROR otherwise. */
cgai_status npc_policy_resources(const cgai_gameplay_model *model,
                                 cgai_gameplay_resources *resources, size_t *session_bytes);
#endif
