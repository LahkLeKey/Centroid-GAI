/**
 * @file centroid_gai_abi.h
 * @brief Stable, fixed-width C ABI for FFI and native-language bindings.
 *
 * This interface avoids C-library-owned variable-size structs and platform-sized
 * integers at the boundary. Callers must compare cgai_abi_version() before using
 * an ABI they were compiled against. Model handles are not thread-safe for
 * mutation; callers must provide synchronization.
 */

#ifndef CENTROID_GAI_ABI_H
#define CENTROID_GAI_ABI_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(CGAI_ABI_BUILD)
#define CGAI_ABI_EXPORT __declspec(dllexport)
#else
#define CGAI_ABI_EXPORT __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define CGAI_ABI_EXPORT __attribute__((visibility("default")))
#else
#define CGAI_ABI_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CGAI_ABI_VERSION UINT32_C(2) /**< Version of every declaration in this header. */

/** Opaque model handle used only through this ABI. */
typedef struct cgai_abi_model cgai_abi_model;

/**
 * @brief Fixed-width status type suitable for foreign runtimes.
 *
 * Status values are macros rather than an enum so their width is explicit at
 * the ABI boundary. Do not persist or reinterpret values outside this set.
 */
typedef uint32_t cgai_abi_status;

#define CGAI_ABI_OK UINT32_C(0)               /**< Operation succeeded. */
#define CGAI_ABI_ERROR UINT32_C(1)            /**< Model operation failed. */
#define CGAI_ABI_INVALID_ARGUMENT UINT32_C(2) /**< A pointer or value was invalid. */
#define CGAI_ABI_BUFFER_TOO_SMALL UINT32_C(3) /**< An output buffer was too small. */
#define CGAI_ABI_VERSION_MISMATCH UINT32_C(4) /**< A struct uses another ABI version. */
#define CGAI_ABI_OUT_OF_MEMORY UINT32_C(5)    /**< The library could not allocate storage. */

/**
 * @brief Library-owned or caller-provided bytes described by an explicit length.
 *
 * Functions that produce buffers in this ABI allocate @p data with the
 * library allocator. The caller must release those buffers with
 * cgai_abi_buffer_free(), including on error paths after partial success.
 */
typedef struct cgai_abi_buffer {
    uint8_t *data; /**< Byte storage; ownership depends on the calling function. */
    size_t size;   /**< Number of initialized bytes in @p data. */
} cgai_abi_buffer;

/** ABI-stable model configuration. Initialize with cgai_abi_default_config(). */
typedef struct cgai_abi_config {
    uint32_t struct_size;    /**< Must equal sizeof(cgai_abi_config). */
    uint32_t abi_version;    /**< Must equal CGAI_ABI_VERSION. */
    uint32_t dimensions;     /**< Embedding dimensions. */
    uint32_t centroid_count; /**< Maximum centroid count. */
    uint32_t context_window; /**< Recent tokens represented in each context. */
    uint32_t reserved;       /**< Must be zero. */
    uint64_t seed;           /**< Deterministic embedding seed. */
} cgai_abi_config;

/** Queryable metadata shared with the Prisma ModelArtifact contract. */
typedef struct cgai_abi_model_metadata {
    uint32_t struct_size;     /**< Set by the library to the structure size. */
    uint32_t abi_version;     /**< ABI version that produced the metadata. */
    uint32_t format_version;  /**< Binary model format version. */
    uint32_t dimensions;      /**< Embedding dimensions. */
    uint32_t centroid_count;  /**< Configured centroid capacity. */
    uint32_t context_window;  /**< Configured context size. */
    uint64_t vocabulary_size; /**< Number of vocabulary entries. */
    uint64_t examples_seen;   /**< Number of learned transitions. */
} cgai_abi_model_metadata;

/** @return The runtime ABI version. */
CGAI_ABI_EXPORT uint32_t cgai_abi_version(void);

/** @return The library semantic version as a static UTF-8 string. */
CGAI_ABI_EXPORT const char *cgai_abi_library_version(void);

/**
 * @brief Returns a static JSON Schema describing persistence metadata.
 * @return Library-owned UTF-8 JSON valid for the process lifetime.
 */
CGAI_ABI_EXPORT const char *cgai_abi_persistence_schema_json(void);

/** @return The calling thread's last native error description. */
CGAI_ABI_EXPORT const char *cgai_abi_last_error(void);

/**
 * @brief Fills @p output with default values and ABI identity fields.
 * @pre @p output points to writable cgai_abi_config storage.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_default_config(cgai_abi_config *output);

/**
 * @brief Creates an empty native model handle.
 * @param config ABI-sized configuration whose version and size are checked.
 * @param output Receives a caller-owned opaque handle on success.
 * @ownership Release the handle with cgai_abi_model_destroy().
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_create(const cgai_abi_config *config,
                                                      cgai_abi_model **output);

/**
 * @brief Imports a complete versioned model artifact from memory.
 * @param data Caller-owned artifact bytes; copied during the call.
 * @param size Number of bytes available at @p data.
 * @param output Receives a caller-owned opaque handle on success.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_import(const uint8_t *data, size_t size,
                                                      cgai_abi_model **output);

/** Releases a native model handle; NULL is accepted. */
CGAI_ABI_EXPORT void cgai_abi_model_destroy(cgai_abi_model *model);

/**
 * @brief Adds UTF-8 training text to a model.
 * @pre @p model is mutable and @p utf8_text is NUL-terminated.
 * @note The ABI does not synchronize concurrent mutation.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_train(cgai_abi_model *model, const char *utf8_text);

/**
 * @brief Fills fixed-width metadata for a model.
 * @param output Receives only fixed-width scalar fields; no ownership transfer occurs.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_get_metadata(const cgai_abi_model *model,
                                                            cgai_abi_model_metadata *output);

/**
 * @brief Allocates a serialized model artifact.
 * @param output Receives library-owned bytes and their exact size.
 * @note Release @p output with cgai_abi_buffer_free().
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_export(const cgai_abi_model *model,
                                                      cgai_abi_buffer *output);

/** Releases bytes allocated by an ABI operation and clears the descriptor. */
CGAI_ABI_EXPORT void cgai_abi_buffer_free(cgai_abi_buffer *buffer);

/** Generates a UTF-8 continuation into a library-owned byte buffer. */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_generate(const cgai_abi_model *model,
                                                        const char *utf8_prompt,
                                                        uint32_t max_tokens, double temperature,
                                                        uint64_t seed, cgai_abi_buffer *output);

#ifdef __cplusplus
}
#endif

#endif
