/** @file model_decode.c @brief In-memory model deserialization. */

#include "internal/model_io.h"

#include "internal/constants.h"
#include "internal/error.h"
#include "internal/model_format.h"
#include "internal/vocabulary.h"

#include <stdlib.h>
#include <string.h>

typedef struct byte_reader {
    const uint8_t *data;
    size_t size;
    size_t offset;
} byte_reader;

/** Copies one bounded field from the serialized input. */
static int reader_take(byte_reader *reader, void *output, size_t size) {
    if (reader->offset > reader->size || size > reader->size - reader->offset) {
        return 0;
    }
    memcpy(output, reader->data + reader->offset, size);
    reader->offset += size;
    return 1;
}

/** Validates the magic and configuration fields of a decoded header. */
static int valid_header(const char *magic, const uint64_t *header) {
    return memcmp(magic, CGAI_MODEL_MAGIC, sizeof(CGAI_MODEL_MAGIC)) == 0 &&
           header[CGAI_HEADER_DIMENSIONS] != 0U && header[CGAI_HEADER_CENTROID_COUNT] != 0U &&
           header[CGAI_HEADER_CONTEXT_WINDOW] != 0U &&
           header[CGAI_HEADER_DIMENSIONS] <= CGAI_MAX_DIMENSIONS &&
           header[CGAI_HEADER_CENTROID_COUNT] <= CGAI_MAX_CENTROID_COUNT &&
           header[CGAI_HEADER_CONTEXT_WINDOW] <= CGAI_MAX_CONTEXT_WINDOW &&
           header[CGAI_HEADER_VOCABULARY_SIZE] >= CGAI_SPECIAL_TOKEN_COUNT &&
           header[CGAI_HEADER_VOCABULARY_SIZE] <= SIZE_MAX;
}

/** Reads and validates the fixed serialized header. */
static int read_header(byte_reader *reader, uint64_t *header) {
    /* The magic protects against unrelated files; the header limits resource use. */
    char magic[sizeof(CGAI_MODEL_MAGIC)];
    return reader_take(reader, magic, sizeof(magic)) &&
           reader_take(reader, header, sizeof(uint64_t) * CGAI_MODEL_HEADER_FIELD_COUNT) &&
           valid_header(magic, header);
}

/** Converts validated header fields into a newly allocated model. */
static cgai_model *create_from_header(const uint64_t *header) {
    const cgai_config config = {
        (size_t)header[CGAI_HEADER_DIMENSIONS], (size_t)header[CGAI_HEADER_CENTROID_COUNT],
        (size_t)header[CGAI_HEADER_CONTEXT_WINDOW], header[CGAI_HEADER_SEED]};
    return cgai_model_create(&config);
}

/** Releases constructor vocabulary so the serialized vocabulary can replace it. */
static void clear_constructor_vocabulary(cgai_model *model) {
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        free(model->vocabulary[i]);
    }
    free(model->vocabulary);
    free(model->token_counts);
    model->vocabulary = NULL;
    model->token_counts = NULL;
    model->vocabulary_size = 0U;
    model->vocabulary_capacity = 0U;
}

/** Reads serialized vocabulary strings and rebuilds their count rows. */
static int read_vocabulary(byte_reader *reader, cgai_model *model, uint64_t count) {
    /* Rebuilding through vocabulary_add also recreates correctly sized count rows. */
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t length = 0U;
        if (!reader_take(reader, &length, sizeof(length)) || length > CGAI_MAX_TOKEN_BYTES) {
            return 0;
        }
        char *token = (char *)malloc((size_t)length + 1U);
        if (token == NULL || !reader_take(reader, token, (size_t)length)) {
            free(token);
            return 0;
        }
        token[length] = '\0';
        const int added = cgai_token_id_is_valid(cgai_vocabulary_add(model, token));
        free(token);
        if (!added) {
            return 0;
        }
    }
    return 1;
}

/** Reads fixed numeric payloads and confirms that no bytes remain. */
static int read_numeric_payload(byte_reader *reader, cgai_model *model) {
    /* reader_take prevents every copy from crossing the supplied artifact boundary. */
    return reader_take(reader, model->centroids,
                       model->config.centroid_count * model->config.dimensions * sizeof(float)) &&
           reader_take(reader, model->cluster_sizes,
                       model->config.centroid_count * sizeof(uint64_t)) &&
           reader_take(reader, model->token_counts,
                       model->config.centroid_count * model->vocabulary_size * sizeof(uint64_t)) &&
           reader->offset == reader->size;
}

/** Restores vocabulary, metadata, and numeric arrays after model creation. */
static int restore_model_body(byte_reader *reader, cgai_model *model, const uint64_t *header) {
    clear_constructor_vocabulary(model);
    if (!read_vocabulary(reader, model, header[CGAI_HEADER_VOCABULARY_SIZE])) {
        return 0;
    }
    model->initialized_centroids = (size_t)header[CGAI_HEADER_INITIALIZED_CENTROIDS];
    model->examples_seen = (size_t)header[CGAI_HEADER_EXAMPLES_SEEN];
    if (model->initialized_centroids > model->config.centroid_count) {
        return 0;
    }
    return read_numeric_payload(reader, model);
}

cgai_model *cgai_model_decode(const uint8_t *data, size_t size) {
    if (data == NULL) {
        (void)cgai_fail("model bytes are required");
        return NULL;
    }
    byte_reader reader = {data, size, 0U};
    uint64_t header[CGAI_MODEL_HEADER_FIELD_COUNT];
    /* Validate the fixed header before allocating any variable model storage. */
    if (!read_header(&reader, header)) {
        (void)cgai_fail("invalid or unsupported model data");
        return NULL;
    }
    cgai_model *model = create_from_header(header);
    if (model == NULL) {
        return NULL;
    }
    /* Rebuild vocabulary rows, metadata, and arrays in serialized order. */
    if (!restore_model_body(&reader, model, header)) {
        cgai_model_destroy(model);
        (void)cgai_fail("model data is truncated or invalid");
        return NULL;
    }
    return model;
}