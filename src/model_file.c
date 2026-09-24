/** @file model_file.c @brief Filesystem persistence for serialized models. */

#include "centroid_gai.h"

#include "internal/error.h"
#include "internal/file_utils.h"
#include "internal/model_io.h"

#include <stdlib.h>

/**
 * @brief Encode a model into temporary bytes and write those bytes to a file.
 *
 * Serialization allocates an intermediate byte buffer because the file helper accepts a complete
 * artifact. That buffer is released after writing regardless of success. The destination is opened
 * for replacement; writing is not an atomic rename transaction, so a failure may leave a truncated
 * file. The model and path remain caller-owned.
 *
 * @param model Non-NULL stable model to serialize.
 * @param path Non-NULL borrowed NUL-terminated destination path.
 * @return CGAI_STATUS_OK if encoding, writing, and file close succeed; otherwise CGAI_STATUS_ERROR.
 */
cgai_status cgai_model_save(const cgai_model *model, const char *path) {
    /* Step 1: Reset diagnostics and reject missing model/path inputs. */
    cgai_error_clear();
    if (model == NULL || path == NULL) {
        return cgai_fail("model and path are required");
    }
    /* Step 2: Ask the memory encoder for the exact temporary buffer capacity. */
    size_t size = 0U;
    if (cgai_model_encode(model, NULL, 0U, &size) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    /* Step 3: Allocate and fill the artifact, cleaning up immediately if either phase fails. */
    uint8_t *data = (uint8_t *)malloc(size);
    if (data == NULL || cgai_model_encode(model, data, size, &size) != CGAI_STATUS_OK) {
        free(data);
        return data == NULL ? cgai_fail("could not allocate encoded model") : CGAI_STATUS_ERROR;
    }
    /* Step 4: Write the completed bytes, then free them while preserving the write result. */
    const cgai_status status = cgai_file_write_all(path, data, size);
    free(data);
    return status;
}

/**
 * @brief Read a complete trusted artifact and return an independently owned model.
 *
 * The file helper appends a convenience NUL byte, but the decoder receives only the actual file
 * length. Decoding copies spellings and numeric arrays, so the temporary file bytes can be freed
 * before return. The format assumes compatible native numeric representations and trusted input.
 *
 * @param path Borrowed NUL-terminated source path; NULL is rejected.
 * @return New caller-owned model to destroy with cgai_model_destroy(), or NULL with a diagnostic.
 */
cgai_model *cgai_model_load(const char *path) {
    /* Step 1: Clear earlier diagnostics and check that a path was supplied. */
    cgai_error_clear();
    if (path == NULL) {
        (void)cgai_fail("path is required");
        return NULL;
    }
    /* Step 2: Initialize temporary file-buffer ownership before reading. */
    uint8_t *data = NULL;
    size_t size = 0U;
    /* Step 3: Read the full artifact or return the file helper's failure. */
    if (cgai_file_read_all(path, &data, &size) != CGAI_STATUS_OK) {
        return NULL;
    }
    /* Step 4: Decode independent model storage, free the borrowed input copy, and return the result. */
    cgai_model *model = cgai_model_decode(data, size);
    free(data);
    return model;
}