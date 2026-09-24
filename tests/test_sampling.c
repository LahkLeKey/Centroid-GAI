/** @file test_sampling.c @brief Unit tests for centroid token sampling. */

#include "centroid_gai.h"
#include "internal/cgai_internal.h"
#include "internal/model_sampling.h"
#include "test_utils.h"

/**
 * @brief Check greedy validity and fixed-seed reproducibility of weighted sampling.
 *
 * A trained small model supplies an initialized count row. The greedy case checks that an ID is
 * returned, while the weighted case initializes two independent RNG states equally and expects the
 * same chosen ID. The model remains unchanged throughout selection.
 *
 * @return Zero after both selection modes pass.
 */
int test_sampling(void) {
    /* Step 1: Configure and train a small model whose first centroid can be sampled. */
    cgai_config config = cgai_default_config();
    config.dimensions = 8U;
    config.centroid_count = 2U;
    cgai_model *model = cgai_model_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    TEST_CHECK(cgai_model_train_text(model, "alpha beta alpha gamma") == CGAI_STATUS_OK,
               cgai_last_error());

    /* Step 2: Choose a valid row and exercise greedy temperature-zero selection. */
    const cgai_centroid_id cluster = cgai_centroid_id_from_size(0U);
    uint64_t greedy_state = 7U;
    const cgai_token_id greedy = cgai_select_token(model, cluster, 0.0, &greedy_state);
    TEST_CHECK(cgai_token_id_is_valid(greedy), "greedy sampling returned an invalid token");

    /* Step 3: Sample twice at positive temperature using matching independent seeds. */
    uint64_t first_state = 11U;
    uint64_t second_state = 11U;
    const cgai_token_id first = cgai_select_token(model, cluster, 0.8, &first_state);
    const cgai_token_id second = cgai_select_token(model, cluster, 0.8, &second_state);
    TEST_CHECK(first.value == second.value, "temperature sampling was not deterministic");
    /* Step 4: Release the fixture after comparing sampled identifiers. */
    cgai_model_destroy(model);
    return 0;
}