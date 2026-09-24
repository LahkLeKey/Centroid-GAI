/** @file vocabulary.h @brief Private vocabulary operations with typed token IDs. */

#ifndef CGAI_VOCABULARY_H
#define CGAI_VOCABULARY_H

#include "cgai_internal.h"

/**
 * @brief Find the stable identifier associated with an exact spelling.
 *
 * strcmp returns zero for equal C strings; the search therefore checks equality explicitly rather
 * than treating its result as a success flag. Lookup is a linear scan and does not normalize text,
 * allocate memory, or add missing spellings.
 *
 * @param model Non-NULL borrowed model with initialized vocabulary.
 * @param token Non-NULL borrowed NUL-terminated spelling to match exactly.
 * @return Typed vocabulary index, or the SIZE_MAX token sentinel when no spelling matches.
 */
cgai_token_id cgai_vocabulary_find(const cgai_model *model, const char *token);

/**
 * @brief Return an existing token ID or append a newly owned vocabulary spelling.
 *
 * An ID is the spelling's insertion index and stays stable for the life of this model. A new
 * entry needs both a copied spelling and a wider count table. These are prepared before the occupied
 * vocabulary size is advanced; capacity may still grow on a later failure. This mutates the model
 * and requires exclusive access.
 *
 * @param model Non-NULL mutable model with consistent vocabulary and count arrays.
 * @param token Non-NULL borrowed NUL-terminated spelling; copied only if it is new.
 * @return Existing/new token ID, or the invalid sentinel on failure.
 */
cgai_token_id cgai_vocabulary_add(cgai_model *model, const char *token);

#endif
