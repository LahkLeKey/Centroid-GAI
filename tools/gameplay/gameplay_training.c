/** @file gameplay_training.c @brief Balanced task continuation and exact joint recipe replay. */
#include "gameplay_tool.h"
#include "internal/error.h"
#include <string.h>

/** @brief Require the complete pinned shared shape and typed domains.
 * @param model Borrowed initialized model.
 * @return Nonzero when every recorded shape field matches this compiled registry. */
static int recipe_matches(const cgai_gameplay_model *model) {
    /* Step1: Compare semantic fields without relying on structure padding bytes. */
    const cgai_gameplay_config expected = gameplay_fixture_config();
    cgai_gameplay_config actual = {0};
    if (!cgai_gameplay_get_config(model, &actual))
        return 0;
    return actual.seed == expected.seed &&
           actual.routing_temperature == expected.routing_temperature &&
           actual.feature_count == expected.feature_count &&
           actual.embedding_dimensions == expected.embedding_dimensions &&
           actual.hidden_dimensions == expected.hidden_dimensions &&
           actual.module_count == expected.module_count &&
           actual.centroids_per_module == expected.centroids_per_module &&
           actual.task_count == expected.task_count &&
           memcmp(actual.cardinalities, expected.cardinalities, sizeof(actual.cardinalities)) ==
               0 &&
           memcmp(actual.output_counts, expected.output_counts, sizeof(actual.output_counts)) ==
               0 &&
           memcmp(actual.task_modules, expected.task_modules, sizeof(actual.task_modules)) == 0 &&
           memcmp(actual.task_features, expected.task_features, sizeof(actual.task_features)) == 0;
}

/** @brief Prepare the complete canonical balanced training records and continue AdamW.
 * @param model Mutable compatible model.
 * @param epochs Additional successful complete passes.
 * @param rate Recorded constant learning rate.
 * @return OK on complete continuation, ERROR otherwise. */
static cgai_status train_balanced(cgai_gameplay_model *model, size_t epochs, double rate) {
    /* Step1: Construct training-only records; frozen development/test targets never enter updates.
     */
    cgai_gameplay_example examples[GAMEPLAY_TRAINING_COUNT] = {0};
    size_t count = 0U;
    if (!recipe_matches(model) ||
        !gameplay_fixture_training(examples, GAMEPLAY_TRAINING_COUNT, &count))
        return cgai_fail("gameplay checkpoint differs from the balanced task recipe");
    /* Step2: A pass contains exactly192 updates from each of the two task heads. */
    const cgai_gameplay_training training = {epochs, rate, 5.0, 0.05};
    return cgai_gameplay_train_continue(model, examples, count, &training);
}

/** @brief Load a complete-epoch reference with the exact compiled shared task shape.
 * @param path Existing checkpoint path.
 * @return Owned compatible model, or NULL with a diagnostic. */
static cgai_gameplay_model *load_recipe(const char *path) {
    /* Step1: Validate all weights and continuation state before inspecting recipe counters. */
    cgai_gameplay_model *model = cgai_gameplay_checkpoint_load(path);
    if (model == NULL)
        return NULL;
    const cgai_gameplay_progress progress = cgai_gameplay_get_progress(model);
    /* Step2: Replay admits only bounded complete balanced epochs, not partial failed updates. */
    if (!recipe_matches(model) || progress.epochs > 10000U ||
        progress.steps != progress.epochs * GAMEPLAY_TRAINING_COUNT) {
        cgai_gameplay_destroy(model);
        cgai_fail("gameplay recipe requires complete compatible balanced epochs");
        return NULL;
    }
    return model;
}

/** @brief Initialize a seeded aligned shared encoder, model centers, banks and task heads.
 * @param path New checkpoint destination.
 * @return OK after complete write, ERROR otherwise. */
cgai_status gameplay_tool_init(const char *path) {
    /* Step1: The compiled task contract fixes every typed category and output domain. */
    cgai_gameplay_model *model = gameplay_tool_create();
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    /* Step2: No training is performed before the seed checkpoint is saved. */
    const cgai_status status = cgai_gameplay_checkpoint_save(model, path);
    cgai_gameplay_destroy(model);
    return status;
}

/** @brief Continue joint complete epochs with recorded Adam and shuffle state.
 * @param input Existing checkpoint.
 * @param output Different checkpoint destination.
 * @param epochs Additional complete balanced passes.
 * @param rate Recorded positive learning rate.
 * @return OK after complete save, ERROR otherwise. */
cgai_status gameplay_tool_step(const char *input, const char *output, size_t epochs, double rate) {
    /* Step1: Reject direct destructive reuse of the input and incompatible progress. */
    if (input == NULL || output == NULL || strcmp(input, output) == 0)
        return cgai_fail("gameplay step requires different input and output paths");
    cgai_gameplay_model *model = load_recipe(input);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    const cgai_gameplay_progress progress = cgai_gameplay_get_progress(model);
    /* Step2: Enforce the publication replay epoch cap before any optimizer update. */
    cgai_status status = epochs > 10000U - progress.epochs
                             ? cgai_fail("gameplay total epoch cap exceeded")
                             : train_balanced(model, epochs, rate);
    if (status)
        status = cgai_gameplay_checkpoint_save(model, output);
    cgai_gameplay_destroy(model);
    return status;
}

/** @brief Rebuild a compatible reference from its seeded full balanced recipe.
 * @param input Existing exact checkpoint.
 * @param output Different replay destination.
 * @param rate Recorded constant learning rate.
 * @return OK after complete replay save, ERROR otherwise. */
cgai_status gameplay_tool_replay(const char *input, const char *output, double rate) {
    /* Step1: Inspect bounded compatible progress without retaining reference weights. */
    if (input == NULL || output == NULL || strcmp(input, output) == 0)
        return cgai_fail("gameplay replay requires different input and output paths");
    cgai_gameplay_model *reference = load_recipe(input);
    if (reference == NULL)
        return CGAI_STATUS_ERROR;
    const cgai_gameplay_progress progress = cgai_gameplay_get_progress(reference);
    cgai_gameplay_destroy(reference);
    /* Step2: Recreate aligned initialization and every original balanced Adam update. */
    cgai_gameplay_model *model = gameplay_tool_create();
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    cgai_status status = progress.epochs == 0U
                             ? CGAI_STATUS_OK
                             : train_balanced(model, (size_t)progress.epochs, rate);
    if (status)
        status = cgai_gameplay_checkpoint_save(model, output);
    cgai_gameplay_destroy(model);
    return status;
}

/** @brief Export compiled portable weights without training moments.
 * @param input Existing exact checkpoint.
 * @param output New inference artifact.
 * @return OK on complete write, ERROR otherwise. */
cgai_status gameplay_tool_export(const char *input, const char *output) {
    /* Step1: Full schema and recipe validation precede deployment export. */
    cgai_gameplay_model *model = load_recipe(input);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    const cgai_status status = cgai_gameplay_save(model, output);
    cgai_gameplay_destroy(model);
    return status;
}
