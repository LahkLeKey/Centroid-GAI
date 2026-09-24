/** @file error.h @brief Private error-state helpers. */

#ifndef CGAI_ERROR_H
#define CGAI_ERROR_H

#include "centroid_gai.h"

/**
 * @brief Reset the current thread's diagnostic to the empty state.
 *
 * Only the first character needs to become NUL for the buffer to represent an empty C string.
 * Other threads have independent storage. Clearing a diagnostic does not change any model or
 * status previously returned to the caller.
 *
 */
void cgai_error_clear(void);

/**
 * @brief Copy a diagnostic into bounded storage belonging to the current thread.
 *
 * snprintf copies at most the fixed buffer capacity, including its terminating NUL. Longer messages
 * are truncated. The input is borrowed only for this call, so callers need not keep their message
 * storage alive afterward. This setter does not itself return an operation status.
 *
 * @param message Non-NULL readable NUL-terminated diagnostic text to copy.
 */
void cgai_error_set(const char *message);

/**
 * @brief Record a native diagnostic and return the core failure constant.
 *
 * This small inline helper keeps error reporting and a failed return adjacent at each call site.
 * Its constant return is visible to the compiler and analyzer. The message is copied into the
 * current thread's diagnostic storage, so the caller's string need not outlive the call.
 *
 * @param message Non-NULL borrowed NUL-terminated diagnostic text.
 * @return Always CGAI_STATUS_ERROR; never an ABI status code.
 */
static inline cgai_status cgai_fail(const char *message) {
    /* Step 1: Copy the explanation into thread-local storage. */
    cgai_error_set(message);
    /* Step 2: Return the named core failure value after recording its explanation. */
    return CGAI_STATUS_ERROR;
}

#endif
