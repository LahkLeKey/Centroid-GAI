/** @file chat_model.c @brief Conversation model construction and bounded dataset ownership. */
#include "internal/chat_internal.h"
#include "internal/error.h"
#include <stdlib.h>
#include <string.h>

cgai_chat_config cgai_chat_default_config(void) {
    const cgai_chat_config config = {8U, 16U, 16U, 160U, 32U, 42U, 1.0, 0U};
    return config;
}

cgai_chat_options cgai_chat_default_options(void) {
    const cgai_chat_options options = {128U, 0.0, 0U};
    return options;
}

cgai_status cgai_chat_validate_config(const cgai_chat_config *config, cgai_neural_config *network) {
    if (config == NULL || config->prompt_window < 4U || config->prompt_window > 255U ||
        config->response_window == 0U || config->response_window > 256U - config->prompt_window)
        return cgai_fail("invalid chat prompt or response window");
    if (config->evidence_window > config->prompt_window - 4U)
        return cgai_fail("evidence window must leave four prompt slots for the question");
    const cgai_neural_config shape = {config->embedding_dimensions,
                                      config->hidden_dimensions,
                                      config->centroid_count,
                                      config->prompt_window + config->response_window,
                                      config->seed,
                                      config->routing_temperature};
    if (!cgai_neural_validate_config(&shape))
        return CGAI_STATUS_ERROR;
    if (network != NULL)
        *network = shape;
    return CGAI_STATUS_OK;
}

/** @brief Append text to a bounded vocabulary corpus.
 * @param corpus Owned growable corpus pointer.
 * @param length Current bytes excluding NUL.
 * @param text Borrowed text.
 * @return OK or ERROR retaining the previous allocation. */
static cgai_status append_corpus(char **corpus, size_t *length, const char *text) {
    if (text == NULL)
        return cgai_fail("missing chat text");
    const size_t bytes = strlen(text);
    if (bytes + 1U > CGAI_CHAT_MAX_TEXT_BYTES - *length)
        return cgai_fail("chat dataset exceeds text budget");
    char *next = realloc(*corpus, *length + bytes + 2U);
    if (next == NULL)
        return cgai_fail("could not allocate chat vocabulary corpus");
    *corpus = next;
    memcpy(next + *length, text, bytes);
    *length += bytes;
    next[(*length)++] = ' ';
    next[*length] = '\0';
    return CGAI_STATUS_OK;
}

/** @brief Collect only training spellings, independently from structural controls.
 * @param examples Borrowed validated example table.
 * @param count Table length.
 * @return Owned text or NULL; caller frees. */
static char *training_corpus(const cgai_chat_example *examples, size_t count) {
    char *corpus = NULL;
    size_t length = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (!cgai_chat_validate_messages(examples[i].messages, examples[i].message_count) ||
            !append_corpus(&corpus, &length, examples[i].answer)) {
            free(corpus);
            return NULL;
        }
        for (size_t j = 0U; j < examples[i].message_count; ++j) {
            if (!append_corpus(&corpus, &length, examples[i].messages[j].content)) {
                free(corpus);
                return NULL;
            }
        }
    }
    return corpus;
}

/** @brief Initialize padding and distinct output preferences for chat optimization.
 *
 * Constant zero BOS inputs avoid repeated padding overwhelming the tanh encoder.
 * Partitioning predictable tokens across experts breaks their near-uniform symmetry;
 * all expert logits remain trainable, including initially disfavored tokens.
 * @param network Mutable initialized network with its output-only prefix configured. */
static void initialize_chat_experts(cgai_neural_model *network) {
    memset(network->embeddings, 0, network->config.embedding_dimensions * sizeof(double));
    for (size_t row = 0U; row < network->config.centroid_count; ++row) {
        for (size_t token = 1U; token < network->output_size; ++token) {
            network->logits[row * network->vocabulary_size + token] =
                (token - 1U) % network->config.centroid_count == row ? 2.0 : -2.0;
        }
    }
}

/** @brief Add input-only controls and allocate the final network.
 * @param model Owned shell containing a vocabulary but no parameters.
 * @return OK or ERROR with partial ownership retained. */
static cgai_status initialize_chat(cgai_chat_model *model) {
    const char *controls[] = {"<chat:user>", "<chat:assistant>", "<chat:evidence>", "<chat:end>"};
    for (size_t i = 0U; i < CGAI_CHAT_CONTROL_COUNT; ++i)
        if (!cgai_neural_append_spelling(model->network, controls[i]))
            return CGAI_STATUS_ERROR;
    if (!cgai_neural_allocate_parameters(model->network))
        return CGAI_STATUS_ERROR;
    model->network->output_size -= CGAI_CHAT_CONTROL_COUNT;
    cgai_neural_initialize_parameters(model->network);
    initialize_chat_experts(model->network);
    return CGAI_STATUS_OK;
}

/** @brief Build a model shell and parameters from validated training text.
 * @param config Borrowed shape.
 * @param corpus Borrowed training-only vocabulary text.
 * @return Owned model or NULL after cleanup. */
static cgai_chat_model *create_from_corpus(const cgai_chat_config *config, const char *corpus) {
    cgai_chat_model *model = calloc(1U, sizeof(*model));
    if (model != NULL)
        model->network = calloc(1U, sizeof(*model->network));
    int ok = model != NULL && model->network != NULL;
    if (ok) {
        model->config = *config;
        model->protocol_version = CGAI_CHAT_PROTOCOL_VERSION;
        if (model->config.evidence_window == 0U)
            model->config.evidence_window = config->prompt_window / 2U < config->prompt_window - 4U
                                                ? config->prompt_window / 2U
                                                : config->prompt_window - 4U;
        ok = cgai_chat_validate_config(config, &model->network->config) &&
             cgai_neural_build_vocabulary(model->network, corpus) && initialize_chat(model);
    }
    if (!ok) {
        cgai_chat_destroy(model);
        return NULL;
    }
    return model;
}

cgai_chat_model *cgai_chat_create(const cgai_chat_config *requested,
                                  const cgai_chat_example *examples, size_t count) {
    cgai_error_clear();
    const cgai_chat_config config = requested ? *requested : cgai_chat_default_config();
    if (examples == NULL || count == 0U || count > CGAI_CHAT_MAX_EXAMPLES ||
        !cgai_chat_validate_config(&config, NULL)) {
        cgai_fail("invalid chat creation arguments");
        return NULL;
    }
    char *corpus = training_corpus(examples, count);
    if (corpus == NULL)
        return NULL;
    cgai_chat_model *model = create_from_corpus(&config, corpus);
    free(corpus);
    cgai_chat_dataset dataset = {0};
    const int ok = model != NULL && cgai_chat_prepare_dataset(model, examples, count, &dataset);
    cgai_chat_destroy_dataset(&dataset);
    if (!ok) {
        cgai_chat_destroy(model);
        return NULL;
    }
    return model;
}

void cgai_chat_destroy(cgai_chat_model *model) {
    if (model == NULL)
        return;
    cgai_neural_destroy(model->network);
    free(model);
}

cgai_status cgai_chat_metadata(const cgai_chat_model *model, cgai_chat_config *config,
                               size_t *vocabulary_size, size_t *parameter_count) {
    if (model == NULL)
        return cgai_fail("chat model required");
    if (config != NULL)
        *config = model->config;
    if (vocabulary_size != NULL)
        *vocabulary_size = model->network->vocabulary_size;
    if (parameter_count != NULL)
        *parameter_count = model->network->parameter_count;
    return CGAI_STATUS_OK;
}

unsigned int cgai_chat_protocol_version(const cgai_chat_model *model) {
    return model == NULL ? 0U : model->protocol_version;
}

void cgai_chat_destroy_dataset(cgai_chat_dataset *dataset) {
    if (dataset->records != NULL)
        for (size_t i = 0U; i < dataset->count; ++i)
            free(dataset->records[i].answer);
    free(dataset->records);
    memset(dataset, 0, sizeof(*dataset));
}

/** @brief Prepare one complete evidence-conditioned prompt and independent answer target.
 * @param model Borrowed immutable model.
 * @param example Borrowed supervised example.
 * @param record Mutable zeroed record receiving answer ownership, including on failure.
 * @return OK or ERROR; new protocols reject omitted evidence before training. */
static cgai_status prepare_record(const cgai_chat_model *model, const cgai_chat_example *example,
                                  cgai_chat_record *record) {
    if (!cgai_chat_format(model, example->messages, example->message_count, &record->prompt))
        return CGAI_STATUS_ERROR;
    if (model->protocol_version >= 2U && record->prompt.dropped_evidence != 0U)
        return cgai_fail("chat example evidence exceeds the configured prompt or evidence window");
    record->answer =
        cgai_neural_sequence(model->network, example->answer, &record->count, &record->unknown);
    return record->answer != NULL ? CGAI_STATUS_OK : CGAI_STATUS_ERROR;
}

/** @brief Map a validated dataset to independent causal records.
 * @param model Borrowed model.
 * @param examples Borrowed examples.
 * @param dataset Mutable allocated record table.
 * @return OK or ERROR with partial ownership retained. */
static cgai_status prepare_records(const cgai_chat_model *model, const cgai_chat_example *examples,
                                   cgai_chat_dataset *dataset) {
    size_t total = 0U;
    /* Step 2: Prepare every independent target before training mutates weights. */
    for (size_t i = 0U; i < dataset->count; ++i) {
        cgai_chat_record *record = &dataset->records[i];
        if (!prepare_record(model, &examples[i], record))
            return CGAI_STATUS_ERROR;
        total += record->count;
        if (total > CGAI_NEURAL_MAX_TOKENS)
            return cgai_fail("too many chat targets");
    }
    return CGAI_STATUS_OK;
}

cgai_status cgai_chat_prepare_dataset(const cgai_chat_model *model,
                                      const cgai_chat_example *examples, size_t count,
                                      cgai_chat_dataset *dataset) {
    if (model == NULL || examples == NULL || count == 0U || count > CGAI_CHAT_MAX_EXAMPLES)
        return cgai_fail("invalid chat dataset");
    /* Step 1: Bound aggregate bytes before retaining token arrays. */
    char *corpus = training_corpus(examples, count);
    if (corpus == NULL)
        return CGAI_STATUS_ERROR;
    free(corpus);
    dataset->records = calloc(count, sizeof(*dataset->records));
    if (dataset->records == NULL)
        return cgai_fail("could not allocate chat dataset");
    dataset->count = count;
    return prepare_records(model, examples, dataset);
}
