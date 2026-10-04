/** @file centroid_contract.h @brief Shared private centroid row identifiers. */
#ifndef CGAI_CENTROID_CONTRACT_H
#define CGAI_CENTROID_CONTRACT_H

#include <stddef.h>

/** Centroid row index, distinct from a vocabulary token identifier. */
typedef struct cgai_centroid_id {
    size_t value; /**< Row within the owning model's active centroid bank. */
} cgai_centroid_id;

/** @brief Wrap a row index without validating a particular model.
 * @param value Raw centroid row index.
 * @return Centroid identifier with no ownership. */
static inline cgai_centroid_id cgai_centroid_id_from_size(size_t value) {
    return (cgai_centroid_id){value};
}

#endif
