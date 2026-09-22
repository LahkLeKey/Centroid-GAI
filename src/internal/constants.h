/** @file constants.h @brief Shared internal model and format constants. */

#ifndef CGAI_CONSTANTS_H
#define CGAI_CONSTANTS_H

#include <stddef.h>
#include <stdint.h>

/** Default model dimensions used by cgai_default_config(). */
#define CGAI_DEFAULT_DIMENSIONS ((size_t)32U)
/** Default number of centroids used by cgai_default_config(). */
#define CGAI_DEFAULT_CENTROID_COUNT ((size_t)24U)
/** Default context size used by cgai_default_config(). */
#define CGAI_DEFAULT_CONTEXT_WINDOW ((size_t)3U)
/** Default deterministic embedding seed. */
#define CGAI_DEFAULT_SEED UINT64_C(0xC3A17D5EED)

/** Maximum supported embedding dimensions. */
#define CGAI_MAX_DIMENSIONS ((size_t)4096U)
#define CGAI_MAX_DIMENSIONS_TEXT "4096"
/** Maximum supported centroid count. */
#define CGAI_MAX_CENTROID_COUNT ((size_t)65536U)
#define CGAI_MAX_CENTROID_COUNT_TEXT "65536"
/** Maximum supported context window. */
#define CGAI_MAX_CONTEXT_WINDOW ((size_t)65536U)
#define CGAI_MAX_CONTEXT_WINDOW_TEXT "65536"
/** Maximum serialized token length in bytes. */
#define CGAI_MAX_TOKEN_BYTES ((size_t)4096U)

/** Serialized model magic, including its terminating byte. */
#define CGAI_MODEL_MAGIC "CGAI001"
/** Number of uint64_t fields in the serialized model header. */
#define CGAI_MODEL_HEADER_FIELD_COUNT ((size_t)7U)

/** Names reserved for the initial vocabulary entries. */
#define CGAI_TOKEN_BOS_TEXT "<bos>"
#define CGAI_TOKEN_EOS_TEXT "<eos>"
#define CGAI_TOKEN_UNKNOWN_TEXT "<unk>"
/** Number of entries installed before user vocabulary is learned. */
#define CGAI_SPECIAL_TOKEN_COUNT ((size_t)3U)

/** Initial tokenizer allocation measured in token pointers. */
#define CGAI_INITIAL_TOKEN_CAPACITY ((size_t)32U)
/** Initial vocabulary allocation measured in token pointers. */
#define CGAI_INITIAL_VOCABULARY_CAPACITY ((size_t)16U)

/** Maximum CLI generation length. */
#define CGAI_MAX_GENERATION_TOKENS ((size_t)1000000U)
/** Conservative CLI output storage reserved per generated token. */
#define CGAI_OUTPUT_BYTES_PER_TOKEN ((size_t)128U)

/** First byte value treated as non-ASCII by the tokenizer. */
#define CGAI_NON_ASCII_BYTE ((unsigned char)128U)

#endif