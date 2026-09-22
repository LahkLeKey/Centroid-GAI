/** @file error.h @brief Private error-state helpers. */

#ifndef CGAI_ERROR_H
#define CGAI_ERROR_H

#include "centroid_gai.h"

/** Clears the calling thread's current error message before a new operation. */
void cgai_error_clear(void);

/** Stores a message in the calling thread's error slot and returns CGAI_STATUS_ERROR. */
cgai_status cgai_fail(const char *message);

#endif
