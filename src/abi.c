/** @file abi.c @brief Fixed-width native ABI implementation for FFI consumers. */

#include "centroid_gai_abi.h"

#include "centroid_gai.h"
#include "internal/constants.h"

static const char persistence_schema[] =
    "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\","
    "\"title\":\"Centroid-GAI ModelArtifact metadata\",\"type\":\"object\","
    "\"required\":[\"formatVersion\",\"libraryVersion\",\"dimensions\","
    "\"centroidCount\",\"contextWindow\",\"vocabularySize\",\"examplesSeen\","
    "\"checksumSha256\"],\"properties\":{"
    "\"formatVersion\":{\"type\":\"integer\",\"const\":1},"
    "\"libraryVersion\":{\"type\":\"string\"},"
    "\"dimensions\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":" CGAI_MAX_DIMENSIONS_TEXT "},"
    "\"centroidCount\":{\"type\":\"integer\",\"minimum\":1,"
    "\"maximum\":" CGAI_MAX_CENTROID_COUNT_TEXT "},"
    "\"contextWindow\":{\"type\":\"integer\",\"minimum\":1,"
    "\"maximum\":" CGAI_MAX_CONTEXT_WINDOW_TEXT "},"
    "\"vocabularySize\":{\"type\":\"integer\",\"minimum\":3},"
    "\"examplesSeen\":{\"type\":\"integer\",\"minimum\":0},"
    "\"checksumSha256\":{\"type\":\"string\",\"pattern\":\"^[0-9a-f]{64}$\"}}}";

/**
 * @brief Return the ABI declaration version for foreign-runtime compatibility checks.
 *
 * The ABI version identifies the layout and calling contract of this header. It is separate from
 * the library's semantic version and from the serialized model format version. A binding can call
 * this before using configuration structures compiled against a particular ABI.
 *
 * @return The compile-time CGAI_ABI_VERSION constant; no allocation or error state is involved.
 */
uint32_t
cgai_abi_version(void) { /* Step 1: Expose the declaration version used to build this library. */
    return CGAI_ABI_VERSION;
}

/**
 * @brief Return the native library's semantic version string.
 *
 * The returned pointer refers to a string literal stored for the lifetime of the loaded library.
 * Foreign callers may copy it when storing metadata, but must never modify or free that pointer.
 * This describes the library release, not the artifact's binary layout.
 *
 * @return Borrowed NUL-terminated UTF-8 version text.
 */
const char *cgai_abi_library_version(
    void) { /* Step 1: Return the static release label without creating a caller-owned buffer. */
    return "0.2.0";
}

/**
 * @brief Expose the static JSON Schema used to describe persistence metadata.
 *
 * The concatenated C string above becomes one NUL-terminated JSON document at compilation.
 * Returning it performs no parsing, database access, or schema migration. Prisma and PostgreSQL
 * remain responsibilities of the TypeScript adapter, which can copy and parse this description.
 *
 * @return Borrowed immutable JSON text valid while this library remains loaded.
 */
const char *cgai_abi_persistence_schema_json(
    void) { /* Step 1: Return the schema's static storage; callers must not free it. */
    return persistence_schema;
}

/**
 * @brief Read the current thread's core diagnostic through the ABI.
 *
 * The ABI and core share one thread-local diagnostic buffer. The returned pointer is borrowed
 * and can be overwritten by a later operation on the same thread. Copy the message if it must
 * survive another call. Status codes, not the presence of an old message, determine success.
 *
 * @return Borrowed NUL-terminated diagnostic text, or the no-error sentinel.
 */
const char *cgai_abi_last_error(void) { /* Step 1: Forward to the shared diagnostic accessor without
                                           copying or clearing the message. */
    return cgai_last_error();
}
