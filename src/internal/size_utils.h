/** @file size_utils.h @brief Overflow-safe size arithmetic for internal buffers. */

#ifndef CGAI_SIZE_UTILS_H
#define CGAI_SIZE_UTILS_H

#include <stddef.h>
#include <stdint.h>

/** Adds two allocation sizes without wrapping; returns zero on overflow or NULL output. */
static inline int cgai_size_add(size_t left, size_t right, size_t *result) {
    if (result == NULL || right > SIZE_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

/** Multiplies two allocation sizes without wrapping; returns zero on overflow or NULL output. */
static inline int cgai_size_mul(size_t left, size_t right, size_t *result) {
    if (result == NULL || (left != 0U && right > SIZE_MAX / left)) {
        return 0;
    }
    *result = left * right;
    return 1;
}

#endif