/** @file test_model_math.c @brief Unit tests for embedding and centroid math. */

#include "centroid_gai.h"
#include "internal/cgai_internal.h"
#include "internal/model_math.h"
#include "internal/vocabulary.h"
#include "test_utils.h"

#include <math.h>

int test_model_math(void) {
    cgai_config config = cgai_default_config();
    config.dimensions = 8U;
    config.centroid_count = 3U;
    cgai_model *model = cgai_model_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(cgai_model_train_text(model, "red fox blue bird") == CGAI_STATUS_OK,
               cgai_last_error());

    const cgai_token_id history[] = {cgai_token_id_from_size(CGAI_TOKEN_BOS),
                                     cgai_token_id_from_size(CGAI_TOKEN_UNKNOWN)};
    float output[8];
    float scratch[8];
    cgai_context_embedding(model, history, 2U, output, scratch);
    double length = 0.0;
    for (size_t i = 0; i < config.dimensions; ++i) {
        length += (double)output[i] * (double)output[i];
    }
    TEST_CHECK(isfinite(length) && length > 0.0, "context embedding was empty");
    TEST_CHECK(cgai_nearest_centroid(model, output).value < model->initialized_centroids,
               "nearest centroid was not initialized");

    uint64_t first = 7U;
    uint64_t second = 7U;
    TEST_CHECK(cgai_random_next(&first) == cgai_random_next(&second),
               "random generator is not deterministic");
    cgai_model_destroy(model);
    return 0;
}