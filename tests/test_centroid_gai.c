#include "centroid_gai.h"
#include "test_utils.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, cgai_last_error())

static int test_invalid_configuration(void) {
    cgai_config config = cgai_default_config();
    config.dimensions = 0U;
    CHECK(cgai_model_create(&config) == NULL);
    config = cgai_default_config();
    config.dimensions = SIZE_MAX;
    CHECK(cgai_model_create(&config) == NULL);
    return 0;
}

static int test_argument_validation(void) {
    char output[8];
    CHECK(cgai_model_train_text(NULL, "text") == CGAI_STATUS_ERROR);
    CHECK(cgai_model_train_text(NULL, NULL) == CGAI_STATUS_ERROR);
    CHECK(cgai_model_generate(NULL, "prompt", 1U, 0.0, 0U, output, sizeof(output)) ==
          CGAI_STATUS_ERROR);
    cgai_model *model = cgai_model_create(NULL);
    CHECK(model != NULL);
    CHECK(cgai_model_train_text(model, "one two three") == CGAI_STATUS_OK);
    CHECK(cgai_model_generate(model, "prompt", 1U, -1.0, 0U, output, sizeof(output)) ==
          CGAI_STATUS_ERROR);
    CHECK(cgai_model_generate(model, "prompt", 1U, 0.0, 0U, output, 0U) == CGAI_STATUS_ERROR);
    CHECK(cgai_model_generate(model, "prompt", 8U, 0.0, 0U, output, sizeof(output)) ==
          CGAI_STATUS_ERROR);
    cgai_model_destroy(model);
    return 0;
}

int main(void) {
    CHECK(test_invalid_configuration() == 0);
    CHECK(test_argument_validation() == 0);
    cgai_config config = cgai_default_config();
    config.dimensions = 12U;
    config.centroid_count = 4U;
    config.context_window = 2U;

    cgai_model *model = cgai_model_create(&config);
    CHECK(model != NULL);
    CHECK(cgai_model_train_text(model, "the red fox runs. the blue fox sleeps. "
                                       "the red bird sings. the blue bird flies."));
    CHECK(cgai_model_vocabulary_size(model) >= 10U);
    CHECK(cgai_model_examples_seen(model) > 0U);
    const size_t examples_before = cgai_model_examples_seen(model);
    CHECK(cgai_model_train_text(model, "another short example") == CGAI_STATUS_OK);
    CHECK(cgai_model_examples_seen(model) > examples_before);

    char first[256];
    char second[256];
    CHECK(cgai_model_generate(model, "the red", 8U, 0.0, 42U, first, sizeof(first)));
    CHECK(cgai_model_generate(model, "the red", 8U, 0.0, 99U, second, sizeof(second)));
    CHECK(strcmp(first, second) == 0);

    const char *path = "centroid_gai_test_model.cgai";
    CHECK(cgai_model_save(model, path));
    cgai_model *loaded = cgai_model_load(path);
    CHECK(loaded != NULL);
    CHECK(cgai_model_vocabulary_size(loaded) == cgai_model_vocabulary_size(model));
    CHECK(cgai_model_generate(loaded, "the red", 8U, 0.0, 1U, second, sizeof(second)));
    CHECK(strcmp(first, second) == 0);

    (void)remove(path);
    CHECK(cgai_model_load(path) == NULL);
    CHECK(cgai_model_load(NULL) == NULL);
    CHECK(cgai_model_save(model, NULL) == CGAI_STATUS_ERROR);
    cgai_model_destroy(loaded);
    cgai_model_destroy(model);
    return 0;
}
