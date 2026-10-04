/** @file life_policy.c @brief Pair-restricted Adam and mass-aware centroid composition. */
#include "life_policy.h"
#include "gameplay/gameplay_internal.h"
#include "internal/error.h"
#include "internal/model_random.h"
#include "life_optimizer.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct life_policy {
    cgai_gameplay_model *model;
    cgai_gameplay_session *session;
    double *gradient;
    unsigned char *owner; /* Zero: frozen shared scalar. One..group_count: owned slot. */
    uint32_t active;
    uint64_t version;
    uint64_t steps[LIFE_POLICY_MODULES];
    double mass[LIFE_POLICY_MODULES];
};

static cgai_gameplay_config life_config(uint64_t seed, uint32_t group_count) {
    cgai_gameplay_config config = {0};
    config.seed = seed;
    config.routing_temperature = 0.8;
    config.feature_count = 16U;
    config.embedding_dimensions = 16U;
    config.hidden_dimensions = LIFE_POLICY_HIDDEN;
    config.module_count = group_count;
    config.centroids_per_module = 4U;
    config.task_count = 1U;
    for (size_t i = 0U; i < 16U; ++i)
        config.cardinalities[i] = i < 6U ? 55U : i < 12U ? 5U : i < 15U ? 8U : group_count + 1U;
    config.output_counts[0] = LIFE_POLICY_OUTPUTS;
    config.task_modules[0] = (1U << group_count) - 1U;
    config.task_features[0] = 65535U;
    return config;
}

static int allocate_adapter(life_policy *policy) {
    cgai_gameplay_model *model = policy->model;
    policy->session = cgai_gameplay_session_create(model, 0U);
    policy->gradient = calloc(model->parameter_count, sizeof(double));
    policy->owner = calloc(model->parameter_count, sizeof(unsigned char));
    model->adam_first = calloc(model->parameter_count, sizeof(double));
    model->adam_second = calloc(model->parameter_count, sizeof(double));
    if (policy->session == NULL || policy->gradient == NULL || policy->owner == NULL ||
        model->adam_first == NULL || model->adam_second == NULL)
        return cgai_fail("could not allocate Life policy adapter");
    if (!life_optimizer_bind(model, policy->owner))
        return CGAI_STATUS_ERROR;
    for (size_t module = 0U; module < model->config.module_count; ++module)
        policy->mass[module] = 1.0;
    policy->active = (1U << model->config.module_count) - 1U;
    model->training_shuffle = model->config.seed;
    return CGAI_STATUS_OK;
}

static double generic_random(uint64_t *stream, double scale) {
    return ((double)(cgai_random_next(stream) >> 11U) * 0x1p-53 * 2.0 - 1.0) * scale;
}

static void initialize_generic_heads(life_policy *policy, uint64_t seed) {
    /* Four experts cannot encode 23 authored class prototypes. Use unbiased head/readout draws. */
    uint64_t stream = seed ^ UINT64_C(0x63b4ef92dc079851);
    for (size_t i = 0U; i < policy->model->config.module_count * 4U * LIFE_POLICY_OUTPUTS; ++i)
        policy->model->heads[0][i] = generic_random(&stream, 0.04);
    for (size_t i = 0U;
         i < policy->model->config.module_count * LIFE_POLICY_OUTPUTS * LIFE_POLICY_HIDDEN; ++i)
        policy->model->decoders[0][i] = generic_random(&stream, 0.03);
}

life_policy *life_policy_create_groups(uint64_t seed, uint32_t group_count) {
    if (group_count == 0U)
        group_count = LIFE_POLICY_DEFAULT_MODULES;
    if (group_count < 2U || group_count > LIFE_POLICY_MODULES)
        return NULL;
    life_policy *policy = calloc(1U, sizeof(*policy));
    const cgai_gameplay_config config = life_config(seed, group_count);
    if (policy == NULL) {
        (void)cgai_fail("could not allocate Life policy");
        return NULL;
    }
    policy->model = cgai_gameplay_create(&config);
    if (policy->model == NULL || !allocate_adapter(policy)) {
        life_policy_destroy(policy);
        return NULL;
    }
    initialize_generic_heads(policy, seed);
    return policy;
}

life_policy *life_policy_create(uint64_t seed) {
    return life_policy_create_groups(seed, LIFE_POLICY_DEFAULT_MODULES);
}

uint32_t life_policy_group_count(const life_policy *policy) {
    return policy == NULL ? 0U : (uint32_t)policy->model->config.module_count;
}

void life_policy_destroy(life_policy *policy) {
    if (policy == NULL)
        return;
    cgai_gameplay_session_destroy(policy->session);
    cgai_gameplay_destroy(policy->model);
    free(policy->gradient);
    free(policy->owner);
    free(policy);
}

static void copy_state(life_policy *destination, const life_policy *source) {
    const size_t bytes = source->model->parameter_count * sizeof(double);
    memcpy(destination->model->parameters, source->model->parameters, bytes);
    memcpy(destination->model->adam_first, source->model->adam_first, bytes);
    memcpy(destination->model->adam_second, source->model->adam_second, bytes);
    destination->model->training_step = source->model->training_step;
    destination->model->training_epochs = source->model->training_epochs;
    destination->model->training_shuffle = source->model->training_shuffle;
    destination->active = source->active;
    destination->version = source->version;
    memcpy(destination->steps, source->steps, sizeof(source->steps));
    memcpy(destination->mass, source->mass, sizeof(source->mass));
}

life_policy *life_policy_clone(const life_policy *policy) {
    if (policy == NULL) {
        (void)cgai_fail("missing Life policy to clone");
        return NULL;
    }
    life_policy *copy =
        life_policy_create_groups(policy->model->config.seed, life_policy_group_count(policy));
    if (copy != NULL)
        copy_state(copy, policy);
    return copy;
}

static life_optimizer_view optimizer_view(life_policy *policy) {
    const life_optimizer_view view = {policy->model, policy->session, policy->gradient,
                                      policy->owner, policy->steps,   policy->mass};
    return view;
}

static cgai_status policy_forward(life_policy *policy, const cgai_gameplay_state *state,
                                  uint32_t modules) {
    const life_optimizer_view view = optimizer_view(policy);
    return life_optimizer_forward(&view, state, modules);
}
static uint32_t best_output(const life_policy *policy, uint64_t outputs) {
    uint32_t best = 0U;
    for (uint32_t output = 1U; output < LIFE_POLICY_OUTPUTS; ++output)
        if ((outputs & (UINT64_C(1) << output)) != 0U &&
            policy->session->log_outputs[output] > policy->session->log_outputs[best])
            best = output;
    return best;
}

static void describe_selection(const life_policy *policy, uint32_t modules, uint64_t outputs,
                               cgai_gameplay_result *selected) {
    selected->output = best_output(policy, outputs);
    selected->probability = policy->session->outputs[selected->output];
    selected->abstained = selected->output == 0U;
    selected->forward_passes = 1U;
    for (size_t module = 0U; module < life_policy_group_count(policy); ++module)
        if ((modules & (1U << module)) != 0U) {
            ++selected->active_modules;
            selected->module_weights[module] = policy->session->alpha[module];
            selected->module_contributions[module] = fmin(
                1.0, exp(policy->session->log_alpha[module] +
                         policy->session
                             ->log_module_outputs[module * LIFE_POLICY_OUTPUTS + selected->output] -
                         policy->session->log_outputs[selected->output]));
        }
    selected->active_centroids = selected->active_modules * 4U;
}

cgai_status life_policy_select(life_policy *policy, const cgai_gameplay_state *state,
                               uint32_t modules, uint64_t allowed_outputs,
                               cgai_gameplay_result *result) {
    if (policy == NULL || result == NULL ||
        (modules & ~((1U << life_policy_group_count(policy)) - 1U)) != 0U ||
        (allowed_outputs & ~((UINT64_C(1) << LIFE_POLICY_OUTPUTS) - 1U)) != 0U ||
        !cgai_gameplay_validate_state(policy->model, state))
        return cgai_fail("invalid Life policy selection");
    cgai_gameplay_result selected = {0};
    modules &= policy->active;
    const uint64_t outputs = allowed_outputs | UINT64_C(1);
    if (modules == 0U || outputs == 1U) {
        selected.probability = 1.0;
        selected.abstained = 1;
    } else {
        if (!policy_forward(policy, state, modules))
            return CGAI_STATUS_ERROR;
        describe_selection(policy, modules, outputs, &selected);
    }
    *result = selected;
    return CGAI_STATUS_OK;
}

static cgai_status validate_record(const life_policy *policy, const life_record *record) {
    if (policy == NULL || record == NULL || record->target >= LIFE_POLICY_OUTPUTS ||
        record->module_mask == 0U || (record->module_mask & ~policy->active) != 0U ||
        !cgai_gameplay_validate_state(policy->model, &record->state))
        return cgai_fail("invalid Life collision training record");
    return CGAI_STATUS_OK;
}

cgai_status life_policy_evaluate(life_policy *policy, const life_record *record, double *loss) {
    if (loss == NULL || !validate_record(policy, record))
        return cgai_fail("invalid Life policy evaluation");
    if (!policy_forward(policy, &record->state, record->module_mask))
        return CGAI_STATUS_ERROR;
    *loss = -policy->session->log_outputs[record->target];
    return CGAI_STATUS_OK;
}

static cgai_status train_record(life_policy *policy, const life_record *record) {
    const life_optimizer_view view = optimizer_view(policy);
    if (!life_optimizer_step(&view, &record->state, record->target, record->module_mask))
        return CGAI_STATUS_ERROR;
    ++policy->version;
    return CGAI_STATUS_OK;
}
static cgai_status validate_batch_records(const life_policy *policy, const life_record *records,
                                          size_t count, size_t epochs) {
    if (policy == NULL || records == NULL || count == 0U || count > LIFE_POLICY_MAX_RECORDS ||
        epochs == 0U || epochs > 10000U)
        return cgai_fail("invalid Life training batch");
    for (size_t i = 0U; i < count; ++i)
        if (!validate_record(policy, &records[i]))
            return CGAI_STATUS_ERROR;
    return CGAI_STATUS_OK;
}

static cgai_status validate_batch(const life_policy *policy, const life_record *records,
                                  size_t count, size_t epochs) {
    if (!validate_batch_records(policy, records, count, epochs))
        return CGAI_STATUS_ERROR;
    const uint64_t updates = (uint64_t)count * (uint64_t)epochs;
    if ((uint64_t)epochs > UINT64_MAX - policy->model->training_epochs ||
        updates > UINT64_MAX - policy->model->training_step ||
        updates > UINT64_MAX - policy->version)
        return cgai_fail("Life training counters would overflow");
    for (size_t module = 0U; module < life_policy_group_count(policy); ++module)
        if (updates > UINT64_MAX - policy->steps[module])
            return cgai_fail("Life module training clock would overflow");
    return CGAI_STATUS_OK;
}

static void shuffle_records(life_policy *policy, size_t *order, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        order[i] = i;
    for (size_t remaining = count; remaining > 1U; --remaining) {
        const size_t selected =
            (size_t)(cgai_random_next(&policy->model->training_shuffle) % remaining);
        const size_t temporary = order[remaining - 1U];
        order[remaining - 1U] = order[selected];
        order[selected] = temporary;
    }
}

static cgai_status train_epoch(life_policy *policy, const life_record *records, size_t count,
                               size_t *order) {
    shuffle_records(policy, order, count);
    for (size_t i = 0U; i < count; ++i)
        if (!train_record(policy, &records[order[i]]))
            return CGAI_STATUS_ERROR;
    ++policy->model->training_epochs;
    return CGAI_STATUS_OK;
}

cgai_status life_policy_train(life_policy *policy, const life_record *records, size_t count,
                              size_t epochs) {
    if (count == 0U || !validate_batch(policy, records, count, epochs))
        return CGAI_STATUS_ERROR;
    size_t *order = malloc(count * sizeof(*order));
    if (order == NULL)
        return cgai_fail("could not allocate Life batch ordering");
    cgai_status status = CGAI_STATUS_OK;
    for (size_t epoch = 0U; status && epoch < epochs; ++epoch)
        status = train_epoch(policy, records, count, order);
    free(order);
    return status;
}

uint32_t life_policy_active_mask(const life_policy *policy) {
    return policy == NULL ? 0U : policy->active;
}

uint64_t life_policy_version(const life_policy *policy) {
    return policy == NULL ? 0U : policy->version;
}

uint64_t life_policy_steps(const life_policy *policy) {
    return policy == NULL ? 0U : policy->model->training_step;
}

static uint64_t hash_word(uint64_t hash, uint64_t word) {
    for (size_t i = 0U; i < 8U; ++i) {
        hash ^= word & 255U;
        hash *= UINT64_C(1099511628211);
        word >>= 8U;
    }
    return hash;
}

static uint64_t hash_double(uint64_t hash, double value) {
    uint64_t word = 0U;
    memcpy(&word, &value, sizeof(word));
    return hash_word(hash, word);
}

static void hash_parameter_slices(const life_policy *policy, life_policy_info *result) {
    for (size_t i = 0U; i < policy->model->parameter_count; ++i) {
        uint64_t *hash = policy->owner[i] == 0U
                             ? &result->shared_hash
                             : &result->module_hash[(size_t)policy->owner[i] - 1U];
        *hash = hash_double(*hash, policy->model->parameters[i]);
        *hash = hash_double(*hash, policy->model->adam_first[i]);
        *hash = hash_double(*hash, policy->model->adam_second[i]);
    }
}

cgai_status life_policy_inspect(const life_policy *policy, life_policy_info *info) {
    if (policy == NULL || info == NULL)
        return cgai_fail("missing Life policy inspection argument");
    life_policy_info result = {.group_count = life_policy_group_count(policy)};
    result.seed = policy->model->config.seed;
    result.active_mask = policy->active;
    result.version = policy->version;
    result.epochs = policy->model->training_epochs;
    result.updates = policy->model->training_step;
    result.shared_hash = UINT64_C(14695981039346656037);
    for (size_t module = 0U; module < life_policy_group_count(policy); ++module) {
        result.module_steps[module] = policy->steps[module];
        result.module_mass[module] = policy->mass[module];
        result.module_hash[module] =
            hash_word(UINT64_C(14695981039346656037), policy->steps[module]);
    }
    hash_parameter_slices(policy, &result);
    *info = result;
    return CGAI_STATUS_OK;
}

uint64_t life_policy_hash(const life_policy *policy) {
    life_policy_info info;
    if (policy == NULL || !life_policy_inspect(policy, &info))
        return 0U;
    uint64_t hash = hash_word(info.shared_hash, policy->model->config.seed);
    hash = hash_word(hash, info.active_mask);
    hash = hash_word(hash, info.version);
    hash = hash_word(hash, info.epochs);
    hash = hash_word(hash, info.updates);
    hash = hash_word(hash, policy->model->training_shuffle);
    for (size_t module = 0U; module < life_policy_group_count(policy); ++module) {
        hash = hash_word(hash, info.module_hash[module]);
        hash = hash_double(hash, info.module_mass[module]);
    }
    return hash;
}

cgai_status life_policy_module_vector(const life_policy *policy, uint32_t module,
                                      double vector[LIFE_POLICY_HIDDEN]) {
    if (policy == NULL || vector == NULL || module >= life_policy_group_count(policy))
        return cgai_fail("invalid Life centroid inspection");
    memcpy(vector, policy->model->outer + (size_t)module * LIFE_POLICY_HIDDEN,
           LIFE_POLICY_HIDDEN * sizeof(double));
    return CGAI_STATUS_OK;
}

typedef struct life_merge_work {
    life_policy *policy;
    life_policy *candidate;
    const life_record *records;
    size_t count;
    size_t epochs;
    uint32_t parents;
    life_merge_report result;
} life_merge_work;

static cgai_status merge_contract(const life_merge_work *work, const life_merge_report *report) {
    if (work->policy == NULL || report == NULL || work->records == NULL || work->count == 0U ||
        work->count > 256U || work->epochs == 0U || work->epochs > 128U || work->parents == 0U ||
        (work->parents & (work->parents - 1U)) == 0U ||
        (work->parents & ~work->policy->active) != 0U || work->policy->version == UINT64_MAX)
        return cgai_fail("invalid Life consolidation request");
    return CGAI_STATUS_OK;
}

static cgai_status merge_evidence(life_merge_work *work) {
    while ((work->parents & (1U << work->result.survivor)) == 0U)
        ++work->result.survivor;
    work->result.retired_mask = work->parents & ~(1U << work->result.survivor);
    for (size_t i = 0U; i < work->count; ++i) {
        if (!validate_record(work->policy, &work->records[i]))
            return CGAI_STATUS_ERROR;
        for (size_t module = 0U; module < life_policy_group_count(work->policy); ++module)
            if ((work->parents & work->records[i].module_mask & (1U << module)) != 0U)
                ++work->result.parent_samples[module];
    }
    return CGAI_STATUS_OK;
}

static int merge_covered(const life_merge_work *work) {
    for (size_t module = 0U; module < life_policy_group_count(work->policy); ++module)
        if ((work->parents & (1U << module)) != 0U && work->result.parent_samples[module] == 0U)
            return 0;
    return 1;
}

static cgai_status distill_candidate(life_merge_work *work) {
    life_record *distilled = malloc(work->count * sizeof(*distilled));
    if (distilled == NULL)
        return cgai_fail("could not allocate Life consolidation replay");
    size_t used = 0U;
    for (size_t i = 0U; i < work->count; ++i)
        if ((work->records[i].module_mask & work->parents) != 0U) {
            distilled[used] = work->records[i];
            distilled[used++].module_mask = 1U << work->result.survivor;
        }
    const cgai_status status =
        used == 0U ? cgai_fail("Life consolidation replay has no parent examples")
                   : life_policy_train(work->candidate, distilled, used, work->epochs);
    free(distilled);
    return status;
}

static void consolidate_mass(life_merge_work *work) {
    for (size_t module = 0U; module < life_policy_group_count(work->policy); ++module)
        if ((work->result.retired_mask & (1U << module)) != 0U) {
            work->candidate->mass[work->result.survivor] += work->candidate->mass[module];
            work->candidate->mass[module] = 0.0;
        }
    work->candidate->active &= ~work->result.retired_mask;
}

static cgai_status validate_parent_sample(life_merge_work *work, const life_record *record,
                                          size_t module) {
    if (!policy_forward(work->policy, &record->state, 1U << module))
        return CGAI_STATUS_ERROR;
    const uint32_t before = best_output(work->policy, UINT64_MAX);
    work->result.parent_loss_before[module] -= work->policy->session->log_outputs[record->target];
    if (!policy_forward(work->candidate, &record->state, 1U << work->result.survivor))
        return CGAI_STATUS_ERROR;
    work->result.parent_loss_after[module] -= work->candidate->session->log_outputs[record->target];
    work->result.parent_agreement[module] +=
        best_output(work->candidate, UINT64_MAX) == before ? 1.0 : 0.0;
    return CGAI_STATUS_OK;
}

static cgai_status validate_global_sample(life_merge_work *work, const life_record *record) {
    if (!policy_forward(work->policy, &record->state, work->policy->active))
        return CGAI_STATUS_ERROR;
    const uint32_t before = best_output(work->policy, UINT64_MAX);
    work->result.global_loss_before -= work->policy->session->log_outputs[record->target];
    if (!policy_forward(work->candidate, &record->state, work->candidate->active))
        return CGAI_STATUS_ERROR;
    work->result.global_loss_after -= work->candidate->session->log_outputs[record->target];
    work->result.global_agreement += best_output(work->candidate, UINT64_MAX) == before ? 1.0 : 0.0;
    return CGAI_STATUS_OK;
}

static uint64_t record_legal_outputs(const life_record *record) {
    uint64_t allowed = 3U;
    uint32_t output = 8U;
    for (size_t i = 0U; i < 6U; ++i) {
        const int present = record->state.values[i] != 54U;
        if (present)
            allowed |= UINT64_C(1) << (i + 2U);
        for (size_t j = i + 1U; j < 6U; ++j) {
            if (present && record->state.values[j] != 54U)
                allowed |= UINT64_C(1) << output;
            ++output;
        }
    }
    return allowed;
}

static cgai_status validate_runtime_sample(life_merge_work *work, const life_record *record) {
    uint32_t remapped = record->module_mask & ~work->parents;
    if ((record->module_mask & work->parents) != 0U)
        remapped |= 1U << work->result.survivor;
    const uint64_t allowed = record_legal_outputs(record);
    cgai_gameplay_result before, after;
    if (!life_policy_select(work->policy, &record->state, record->module_mask, allowed, &before) ||
        !life_policy_select(work->candidate, &record->state, remapped, allowed, &after))
        return CGAI_STATUS_ERROR;
    work->result.runtime_agreement += before.output == after.output ? 1.0 : 0.0;
    return CGAI_STATUS_OK;
}

static cgai_status validate_merge_samples(life_merge_work *work) {
    for (size_t i = 0U; i < work->count; ++i) {
        const life_record *record = &work->records[i];
        for (size_t module = 0U; module < life_policy_group_count(work->policy); ++module)
            if ((record->module_mask & work->parents & (1U << module)) != 0U &&
                !validate_parent_sample(work, record, module))
                return CGAI_STATUS_ERROR;
        if (!validate_global_sample(work, record) || !validate_runtime_sample(work, record))
            return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

static void finish_parent_report(life_merge_report *result) {
    for (size_t module = 0U; module < LIFE_POLICY_MODULES; ++module)
        if (result->parent_samples[module] != 0U) {
            const double samples = (double)result->parent_samples[module];
            result->parent_loss_before[module] /= samples;
            result->parent_loss_after[module] /= samples;
            result->parent_agreement[module] /= samples;
            if (result->parent_agreement[module] < 0.95 ||
                result->parent_loss_after[module] > result->parent_loss_before[module] + 0.10)
                result->accepted = 0;
        }
}

static void finish_merge_report(life_merge_work *work) {
    life_merge_report *result = &work->result;
    result->accepted = 1;
    finish_parent_report(result);
    result->global_loss_before /= (double)work->count;
    result->global_loss_after /= (double)work->count;
    result->global_agreement /= (double)work->count;
    result->runtime_agreement /= (double)work->count;
    if (result->global_agreement < 0.95 || result->runtime_agreement < 0.95 ||
        result->global_loss_after > result->global_loss_before + 0.10)
        result->accepted = 0;
    if (result->accepted) {
        ++work->candidate->version;
        copy_state(work->policy, work->candidate);
    }
}

static cgai_status run_merge_candidate(life_merge_work *work) {
    work->candidate = life_policy_clone(work->policy);
    if (work->candidate == NULL)
        return CGAI_STATUS_ERROR;
    cgai_status status = distill_candidate(work);
    if (status) {
        consolidate_mass(work);
        status = validate_merge_samples(work);
    }
    if (status)
        finish_merge_report(work);
    life_policy_destroy(work->candidate);
    return status;
}

cgai_status life_policy_try_merge(life_policy *policy, uint32_t parents, const life_record *records,
                                  size_t count, size_t epochs, life_merge_report *report) {
    life_merge_work work = {policy, NULL, records, count, epochs, parents, {0}};
    if (!merge_contract(&work, report) || !merge_evidence(&work))
        return CGAI_STATUS_ERROR;
    if (!merge_covered(&work)) {
        *report = work.result;
        return CGAI_STATUS_OK;
    }
    const cgai_status status = run_merge_candidate(&work);
    if (status)
        *report = work.result;
    return status;
}

static int checkpoint_write(FILE *file, const life_policy *policy) {
    int okay = fprintf(file,
                       "CGAI_LIFE_POLICY 2\n%" PRIu32 " %" PRIu64 " %" PRIu32 " %" PRIu64
                       " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
                       life_policy_group_count(policy), policy->model->config.seed, policy->active,
                       policy->version, policy->model->training_epochs,
                       policy->model->training_step, policy->model->training_shuffle) >= 0;
    for (size_t module = 0U; okay && module < life_policy_group_count(policy); ++module)
        okay = fprintf(file, "%" PRIu64 " %a\n", policy->steps[module], policy->mass[module]) >= 0;
    if (okay)
        okay = fprintf(file, "%zu\n", policy->model->parameter_count) >= 0;
    for (size_t i = 0U; okay && i < policy->model->parameter_count; ++i)
        okay = fprintf(file, "%a %a %a\n", policy->model->parameters[i],
                       policy->model->adam_first[i], policy->model->adam_second[i]) >= 0;
    return okay;
}

cgai_status life_policy_checkpoint_write(const life_policy *policy, FILE *file) {
    if (policy == NULL || file == NULL)
        return cgai_fail("invalid Life policy checkpoint stream");
    return checkpoint_write(file, policy) ? CGAI_STATUS_OK
                                          : cgai_fail("could not write Life policy checkpoint");
}

cgai_status life_policy_checkpoint_save(const life_policy *policy, const char *path) {
    if (policy == NULL || path == NULL || path[0] == '\0')
        return cgai_fail("invalid Life policy checkpoint destination");
    FILE *file = fopen(path, "w");
    if (file == NULL)
        return cgai_fail("could not open Life policy checkpoint");
    int okay = life_policy_checkpoint_write(policy, file) == CGAI_STATUS_OK;
    if (fclose(file) != 0)
        okay = 0;
    return okay ? CGAI_STATUS_OK : cgai_fail("could not write Life policy checkpoint");
}

static int read_token(FILE *file, char *token, size_t capacity) {
    size_t length = 0U;
    int next;
    if (ferror(file) || feof(file))
        return 0;
    do {
        next = fgetc(file);
    } while (next != EOF && isspace((unsigned char)next));
    while (next != EOF && !isspace((unsigned char)next)) {
        if (next == 0 || length + 1U >= capacity)
            return 0;
        token[length++] = (char)next;
        next = fgetc(file);
    }
    token[length] = '\0';
    return length != 0U && !ferror(file);
}

static int read_word(FILE *file, uint64_t *value) {
    char token[64];
    if (!read_token(file, token, sizeof(token)) || token[0] < '0' || token[0] > '9')
        return 0;
    char *end = NULL;
    errno = 0;
    const unsigned long long parsed = strtoull(token, &end, 10);
    if (errno != 0 || end == token || *end != '\0')
        return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static int read_double(FILE *file, double *value) {
    char token[96];
    if (!read_token(file, token, sizeof(token)))
        return 0;
    const char *start = token[0] == '-' ? token + 1 : token;
    if (start[0] != '0' || start[1] != 'x')
        return 0;
    char *end = NULL;
    errno = 0;
    const double parsed = strtod(token, &end);
    /* Tiny nonzero hexadecimal doubles may set ERANGE despite exact representability. */
    if ((errno != 0 && errno != ERANGE) || end == token || *end != '\0' || !isfinite(parsed))
        return 0;
    *value = parsed;
    return 1;
}

static int checkpoint_mass_coherent(const life_policy *policy) {
    double mass = 0.0;
    for (size_t module = 0U; module < life_policy_group_count(policy); ++module) {
        if (policy->steps[module] > policy->model->training_step || policy->mass[module] < 0.0 ||
            policy->mass[module] > (double)life_policy_group_count(policy) ||
            policy->mass[module] != floor(policy->mass[module]) ||
            (((policy->active & (1U << module)) != 0U) != (policy->mass[module] > 0.0)))
            return 0;
        mass += policy->mass[module];
    }
    return mass == (double)life_policy_group_count(policy);
}

static int checkpoint_moments_coherent(const life_policy *policy) {
    for (size_t i = 0U; i < policy->model->parameter_count; ++i) {
        if (policy->model->adam_second[i] < 0.0)
            return 0;
        if ((policy->owner[i] == 0U || policy->steps[(size_t)policy->owner[i] - 1U] == 0U) &&
            (policy->model->adam_first[i] != 0.0 || policy->model->adam_second[i] != 0.0))
            return 0;
    }
    return 1;
}

static int checkpoint_coherent(const life_policy *policy) {
    if (policy->active == 0U ||
        (policy->active & ~((1U << life_policy_group_count(policy)) - 1U)) != 0U ||
        policy->version < policy->model->training_step ||
        policy->model->training_epochs > policy->model->training_step)
        return 0;
    return checkpoint_mass_coherent(policy) && checkpoint_moments_coherent(policy);
}

static life_policy *checkpoint_read_header(FILE *file) {
    char magic[32];
    uint64_t format = 0U, seed = 0U, groups = LIFE_POLICY_DEFAULT_MODULES;
    if (!read_token(file, magic, sizeof(magic)) || strcmp(magic, "CGAI_LIFE_POLICY") != 0 ||
        !read_word(file, &format) || (format != 1U && format != 2U))
        return NULL;
    if ((format == 2U && !read_word(file, &groups)) || groups < 2U ||
        groups > LIFE_POLICY_MODULES || !read_word(file, &seed))
        return NULL;
    return life_policy_create_groups(seed, (uint32_t)groups);
}

static int checkpoint_read_adapter(FILE *file, life_policy *policy) {
    uint64_t active = 0U;
    int okay =
        read_word(file, &active) && active <= ((1U << life_policy_group_count(policy)) - 1U) &&
        read_word(file, &policy->version) && read_word(file, &policy->model->training_epochs) &&
        read_word(file, &policy->model->training_step) &&
        read_word(file, &policy->model->training_shuffle);
    policy->active = (uint32_t)active;
    for (size_t module = 0U; okay && module < life_policy_group_count(policy); ++module)
        okay = read_word(file, &policy->steps[module]) && read_double(file, &policy->mass[module]);
    return okay;
}

static int checkpoint_read_parameters(FILE *file, life_policy *policy) {
    uint64_t count = 0U;
    int okay = read_word(file, &count) && count == policy->model->parameter_count;
    for (size_t i = 0U; okay && i < policy->model->parameter_count; ++i)
        okay = read_double(file, &policy->model->parameters[i]) &&
               read_double(file, &policy->model->adam_first[i]) &&
               read_double(file, &policy->model->adam_second[i]);
    return okay;
}

static int checkpoint_end(FILE *file) {
    int character;
    if (feof(file))
        return !ferror(file);
    do {
        character = fgetc(file);
    } while (character != EOF && isspace((unsigned char)character));
    return character == EOF && !ferror(file);
}

life_policy *life_policy_checkpoint_read(FILE *file) {
    if (file == NULL)
        return NULL;
    life_policy *policy = checkpoint_read_header(file);
    const int okay = policy != NULL && checkpoint_read_adapter(file, policy) &&
                     checkpoint_read_parameters(file, policy) && checkpoint_coherent(policy);
    if (!okay) {
        life_policy_destroy(policy);
        (void)cgai_fail("malformed or incoherent Life policy checkpoint");
        return NULL;
    }
    return policy;
}

life_policy *life_policy_checkpoint_load(const char *path) {
    if (path == NULL || path[0] == '\0') {
        (void)cgai_fail("invalid Life policy checkpoint source");
        return NULL;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        (void)cgai_fail("could not open Life policy checkpoint");
        return NULL;
    }
    life_policy *policy = life_policy_checkpoint_read(file);
    int okay = policy != NULL && checkpoint_end(file);
    if (fclose(file) != 0)
        okay = 0;
    if (!okay) {
        life_policy_destroy(policy);
        (void)cgai_fail("malformed or incoherent Life policy checkpoint");
        return NULL;
    }
    return policy;
}
