/** @file addon.c @brief Node-API module registration and version queries. */

#include "node_callbacks.h"
#include "node_error.h"

/**
 * @brief Implement JavaScript abiVersion() by wrapping the ABI version in a Number.
 *
 * Node-API values are runtime-managed handles rather than C heap allocations. The callback ignores
 * its argument metadata because no arguments are needed. NAPI_CALL returns NULL on failure after
 * requesting a JavaScript exception through the shared error helper.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param info Opaque callback metadata supplied by Node for this invocation.
 * @return JavaScript Number handle, or NULL with a Node-API error.
 */
static napi_value abi_version(napi_env env, napi_callback_info info) {
    /* Step 1: Explicitly mark unused callback metadata without changing it. */
    (void)info;
    /* Step 2: Create the JavaScript numeric value and return its runtime-managed handle. */
    napi_value result;
    NAPI_CALL(env, napi_create_uint32(env, cgai_abi_version(), &result));
    return result;
}

/**
 * @brief Implement JavaScript libraryVersion() using the ABI's static UTF-8 string.
 *
 * NAPI_AUTO_LENGTH asks Node to find the first NUL. Creating a JavaScript string copies the native
 * text, so JavaScript does not retain or free the C literal. The callback needs no argument values.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param info Opaque callback metadata supplied by Node for this invocation.
 * @return JavaScript String handle, or NULL if Node cannot create it.
 */
static napi_value library_version(napi_env env, napi_callback_info info) {
    /* Step 1: Mark argument metadata unused for this zero-argument query. */
    (void)info;
    /* Step 2: Copy the static library version into a JavaScript-managed string. */
    napi_value result;
    NAPI_CALL(env,
              napi_create_string_utf8(env, cgai_abi_library_version(), NAPI_AUTO_LENGTH, &result));
    return result;
}

/**
 * @brief Return the native persistence schema as a JavaScript JSON string.
 *
 * The ABI returns static text; Node copies it into its own string storage. Parsing that JSON and
 * using Prisma/PostgreSQL are responsibilities of the TypeScript layer. This callback performs
 * neither a database operation nor a migration.
 *
 * @param env Node-API environment for the current callback; borrowed, never freed here.
 * @param info Opaque callback metadata supplied by Node for this invocation.
 * @return JavaScript String containing JSON, or NULL on a Node-API failure.
 */
static napi_value persistence_schema(napi_env env, napi_callback_info info) {
    /* Step 1: Ignore callback arguments because the schema is a library constant. */
    (void)info;
    /* Step 2: Copy the ABI's schema text into a JavaScript string and return it. */
    napi_value result;
    NAPI_CALL(env, napi_create_string_utf8(env, cgai_abi_persistence_schema_json(),
                                           NAPI_AUTO_LENGTH, &result));
    return result;
}

/**
 * @brief Register the JavaScript-facing functions when Node loads the native addon.
 *
 * The Node-API macro supplies the module initializer signature, including env and exports.
 * Each descriptor names one JavaScript property and its C callback; unused descriptor fields are
 * NULL. The properties table lives only during registration because Node records the definitions.
 * This layer exposes the compiled ABI without creating a global model or opening a database.
 *
 * @return The supplied exports object after registration, or NULL on Node-API failure.
 */
NAPI_MODULE_INIT() {
    /* Step 1: Describe the six exported methods and their callback function pointers. */
    const napi_property_descriptor properties[] = {
        {"abiVersion", NULL, abi_version, NULL, NULL, NULL, napi_default, NULL},
        {"libraryVersion", NULL, library_version, NULL, NULL, NULL, napi_default, NULL},
        {"persistenceSchema", NULL, persistence_schema, NULL, NULL, NULL, napi_default, NULL},
        {"trainModel", NULL, cgai_node_train, NULL, NULL, NULL, napi_default, NULL},
        {"inspectModel", NULL, cgai_node_inspect, NULL, NULL, NULL, napi_default, NULL},
        {"generateModel", NULL, cgai_node_generate, NULL, NULL, NULL, napi_default, NULL},
    };
    /* Step 2: Register the complete table using its element count, not its byte size. */
    NAPI_CALL(env, napi_define_properties(env, exports, sizeof(properties) / sizeof(properties[0]),
                                          properties));
    /* Step 3: Return the module object that Node will provide to the TypeScript binding. */
    return exports;
}
