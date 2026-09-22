/** @file test_sampling.c @brief Unit tests for centroid token sampling. */

#include "centroid_gai.h"
#include "internal/cgai_internal.h"
#include "internal/model_sampling.h"
#include "test_utils.h"

int test_sampling(void) {
    cgai_config config = cgai_default_config();
    config.dimensions = 8U;
    config.centroid_count = 2U;
    cgai_model *model = cgai_model_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(cgai_model_train_text(model, "alpha beta alpha gamma") == CGAI_STATUS_OK,
               cgai_last_error());

    const cgai_centroid_id cluster = cgai_centroid_id_from_size(0U);
    uint64_t greedy_state = 7U;
    const cgai_token_id greedy = cgai_select_token(model, cluster, 0.0, &greedy_state);
    TEST_CHECK(cgai_token_id_is_valid(greedy), "greedy sampling returned an invalid token");

    uint64_t first_state = 11U;
    uint64_t second_state = 11U;
    const cgai_token_id first = cgai_select_token(model, cluster, 0.8, &first_state);
    const cgai_token_id second = cgai_select_token(model, cluster, 0.8, &second_state);
    TEST_CHECK(first.value == second.value, "temperature sampling was not deterministic");
    cgai_model_destroy(model);
    return 0;
}