/** @file abi_buffer.c @brief Ownership of buffers returned by the ABI. */

#include "centroid_gai_abi.h"
#include <stdlib.h>

/**
 * @brief Release an allocation returned in an ABI buffer and reset its descriptor.
 *
 * The descriptor itself is caller-owned and is never freed here. Only data returned by an ABI
 * buffer-producing operation may be passed to this function. Resetting both fields makes a second
 * call on the same descriptor harmless. Copies of that descriptor still contain a stale pointer;
 * resetting one copy does not reset another.
 *
 * @param buffer Writable ABI buffer descriptor, or NULL when there is nothing to release.
 */
void cgai_abi_buffer_free(cgai_abi_buffer *buffer) {
    /* Step 1: Accept NULL so callers can use the same cleanup path after partial initialization. */
    if (buffer != NULL) {
        /* Step 2: Release the ABI allocation; the C allocator also accepts a NULL data pointer. */
        free(buffer->data);
        /* Step 3: Clear the caller's descriptor so it no longer advertises released storage. */
        buffer->data = NULL;
        buffer->size = 0U;
    }
}
