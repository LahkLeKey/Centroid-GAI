/** @file error.c @brief Thread-local error reporting implementation. */

#include "internal/error.h"

#include <stdio.h>

#if defined(_MSC_VER)
#define CGAI_THREAD_LOCAL __declspec(thread)
#else
#define CGAI_THREAD_LOCAL _Thread_local
#endif

static CGAI_THREAD_LOCAL char cgai_error_message[256];

/**
 * @brief Reset the current thread's diagnostic to the empty state.
 *
 * Only the first character needs to become NUL for the buffer to represent an empty C string.
 * Other threads have independent storage. Clearing a diagnostic does not change any model or
 * status previously returned to the caller.
 *
 */
void cgai_error_clear(void) { /* Step 1: Place a string terminator at the beginning to mark the diagnostic empty. */
 cgai_error_message[0] = '\0'; }

/**
 * @brief Copy a diagnostic into bounded storage belonging to the current thread.
 *
 * snprintf copies at most the fixed buffer capacity, including its terminating NUL. Longer messages
 * are truncated. The input is borrowed only for this call, so callers need not keep their message
 * storage alive afterward. This setter does not itself return an operation status.
 *
 * @param message Non-NULL readable NUL-terminated diagnostic text to copy.
 */
void cgai_error_set(const char *message) {
    /* Step 1: Copy with a fixed %s format and an explicit destination capacity. */
    (void)snprintf(cgai_error_message, sizeof(cgai_error_message), "%s", message);
}

/**
 * @brief Return borrowed diagnostic text for the current thread.
 *
 * An empty diagnostic buffer is presented as the static no-error sentinel. Otherwise callers
 * receive the thread-local buffer directly, which can change on the next error-setting or clearing
 * operation in this thread. Copy text before another operation if it must be preserved.
 *
 * @return Borrowed NUL-terminated message or no-error sentinel; never free this pointer.
 */
const char *cgai_last_error(void) {
    /* Step 1: Choose the stored message only if its first byte is not the empty-string terminator. */
    return cgai_error_message[0] != '\0' ? cgai_error_message : "no error";
}
