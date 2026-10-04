/** @file life_optimizer.h @brief Shared participant-restricted composed-model optimizer. */
#ifndef CGAI_LIFE_OPTIMIZER_H
#define CGAI_LIFE_OPTIMIZER_H
#include "gameplay/gameplay_contract.h"

/** Borrowed numerical state. Shared owner zero never updates; slots are one-based.
 * Each caller owns its model, scratch, gradient, ownership map and Adam clocks.
 * The configured model count bounds slots independently of physical population. */
typedef struct life_optimizer_view {
    cgai_gameplay_model *model;
    cgai_gameplay_session *session;
    double *gradient;
    const unsigned char *owners;
    uint64_t *steps;
    const double *mass;
} life_optimizer_view;

/** Bind centroid/readout slices by module; embeddings/encoder/bias stay owner zero.
 * All provided map entries are initialized. Models must have one task head. */
cgai_status life_optimizer_bind(const cgai_gameplay_model *model, unsigned char *owners);
/** Frozen composed forward with explicit routing mass. Caller validates active bits. */
cgai_status life_optimizer_forward(const life_optimizer_view *view,
                                   const cgai_gameplay_state *state, uint32_t participants);
/** One verified domain record update: .01 learning rate, norm clip5, AdamW .0001.
 * Only participant-owned parameters, moments, decay and clocks change. All new
 * numerical values are validated before a single parameter is committed.
 * Eligibility and independently checked TRAIN target authority belong to caller. */
cgai_status life_optimizer_step(const life_optimizer_view *view, const cgai_gameplay_state *state,
                                uint32_t target, uint32_t participants);
#endif
