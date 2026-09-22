/** @file file_utils.c @brief Private whole-file I/O implementation. */

#include "internal/file_utils.h"

#include "internal/error.h"
#include "internal/size_utils.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/** Opens a file, seeks to its end, and returns its byte length. */
static FILE *open_for_read(const char *path, size_t *size) {
    /* Open in binary mode so model artifacts are not newline-translated. */
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }
    const long end = ftell(file);
    /* Reject seek failures and files whose size cannot fit in size_t. */
    if (end < 0L || (uintmax_t)end > (uintmax_t)SIZE_MAX || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    *size = (size_t)end;
    return file;
}

/** Reads exact bytes from an open file and appends a NUL sentinel. */
static cgai_status read_buffer(FILE *file, size_t file_size, uint8_t **data) {
    /* Add a sentinel byte so the same helper can serve text and binary callers. */
    size_t allocation = 0U;
    if (!cgai_size_add(file_size, 1U, &allocation)) {
        return cgai_fail("file is too large");
    }
    uint8_t *buffer = (uint8_t *)malloc(allocation);
    /* fread must fill the requested payload; short reads are failures, not partial success. */
    if (buffer == NULL || fread(buffer, 1U, file_size, file) != file_size) {
        free(buffer);
        return cgai_fail("could not read complete file");
    }
    buffer[file_size] = '\0';
    /* Publish only fully read data to the caller. */
    *data = buffer;
    return CGAI_STATUS_OK;
}

cgai_status cgai_file_read_all(const char *path, uint8_t **data, size_t *size) {
    /* Initialize outputs first so callers can safely clean up on every error path. */
    if (path == NULL || data == NULL || size == NULL) {
        return cgai_fail("file path, data, and size are required");
    }
    *data = NULL;
    *size = 0U;
    size_t file_size = 0U;
    FILE *file = open_for_read(path, &file_size);
    if (file == NULL) {
        return cgai_fail("could not determine file size");
    }
    uint8_t *buffer = NULL;
    const cgai_status status = read_buffer(file, file_size, &buffer);
    /* The helper owns the stream only for the duration of this read. */
    fclose(file);
    if (status != CGAI_STATUS_OK) {
        return status;
    }
    *data = buffer;
    *size = file_size;
    return CGAI_STATUS_OK;
}

cgai_status cgai_file_write_all(const char *path, const uint8_t *data, size_t size) {
    /* Permit a NULL data pointer only for an explicitly empty file. */
    if (path == NULL || (data == NULL && size != 0U)) {
        return cgai_fail("file path and data are required");
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return cgai_fail("could not open file for writing");
    }
    const int wrote_all = fwrite(data, 1U, size, file) == size;
    /* Check both payload completion and close status before reporting success. */
    const int closed = fclose(file) == 0;
    if (!wrote_all || !closed) {
        return cgai_fail("could not write complete file");
    }
    return CGAI_STATUS_OK;
}