/** @file bark_training.c @brief Deterministic independent bark-label checkpoint training. */
#include "bark_tool.h"
#include "internal/error.h"
#include "internal/neural_internal.h"
#include <stdlib.h>
#include <string.h>

/** Independent examples borrow prompt strings in this fixed caller-owned case table. */
typedef struct bark_training_set {
    bark_fixture_case cases[48];      /**< Training-only prompts with stable storage. */
    cgai_neural_example examples[48]; /**< Single target per independent prompt. */
    size_t count;                     /**< Filled training records. */
} bark_training_set;

/** @brief Assemble the fixed training split without inspecting held-out targets.
 * @param set Borrowed zeroed destination retaining its prompt strings.
 * @return OK with all training cases, ERROR on unexpected fixture layout. */
static cgai_status prepare_training_set(bark_training_set *set) {
    /* Step 1: Copy only training families; each prompt remains independent. */
    for (size_t index = 0U; index < BARK_FIXTURE_CASE_COUNT; ++index) {
        bark_fixture_case scenario = {0};
        if (!bark_fixture_get(index, &scenario))
            return CGAI_STATUS_ERROR;
        if (scenario.split != BARK_FIXTURE_TRAINING)
            continue;
        if (set->count >= 48U)
            return cgai_fail("bark training fixture exceeds its contract");
        set->cases[set->count] = scenario;
        set->examples[set->count] = (cgai_neural_example){
            set->cases[set->count].prompt, cgai_bark_id_name((cgai_bark_id)scenario.expected_id)};
        ++set->count;
    }
    /* Step 2: Require the complete authored split before any model mutation. */
    return set->count == 48U ? CGAI_STATUS_OK : cgai_fail("incomplete bark training fixture");
}

/** @brief Append a training spelling source to a bounded temporary vocabulary corpus.
 * @param corpus Borrowed 8192-byte writable buffer.
 * @param length Borrowed current byte length excluding NUL.
 * @param text Borrowed terminated prompt or label.
 * @return OK after append, ERROR without changing the corpus otherwise. */
static cgai_status append_training_text(char *corpus, size_t *length, const char *text) {
    /* Step 1: Account for both separator and terminator before copying. */
    const size_t bytes = strlen(text);
    if (bytes + 2U > 8192U - *length)
        return cgai_fail("bark training vocabulary exceeds its temporary byte budget");
    /* Step 2: Retain normalized word boundaries between independent sources. */
    memcpy(corpus + *length, text, bytes);
    *length += bytes;
    corpus[(*length)++] = ' ';
    corpus[*length] = '\0';
    return CGAI_STATUS_OK;
}

/** @brief Find the first occurrence of a training label without inspecting held-out data.
 * @param labels Borrowed initialized prefix of labels.
 * @param count Initialized label count.
 * @param label Candidate frozen vocabulary ID.
 * @return Nonzero when this ID already occurs in the prefix. */
static int label_seen(const cgai_token_id *labels, size_t count, cgai_token_id label) {
    /* Step 1: Preserve deterministic first-training-occurrence order. */
    for (size_t index = 0U; index < count; ++index)
        if (labels[index].value == label.value)
            return 1;
    return 0;
}

/** @brief Collect stable distinct label IDs from the training split only.
 * @param model Borrowed initialized frozen vocabulary.
 * @param set Borrowed training-only labels.
 * @param labels Writable catalog-sized ID array.
 * @return OK after collecting the complete inventory, ERROR otherwise. */
static cgai_status training_labels(const cgai_neural_model *model, const bark_training_set *set,
                                   cgai_token_id *labels) {
    /* Step 1: Discover label IDs only from training examples in stable first-occurrence order. */
    size_t count = 0U;
    for (size_t index = 0U; index < set->count; ++index) {
        const cgai_token_id label = cgai_neural_lookup(model, set->examples[index].target);
        if (!label_seen(labels, count, label)) {
            if (count == CGAI_BARK_ID_COUNT)
                return cgai_fail("bark training labels exceed the authored catalog");
            labels[count++] = label;
        }
    }
    return count == CGAI_BARK_ID_COUNT
               ? CGAI_STATUS_OK
               : cgai_fail("bark training split does not cover every authored label");
}

/** @brief Give every expert a distinct initial preference to avoid uniform-routing collapse.
 * @param model Mutable initialized random network.
 * @param set Borrowed training-only labels.
 * @return OK after initialization, ERROR if the authored label inventory is inconsistent. */
static cgai_status initialize_label_experts(cgai_neural_model *model,
                                            const bark_training_set *set) {
    /* Step 1: Draw preferences from observed training labels, never from held-out examples. */
    cgai_token_id labels[CGAI_BARK_ID_COUNT] = {0};
    if (!training_labels(model, set, labels))
        return CGAI_STATUS_ERROR;
    /* Step 2: Random encoder/centroids remain unchanged; all expert logits are still trainable. */
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        for (size_t token = 0U; token < model->output_size; ++token)
            model->logits[row * model->vocabulary_size + token] = -2.0;
        model->logits[row * model->vocabulary_size + labels[row % CGAI_BARK_ID_COUNT].value] = 2.0;
    }
    return CGAI_STATUS_OK;
}

/** @brief Begin categorical state inputs as orthogonal coordinates rather than random aliases.
 * @param model Mutable initialized network with a training-only vocabulary.
 * @return OK after all state rows are initialized, ERROR for insufficient shape or coverage. */
static cgai_status initialize_state_embeddings(cgai_neural_model *model) {
    /* Step 1: Fix category order to the public enums without inspecting held-out labels. */
    const char *const spellings[] = {
        "eventidle",       "eventgreet",    "eventthreat",   "eventvictory",     "eventdiscovery",
        "eventretreat",    "dangerlow",     "dangerhigh",    "relationfriendly", "relationneutral",
        "relationhostile", "settingindoor", "settingoutdoor"};
    const size_t count = sizeof(spellings) / sizeof(*spellings);
    const size_t dimensions = model->config.embedding_dimensions;
    if (dimensions < count)
        return cgai_fail("bark state initialization requires thirteen embedding coordinates");
    /* Step 2: Replace only known state embeddings; controls and targets retain random values. */
    for (size_t index = 0U; index < count; ++index) {
        const cgai_token_id token = cgai_neural_lookup(model, spellings[index]);
        if (token.value <= CGAI_TOKEN_UNKNOWN || token.value >= model->vocabulary_size)
            return cgai_fail("bark training vocabulary lacks an authored state category");
        double *embedding = model->embeddings + token.value * dimensions;
        memset(embedding, 0, dimensions * sizeof(*embedding));
        embedding[index] = 1.0;
    }
    return CGAI_STATUS_OK;
}

/** @brief Create the fixed shape from training prompts and labels only.
 * @param config Borrowed validated configuration from the recipe or reference checkpoint.
 * @param set Borrowed complete training split.
 * @return Owned model, or NULL with a diagnostic. */
static cgai_neural_model *create_training_model(const cgai_neural_config *config,
                                                const bark_training_set *set) {
    /* Step 1: Held-out prompts and targets never enter vocabulary construction. */
    char corpus[8192] = {0};
    size_t length = 0U;
    for (size_t index = 0U; index < set->count; ++index) {
        if (!append_training_text(corpus, &length, set->examples[index].prompt) ||
            !append_training_text(corpus, &length, set->examples[index].target))
            return NULL;
    }
    /* Step 2: Reuse the native initialization and checkpoint parameter layout. */
    cgai_neural_model *model = cgai_neural_create(config, corpus);
    if (model != NULL &&
        (!initialize_state_embeddings(model) || !initialize_label_experts(model, set))) {
        cgai_neural_destroy(model);
        return NULL;
    }
    return model;
}

/** @brief Initialize the fixed small architecture using only training vocabulary.
 * @param path New checkpoint destination.
 * @return OK on complete checkpoint write, ERROR otherwise. */
cgai_status bark_tool_init(const char *path) {
    /* Step 1: Build only the training split and its reproducible initial weights. */
    bark_training_set set = {0};
    if (!prepare_training_set(&set))
        return CGAI_STATUS_ERROR;
    const cgai_neural_config config = {16U, 16U, 16U, 4U, 42U, 1.0};
    cgai_neural_model *model = create_training_model(&config, &set);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    /* Step 2: Persist exact initial parameters without performing any update. */
    const cgai_status status = cgai_neural_checkpoint_save(model, path);
    cgai_neural_destroy(model);
    return status;
}

/** @brief Continue independent supervised labels with preserved Adam and shuffle state.
 * @param input Borrowed checkpoint path.
 * @param output Different checkpoint destination.
 * @param epochs Additional complete passes.
 * @param rate Positive Adam learning rate.
 * @return OK after complete training/save, ERROR otherwise. */
cgai_status bark_tool_step(const char *input, const char *output, size_t epochs, double rate) {
    /* Step 1: Reject destructive in-place use and prepare independent training ownership. */
    if (input == NULL || output == NULL || strcmp(input, output) == 0)
        return cgai_fail("bark training requires different input and output paths");
    bark_training_set set = {0};
    if (!prepare_training_set(&set))
        return CGAI_STATUS_ERROR;
    cgai_neural_model *model = cgai_neural_checkpoint_load(input);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    /* Step 2: Resume complete epochs using the shared continuation optimizer. */
    const cgai_neural_training training = {epochs, rate, 5.0};
    cgai_status status =
        cgai_neural_train_examples_continue(model, set.examples, set.count, &training);
    if (status)
        status = cgai_neural_checkpoint_save(model, output);
    cgai_neural_destroy(model);
    return status;
}

/** @brief Inspect the replay shape and consistent complete-epoch counters.
 * @param input Borrowed reference checkpoint path.
 * @param config Writable copied architecture.
 * @param epochs Writable bounded complete-epoch count.
 * @return OK after inspection, ERROR otherwise. */
static cgai_status replay_shape(const char *input, cgai_neural_config *config, size_t *epochs) {
    /* Step 1: Inspect complete owned state without retaining reference weights. */
    cgai_neural_model *reference = cgai_neural_checkpoint_load(input);
    cgai_neural_resources resources = {0};
    if (reference == NULL)
        return CGAI_STATUS_ERROR;
    const cgai_neural_progress progress = cgai_neural_get_progress(reference);
    const cgai_status inspected = cgai_neural_get_resources(reference, &resources);
    cgai_neural_destroy(reference);
    /* Step 2: Every recorded pass must consist of the forty-eight independent targets. */
    if (!inspected || progress.epochs > 10000U || progress.steps != progress.epochs * 48U)
        return cgai_fail("bark replay requires complete independent-example epochs");
    *config = resources.config;
    *epochs = (size_t)progress.epochs;
    return CGAI_STATUS_OK;
}

/** @brief Recreate a checkpoint from the complete independent training recipe.
 * @param input Reference checkpoint.
 * @param output New replay checkpoint destination.
 * @param rate Original Adam learning rate.
 * @return OK after complete replay save, ERROR otherwise. */
cgai_status bark_tool_replay(const char *input, const char *output, double rate) {
    /* Step 1: Reject in-place replay and inspect consistent complete independent passes. */
    if (input == NULL || output == NULL || strcmp(input, output) == 0)
        return cgai_fail("bark replay requires different input and output paths");
    cgai_neural_config config = {0};
    size_t epochs = 0U;
    bark_training_set set = {0};
    if (!prepare_training_set(&set) || !replay_shape(input, &config, &epochs))
        return CGAI_STATUS_ERROR;
    /* Step 2: Rebuild training-only vocabulary and replay every optimizer update. */
    cgai_neural_model *model = create_training_model(&config, &set);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    const cgai_neural_training training = {epochs, rate, 5.0};
    cgai_status status = epochs == 0U ? CGAI_STATUS_OK
                                      : cgai_neural_train_examples_continue(model, set.examples,
                                                                            set.count, &training);
    if (status)
        status = cgai_neural_checkpoint_save(model, output);
    cgai_neural_destroy(model);
    return status;
}

/** @brief Export weights only for the gameplay runtime.
 * @param input Borrowed continuation checkpoint.
 * @param output Inference artifact destination.
 * @return OK on complete export, ERROR otherwise. */
cgai_status bark_tool_export(const char *input, const char *output) {
    /* Step 1: Validate and load exact continuation state. */
    cgai_neural_model *model = cgai_neural_checkpoint_load(input);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    /* Step 2: Publish only weights/vocabulary to the inference artifact. */
    const cgai_status status = cgai_neural_save(model, output);
    cgai_neural_destroy(model);
    return status;
}
