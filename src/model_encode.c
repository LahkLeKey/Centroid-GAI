/** @file model_encode.c @brief In-memory model serialization. */

#include "internal/model_io.h"

#include "internal/constants.h"
#include "internal/error.h"
#include "internal/size_utils.h"

#include <string.h>

typedef struct byte_writer {
    uint8_t *data;
    size_t capacity;
    size_t offset;
} byte_writer;

/** Appends one bounded field to the serialized output. */
static int writer_append(byte_writer *writer, const void *data, size_t size) {
    if (writer->offset > writer->capacity || size > writer->capacity - writer->offset) {
        return 0;
    }
    memcpy(writer->data + writer->offset, data, size);
    writer->offset += size;
    return 1;
}

/** Writes the fixed model header in the versioned field order. */
static int write_header(byte_writer *writer, const cgai_model *model) {
    const uint64_t header[] = {
        (uint64_t)model->config.dimensions,     (uint64_t)model->config.centroid_count,
        (uint64_t)model->config.context_window, model->config.seed,
        (uint64_t)model->vocabulary_size,       (uint64_t)model->initialized_centroids,
        (uint64_t)model->examples_seen};
    return writer_append(writer, CGAI_MODEL_MAGIC, sizeof(CGAI_MODEL_MAGIC)) &&
           writer_append(writer, header, sizeof(header));
}

/** Writes each vocabulary spelling with its explicit byte length. */
static int write_vocabulary(byte_writer *writer, const cgai_model *model) {
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        const uint64_t length = (uint64_t)strlen(model->vocabulary[i]);
        if (!writer_append(writer, &length, sizeof(length)) ||
            !writer_append(writer, model->vocabulary[i], (size_t)length)) {
            return 0;
        }
    }
    return 1;
}

/** Writes centroids, cluster sizes, and token counts in payload order. */
static int write_numeric_payload(byte_writer *writer, const cgai_model *model) {
    return writer_append(writer, model->centroids,
                         model->config.centroid_count * model->config.dimensions * sizeof(float)) &&
           writer_append(writer, model->cluster_sizes,
                         model->config.centroid_count * sizeof(uint64_t)) &&
           writer_append(writer, model->token_counts,
                         model->config.centroid_count * model->vocabulary_size * sizeof(uint64_t));
}

/** Computes the exact serialized size without writing model bytes. */
static cgai_status encoded_size(const cgai_model *model, size_t *size) {
    /* Size is computed before writing so the caller can query capacity safely. */
    size_t total = sizeof(CGAI_MODEL_MAGIC) + CGAI_MODEL_HEADER_FIELD_COUNT * sizeof(uint64_t);
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        if (!cgai_size_add(total, sizeof(uint64_t), &total) ||
            !cgai_size_add(total, strlen(model->vocabulary[i]), &total)) {
            return cgai_fail("encoded model is too large");
        }
    }
    size_t centroid_values = 0U;
    size_t centroid_bytes = 0U;
    size_t cluster_bytes = 0U;
    size_t token_count_values = 0U;
    size_t token_count_bytes = 0U;
    if (!cgai_size_mul(model->config.centroid_count, model->config.dimensions, &centroid_values) ||
        !cgai_size_mul(centroid_values, sizeof(float), &centroid_bytes) ||
        !cgai_size_mul(model->config.centroid_count, sizeof(uint64_t), &cluster_bytes) ||
        !cgai_size_mul(model->config.centroid_count, model->vocabulary_size, &token_count_values) ||
        !cgai_size_mul(token_count_values, sizeof(uint64_t), &token_count_bytes) ||
        !cgai_size_add(total, centroid_bytes, &total) ||
        !cgai_size_add(total, cluster_bytes, &total) ||
        !cgai_size_add(total, token_count_bytes, &total)) {
        return cgai_fail("encoded model is too large");
    }
    *size = total;
    return CGAI_STATUS_OK;
}

/** Encodes a model into caller-owned storage or reports its required size. */
cgai_status cgai_model_encode(const cgai_model *model, uint8_t *output, size_t output_size,
                              size_t *written) {
    if (model == NULL || written == NULL) {
        return cgai_fail("model and encoded-size output are required");
    }
    size_t required = 0U;
    if (encoded_size(model, &required) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    *written = required;
    if (output == NULL) {
        return output_size == 0U ? CGAI_STATUS_OK
                                 : cgai_fail("model output is null but capacity is nonzero");
    }
    if (output_size < required) {
        return cgai_fail("model output buffer is too small");
    }

    /* The decoder depends on this exact order: header, strings, then numeric arrays. */
    byte_writer writer = {output, output_size, 0U};
    const int ok = write_header(&writer, model) && write_vocabulary(&writer, model) &&
                   write_numeric_payload(&writer, model);
    return ok && writer.offset == required ? CGAI_STATUS_OK
                                           : cgai_fail("could not encode complete model");
}