/** @file life_context_feedback.c @brief Measured domain utility learned at authentic contacts. */
#include "life_context.h"
#include <math.h>
#include <string.h>

#define CHOICE_HASH_START UINT64_C(14695981039346656037)
#define CHOICE_HASH_PRIME UINT64_C(1099511628211)
#define CHOICE_EVENT_MASK ((1U << CGAI_LIFE_MAX_COLLISION_EVENTS) - 1U)
#define CHOICE_RELEVANCE 0.5
#define CHOICE_LEARNING_RATE 0.1

typedef struct choice_prediction {
    double utility;
    uint32_t supported;
} choice_prediction;

static uint64_t hash_word(uint64_t hash, uint64_t value) {
    for (size_t i = 0U; i < 8U; ++i) {
        hash = (hash ^ (value & UINT64_C(255))) * CHOICE_HASH_PRIME;
        value >>= 8U;
    }
    return hash;
}

static uint64_t real_bits(double value) {
    uint64_t bits;
    _Static_assert(sizeof(bits) == sizeof(value), "Outcome hashes require 64-bit doubles");
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t hash_vector(uint64_t hash, const double *values) {
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
        hash = hash_word(hash, real_bits(values[i]));
    return hash;
}

static uint64_t hash_decision(uint64_t hash, const cgai_life_context_choice *decision) {
    hash = hash_word(hash, decision->input_hash);
    hash = hash_word(hash, decision->model_version);
    hash = hash_word(hash, decision->world_hash);
    hash = hash_word(hash, decision->generation);
    hash = hash_word(hash, decision->conflict_id);
    hash = hash_word(hash, decision->participant_mask);
    hash = hash_word(hash, decision->action);
    hash = hash_word(hash, decision->supported_groups);
    return hash_word(hash, real_bits(decision->predicted_utility));
}

static uint64_t decision_token(const life_context_choice_state *state,
                               const cgai_life_context_choice *decision) {
    const uint64_t hash = hash_decision(hash_word(CHOICE_HASH_START, state->decisions), decision);
    return hash == 0U ? UINT64_C(1) : hash;
}

static uint64_t model_hash(uint64_t hash, const life_context_action_model *model) {
    hash = hash_word(hash, model->observations);
    hash = hash_word(hash, model->steps);
    hash = hash_vector(hash, model->centroid);
    return hash_vector(hash, model->readout);
}

static uint64_t owned_hash(const life_context_choice_state *state, size_t group) {
    uint64_t hash = hash_word(CHOICE_HASH_START, state->group_steps[group]);
    for (size_t action = 0U; action < CGAI_LIFE_CONTEXT_ACTIONS; ++action)
        hash = model_hash(hash, &state->models[group][action]);
    return hash;
}

uint64_t life_context_choice_hash(const cgai_life_context *owner) {
    const life_context_choice_state *state = &owner->choice;
    uint64_t hash = hash_word(CHOICE_HASH_START, state->version);
    hash = hash_word(hash, state->decisions);
    hash = hash_word(hash, state->observations);
    hash = hash_word(hash, state->deferred);
    hash = hash_word(hash, state->consumed_generation);
    hash = hash_word(hash, state->consumed_mask);
    hash = hash_word(hash, state->pending);
    hash = hash_word(hash_decision(hash, &state->decision), state->decision.token);
    hash = hash_word(hash, life_context_bytes_hash(state->input, strlen(state->input)));
    hash = hash_vector(hash, state->features);
    for (size_t group = 0U; group < life_context_group_count(owner); ++group)
        hash = hash_word(hash, owned_hash(state, group));
    return hash;
}

static double bounded(double value) { return fmax(-1.0, fmin(1.0, value)); }

static double model_prediction(const life_context_action_model *model, const double *features) {
    double value = 0.0;
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
        value += model->readout[i] * features[i];
    return bounded(value);
}

static double model_relevance(const life_context_action_model *model, const double *features) {
    double dot = 0.0, squared = 0.0;
    if (model->observations == 0U)
        return 0.0;
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i) {
        dot += features[i] * model->centroid[i];
        squared += model->centroid[i] * model->centroid[i];
    }
    return squared == 0.0 ? 0.0 : fmin(1.0, dot / sqrt(squared));
}

static choice_prediction action_prediction(const life_context_choice_state *state,
                                           const double *features, uint32_t groups,
                                           uint32_t action) {
    choice_prediction prediction = {0};
    double weight = 0.0;
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group) {
        const life_context_action_model *model = &state->models[group][action];
        const double relevance = model_relevance(model, features);
        if ((groups & (1U << group)) != 0U && relevance >= CHOICE_RELEVANCE) {
            prediction.utility += relevance * model_prediction(model, features);
            prediction.supported |= 1U << group;
            weight += relevance;
        }
    }
    prediction.utility = weight == 0.0 ? 0.0 : bounded(prediction.utility / weight);
    return prediction;
}

static int actions_valid(const uint32_t *actions, size_t count, uint32_t fallback) {
    int found = 0;
    if (actions == NULL || count == 0U || count > CGAI_LIFE_CONTEXT_MAX_CHOICES)
        return 0;
    for (size_t i = 0U; i < count; ++i) {
        if (actions[i] >= CGAI_LIFE_CONTEXT_ACTIONS)
            return 0;
        for (size_t j = 0U; j < i; ++j)
            if (actions[j] == actions[i])
                return 0;
        found |= actions[i] == fallback;
    }
    return found;
}

static void select_action(const life_context_choice_state *state, const double *features,
                          uint32_t groups, const uint32_t *actions, size_t count,
                          cgai_life_context_choice *selected) {
    choice_prediction best = action_prediction(state, features, groups, selected->action);
    for (size_t i = 0U; i < count; ++i) {
        const choice_prediction candidate = action_prediction(state, features, groups, actions[i]);
        if (candidate.utility > best.utility) {
            best = candidate;
            selected->action = actions[i];
        }
    }
    selected->supported_groups = best.supported;
    selected->predicted_utility = best.utility;
}

cgai_life_status cgai_life_context_choice_predict(const cgai_life_context *owner, const char *input,
                                                  uint32_t participant_mask,
                                                  const uint32_t *actions, size_t action_count,
                                                  uint32_t fallback,
                                                  cgai_life_context_choice *output) {
    double features[CGAI_LIFE_CONTEXT_FEATURES];
    cgai_life_context_choice selected = {0};
    if (owner == NULL || output == NULL || participant_mask == 0U ||
        (participant_mask & ~life_context_group_mask(owner)) != 0U ||
        !actions_valid(actions, action_count, fallback) || !life_context_encode(input, features))
        return CGAI_LIFE_INVALID_ARGUMENT;
    selected.input_hash = life_context_bytes_hash(input, strlen(input));
    selected.model_version = owner->choice.version;
    selected.participant_mask = participant_mask;
    selected.action = fallback;
    select_action(&owner->choice, features, participant_mask, actions, action_count, &selected);
    *output = selected;
    return CGAI_LIFE_OK;
}

cgai_life_status cgai_life_context_get_events(const cgai_life_context *owner,
                                              cgai_life_events *output) {
    if (owner == NULL || output == NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    *output = owner->events;
    return CGAI_LIFE_OK;
}

static int contact_available(const cgai_life_context *owner, uint32_t index) {
    const cgai_life_events *events = &owner->events;
    if (owner->choice.pending != 0U || events->generation_valid != 1U ||
        events->generation != owner->run.world.tick || index >= events->event_count ||
        index >= CGAI_LIFE_MAX_COLLISION_EVENTS)
        return 0;
    return owner->choice.consumed_generation != events->generation ||
           (owner->choice.consumed_mask & (1U << index)) == 0U;
}

static void publish_decision(cgai_life_context *owner, uint32_t index, const char *input,
                             const double *features, cgai_life_context_choice *decision) {
    life_context_choice_state *state = &owner->choice;
    if (state->consumed_generation != decision->generation)
        state->consumed_mask = 0U;
    state->consumed_generation = decision->generation;
    state->consumed_mask |= 1U << index;
    ++state->decisions;
    decision->token = decision_token(state, decision);
    state->decision = *decision;
    memcpy(state->input, input, strlen(input) + 1U);
    memcpy(state->features, features, sizeof(state->features));
    state->pending = 1U;
}

cgai_life_status cgai_life_context_choice_begin(cgai_life_context *owner, uint32_t event_index,
                                                const char *input, const uint32_t *actions,
                                                size_t action_count, uint32_t fallback,
                                                cgai_life_context_choice *output) {
    double features[CGAI_LIFE_CONTEXT_FEATURES];
    cgai_life_context_choice decision;
    if (owner == NULL || output == NULL || !contact_available(owner, event_index) ||
        !life_context_encode(input, features))
        return CGAI_LIFE_INVALID_ARGUMENT;
    if (owner->choice.decisions == UINT64_MAX)
        return CGAI_LIFE_LIMIT_REACHED;
    const cgai_life_collision_event *event = &owner->events.events[event_index];
    if (cgai_life_context_choice_predict(owner, input, event->participant_mask, actions,
                                         action_count, fallback, &decision) != CGAI_LIFE_OK)
        return CGAI_LIFE_INVALID_ARGUMENT;
    decision.generation = event->generation;
    decision.conflict_id = event->conflict_id;
    decision.world_hash = event->source_world_hash;
    publish_decision(owner, event_index, input, features, &decision);
    *output = decision;
    return CGAI_LIFE_OK;
}

static int same_decision(const cgai_life_context_choice *first,
                         const cgai_life_context_choice *second) {
    return first->token == second->token && first->input_hash == second->input_hash &&
           first->model_version == second->model_version &&
           first->world_hash == second->world_hash && first->generation == second->generation &&
           first->conflict_id == second->conflict_id &&
           first->participant_mask == second->participant_mask && first->action == second->action &&
           first->supported_groups == second->supported_groups &&
           real_bits(first->predicted_utility) == real_bits(second->predicted_utility);
}

static int feedback_valid(const cgai_life_context_feedback *feedback) {
    return feedback != NULL && feedback->split == CGAI_LIFE_CONTEXT_TRAIN &&
           feedback->verified == 1U && feedback->deferred <= 1U && feedback->evidence_hash != 0U &&
           isfinite(feedback->utility) && feedback->utility >= -1.0 && feedback->utility <= 1.0;
}

static int update_counters_valid(const cgai_life_context *owner) {
    const life_context_choice_state *state = &owner->choice;
    const uint64_t epochs = owner->run.config.training_epochs;
    if (state->observations == UINT64_MAX || state->version == UINT64_MAX)
        return 0;
    for (size_t group = 0U; group < life_context_group_count(owner); ++group) {
        const life_context_action_model *model = &state->models[group][state->decision.action];
        if ((state->decision.participant_mask & (1U << group)) != 0U &&
            (model->observations == UINT64_MAX || model->steps > UINT64_MAX - epochs ||
             state->group_steps[group] > UINT64_MAX - epochs))
            return 0;
    }
    return 1;
}

static void fit_centroid(life_context_action_model *model, const double *features) {
    ++model->observations;
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
        model->centroid[i] += (features[i] - model->centroid[i]) / (double)model->observations;
}

static void fit_readout(life_context_action_model *model, const double *features, double utility,
                        uint32_t epochs) {
    for (uint32_t epoch = 0U; epoch < epochs; ++epoch) {
        const double error = utility - model_prediction(model, features);
        for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
            model->readout[i] =
                bounded(model->readout[i] + CHOICE_LEARNING_RATE * error * features[i]);
        ++model->steps;
    }
}

static void learn_feedback(cgai_life_context *owner, double utility) {
    life_context_choice_state *state = &owner->choice;
    const uint32_t epochs = owner->run.config.training_epochs;
    if (epochs == 0U)
        return;
    for (size_t group = 0U; group < life_context_group_count(owner); ++group) {
        if ((state->decision.participant_mask & (1U << group)) != 0U) {
            life_context_action_model *model = &state->models[group][state->decision.action];
            fit_centroid(model, state->features);
            fit_readout(model, state->features, utility, epochs);
            state->group_steps[group] += epochs;
        }
    }
    ++state->version;
}

static void close_pending(life_context_choice_state *state) {
    state->pending = 0U;
    memset(&state->decision, 0, sizeof(state->decision));
    memset(state->input, 0, sizeof(state->input));
    memset(state->features, 0, sizeof(state->features));
}

cgai_life_status cgai_life_context_choice_observe(cgai_life_context *owner,
                                                  const cgai_life_context_choice *decision,
                                                  const cgai_life_context_feedback *feedback) {
    if (owner == NULL || decision == NULL || owner->choice.pending != 1U ||
        !same_decision(&owner->choice.decision, decision) || !feedback_valid(feedback))
        return CGAI_LIFE_INVALID_ARGUMENT;
    if (feedback->deferred == 1U) {
        if (owner->choice.deferred == UINT64_MAX)
            return CGAI_LIFE_LIMIT_REACHED;
        ++owner->choice.deferred;
    } else {
        if (!update_counters_valid(owner))
            return CGAI_LIFE_LIMIT_REACHED;
        learn_feedback(owner, feedback->utility);
        ++owner->choice.observations;
    }
    close_pending(&owner->choice);
    return CGAI_LIFE_OK;
}

cgai_life_status cgai_life_context_choice_get_stats(const cgai_life_context *owner,
                                                    cgai_life_context_choice_stats *output) {
    cgai_life_context_choice_stats stats = {0};
    if (owner == NULL || output == NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    stats.group_count = life_context_group_count(owner);
    stats.version = owner->choice.version;
    stats.decisions = owner->choice.decisions;
    stats.observations = owner->choice.observations;
    stats.deferred = owner->choice.deferred;
    stats.pending = owner->choice.pending;
    for (size_t group = 0U; group < stats.group_count; ++group) {
        stats.group_steps[group] = owner->choice.group_steps[group];
        stats.group_hashes[group] = owned_hash(&owner->choice, group);
    }
    *output = stats;
    return CGAI_LIFE_OK;
}

static int model_values_valid(const life_context_action_model *model) {
    double squared = 0.0;
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i) {
        if (!isfinite(model->centroid[i]) || model->centroid[i] < 0.0 || model->centroid[i] > 1.0 ||
            !isfinite(model->readout[i]) || fabs(model->readout[i]) > 1.0)
            return 0;
        if (model->observations == 0U && (model->centroid[i] != 0.0 || model->readout[i] != 0.0))
            return 0;
        squared += model->centroid[i] * model->centroid[i];
    }
    return squared <= 1.0 + 1e-12;
}

static int model_counters_valid(const life_context_action_model *model, uint32_t epochs) {
    if (epochs == 0U)
        return model->observations == 0U && model->steps == 0U;
    return model->observations <= UINT64_MAX / epochs &&
           model->steps == model->observations * epochs;
}

static int group_models_valid(const cgai_life_context *owner, size_t group, uint64_t *coverage) {
    const life_context_choice_state *state = &owner->choice;
    uint64_t steps = 0U, observations = 0U;
    for (size_t action = 0U; action < CGAI_LIFE_CONTEXT_ACTIONS; ++action) {
        const life_context_action_model *model = &state->models[group][action];
        if (!model_values_valid(model) ||
            !model_counters_valid(model, owner->run.config.training_epochs) ||
            model->steps > UINT64_MAX - steps ||
            model->observations > state->version - observations)
            return 0;
        observations += model->observations;
        steps += model->steps;
    }
    *coverage +=
        observations < state->version - *coverage ? observations : state->version - *coverage;
    return steps == state->group_steps[group];
}

static int decision_zero(const cgai_life_context_choice *decision) {
    const cgai_life_context_choice zero = {0};
    return same_decision(decision, &zero);
}

static int empty_pending_valid(const life_context_choice_state *state) {
    if (!decision_zero(&state->decision))
        return 0;
    for (size_t i = 0U; i < sizeof(state->input); ++i)
        if (state->input[i] != '\0')
            return 0;
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
        if (real_bits(state->features[i]) != 0U)
            return 0;
    return 1;
}

static int pending_decision_valid(const cgai_life_context *owner) {
    const life_context_choice_state *state = &owner->choice;
    const cgai_life_context_choice *decision = &state->decision;
    return decision->generation == owner->run.world.tick && decision->conflict_id != 0U &&
           decision->generation == state->consumed_generation && state->consumed_mask != 0U &&
           decision->participant_mask != 0U &&
           (decision->participant_mask & ~life_context_group_mask(owner)) == 0U &&
           decision->action < CGAI_LIFE_CONTEXT_ACTIONS &&
           (decision->supported_groups & ~decision->participant_mask) == 0U &&
           decision->model_version == state->version && decision->world_hash != 0U &&
           isfinite(decision->predicted_utility) && fabs(decision->predicted_utility) <= 1.0 &&
           decision->token == decision_token(state, decision);
}

static int pending_features_valid(const life_context_choice_state *state) {
    double features[CGAI_LIFE_CONTEXT_FEATURES];
    if (!life_context_encode(state->input, features) ||
        state->decision.input_hash != life_context_bytes_hash(state->input, strlen(state->input)))
        return 0;
    for (size_t i = 0U; i < CGAI_LIFE_CONTEXT_FEATURES; ++i)
        if (real_bits(state->features[i]) != real_bits(features[i]))
            return 0;
    return 1;
}

static int state_counters_valid(const cgai_life_context *owner) {
    const life_context_choice_state *state = &owner->choice;
    return state->pending <= 1U && state->version <= state->observations &&
           state->observations <= state->decisions &&
           state->deferred <= state->decisions - state->observations &&
           state->pending == state->decisions - state->observations - state->deferred &&
           state->decisions <= owner->contact_events &&
           state->consumed_generation <= owner->run.world.tick &&
           (state->consumed_mask & ~CHOICE_EVENT_MASK) == 0U &&
           ((state->consumed_generation == 0U) == (state->consumed_mask == 0U)) &&
           ((state->decisions == 0U) == (state->consumed_generation == 0U));
}

static int model_zero(const life_context_action_model *model) {
    if (model->observations != 0U || model->steps != 0U)
        return 0;
    for (size_t feature = 0U; feature < CGAI_LIFE_CONTEXT_FEATURES; ++feature)
        if (real_bits(model->centroid[feature]) != 0U || real_bits(model->readout[feature]) != 0U)
            return 0;
    return 1;
}

static int unused_models_zero(const cgai_life_context *owner) {
    for (size_t group = life_context_group_count(owner); group < CGAI_LIFE_GROUPS; ++group) {
        if (owner->choice.group_steps[group] != 0U)
            return 0;
        for (size_t action = 0U; action < CGAI_LIFE_CONTEXT_ACTIONS; ++action)
            if (!model_zero(&owner->choice.models[group][action]))
                return 0;
    }
    return 1;
}

int life_context_choice_valid(const cgai_life_context *owner) {
    uint64_t coverage = 0U;
    if (owner == NULL || life_context_group_mask(owner) == 0U || !state_counters_valid(owner) ||
        !unused_models_zero(owner))
        return 0;
    for (size_t group = 0U; group < life_context_group_count(owner); ++group)
        if (!group_models_valid(owner, group, &coverage))
            return 0;
    if (coverage != owner->choice.version)
        return 0;
    return owner->choice.pending == 0U
               ? empty_pending_valid(&owner->choice)
               : pending_decision_valid(owner) && pending_features_valid(&owner->choice);
}
