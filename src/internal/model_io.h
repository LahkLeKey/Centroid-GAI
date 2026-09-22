/** @file model_io.h @brief Private in-memory model codec used by storage adapters. */

#ifndef CGAI_MODEL_IO_H
#define CGAI_MODEL_IO_H

#include "cgai_internal.h"

#include <stdint.h>

/**
 * @brief Encodes a model into caller-owned storage or reports required size.
 * @param model Trusted in-memory model to serialize.
 * @param output Destination bytes, or NULL for a size query.
 * @param output_size Capacity of @p output; must be zero for a NULL query.
 * @param written Receives the exact artifact size on every successful sizing pass.
 * @return CGAI_STATUS_OK, or an error for invalid arguments, overflow, or short capacity.
 * @note The format is native-endian and intended for trusted compatible builds.
 */
cgai_status cgai_model_encode(const cgai_model *model, uint8_t *output, size_t output_size,
                              size_t *written);

/**
 * @brief Decodes a complete trusted model artifact from memory.
 * @param data Artifact bytes; copied into a caller-owned model.
 * @param size Number of bytes available at @p data.
 * @return New model on success, or NULL after rejecting malformed/truncated data.
 * @ownership The returned model must be destroyed with cgai_model_destroy().
 */
cgai_model *cgai_model_decode(const uint8_t *data, size_t size);

#endif
