/** @file file_utils.h @brief Private whole-file I/O helpers. */

#ifndef CGAI_FILE_UTILS_H
#define CGAI_FILE_UTILS_H

#include "centroid_gai.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Read a seekable file into caller-owned memory and report its payload length.
 *
 * After argument validation, both outputs are initialized for safe failure cleanup. Stream ownership
 * is local and ends whether the read succeeds or fails. The returned byte allocation contains an
 * extra NUL after size payload bytes; callers release that allocation with free().
 *
 * @param path Non-NULL borrowed NUL-terminated path.
 * @param data Non-NULL address of an empty caller-owned byte pointer.
 * @param size Non-NULL writable output for the payload length, excluding the sentinel.
 * @return CGAI_STATUS_OK after a complete read, otherwise CGAI_STATUS_ERROR with a diagnostic.
 */
cgai_status cgai_file_read_all(const char *path, uint8_t **data, size_t *size);

/**
 * @brief Write an entire byte span to a binary file and verify close succeeds.
 *
 * Opening with wb replaces any existing contents. A failure is not rolled back and may leave a
 * partial file. Checking fclose matters because buffered writes can fail while being flushed at
 * close. This function borrows input memory and does not free it.
 *
 * @param path Non-NULL borrowed NUL-terminated destination path.
 * @param data Readable payload bytes; NULL is allowed only when size is zero.
 * @param size Number of payload bytes to write.
 * @return CGAI_STATUS_OK only if the requested bytes and close both succeed, otherwise CGAI_STATUS_ERROR.
 */
cgai_status cgai_file_write_all(const char *path, const uint8_t *data, size_t size);

#endif