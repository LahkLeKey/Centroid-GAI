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

/** Returns the ABI declaration version implemented by this library. */
uint32_t cgai_abi_version(void) { return CGAI_ABI_VERSION; }

/** Returns the native library semantic version. */
const char *cgai_abi_library_version(void) { return "0.2.0"; }

/** Returns the static persistence metadata schema JSON. */
const char *cgai_abi_persistence_schema_json(void) { return persistence_schema; }

/** Returns the shared native error for FFI callers. */
const char *cgai_abi_last_error(void) { return cgai_last_error(); }
