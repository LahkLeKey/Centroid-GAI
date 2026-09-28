/** @file node_spatial.c @brief Typed-array and owned-handle boundary for the C spatial index. */
#include "node_callbacks.h"
#include "node_error.h"
#include <stdlib.h>

/** Private owner attached to a tagged JavaScript object and released by its finalizer. */
typedef struct node_spatial {
    cgai_abi_spatial_index *index; /**< Owned ABI index. */
    size_t dimensions;             /**< Required query coordinate count. */
} node_spatial;
/** Borrowed synchronous typed-array view; shared buffers are rejected. */
typedef struct spatial_view {
    void *data;   /**< Borrowed nonshared array storage. */
    size_t count; /**< Number of typed elements, not bytes. */
} spatial_view;
/** Unique brand prevents arbitrary objects or unrelated addon handles from being unwrapped. */
static const napi_type_tag spatial_tag = {UINT64_C(0xcb62196652078ef1),
                                          UINT64_C(0x80e779356a80a603)};

/**
 * @brief Validate and borrow a nonempty, nonshared typed-array view for this synchronous call.
 * @param env Borrowed Node environment.
 * @param value Borrowed JavaScript value.
 * @param expected Required typed-array element kind.
 * @param output Writable borrowed view descriptor.
 * @return Nonzero on success; zero with a pending JavaScript exception on failure.
 */
static int typed_view(napi_env env, napi_value value, napi_typedarray_type expected,
                      spatial_view *output) {
    /* Step 1: Require a typed array before requesting its backing storage. */
    bool typed = false;
    if (napi_is_typedarray(env, value, &typed) != napi_ok || !typed) {
        (void)napi_throw_type_error(env, NULL, "spatial input must be a typed array");
        return 0;
    }
    napi_typedarray_type actual;
    napi_value buffer;
    size_t offset;
    bool ordinary = false;
    /* Step 2: Reject wrong element types, shared backing buffers, and detached/empty views. */
    if (napi_get_typedarray_info(env, value, &actual, &output->count, &output->data, &buffer,
                                 &offset) != napi_ok ||
        actual != expected || napi_is_arraybuffer(env, buffer, &ordinary) != napi_ok || !ordinary ||
        !output->data || !output->count) {
        (void)napi_throw_type_error(env, NULL, "invalid or shared spatial typed array");
        return 0;
    }
    return 1;
}

/**
 * @brief Release the C index when its tagged JavaScript owner becomes unreachable.
 * @param env Unused borrowed Node environment.
 * @param data Owned node_spatial descriptor attached during creation.
 * @param hint Unused finalizer hint.
 */
static void finalize_spatial(napi_env env, void *data, void *hint) {
    /* Step 1: Finalization performs only native deallocation and uses no JavaScript handles. */
    (void)env;
    (void)hint;
    node_spatial *owner = data;
    cgai_abi_spatial_destroy(owner->index);
    free(owner);
}

/**
 * @brief Transfer a completed native owner to a type-tagged JavaScript object.
 * @param env Borrowed Node environment.
 * @param owner Owned completed index descriptor, consumed on success and failure.
 * @return Tagged JavaScript owner, or NULL after cleanup with a pending exception.
 */
static napi_value wrap_spatial(napi_env env, node_spatial *owner) {
    /* Step 1: Create and brand the object before registering its ownership finalizer. */
    napi_value object;
    if (!cgai_node_check(env, napi_create_object(env, &object)) ||
        !cgai_node_check(env, napi_type_tag_object(env, object, &spatial_tag)) ||
        !cgai_node_check(env, napi_wrap(env, object, owner, finalize_spatial, NULL, NULL))) {
        finalize_spatial(env, owner, NULL);
        return NULL;
    }
    /* Step 2: Node now owns the descriptor until garbage collection. */
    return object;
}

/**
 * @brief Copy validated typed arrays through the ABI and return their native owner.
 * @param env Borrowed Node environment.
 * @param vectors Borrowed Float64 coordinate view, divisible by category count.
 * @param categories Borrowed Uint32 category view, at most 4096 rows.
 * @return Tagged owner object or NULL with an exception and no leaked allocations.
 */
static napi_value create_spatial(napi_env env, spatial_view vectors, spatial_view categories) {
    /* Step 1: Infer and bound dimensions before narrowing sizes to the fixed-width ABI. */
    if (categories.count > 4096U || vectors.count % categories.count ||
        vectors.count / categories.count > 4096U) {
        (void)napi_throw_range_error(env, NULL, "spatial arrays have invalid dimensions");
        return NULL;
    }
    node_spatial *owner = calloc(1U, sizeof(*owner));
    if (!owner) {
        (void)napi_throw_error(env, NULL, "could not allocate spatial owner");
        return NULL;
    }
    owner->dimensions = vectors.count / categories.count;
    /* Step 2: The core copies both arrays; no JavaScript memory survives in the index. */
    if (cgai_abi_spatial_create(vectors.data, categories.data, (uint32_t)categories.count,
                                (uint32_t)owner->dimensions, &owner->index) != CGAI_ABI_OK) {
        free(owner);
        return cgai_node_native_error(env);
    }
    return wrap_spatial(env, owner);
}

/*
 * @brief Validate array kinds and create an immutable C index.
 * @param env Borrowed Node environment.
 * @param info Borrowed callback arguments: Float64Array coordinates and Uint32Array categories.
 * @return Tagged owned index or NULL with an exception.
 */
napi_value cgai_node_spatial_create(napi_env env, napi_callback_info info) {
    /* Step 1: Read exactly the required two argument slots. */
    size_t count = 2U;
    napi_value arguments[2];
    NAPI_CALL(env, napi_get_cb_info(env, info, &count, arguments, NULL, NULL));
    if (count != 2U) {
        (void)napi_throw_type_error(env, NULL, "spatial create requires two arrays");
        return NULL;
    }
    spatial_view vectors, categories;
    /* Step 2: Preserve typed-array offsets and reject malformed storage before native access. */
    if (!typed_view(env, arguments[0], napi_float64_array, &vectors) ||
        !typed_view(env, arguments[1], napi_uint32_array, &categories))
        return NULL;
    return create_spatial(env, vectors, categories);
}

/**
 * @brief Validate an opaque object's brand before interpreting its wrapped native address.
 * @param env Borrowed Node environment.
 * @param object Borrowed JavaScript candidate owner.
 * @return Borrowed live owner, or NULL with a pending exception.
 */
static node_spatial *unwrap_spatial(napi_env env, napi_value object) {
    /* Step 1: Type tags reject forged objects and handles belonging to other native modules. */
    bool tagged = false;
    node_spatial *owner = NULL;
    if (napi_check_object_type_tag(env, object, &spatial_tag, &tagged) != napi_ok || !tagged ||
        napi_unwrap(env, object, (void **)&owner) != napi_ok || !owner) {
        (void)napi_throw_type_error(env, NULL, "invalid spatial index handle");
        return NULL;
    }
    return owner;
}

/**
 * @brief Query the ABI and copy its JSON before releasing the ABI allocation.
 * @param env Borrowed Node environment.
 * @param owner Borrowed live typed owner.
 * @param vector Borrowed dimension-sized Float64 view.
 * @param options Borrowed Uint32 view containing limit, category, and exclusion.
 * @return JavaScript JSON string or NULL with an exception.
 */
static napi_value query_spatial(napi_env env, const node_spatial *owner, spatial_view vector,
                                spatial_view options) {
    /* Step 1: Exact dimension validation prevents the core from reading beyond the JS view. */
    if (vector.count != owner->dimensions) {
        (void)napi_throw_range_error(env, NULL, "spatial query dimension mismatch");
        return NULL;
    }
    if (options.count != 3U) {
        (void)napi_throw_type_error(env, NULL, "spatial options require three integers");
        return NULL;
    }
    const uint32_t *values = options.data;
    cgai_abi_buffer json = {0};
    if (cgai_abi_spatial_query(owner->index, vector.data, values[0], values[1], values[2], &json) !=
        CGAI_ABI_OK)
        return cgai_node_native_error(env);
    /* Step 2: Always release owned ABI bytes, even if JavaScript string creation fails. */
    napi_value result;
    const napi_status status =
        napi_create_string_utf8(env, (const char *)json.data, json.size, &result);
    cgai_abi_buffer_free(&json);
    return cgai_node_check(env, status) ? result : NULL;
}

/*
 * @brief Validate a branded owner and query arrays before invoking read-only native traversal.
 * @param env Borrowed Node environment.
 * @param info Callback metadata containing owner, Float64 query, and three Uint32 options.
 * @return JSON result string or NULL with an exception.
 */
napi_value cgai_node_spatial_query(napi_env env, napi_callback_info info) {
    /* Step 1: Check argument count and ownership before borrowing coordinate storage. */
    size_t count = 3U;
    napi_value arguments[3];
    NAPI_CALL(env, napi_get_cb_info(env, info, &count, arguments, NULL, NULL));
    if (count != 3U) {
        (void)napi_throw_type_error(env, NULL, "spatial query requires handle, vector, options");
        return NULL;
    }
    node_spatial *owner = unwrap_spatial(env, arguments[0]);
    spatial_view vector, options;
    if (!owner || !typed_view(env, arguments[1], napi_float64_array, &vector) ||
        !typed_view(env, arguments[2], napi_uint32_array, &options))
        return NULL;
    /* Step 2: Validate lengths and execute the bounded native query. */
    return query_spatial(env, owner, vector, options);
}
