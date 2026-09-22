/** @file error.c @brief Thread-local error reporting implementation. */

#include "internal/error.h"

#include <stdio.h>

#if defined(_MSC_VER)
#define CGAI_THREAD_LOCAL __declspec(thread)
#else
#define CGAI_THREAD_LOCAL _Thread_local
#endif

static CGAI_THREAD_LOCAL char cgai_error_message[256];

/** Clears the calling thread's last error. */
void cgai_error_clear(void) { cgai_error_message[0] = '\0'; }

/** Stores a bounded error message and returns the library failure status. */
cgai_status cgai_fail(const char *message) {
    (void)snprintf(cgai_error_message, sizeof(cgai_error_message), "%s", message);
    return CGAI_STATUS_ERROR;
}

/** Returns the calling thread's last error or the no-error sentinel. */
const char *cgai_last_error(void) {
    return cgai_error_message[0] != '\0' ? cgai_error_message : "no error";
}
