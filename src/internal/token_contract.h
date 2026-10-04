/** @file token_contract.h @brief Shared private token identifiers and control IDs. */
#ifndef CGAI_TOKEN_CONTRACT_H
#define CGAI_TOKEN_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

/** Vocabulary index; SIZE_MAX is an invalid sentinel, not a model-specific bound. */
typedef struct cgai_token_id {
    size_t value; /**< Index in the owning model's frozen vocabulary. */
} cgai_token_id;

/** @brief Wrap an index without validating a particular vocabulary.
 * @param value Raw index or SIZE_MAX sentinel.
 * @return Token identifier with no ownership. */
static inline cgai_token_id cgai_token_id_from_size(size_t value) { return (cgai_token_id){value}; }

/** @brief Construct the shared lookup-failure sentinel.
 * @return Token identifier containing SIZE_MAX. */
static inline cgai_token_id cgai_token_id_invalid(void) {
    return cgai_token_id_from_size(SIZE_MAX);
}

/** @brief Check the sentinel while leaving vocabulary bounds to the caller.
 * @param id Token identifier returned by a trusted lookup or insertion.
 * @return Nonzero for an identifier other than SIZE_MAX. */
static inline int cgai_token_id_is_valid(cgai_token_id id) { return id.value != SIZE_MAX; }

/** Reserved insertion-order IDs shared by retained vocabulary consumers and codecs. */
enum {
    CGAI_TOKEN_BOS = 0,    /**< Beginning-of-sequence input padding. */
    CGAI_TOKEN_EOS = 1,    /**< End-of-sequence output target. */
    CGAI_TOKEN_UNKNOWN = 2 /**< Frozen-vocabulary replacement for an absent spelling. */
};

#endif
