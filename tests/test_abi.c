/** @file test_abi.c @brief Contract tests for the fixed-width native ABI. */

#include "centroid_gai_abi.h"
#include "test_utils.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, cgai_abi_last_error())

int main(void) {
    _Static_assert(sizeof(cgai_abi_status) == sizeof(uint32_t), "ABI status width changed");
    _Static_assert(offsetof(cgai_abi_buffer, data) == 0U, "ABI buffer layout changed");
    CHECK(cgai_abi_version() == CGAI_ABI_VERSION);
    CHECK(strstr(cgai_abi_persistence_schema_json(), "checksumSha256") != NULL);
    CHECK(cgai_abi_default_config(NULL) == CGAI_ABI_INVALID_ARGUMENT);
    CHECK(cgai_abi_model_create(NULL, NULL) == CGAI_ABI_INVALID_ARGUMENT);
    cgai_abi_buffer_free(NULL);

    cgai_abi_config config;
    CHECK(cgai_abi_default_config(&config) == CGAI_ABI_OK);
    config.dimensions = 12U;
    config.centroid_count = 4U;

    cgai_abi_model *model = NULL;
    CHECK(cgai_abi_model_create(&config, &model) == CGAI_ABI_OK);
    CHECK(cgai_abi_model_train(model, "native models persist through postgres") == CGAI_ABI_OK);

    cgai_abi_model_metadata metadata;
    CHECK(cgai_abi_model_get_metadata(model, &metadata) == CGAI_ABI_OK);
    CHECK(metadata.format_version == 1U);
    CHECK(metadata.dimensions == 12U);
    CHECK(metadata.examples_seen > 0U);

    cgai_abi_buffer encoded = {0};
    CHECK(cgai_abi_model_export(model, &encoded) == CGAI_ABI_OK);
    CHECK(encoded.data != NULL && encoded.size > 0U);

    cgai_abi_model *loaded = NULL;
    CHECK(cgai_abi_model_import(encoded.data, encoded.size, &loaded) == CGAI_ABI_OK);
    cgai_abi_buffer generated = {0};
    CHECK(cgai_abi_model_generate(loaded, "native", 8U, 0.0, 42U, &generated) == CGAI_ABI_OK);
    CHECK(generated.data != NULL && generated.size == strlen((char *)generated.data));

    cgai_abi_buffer_free(&generated);
    cgai_abi_buffer_free(&encoded);
    cgai_abi_model_destroy(loaded);
    cgai_abi_model_destroy(model);
    CHECK(cgai_abi_model_import(NULL, 0U, NULL) == CGAI_ABI_INVALID_ARGUMENT);
    return 0;
}
