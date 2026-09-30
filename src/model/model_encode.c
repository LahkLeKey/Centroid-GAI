/** @file model_encode.c @brief In-memory model serialization. */

#include "internal/model_io.h"

#include "internal/constants.h"
#include "internal/error.h"
#include "internal/size_utils.h"

#include <string.h>

/**
 * @brief Borrowed output allocation and its initialized byte prefix.
 *
 * Appends move offset forward without changing capacity or taking ownership of
 * data. The top-level encoder checks the measured size before constructing this
 * cursor; each append also checks its own remaining capacity.
 */
typedef struct byte_writer {
    uint8_t *data;   /**< Borrowed writable destination memory. */
    size_t capacity; /**< Total available destination bytes. */
    size_t offset;   /**< Initialized byte count and position of the next write. */
} byte_writer;

/**
 * @brief Copy a bounded byte field into the next position of a serialized artifact.
 *
 * Offset tracks already-written bytes. Comparing size against capacity - offset avoids overflowing
 * an offset + size test. A failed bounds check leaves both destination bytes and offset unchanged.
 * No allocation occurs; this cursor borrows the caller's complete output buffer.
 *
 * @param writer Non-NULL cursor with writable data and its total capacity.
 * @param data Readable source span that does not overlap the destination.
 * @param size Number of source bytes to append.
 * @return One on success with offset advanced, or zero if the field does not fit.
 */
static int writer_append(byte_writer *writer, const void *data, size_t size) {
    /* Step 1: Check cursor validity and remaining capacity before copying. */
    if (writer->offset > writer->capacity || size > writer->capacity - writer->offset) {
        return 0;
    }
    /* Step 2: Copy the field at the current offset. */
    memcpy(writer->data + writer->offset, data, size);
    /* Step 3: Advance the cursor so the next field follows this one. */
    writer->offset += size;
    return 1;
}

/**
 * @brief Write the magic bytes and fixed model metadata in format order.
 *
 * The uint64_t array's order matches CGAI_HEADER_* indices used by decoding. Values are copied
 * in native machine representation; this is not an endian-converting protocol. sizeof the magic
 * array includes its defined complete byte sequence, including the C string terminator.
 *
 * @param writer Borrowed writable artifact cursor.
 * @param model Non-NULL stable model supplying header fields.
 * @return One if both magic and metadata fit, or zero after a bounded write fails.
 */
static int write_header(byte_writer *writer, const cgai_model *model) {
    /* Step 1: Convert native counters and dimensions into the format's fixed-width field sequence.
     */
    const uint64_t header[] = {
        (uint64_t)model->config.dimensions,     (uint64_t)model->config.centroid_count,
        (uint64_t)model->config.context_window, model->config.seed,
        (uint64_t)model->vocabulary_size,       (uint64_t)model->initialized_centroids,
        (uint64_t)model->examples_seen};
    /* Step 2: Append magic first, then the header; short-circuit if the first write fails. */
    return writer_append(writer, CGAI_MODEL_MAGIC, sizeof(CGAI_MODEL_MAGIC)) &&
           writer_append(writer, header, sizeof(header));
}

/**
 * @brief Serialize vocabulary spellings in stable identifier order.
 *
 * Every record stores a uint64_t byte length followed by that many spelling bytes. The NUL
 * terminator is excluded because the decoder allocates and supplies its own. Preserving insertion
 * order preserves the token IDs referenced by the following count matrix.
 *
 * @param writer Non-NULL writable cursor positioned after the header.
 * @param model Borrowed model with stable NUL-terminated vocabulary spellings.
 * @return One after every spelling is written, otherwise zero at the first failed append.
 */
static int write_vocabulary(byte_writer *writer, const cgai_model *model) {
    /* Step 1: Walk occupied vocabulary entries in identifier order. */
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        /* Step 2: Measure spelling bytes without their in-memory terminator. */
        const uint64_t length = (uint64_t)strlen(model->vocabulary[i]);
        /* Step 3: Append each length and spelling as a pair, stopping if either exceeds capacity.
         */
        if (!writer_append(writer, &length, sizeof(length)) ||
            !writer_append(writer, model->vocabulary[i], (size_t)length)) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Append centroid vectors, cluster counts, and token frequencies in order.
 *
 * The arrays are flat contiguous allocations, so each can be copied as one byte span. Decoder
 * order must match these writes exactly. The surrounding encode operation has already checked the
 * overall size arithmetic and requires a stable model for the duration of serialization.
 *
 * @param writer Non-NULL writable cursor positioned after vocabulary records.
 * @param model Borrowed model whose numeric arrays match its configuration and vocabulary size.
 * @return One if all three arrays fit, otherwise zero without attempting later arrays.
 */
static int write_numeric_payload(byte_writer *writer, const cgai_model *model) {
    /* Step 1: Write centroid float components, then observation counters, then the row-major
     * token-count matrix. */
    return writer_append(writer, model->centroids,
                         model->config.centroid_count * model->config.dimensions * sizeof(float)) &&
           writer_append(writer, model->cluster_sizes,
                         model->config.centroid_count * sizeof(uint64_t)) &&
           writer_append(writer, model->token_counts,
                         model->config.centroid_count * model->vocabulary_size * sizeof(uint64_t));
}

/**
 * @brief Compute the exact number of bytes required by a complete model artifact.
 *
 * The calculation mirrors encoder field order: magic/header, length-prefixed spellings, then
 * numeric arrays. Counts are converted to bytes with sizeof the stored element type. Checked
 * addition and multiplication reject wraparound so the caller never allocates a truncated size.
 *
 * @param model Non-NULL stable model with consistent owned storage.
 * @param size Non-NULL output assigned only after the complete calculation succeeds.
 * @return CGAI_STATUS_OK with an exact size, otherwise CGAI_STATUS_ERROR with an overflow
 * diagnostic.
 */
static cgai_status encoded_size(const cgai_model *model, size_t *size) {
    /* Size is computed before writing so the caller can query capacity safely. */
    /* Step 1: Start with the fixed header and accumulate every vocabulary record using checked
     * addition. */
    size_t total = sizeof(CGAI_MODEL_MAGIC) + CGAI_MODEL_HEADER_FIELD_COUNT * sizeof(uint64_t);
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        if (!cgai_size_add(total, sizeof(uint64_t), &total) ||
            !cgai_size_add(total, strlen(model->vocabulary[i]), &total)) {
            return cgai_fail("encoded model is too large");
        }
    }
    /* Step 2: Keep cell counts separate from byte counts for each numeric array. */
    size_t centroid_values = 0U;
    size_t centroid_bytes = 0U;
    size_t cluster_bytes = 0U;
    size_t token_count_values = 0U;
    size_t token_count_bytes = 0U;
    /* Step 3: Check all numeric products and their addition to the total before publishing it. */
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
    /* Step 4: Return the verified byte count through the caller's output pointer. */
    *size = total;
    return CGAI_STATUS_OK;
}

/**
 * @brief Measure or serialize a model into caller-owned byte storage.
 *
 * A NULL output with zero capacity is a size query and performs no allocation. After sizing
 * succeeds, written receives the required byte count even if a supplied destination is too small.
 * The three write stages must match decoding order. Callers must prevent mutation throughout the
 * operation, especially between a size query and a later encoding call.
 *
 * @param model Non-NULL borrowed model to serialize.
 * @param output Writable byte buffer, or NULL for a size-only query.
 * @param output_size Destination capacity; must be zero when output is NULL.
 * @param written Non-NULL output receiving the exact size after successful measurement.
 * @return CGAI_STATUS_OK for a valid size query or complete encoding, otherwise CGAI_STATUS_ERROR.
 */
cgai_status cgai_model_encode(const cgai_model *model, uint8_t *output, size_t output_size,
                              size_t *written) {
    /* Step 1: Require the model and an address for the measured byte count. */
    if (model == NULL || written == NULL) {
        return cgai_fail("model and encoded-size output are required");
    }
    /* Step 2: Measure the artifact and publish its required capacity. */
    size_t required = 0U;
    if (encoded_size(model, &required) != CGAI_STATUS_OK) {
        return CGAI_STATUS_ERROR;
    }
    *written = required;
    /* Step 3: Handle the size-query mode without touching a destination buffer. */
    if (output == NULL) {
        return output_size == 0U ? CGAI_STATUS_OK
                                 : cgai_fail("model output is null but capacity is nonzero");
    }
    /* Step 4: Reject a short destination before beginning serialization. */
    if (output_size < required) {
        return cgai_fail("model output buffer is too small");
    }

    /* The decoder depends on this exact order: header, strings, then numeric arrays. */
    /* Step 5: Write header, vocabulary, and arrays through a bounded cursor, then confirm the final
     * length. */
    byte_writer writer = {output, output_size, 0U};
    const int ok = write_header(&writer, model) && write_vocabulary(&writer, model) &&
                   write_numeric_payload(&writer, model);
    return ok && writer.offset == required ? CGAI_STATUS_OK
                                           : cgai_fail("could not encode complete model");
}
