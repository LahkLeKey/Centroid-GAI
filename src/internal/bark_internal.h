/** @file bark_internal.h @brief Owned contract-one catalog token maps and selection scratch. */
#ifndef CGAI_BARK_INTERNAL_H
#define CGAI_BARK_INTERNAL_H
#include "bark/bark_contract.h"
#include "neural_math.h"
/** Immutable owned model and vocabulary lookup tables. */
struct cgai_bark_model {
    cgai_neural_model *network;                /**< Owned weights-only network. */
    cgai_token_id events[6];                   /**< Event token IDs in enum order. */
    cgai_token_id dangers[2];                  /**< Danger token IDs in enum order. */
    cgai_token_id relationships[3];            /**< Relationship token IDs in enum order. */
    cgai_token_id settings[2];                 /**< Setting token IDs in enum order. */
    cgai_token_id catalog[CGAI_BARK_ID_COUNT]; /**< Predictable output IDs in catalog order. */
};
/** Exclusive selection scratch; immutable lookup tables remain in the borrowed model. */
struct cgai_bark_session {
    const cgai_bark_model *model;     /**< Borrowed immutable model. */
    cgai_neural_workspace *workspace; /**< Owned reusable numeric scratch. */
    cgai_token_id context[4];         /**< Four state tokens for the current request. */
};
/** @brief Count requested owned selection scratch without allocation.
 * @param model Borrowed initialized model, or NULL.
 * @return Session plus workspace bytes, or zero on missing model or overflow. */
size_t cgai_bark_session_bytes(const cgai_bark_model *model);
#endif
