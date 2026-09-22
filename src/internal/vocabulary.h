/** @file vocabulary.h @brief Private vocabulary operations with typed token IDs. */

#ifndef CGAI_VOCABULARY_H
#define CGAI_VOCABULARY_H

#include "cgai_internal.h"

/**
 * @brief Finds an existing token by linear scan.
 * @param model Vocabulary owner; it is not mutated.
 * @param token NUL-terminated spelling to compare.
 * @return Existing stable ID, or cgai_token_id_invalid() when absent.
 */
cgai_token_id cgai_vocabulary_find(const cgai_model *model, const char *token);

/**
 * @brief Adds a token if absent and returns its stable vocabulary identifier.
 * @param model Mutable vocabulary owner; token-count rows may be reallocated.
 * @param token NUL-terminated spelling copied into model-owned storage.
 * @return Existing or newly assigned ID; invalid ID indicates allocation failure.
 */
cgai_token_id cgai_vocabulary_add(cgai_model *model, const char *token);

#endif
