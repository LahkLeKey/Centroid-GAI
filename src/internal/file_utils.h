/** @file file_utils.h @brief Private whole-file I/O helpers. */

#ifndef CGAI_FILE_UTILS_H
#define CGAI_FILE_UTILS_H

#include "centroid_gai.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Reads a complete file into a NUL-terminated caller-owned byte buffer.
 * @param path Trusted filesystem path supplied by the caller.
 * @param data Receives malloc-owned bytes; the extra NUL is not included in size.
 * @param size Receives the exact file byte count.
 * @return CGAI_STATUS_OK, or an error without publishing partial data.
 * @ownership Release @p data with free() after use.
 */
cgai_status cgai_file_read_all(const char *path, uint8_t **data, size_t *size);

/**
 * @brief Writes exactly @p size bytes from @p data to a file.
 * @param path Destination path; existing content is replaced.
 * @param data Caller-owned bytes; may be NULL only when size is zero.
 * @param size Exact byte count to write.
 * @return CGAI_STATUS_OK only after both write and close succeed.
 */
cgai_status cgai_file_write_all(const char *path, const uint8_t *data, size_t size);

#endif