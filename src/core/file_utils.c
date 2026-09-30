/** @file file_utils.c @brief Private whole-file I/O implementation. */

#include "internal/file_utils.h"

#include "internal/error.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Open a binary file and measure its length with a seekable stream.
 *
 * Binary mode preserves serialized bytes on platforms that translate text newlines. fseek/ftell
 * require a seekable file; this is not a streaming-reader interface. The stream is rewound before
 * return. This helper closes it on every local failure, leaving closure of a successful result
 * to the caller.
 *
 * @param path Non-NULL borrowed NUL-terminated filesystem path.
 * @param size Non-NULL output assigned the file length only after successful measurement and
 * rewind.
 * @return Owned open FILE pointer, or NULL if opening, seeking, or representability checks fail.
 */
static FILE *open_for_read(const char *path, size_t *size) {
    /* Open in binary mode so model artifacts are not newline-translated. */
    /* Step 1: Open in binary mode and seek to the end, closing a partially opened stream on
     * failure. */
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }
    /* Step 2: Measure the end offset and verify it can become a size_t byte length. */
    const long end = ftell(file);
    /* Reject seek failures and files whose size cannot fit in size_t. */
    if (end < 0L || (uintmax_t)end > (uintmax_t)SIZE_MAX || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    /* Step 3: Publish the measured length after successful rewind and transfer stream ownership. */
    *size = (size_t)end;
    return file;
}

/**
 * @brief Read an exact file payload into an owned buffer with a convenience NUL.
 *
 * The allocation has one extra byte beyond the binary payload. That byte permits corpus callers
 * to treat the result as a C string, but it is not included in the file's reported length. A short
 * read fails rather than returning partial data. This helper borrows the stream and never closes
 * it.
 *
 * @param file Non-NULL readable stream positioned at the desired payload start.
 * @param file_size Exact payload bytes expected from the measured file.
 * @param data Non-NULL output receiving allocation ownership only on success.
 * @return CGAI_STATUS_OK with an owned buffer, otherwise CGAI_STATUS_ERROR without publishing
 * partial storage.
 */
static cgai_status read_buffer(FILE *file, size_t file_size, uint8_t **data) {
    /* Add a sentinel byte so the same helper can serve text and binary callers. */
    /* Step 1: Ensure adding the terminator byte cannot wrap the allocation size. */
    if (file_size == SIZE_MAX) {
        return cgai_fail("file is too large");
    }
    /* Step 2: Allocate payload plus sentinel and reject allocation failure. */
    uint8_t *buffer = (uint8_t *)malloc(file_size + 1U);
    if (buffer == NULL) {
        return cgai_fail("could not allocate file buffer");
    }
    /* Step 3: Initialize the sentinel outside the payload range. */
    buffer[file_size] = '\0';
    /* fread must fill the requested payload; short reads are failures, not partial success. */
    /* Step 4: Require the complete payload; release the allocation on a short or failed read. */
    if (fread(buffer, 1U, file_size, file) != file_size) {
        free(buffer);
        return cgai_fail("could not read complete file");
    }
    /* Publish only fully read data to the caller. */
    /* Step 5: Transfer the completely initialized buffer to the caller. */
    *data = buffer;
    return CGAI_STATUS_OK;
}

/**
 * @brief Read a seekable file into caller-owned memory and report its payload length.
 *
 * After argument validation, both outputs are initialized for safe failure cleanup. Stream
 * ownership is local and ends whether the read succeeds or fails. The returned byte allocation
 * contains an extra NUL after size payload bytes; callers release that allocation with free().
 *
 * @param path Non-NULL borrowed NUL-terminated path.
 * @param data Non-NULL address of an empty caller-owned byte pointer.
 * @param size Non-NULL writable output for the payload length, excluding the sentinel.
 * @return CGAI_STATUS_OK after a complete read, otherwise CGAI_STATUS_ERROR with a diagnostic.
 */
cgai_status cgai_file_read_all(const char *path, uint8_t **data, size_t *size) {
    /* Initialize outputs first so callers can safely clean up on every error path. */
    /* Step 1: Validate all pointers before initializing either output. */
    if (path == NULL || data == NULL || size == NULL) {
        return cgai_fail("file path, data, and size are required");
    }
    /* Step 2: Establish empty failure outputs and measure the opened binary stream. */
    *data = NULL;
    *size = 0U;
    size_t file_size = 0U;
    FILE *file = open_for_read(path, &file_size);
    if (file == NULL) {
        return cgai_fail("could not determine file size");
    }
    uint8_t *buffer = NULL;
    /* Step 3: Read the payload and close the stream before handling the read result. */
    const cgai_status status = read_buffer(file, file_size, &buffer);
    /* The helper owns the stream only for the duration of this read. */
    fclose(file);
    if (status != CGAI_STATUS_OK) {
        return status;
    }
    /* Step 4: Publish buffer ownership and length only after the full read succeeds. */
    *data = buffer;
    *size = file_size;
    return CGAI_STATUS_OK;
}

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
 * @return CGAI_STATUS_OK only if the requested bytes and close both succeed, otherwise
 * CGAI_STATUS_ERROR.
 */
cgai_status cgai_file_write_all(const char *path, const uint8_t *data, size_t size) {
    /* Permit a NULL data pointer only for an explicitly empty file. */
    /* Step 1: Reject a missing path or a missing nonempty payload. */
    if (path == NULL || (data == NULL && size != 0U)) {
        return cgai_fail("file path and data are required");
    }
    /* Step 2: Open the destination in binary replacement mode. */
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return cgai_fail("could not open file for writing");
    }
    /* Step 3: Write the requested span, then close and flush the stream. */
    const int wrote_all = fwrite(data, 1U, size, file) == size;
    /* Check both payload completion and close status before reporting success. */
    const int closed = fclose(file) == 0;
    /* Step 4: Report failure from either the payload write or final close. */
    if (!wrote_all || !closed) {
        return cgai_fail("could not write complete file");
    }
    return CGAI_STATUS_OK;
}
