/** @file model_io.h @brief Private in-memory model codec used by storage adapters. */

#ifndef CGAI_MODEL_IO_H
#define CGAI_MODEL_IO_H

#include "cgai_internal.h"

#include <stdint.h>

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
                              size_t *written);

/**
 * @brief Decode a complete trusted artifact into a new owned model.
 *
 * Input bytes remain borrowed; every string and numeric array in the returned model has separate
 * storage. The decoder validates fixed fields before allocation, reconstructs the body, and destroys
 * a partial model on any later failure. Native byte order and numeric representations require
 * compatible producing/consuming builds.
 *
 * @param data Non-NULL readable artifact bytes, borrowed until this call returns.
 * @param size Complete byte length of the supplied artifact.
 * @return Owned model to release with cgai_model_destroy(), or NULL with a thread-local diagnostic.
 */
cgai_model *cgai_model_decode(const uint8_t *data, size_t size);

#endif
