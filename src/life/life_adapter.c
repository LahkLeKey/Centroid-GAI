/** @file life_adapter.c @brief Versioned target-independent categorical representations. */
#include "life_adapter.h"
#include "life_npc_world.h"
#include <string.h>

static const uint32_t npc_cards[CGAI_LIFE_DOMAIN_FEATURES] = {6U, 6U, 6U, 6U, 17U, 17U, 2U, 5U,
                                                              5U, 7U, 8U, 8U, 4U,  2U,  8U, 3U};

static uint32_t category_bits(uint32_t cardinality) {
    uint32_t bits = 0U;
    for (uint32_t maximum = cardinality - 1U; maximum != 0U; maximum >>= 1U)
        ++bits;
    return bits;
}

static void configure_features(cgai_gameplay_config *config, int npc) {
    config->feature_count = npc ? CGAI_LIFE_DOMAIN_FEATURES : 2U;
    config->hidden_dimensions = npc ? 48U : 6U;
    config->cardinalities[0] = config->cardinalities[1] = 3U;
    if (npc)
        memcpy(config->cardinalities, npc_cards, sizeof(npc_cards));
    config->output_counts[0] = npc ? 7U : 3U;
    config->task_features[0] = npc ? UINT64_C(65535) : UINT64_C(3);
}

int life_adapter_config(cgai_life_domain_kind kind, uint64_t seed, uint32_t groups,
                        cgai_gameplay_config *config) {
    if (config == NULL || groups < CGAI_LIFE_MIN_GROUPS || groups > CGAI_LIFE_GROUPS ||
        (kind != CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE && kind != CGAI_LIFE_DOMAIN_NATIVE_NPC))
        return 0;
    const int npc = kind == CGAI_LIFE_DOMAIN_NATIVE_NPC;
    memset(config, 0, sizeof(*config));
    config->seed = seed;
    config->embedding_dimensions = 6U;
    config->module_count = groups;
    config->centroids_per_module = 4U;
    config->task_count = 1U;
    config->task_modules[0] = (UINT64_C(1) << groups) - 1U;
    config->routing_temperature = 0.8;
    configure_features(config, npc);
    return 1;
}

static double category_coordinate(uint32_t value, uint32_t dimension, uint32_t bits) {
    return dimension < bits ? ((value & (1U << dimension)) != 0U ? 1.0 : -1.0) : 0.0;
}

static void initialize_feature(cgai_gameplay_model *model, size_t feature, size_t hidden) {
    const uint32_t bits = category_bits(npc_cards[feature]);
    const size_t dimensions = model->config.embedding_dimensions;
    for (uint32_t value = 0U; value < npc_cards[feature]; ++value)
        for (uint32_t dimension = 0U; dimension < dimensions; ++dimension)
            model->embeddings[(model->category_offsets[feature] + value) * dimensions + dimension] =
                category_coordinate(value, dimension, bits);
    for (uint32_t bit = 0U; bit < bits; ++bit)
        model->encoder[(hidden + bit) * model->input_count + feature * dimensions + bit] = 1.0;
}

void life_adapter_initialize(cgai_life_domain_kind kind, cgai_gameplay_model *model) {
    if (kind != CGAI_LIFE_DOMAIN_NATIVE_NPC)
        return;
    size_t hidden = 0U;
    memset(model->encoder, 0,
           model->config.hidden_dimensions * model->input_count * sizeof(double));
    memset(model->bias, 0, model->config.hidden_dimensions * sizeof(double));
    /* Recipe 1: 110 categories have field-local signed binary coordinates. The
     * encoder assigns 46 distinct axes in feature order, low bit first. Each axis
     * is tanh(-1) or tanh(1), so every category is distinguishable without labels,
     * episode IDs, family IDs, private world state or target-based initialization.
     * The remaining two hidden axes are zero. All shared slices remain frozen. */
    for (size_t feature = 0U; feature < CGAI_LIFE_DOMAIN_FEATURES; ++feature) {
        initialize_feature(model, feature, hidden);
        hidden += category_bits(npc_cards[feature]);
    }
}

static int probe_payload_zero(const cgai_life_domain_task *task) {
    if (task->feature_count != 0U || task->episode_id != 0U || task->tick != 0U)
        return 0;
    for (size_t field = 0U; field < CGAI_LIFE_DOMAIN_FEATURES; ++field)
        if (task->features[field] != 0U)
            return 0;
    return 1;
}

static int npc_payload_valid(const cgai_life_domain_task *task) {
    if (task->x != 0U || task->y != 0U || task->observed_fields != UINT32_C(65535) ||
        task->feature_count != CGAI_LIFE_DOMAIN_FEATURES || task->episode_id == 0U ||
        task->tick >= 64U || task->features[14] != (task->tick < 7U ? task->tick : 7U))
        return 0;
    for (size_t feature = 0U; feature < CGAI_LIFE_DOMAIN_FEATURES; ++feature)
        if (task->features[feature] >= npc_cards[feature] ||
            (feature < 4U && task->features[feature] == 5U))
            return 0;
    return life_npc_task_valid(task);
}

int life_adapter_task_valid(cgai_life_domain_kind kind, const cgai_life_domain_task *task) {
    if (task == NULL)
        return 0;
    if (kind == CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE)
        return task->x <= 2U && task->y <= 2U && task->observed_fields == 3U &&
               probe_payload_zero(task);
    return kind == CGAI_LIFE_DOMAIN_NATIVE_NPC && npc_payload_valid(task);
}

void life_adapter_encode(cgai_life_domain_kind kind, const cgai_life_domain_task *task,
                         cgai_gameplay_state *state) {
    memset(state, 0, sizeof(*state));
    if (kind == CGAI_LIFE_DOMAIN_NATIVE_NPC)
        memcpy(state->values, task->features, sizeof(task->features));
    else {
        state->values[0] = task->x;
        state->values[1] = task->y;
    }
}

int life_adapter_verify(cgai_life_domain_kind kind, const cgai_life_domain_task *task,
                        uint32_t *target, uint64_t *teacher) {
    if (kind == CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE) {
        *target = (task->x + 2U * task->y) % 3U;
        *teacher = LIFE_PROBE_TEACHER;
        return 1;
    }
    if (kind != CGAI_LIFE_DOMAIN_NATIVE_NPC || !life_npc_task_valid(task))
        return 0;
    *target = life_npc_teacher_task(task);
    *teacher = LIFE_NPC_TEACHER;
    return *target < 7U;
}

static int feature_embeddings_valid(const cgai_gameplay_model *model, size_t feature) {
    const uint32_t bits = category_bits(npc_cards[feature]);
    const size_t dimensions = model->config.embedding_dimensions;
    for (uint32_t value = 0U; value < npc_cards[feature]; ++value)
        for (uint32_t dimension = 0U; dimension < dimensions; ++dimension)
            if (model->embeddings[(model->category_offsets[feature] + value) * dimensions +
                                  dimension] != category_coordinate(value, dimension, bits))
                return 0;
    return 1;
}

static int encoder_row_valid(const cgai_gameplay_model *model, size_t row, size_t source) {
    if (model->bias[row] != 0.0)
        return 0;
    for (size_t input = 0U; input < model->input_count; ++input)
        if (model->encoder[row * model->input_count + input] != (input == source ? 1.0 : 0.0))
            return 0;
    return 1;
}

static int feature_shared_valid(const cgai_gameplay_model *model, size_t feature, size_t hidden) {
    if (!feature_embeddings_valid(model, feature))
        return 0;
    for (uint32_t bit = 0U; bit < category_bits(npc_cards[feature]); ++bit)
        if (!encoder_row_valid(model, hidden + bit,
                               feature * model->config.embedding_dimensions + bit))
            return 0;
    return 1;
}

static int npc_shared_valid(const cgai_gameplay_model *model) {
    size_t hidden = 0U;
    for (size_t feature = 0U; feature < CGAI_LIFE_DOMAIN_FEATURES; ++feature) {
        if (!feature_shared_valid(model, feature, hidden))
            return 0;
        hidden += category_bits(npc_cards[feature]);
    }
    for (; hidden < model->config.hidden_dimensions; ++hidden)
        if (!encoder_row_valid(model, hidden, model->input_count))
            return 0;
    return 1;
}

int life_adapter_model_valid(cgai_life_domain_kind kind, const cgai_gameplay_model *model) {
    cgai_gameplay_config expected;
    if (model == NULL || !life_adapter_config(kind, model->config.seed,
                                              (uint32_t)model->config.module_count, &expected))
        return 0;
    const cgai_gameplay_config *actual = &model->config;
    if (actual->feature_count != expected.feature_count ||
        actual->embedding_dimensions != expected.embedding_dimensions ||
        actual->hidden_dimensions != expected.hidden_dimensions ||
        actual->centroids_per_module != expected.centroids_per_module ||
        actual->task_count != expected.task_count ||
        actual->routing_temperature != expected.routing_temperature ||
        memcmp(actual->cardinalities, expected.cardinalities, sizeof(expected.cardinalities)) !=
            0 ||
        memcmp(actual->output_counts, expected.output_counts, sizeof(expected.output_counts)) !=
            0 ||
        memcmp(actual->task_modules, expected.task_modules, sizeof(expected.task_modules)) != 0 ||
        memcmp(actual->task_features, expected.task_features, sizeof(expected.task_features)) != 0)
        return 0;
    return kind != CGAI_LIFE_DOMAIN_NATIVE_NPC || npc_shared_valid(model);
}
