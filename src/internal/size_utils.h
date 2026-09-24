/** @file size_utils.h @brief Overflow-safe size arithmetic for internal buffers. */

#ifndef CGAI_SIZE_UTILS_H
#define CGAI_SIZE_UTILS_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Add two unsigned allocation sizes without allowing size_t wraparound.
 *
 * Unsigned C arithmetic wraps on overflow. Checking right against the remaining distance to
 * SIZE_MAX avoids computing an invalid sum first. On failure the output remains unchanged, which
 * allows callers to decide how to report the error without receiving a misleading small size.
 *
 * @param left First size_t addend.
 * @param right Second size_t addend.
 * @param result Writable output, or NULL to receive failure.
 * @return One with the exact sum stored, or zero for NULL output or overflow.
 */
static inline int cgai_size_add(size_t left, size_t right, size_t *result) {
    /* Step 1: Check output availability and whether the second addend fits in the remaining range. */
    if (result == NULL || right > SIZE_MAX - left) {
        return 0;
    }
    /* Step 2: Store the sum only after its representability is established. */
    *result = left + right;
    return 1;
}

/**
 * @brief Multiply two unsigned allocation sizes without allowing size_t wraparound.
 *
 * For a nonzero left factor, right must not exceed SIZE_MAX / left. The explicit zero case avoids
 * division by zero and allows a legitimate zero product. No diagnostic is set here; the allocation
 * caller can describe the relevant buffer or operation.
 *
 * @param left First size_t factor.
 * @param right Second size_t factor.
 * @param result Writable output, or NULL to receive failure.
 * @return One with the exact product stored, or zero for NULL output or overflow.
 */
static inline int cgai_size_mul(size_t left, size_t right, size_t *result) {
    /* Step 1: Validate the destination and check the division-based bound for nonzero left. */
    if (result == NULL || (left != 0U && right > SIZE_MAX / left)) {
        return 0;
    }
    /* Step 2: Evaluate and store multiplication only after proving it fits. */
    *result = left * right;
    return 1;
}

#endif