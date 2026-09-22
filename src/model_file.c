/** @file model_file.c @brief Filesystem persistence for serialized models. */

#include "centroid_gai.h"

#include "internal/error.h"
#include "internal/file_utils.h"
#include "internal/model_io.h"

#include <stdlib.h>

/** Serializes a model and writes the complete artifact to a trusted path. */
cgai_status cgai_model_save(const cgai_model *model, const char *path) {
    cgai_error_clear();
    if (model == NULL || path == NULL) {
        return cgai_fail("model and path are required");
    }
    size_t size = 0U;
    if (cgai_model_encode(model, NULL, 0U, &size) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    uint8_t *data = (uint8_t *)malloc(size);
    if (data == NULL || cgai_model_encode(model, data, size, &size) != CGAI_STATUS_OK) {
        free(data);
        return data == NULL ? cgai_fail("could not allocate encoded model") : CGAI_STATUS_ERROR;
    }
    const cgai_status status = cgai_file_write_all(path, data, size);
    free(data);
    return status;
}

/** Reads a complete artifact from a trusted path and decodes it. */
cgai_model *cgai_model_load(const char *path) {
    cgai_error_clear();
    if (path == NULL) {
        (void)cgai_fail("path is required");
        return NULL;
    }
    uint8_t *data = NULL;
    size_t size = 0U;
    if (cgai_file_read_all(path, &data, &size) != CGAI_STATUS_OK) {
        return NULL;
    }
    cgai_model *model = cgai_model_decode(data, size);
    free(data);
    return model;
}