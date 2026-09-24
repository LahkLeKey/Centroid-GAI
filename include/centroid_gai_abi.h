/**
 * @file centroid_gai_abi.h
 * @brief Stable, fixed-width C ABI for FFI and native-language bindings.
 *
 * Configuration fields and status values use explicit integer widths. Buffer
 * sizes use the platform's size_t, so bindings must match the library's pointer
 * width and calling convention. Callers must compare cgai_abi_version() before using
 * an ABI they were compiled against. Model handles are not thread-safe for
 * mutation; callers must provide synchronization.
 */

#ifndef CENTROID_GAI_ABI_H
#define CENTROID_GAI_ABI_H

#include <stddef.h>
#include <stdint.h>

/** Export/import decoration selected for the shared library's platform and build side. */
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
 * @brief Caller-owned descriptor for an allocation returned by this ABI.
 *
 * Functions that produce buffers in this ABI allocate @p data with the
 * library allocator. The caller must release those buffers with
 * cgai_abi_buffer_free(), including on error paths after partial success.
 */
typedef struct cgai_abi_buffer {
    uint8_t *data; /**< ABI-allocated bytes to release with cgai_abi_buffer_free(), or NULL. */
    size_t size;   /**< Artifact bytes or generated text bytes; text's trailing NUL is excluded. */
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

/**
 * @brief Return the ABI declaration version for foreign-runtime compatibility checks.
 *
 * The ABI version identifies the layout and calling contract of this header. It is separate from
 * the library's semantic version and from the serialized model format version. A binding can call
 * this before using configuration structures compiled against a particular ABI.
 *
 * @return The compile-time CGAI_ABI_VERSION constant; no allocation or error state is involved.
 */
CGAI_ABI_EXPORT uint32_t cgai_abi_version(void);

/**
 * @brief Return the native library's semantic version string.
 *
 * The returned pointer refers to a string literal stored for the lifetime of the loaded library.
 * Foreign callers may copy it when storing metadata, but must never modify or free that pointer.
 * This describes the library release, not the artifact's binary layout.
 *
 * @return Borrowed NUL-terminated UTF-8 version text.
 */
CGAI_ABI_EXPORT const char *cgai_abi_library_version(void);

/**
 * @brief Expose the static JSON Schema used to describe persistence metadata.
 *
 * The concatenated C string above becomes one NUL-terminated JSON document at compilation.
 * Returning it performs no parsing, database access, or schema migration. Prisma and PostgreSQL
 * remain responsibilities of the TypeScript adapter, which can copy and parse this description.
 *
 * @return Borrowed immutable JSON text valid while this library remains loaded.
 */
CGAI_ABI_EXPORT const char *cgai_abi_persistence_schema_json(void);

/**
 * @brief Read the current thread's core diagnostic through the ABI.
 *
 * The ABI and core share one thread-local diagnostic buffer. The returned pointer is borrowed
 * and can be overwritten by a later operation on the same thread. Copy the message if it must
 * survive another call. Status codes, not the presence of an old message, determine success.
 *
 * @return Borrowed NUL-terminated diagnostic text, or the no-error sentinel.
 */
CGAI_ABI_EXPORT const char *cgai_abi_last_error(void);

/**
 * @brief Translate the core defaults into the versioned ABI configuration layout.
 *
 * The core configuration uses size_t for native array sizes, while the ABI configuration uses
 * explicit integer widths for its configuration fields. The default limits fit those fields.
 * A local result is populated completely before it is copied to the caller. The reserved field
 * is set to zero because nonzero values are rejected by model creation.
 *
 * @param output Non-NULL pointer to writable cgai_abi_config storage; no allocation is returned.
 * @return CGAI_ABI_OK when the structure is filled, or CGAI_ABI_INVALID_ARGUMENT with a native diagnostic.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_default_config(cgai_abi_config *output);

/**
 * @brief Validate the ABI configuration and create an opaque, owned model handle.
 *
 * A pointer-to-pointer lets this function place a newly allocated handle in the caller's variable.
 * After both input pointers are accepted, that variable is cleared before any validation can fail.
 * The ABI handle is the core model pointer viewed through an opaque type; no second model is copied.
 * The caller must eventually destroy a successful handle with cgai_abi_model_destroy().
 *
 * @param config Non-NULL configuration initialized for this ABI's size and version.
 * @param output Non-NULL address of the caller's handle variable; receives NULL on subsequent failure.
 * @return CGAI_ABI_OK, INVALID_ARGUMENT for missing pointers or reserved bits, VERSION_MISMATCH for
 * incompatible layout/version, or ERROR when core validation or allocation fails.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_create(const cgai_abi_config *config,
                                                      cgai_abi_model **output);

/**
 * @brief Decode borrowed artifact bytes into a separately owned model handle.
 *
 * The byte range only needs to stay alive for this call: decoding reconstructs owned model arrays
 * and strings. The format uses native numeric representations and is intended for trusted artifacts
 * from compatible builds. Validation rejects many malformed inputs but is not an untrusted-file
 * sandbox. The caller destroys a returned model with cgai_abi_model_destroy().
 *
 * @param data Non-NULL readable byte range containing the entire artifact.
 * @param size Number of readable bytes beginning at data.
 * @param output Non-NULL address of the destination handle; cleared after pointer validation.
 * @return CGAI_ABI_OK, INVALID_ARGUMENT for NULL pointers, or ERROR when decoding fails.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_import(const uint8_t *data, size_t size,
                                                      cgai_abi_model **output);

/**
 * @brief Destroy a model that was created or imported through the ABI.
 *
 * Converting the opaque handle restores its internal pointer type without allocating or copying.
 * The core destructor frees every model-owned array and the model itself. The caller's handle
 * variable is passed by value, so it is not reset; callers should set their own variable to NULL
 * if it might otherwise be reused. Destroying the same non-NULL handle twice is invalid.
 *
 * @param model Owned model handle to release, or NULL for a no-op.
 */
CGAI_ABI_EXPORT void cgai_abi_model_destroy(cgai_abi_model *model);

/**
 * @brief Apply another training corpus to an existing ABI model.
 *
 * The text is borrowed and is not retained after the call. Training changes vocabulary, centroid
 * means, and transition counts in place. A failure is not a transaction rollback: some learning
 * or vocabulary growth may already have occurred. Callers must serialize mutation of a model.
 * Core success is CGAI_STATUS_OK, while ABI success is CGAI_ABI_OK; compare the named constants.
 *
 * @param model Non-NULL mutable handle owned by the caller.
 * @param utf8_text Non-NULL NUL-terminated corpus; empty or whitespace-only text fails in the core.
 * @return CGAI_ABI_OK on success, INVALID_ARGUMENT for missing pointers, or ERROR for a core failure.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_train(cgai_abi_model *model, const char *utf8_text);

/**
 * @brief Copy model dimensions and learned-state counters into ABI metadata.
 *
 * The result is a scalar snapshot: it contains no pointers into the model and owns no heap storage.
 * Configuration dimensions are narrowed to the fixed widths allowed by the model's limits; counters
 * are widened to uint64_t. Callers must keep the model alive and avoid concurrent mutation while
 * the snapshot is read. This function does not serialize the model or access PostgreSQL.
 *
 * @param model Non-NULL borrowed model handle.
 * @param output Non-NULL writable destination for a complete metadata structure.
 * @return CGAI_ABI_OK after copying the snapshot, or CGAI_ABI_INVALID_ARGUMENT for NULL inputs.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_get_metadata(const cgai_abi_model *model,
                                                            cgai_abi_model_metadata *output);

/**
 * @brief Allocate a complete binary artifact from a borrowed model handle.
 *
 * Pass an empty descriptor; replacing an existing data pointer here would lose that allocation.
 * On success the caller owns the descriptor's lifetime and must release its data through
 * cgai_abi_buffer_free(). The bytes contain model state only; database records and checksums are
 * managed by the TypeScript persistence layer. Concurrent model mutation must be excluded.
 *
 * @param model Non-NULL model to serialize without mutation.
 * @param output Non-NULL writable empty descriptor receiving the artifact allocation.
 * @return CGAI_ABI_OK, INVALID_ARGUMENT for NULL inputs, or the allocation/encoding helper's error status.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_export(const cgai_abi_model *model,
                                                      cgai_abi_buffer *output);

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
CGAI_ABI_EXPORT void cgai_abi_buffer_free(cgai_abi_buffer *buffer);

/**
 * @brief Generate a continuation and return text allocated by the ABI.
 *
 * The handle is borrowed: generation reads the model without transferring or changing its ownership.
 * The output structure belongs to the caller, but its data allocation must be released with
 * cgai_abi_buffer_free(). Pass an empty buffer; this function does not free an earlier allocation.
 * A C string ends with a zero byte (NUL); output->size counts the text bytes before that terminator.
 * The 128-byte allowance per requested token is a capacity estimate, not a tokenizer limit.
 * Long tokens can exhaust that capacity and make the core generation call fail.
 *
 * @param model Non-NULL trained model handle, kept alive throughout this call.
 * @param utf8_prompt Non-NULL NUL-terminated prompt; borrowed for this call.
 * @param max_tokens Maximum number of continuation tokens; zero requests an empty continuation.
 * @param temperature Sampling temperature; the core rejects negative or nonfinite values.
 * @param seed Random seed; zero tells the core to use the model's configured seed.
 * @param output Non-NULL writable buffer descriptor receiving the allocation and text length.
 * @return CGAI_ABI_OK on success, INVALID_ARGUMENT for NULL inputs, OUT_OF_MEMORY on allocation failure,
 * or ERROR if core generation fails. After valid pointers are accepted, failure leaves an empty buffer.
 */
CGAI_ABI_EXPORT cgai_abi_status cgai_abi_model_generate(const cgai_abi_model *model,
                                                        const char *utf8_prompt,
                                                        uint32_t max_tokens, double temperature,
                                                        uint64_t seed, cgai_abi_buffer *output);

#ifdef __cplusplus
}
#endif

#endif
