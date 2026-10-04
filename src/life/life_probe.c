/** @file life_probe.c @brief Native categorical verification and common masked Adam. */
#include "life_probe.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

uint64_t life_domain_hash_word(uint64_t hash, uint64_t word) {
    for (size_t i = 0U; i < 8U; ++i) {
        hash = (hash ^ (word & UINT64_C(255))) * UINT64_C(1099511628211);
        word >>= 8U;
    }
    return hash;
}

static int probe_allocate(life_probe *probe) {
    const size_t count = probe->model->parameter_count;
    probe->session = cgai_gameplay_session_create(probe->model, 0U);
    probe->gradient = calloc(count, sizeof(double));
    probe->owners = calloc(count, sizeof(unsigned char));
    probe->model->adam_first = calloc(count, sizeof(double));
    probe->model->adam_second = calloc(count, sizeof(double));
    return probe->session != NULL && probe->gradient != NULL && probe->owners != NULL &&
           probe->model->adam_first != NULL && probe->model->adam_second != NULL &&
           life_optimizer_bind(probe->model, probe->owners);
}

static void initialize_owned(life_probe *probe) {
    const cgai_gameplay_config *config = &probe->model->config;
    const size_t outputs = config->output_counts[0];
    memset(probe->model->heads[0], 0,
           config->module_count * config->centroids_per_module * outputs * sizeof(double));
    memset(probe->model->decoders[0], 0,
           config->module_count * outputs * config->hidden_dimensions * sizeof(double));
    for (size_t i = 0U; i < config->module_count; ++i) {
        probe->uids[i] = (uint32_t)i + 1U;
        probe->mass[i] = 1.0;
    }
}

int life_probe_init_kind(life_probe *probe, uint64_t seed, uint32_t groups,
                         cgai_life_domain_kind kind) {
    cgai_gameplay_config config;
    if (probe == NULL)
        return 0;
    memset(probe, 0, sizeof(*probe));
    if (!life_adapter_config(kind, seed, groups, &config))
        return 0;
    probe->model = cgai_gameplay_create(&config);
    if (probe->model == NULL || !probe_allocate(probe)) {
        life_probe_destroy(probe);
        return 0;
    }
    /* Equal cold heads carry no target knowledge. All legal categorical actions
     * are tied, and the declared fallback decides before verified encounters. */
    probe->kind = kind;
    life_adapter_initialize(kind, probe->model);
    initialize_owned(probe);
    return 1;
}

int life_probe_init(life_probe *probe, uint64_t seed, uint32_t groups) {
    return life_probe_init_kind(probe, seed, groups, CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE);
}

uint32_t life_probe_output_count(const life_probe *probe) {
    return probe == NULL || probe->model == NULL ? 0U : probe->model->config.output_counts[0];
}

void life_probe_destroy(life_probe *probe) {
    cgai_gameplay_session_destroy(probe->session);
    cgai_gameplay_destroy(probe->model);
    free(probe->gradient);
    free(probe->owners);
    memset(probe, 0, sizeof(*probe));
}

int life_probe_clone(life_probe *destination, const life_probe *source) {
    if (!life_probe_init_kind(destination, source->model->config.seed,
                              (uint32_t)source->model->config.module_count, source->kind))
        return 0;
    const size_t bytes = source->model->parameter_count * sizeof(double);
    memcpy(destination->model->parameters, source->model->parameters, bytes);
    memcpy(destination->model->adam_first, source->model->adam_first, bytes);
    memcpy(destination->model->adam_second, source->model->adam_second, bytes);
    destination->model->training_step = source->model->training_step;
    destination->model->training_epochs = source->model->training_epochs;
    destination->model->training_shuffle = source->model->training_shuffle;
    memcpy(destination->steps, source->steps, sizeof(source->steps));
    memcpy(destination->mass, source->mass, sizeof(source->mass));
    memcpy(destination->uids, source->uids, sizeof(source->uids));
    return 1;
}

static uint32_t uid_bit(const life_probe *probe, uint32_t uid) {
    for (size_t group = 0U; group < probe->model->config.module_count; ++group)
        if (uid == probe->uids[group])
            return 1U << group;
    return 0U;
}

uint32_t life_probe_uid_mask(const life_probe *probe, const uint32_t *uids, size_t count) {
    uint32_t mask = 0U;
    if (uids == NULL || count == 0U || count > probe->model->config.module_count)
        return 0U;
    for (size_t i = 0U; i < count; ++i) {
        const uint32_t bit = uid_bit(probe, uids[i]);
        if (bit == 0U || (mask & bit) != 0U)
            return 0U;
        mask |= bit;
    }
    return mask;
}

int life_probe_task_valid(const life_probe *probe, const cgai_life_domain_task *task, int train) {
    const uint32_t outputs = life_probe_output_count(probe);
    if (task == NULL || task->version != CGAI_LIFE_DOMAIN_VERSION || task->source_hash == 0U ||
        task->family == 0U || outputs < 2U || outputs > 7U || task->reviewed > 1U ||
        (unsigned int)task->split > CGAI_LIFE_CONTEXT_AUDIT || task->legal_actions == 0U ||
        task->legal_actions >= (1U << outputs) || task->fallback >= outputs ||
        (task->legal_actions & (1U << task->fallback)) == 0U || task->eligible_count < 2U ||
        life_probe_uid_mask(probe, task->eligible_uids, task->eligible_count) == 0U ||
        (train && (task->split != CGAI_LIFE_CONTEXT_TRAIN || task->reviewed != 1U)) ||
        !life_adapter_task_valid(probe->kind, task))
        return 0;
    for (size_t i = task->eligible_count; i < CGAI_LIFE_GROUPS; ++i)
        if (task->eligible_uids[i] != 0U)
            return 0;
    return 1;
}

int life_probe_verify(const life_probe *probe, const cgai_life_domain_task *task, uint32_t *target,
                      uint64_t *teacher) {
    uint32_t verified_target;
    uint64_t verified_teacher;
    if (target == NULL || teacher == NULL || !life_probe_task_valid(probe, task, 0) ||
        !life_adapter_verify(probe->kind, task, &verified_target, &verified_teacher))
        return 0;
    *target = verified_target;
    *teacher = verified_teacher;
    return 1;
}

static uint64_t npc_task_hash(uint64_t hash, const cgai_life_domain_task *task) {
    hash = life_domain_hash_word(hash, task->feature_count);
    for (size_t field = 0U; field < CGAI_LIFE_DOMAIN_FEATURES; ++field)
        hash = life_domain_hash_word(hash, task->features[field]);
    hash = life_domain_hash_word(hash, task->episode_id);
    return life_domain_hash_word(hash, task->tick);
}

uint64_t life_probe_task_hash(const cgai_life_domain_task *task) {
    const uint64_t fields[] = {task->version,  task->family,          (uint64_t)task->split,
                               task->reviewed, task->source_hash,     task->x,
                               task->y,        task->observed_fields, task->legal_actions,
                               task->fallback, task->eligible_count};
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i)
        hash = life_domain_hash_word(hash, fields[i]);
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i)
        hash = life_domain_hash_word(hash, task->eligible_uids[i]);
    return task->feature_count == CGAI_LIFE_DOMAIN_FEATURES ? npc_task_hash(hash, task) : hash;
}

static life_optimizer_view probe_view(const life_probe *probe) {
    const life_optimizer_view view = {probe->model,  probe->session,           probe->gradient,
                                      probe->owners, (uint64_t *)probe->steps, probe->mass};
    return view;
}

int life_probe_predict(const life_probe *probe, const cgai_life_domain_task *task, uint32_t mask,
                       cgai_life_domain_prediction *result) {
    cgai_gameplay_state state = {0};
    const life_optimizer_view view = probe_view(probe);
    life_adapter_encode(probe->kind, task, &state);
    if (!life_optimizer_forward(&view, &state, mask))
        return 0;
    result->action = task->fallback;
    result->probability = probe->session->outputs[task->fallback];
    for (uint32_t action = 0U; action < life_probe_output_count(probe); ++action)
        if ((task->legal_actions & (1U << action)) != 0U &&
            probe->session->outputs[action] > result->probability) {
            result->action = action;
            result->probability = probe->session->outputs[action];
        }
    result->context_hash = life_probe_task_hash(task);
    return isfinite(result->probability) && result->probability >= 0.0 &&
           result->probability <= 1.0;
}

int life_probe_update(life_probe *probe, const cgai_life_domain_task *task, uint32_t target,
                      uint32_t mask) {
    cgai_gameplay_state state = {0};
    const life_optimizer_view view = probe_view(probe);
    life_adapter_encode(probe->kind, task, &state);
    return life_optimizer_step(&view, &state, target, mask);
}

static uint64_t hash_real(uint64_t hash, double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return life_domain_hash_word(hash, bits);
}

uint64_t life_probe_slice_hash(const life_probe *probe, size_t group) {
    if (group > CGAI_LIFE_GROUPS ||
        (group < CGAI_LIFE_GROUPS && group >= probe->model->config.module_count))
        return 0U;
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char owner = group == CGAI_LIFE_GROUPS ? 0U : (unsigned char)(group + 1U);
    if (owner != 0U) {
        hash = life_domain_hash_word(hash, probe->uids[group]);
        hash = life_domain_hash_word(hash, probe->steps[group]);
        hash = hash_real(hash, probe->mass[group]);
    }
    for (size_t i = 0U; i < probe->model->parameter_count; ++i)
        if (probe->owners[i] == owner) {
            hash = hash_real(hash, probe->model->parameters[i]);
            hash = hash_real(hash, probe->model->adam_first[i]);
            hash = hash_real(hash, probe->model->adam_second[i]);
        }
    return hash;
}

static int numerical_valid(const life_probe *probe) {
    for (size_t i = 0U; i < probe->model->parameter_count; ++i) {
        if (!isfinite(probe->model->parameters[i]) || !isfinite(probe->model->adam_first[i]) ||
            !isfinite(probe->model->adam_second[i]) || probe->model->adam_second[i] < 0.0)
            return 0;
        if (probe->owners[i] == 0U &&
            (probe->model->adam_first[i] != 0.0 || probe->model->adam_second[i] != 0.0))
            return 0;
    }
    return 1;
}

static int group_valid(const life_probe *probe, size_t group) {
    if (group >= probe->model->config.module_count)
        return probe->uids[group] == 0U && probe->steps[group] == 0U && probe->mass[group] == 0.0;
    if (probe->uids[group] == 0U || probe->mass[group] != 1.0 ||
        probe->steps[group] > probe->model->training_step)
        return 0;
    for (size_t previous = 0U; previous < group; ++previous)
        if (probe->uids[group] == probe->uids[previous])
            return 0;
    return 1;
}

int life_probe_valid(const life_probe *probe) {
    uint64_t steps = 0U;
    if (probe->model == NULL || probe->session == NULL || probe->gradient == NULL ||
        probe->owners == NULL || probe->model->adam_first == NULL ||
        probe->model->adam_second == NULL || !life_adapter_model_valid(probe->kind, probe->model) ||
        !numerical_valid(probe))
        return 0;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i) {
        if (!group_valid(probe, i) || probe->steps[i] > UINT64_MAX - steps)
            return 0;
        steps += probe->steps[i];
    }
    return steps >= probe->model->training_step;
}
