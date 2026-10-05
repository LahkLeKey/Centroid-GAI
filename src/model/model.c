#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The context preserves exact original bytes. This fixed projection consumes
 * every byte and position, but cannot invert arbitrary byte strings. */
static uint64_t position_mix(uint64_t value) {
  value = (value ^ (value >> 30u)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27u)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31u);
}

c_status c_encode(const unsigned char *bytes, size_t length,
                  double out[C_FEATURES]) {
  double features[C_FEATURES] = {0};
  uint64_t sequence = UINT64_C(14695981039346656037);
  double squared = 0.0;
  if (out == NULL || (bytes == NULL && length != 0u))
    return C_INVALID;
  if (length > C_MAX_FILE_BYTES)
    return C_LIMIT;
  features[0] =
      1.0; /* Empty causal prefixes remain learnable, including EOS. */
  for (size_t i = 0u; i < length; ++i) {
    const uint64_t code =
        position_mix(((uint64_t)i << 8u) | (uint64_t)bytes[i]);
    const size_t bucket = 1u + (size_t)(code % (C_FEATURES - 1u));
    const double amplitude = 1.0 + (double)bytes[i] / 255.0;
    features[bucket] += (code >> 63u) != 0u ? amplitude : -amplitude;
    sequence = (sequence ^ (uint64_t)bytes[i]) * UINT64_C(1099511628211);
  }
  if (length != 0u) {
    const uint64_t final = position_mix(sequence ^ (uint64_t)length);
    features[1] += (double)(final >> 11u) / 4503599627370496.0 - 1.0;
  }
  for (size_t i = 0u; i < C_FEATURES; ++i)
    squared += features[i] * features[i];
  const double scale = 1.0 / sqrt(squared);
  for (size_t i = 0u; i < C_FEATURES; ++i)
    out[i] = features[i] * scale;
  return C_OK;
}

typedef struct {
  unsigned actions;
  unsigned active;
  double route[C_MAX_GROUPS], log_route[C_MAX_GROUPS];
  double probabilities[C_MAX_GROUPS][C_TEXT_ACTIONS];
  double log_probabilities[C_MAX_GROUPS][C_TEXT_ACTIONS];
  double mixed[C_TEXT_ACTIONS];
} model_forward;

static unsigned action_count(c_head head) {
  return head == C_TEXT ? C_TEXT_ACTIONS : head == C_CODE ? C_CODE_ACTIONS : 0u;
}

static int scalar_valid(const c_scalar *scalar) {
  return isfinite(scalar->value) && isfinite(scalar->first) &&
         isfinite(scalar->second) && scalar->second >= 0.0;
}

static int expert_valid(const c_expert *expert) {
  if (expert->uid == 0u)
    return 0;
  for (size_t j = 0u; j < C_FEATURES; ++j)
    if (!scalar_valid(&expert->centroid[j]))
      return 0;
  for (size_t i = 0u; i < C_CODE_ACTIONS; ++i)
    for (size_t j = 0u; j < C_FEATURES; ++j)
      if (!scalar_valid(&expert->code[i][j]))
        return 0;
  for (size_t i = 0u; i < C_TEXT_ACTIONS; ++i)
    for (size_t j = 0u; j < C_FEATURES; ++j)
      if (!scalar_valid(&expert->text[i][j]))
        return 0;
  return 1;
}

static int model_valid(const c_model *model) {
  if (model == NULL || model->groups == 0u || model->groups > C_MAX_GROUPS)
    return 0;
  if (model->shared_enabled > 1u ||
      (!model->shared_enabled && model->shared_clock))
    return 0;
  for (size_t j = 0; j < C_FEATURES; ++j)
    if (!scalar_valid(&model->shared_scale[j]) ||
        (!model->shared_enabled &&
         (model->shared_scale[j].value != 1.0 || model->shared_scale[j].first ||
          model->shared_scale[j].second)))
      return 0;
  for (unsigned g = 0u; g < model->groups; ++g) {
    if (!expert_valid(&model->expert[g]))
      return 0;
    for (unsigned earlier = 0u; earlier < g; ++earlier)
      if (model->expert[g].uid == model->expert[earlier].uid)
        return 0;
  }
  return 1;
}

static double initial_value(uint64_t *rng, double scale) {
  return ((double)(c_random(rng) >> 11u) / 4503599627370496.0 - 1.0) * scale;
}

static void initialize_expert(c_expert *expert, unsigned group, uint64_t *rng) {
  expert->uid = (uint64_t)group + 1u;
  for (size_t j = 0u; j < C_FEATURES; ++j)
    expert->centroid[j].value = initial_value(rng, 0.3);
  for (size_t i = 0u; i < C_CODE_ACTIONS; ++i)
    for (size_t j = 0u; j < C_FEATURES; ++j)
      expert->code[i][j].value = initial_value(rng, 0.1);
  for (size_t i = 0u; i < C_TEXT_ACTIONS; ++i)
    for (size_t j = 0u; j < C_FEATURES; ++j)
      expert->text[i][j].value = initial_value(rng, 0.1);
}

c_status c_model_create(unsigned groups, uint64_t seed, c_model **out) {
  if (out == NULL || *out != NULL || groups == 0u || groups > C_MAX_GROUPS)
    return C_INVALID;
  c_model *model = calloc(1u, sizeof(*model));
  if (model == NULL)
    return C_NOMEM;
  uint64_t rng = seed ^ UINT64_C(0x9e3779b97f4a7c15);
  model->groups = groups;
  model->seed = seed;
  for (size_t j = 0; j < C_FEATURES; ++j)
    model->shared_scale[j].value = 1.0;
  for (unsigned g = 0u; g < groups; ++g)
    initialize_expert(&model->expert[g], g, &rng);
  *out = model;
  return C_OK;
}

void c_model_destroy(c_model *model) { free(model); }

unsigned c_model_group_count(const c_model *model) {
  return model != NULL && model->groups <= C_MAX_GROUPS ? model->groups : 0u;
}

static c_status prepare_routes(const c_model *model,
                               const double input[C_FEATURES],
                               unsigned eligible,
                               const double mass[C_MAX_GROUPS],
                               model_forward *forward) {
  double scores[C_MAX_GROUPS] = {0};
  double maximum = -INFINITY, sum = 0.0;
  if (input == NULL || mass == NULL || (eligible >> model->groups) != 0u)
    return C_INVALID;
  for (size_t j = 0u; j < C_FEATURES; ++j)
    if (!isfinite(input[j]))
      return C_INVALID;
  for (unsigned g = 0u; g < model->groups; ++g) {
    if (!isfinite(mass[g]) || mass[g] < 0.0)
      return C_INVALID;
    if ((eligible & (1u << g)) == 0u || mass[g] == 0.0)
      continue;
    double distance = 0.0;
    for (size_t j = 0u; j < C_FEATURES; ++j) {
      const double delta = input[j] - model->expert[g].centroid[j].value;
      distance += delta * delta;
    }
    scores[g] = log(mass[g]) - distance;
    if (!isfinite(scores[g]))
      return C_INVALID;
    forward->active |= 1u << g;
    if (scores[g] > maximum)
      maximum = scores[g];
  }
  if (forward->active == 0u)
    return C_DEFERRED;
  for (unsigned g = 0u; g < model->groups; ++g)
    if ((forward->active & (1u << g)) != 0u)
      sum += exp(scores[g] - maximum);
  const double log_sum = log(sum);
  for (unsigned g = 0u; g < model->groups; ++g)
    if ((forward->active & (1u << g)) != 0u) {
      forward->log_route[g] = scores[g] - maximum - log_sum;
      forward->route[g] = exp(forward->log_route[g]);
    }
  return C_OK;
}

static const c_scalar *readout_row(const c_expert *expert, c_head head,
                                   unsigned action) {
  return head == C_CODE ? expert->code[action] : expert->text[action];
}

static c_status expert_forward(const c_expert *expert, c_head head,
                               const double input[C_FEATURES],
                               model_forward *forward, unsigned group) {
  double logits[C_TEXT_ACTIONS], maximum = -INFINITY, sum = 0.0;
  for (unsigned a = 0u; a < forward->actions; ++a) {
    const c_scalar *row = readout_row(expert, head, a);
    double value = 0.0;
    for (size_t j = 0u; j < C_FEATURES; ++j)
      value += row[j].value * input[j];
    if (!isfinite(value))
      return C_INVALID;
    logits[a] = value;
    if (value > maximum)
      maximum = value;
  }
  for (unsigned a = 0u; a < forward->actions; ++a) {
    if (!isfinite(logits[a] - maximum))
      return C_INVALID;
    sum += exp(logits[a] - maximum);
  }
  const double log_sum = log(sum);
  for (unsigned a = 0u; a < forward->actions; ++a) {
    const double log_probability = logits[a] - maximum - log_sum;
    const double probability = exp(log_probability);
    forward->log_probabilities[group][a] = log_probability;
    forward->probabilities[group][a] = probability;
    forward->mixed[a] += forward->route[group] * probability;
  }
  return C_OK;
}

static c_status model_compute(const c_model *model, c_head head,
                              const double input[C_FEATURES], unsigned eligible,
                              const double mass[C_MAX_GROUPS],
                              model_forward *forward) {
  const unsigned actions = action_count(head);
  if (!model_valid(model) || actions == 0u)
    return C_INVALID;
  memset(forward, 0, sizeof(*forward));
  forward->actions = actions;
  double encoded[C_FEATURES];
  if (!input)
    return C_INVALID;
  for (size_t j = 0; j < C_FEATURES; ++j)
    encoded[j] =
        input[j] * (model->shared_enabled ? model->shared_scale[j].value : 1.0);
  c_status status = prepare_routes(model, encoded, eligible, mass, forward);
  if (status != C_OK)
    return status;
  for (unsigned g = 0u; g < model->groups; ++g)
    if ((forward->active & (1u << g)) != 0u) {
      status = expert_forward(&model->expert[g], head, encoded, forward, g);
      if (status != C_OK)
        return status;
    }
  return C_OK;
}

c_status c_model_predict(const c_model *model, c_head head,
                         const double input[C_FEATURES], unsigned eligible,
                         const double mass[C_MAX_GROUPS], double *probabilities,
                         size_t capacity) {
  model_forward forward;
  const unsigned actions = action_count(head);
  if (probabilities == NULL || actions == 0u || capacity < actions)
    return C_INVALID;
  const c_status status =
      model_compute(model, head, input, eligible, mass, &forward);
  if (status == C_OK)
    memcpy(probabilities, forward.mixed,
           (size_t)actions * sizeof(*probabilities));
  return status;
}

static double target_log_probability(const model_forward *forward,
                                     unsigned target) {
  double maximum = -INFINITY, sum = 0.0;
  for (unsigned g = 0u; g < C_MAX_GROUPS; ++g)
    if ((forward->active & (1u << g)) != 0u) {
      const double score =
          forward->log_route[g] + forward->log_probabilities[g][target];
      if (score > maximum)
        maximum = score;
    }
  for (unsigned g = 0u; g < C_MAX_GROUPS; ++g)
    if ((forward->active & (1u << g)) != 0u)
      sum += exp(forward->log_route[g] + forward->log_probabilities[g][target] -
                 maximum);
  return maximum + log(sum);
}

static void expert_gradient(const c_expert *expert,
                            const double input[C_FEATURES],
                            const model_forward *forward, unsigned group,
                            unsigned target, double log_target,
                            c_gradient *gradient) {
  const double responsibility =
      exp(forward->log_route[group] +
          forward->log_probabilities[group][target] - log_target);
  for (size_t j = 0u; j < C_FEATURES; ++j)
    gradient->centroid[j] = 2.0 * (forward->route[group] - responsibility) *
                            (input[j] - expert->centroid[j].value);
  for (unsigned a = 0u; a < forward->actions; ++a) {
    const double derivative =
        responsibility *
        (forward->probabilities[group][a] - (a == target ? 1.0 : 0.0));
    for (size_t j = 0u; j < C_FEATURES; ++j)
      gradient->readout[a][j] = derivative * input[j];
  }
}

c_status c_model_gradient(const c_model *model, c_head head,
                          const double input[C_FEATURES], unsigned eligible,
                          const double mass[C_MAX_GROUPS], unsigned target,
                          c_gradient gradients[C_MAX_GROUPS], double *loss) {
  model_forward forward;
  if (gradients == NULL || loss == NULL || target >= action_count(head))
    return C_INVALID;
  const c_status status =
      model_compute(model, head, input, eligible, mass, &forward);
  if (status != C_OK)
    return status;
  const double log_target = target_log_probability(&forward, target);
  if (!isfinite(log_target))
    return C_INVALID;
  c_gradient *prepared = calloc(C_MAX_GROUPS, sizeof(*prepared));
  if (prepared == NULL)
    return C_NOMEM;
  for (unsigned g = 0u; g < model->groups; ++g)
    if ((forward.active & (1u << g)) != 0u) {
      double encoded[C_FEATURES];
      for (size_t j = 0; j < C_FEATURES; ++j)
        encoded[j] =
            input[j] *
            (model->shared_enabled ? model->shared_scale[j].value : 1.0);
      expert_gradient(&model->expert[g], encoded, &forward, g, target,
                      log_target, &prepared[g]);
    }
  memcpy(gradients, prepared, C_MAX_GROUPS * sizeof(*gradients));
  *loss = -log_target;
  free(prepared);
  return C_OK;
}

c_status c_model_shared_gradient(const c_model *model, c_head head,
                                 const double input[C_FEATURES],
                                 unsigned eligible,
                                 const double mass[C_MAX_GROUPS],
                                 unsigned target, double out[C_FEATURES]) {
  model_forward forward;
  double candidate[C_FEATURES] = {0};
  if (!model || !model->shared_enabled || !out || target >= action_count(head))
    return C_INVALID;
  c_status s = model_compute(model, head, input, eligible, mass, &forward);
  if (s != C_OK)
    return s;
  const double log_target = target_log_probability(&forward, target);
  for (unsigned g = 0; g < model->groups; ++g)
    if (forward.active & (1u << g)) {
      double responsibility =
          exp(forward.log_route[g] + forward.log_probabilities[g][target] -
              log_target);
      for (size_t j = 0; j < C_FEATURES; ++j) {
        double encoded = input[j] * model->shared_scale[j].value;
        double derivative = 2.0 * (forward.route[g] - responsibility) *
                            (model->expert[g].centroid[j].value - encoded);
        for (unsigned a = 0; a < forward.actions; ++a)
          derivative +=
              responsibility *
              (forward.probabilities[g][a] - (a == target ? 1.0 : 0.0)) *
              readout_row(&model->expert[g], head, a)[j].value;
        candidate[j] += derivative * input[j];
      }
    }
  for (size_t j = 0; j < C_FEATURES; ++j)
    if (!isfinite(candidate[j]))
      return C_INVALID;
  memcpy(out, candidate, sizeof(candidate));
  return C_OK;
}

int c_scalar_adam(c_scalar *scalar, double gradient, double rate,
                  double first_correction, double second_correction) {
  if (!isfinite(gradient))
    return 0;
  const double first = 0.9 * scalar->first + 0.1 * gradient;
  const double second = 0.999 * scalar->second + 0.001 * gradient * gradient;
  const double denominator = sqrt(second / second_correction) + 1e-8;
  const double value =
      scalar->value - rate * (first / first_correction) / denominator;
  if (!isfinite(first) || !isfinite(second) || !isfinite(denominator) ||
      !isfinite(value))
    return 0;
  scalar->first = first;
  scalar->second = second;
  scalar->value = value;
  return 1;
}

static int expert_apply(c_expert *expert, c_head head,
                        const c_gradient *gradient, double rate) {
  if (expert->clock == UINT64_MAX)
    return 0;
  const uint64_t clock = expert->clock + 1u;
  const double first_correction = 1.0 - pow(0.9, (double)clock);
  const double second_correction = 1.0 - pow(0.999, (double)clock);
  for (size_t j = 0u; j < C_FEATURES; ++j)
    if (!c_scalar_adam(&expert->centroid[j], gradient->centroid[j], rate,
                       first_correction, second_correction))
      return 0;
  for (unsigned a = 0u; a < action_count(head); ++a) {
    c_scalar *row = head == C_CODE ? expert->code[a] : expert->text[a];
    for (size_t j = 0u; j < C_FEATURES; ++j)
      if (!c_scalar_adam(&row[j], gradient->readout[a][j], rate,
                         first_correction, second_correction))
        return 0;
  }
  expert->clock = clock;
  return 1;
}

c_status c_model_apply(c_model *model, c_head head, unsigned participants,
                       const c_gradient gradients[C_MAX_GROUPS], double rate) {
  if (!model_valid(model) || action_count(head) == 0u || gradients == NULL ||
      !isfinite(rate) || rate <= 0.0 || (participants >> model->groups) != 0u)
    return C_INVALID;
  if (participants == 0u)
    return C_DEFERRED;
  c_model *candidate = malloc(sizeof(*candidate));
  if (candidate == NULL)
    return C_NOMEM;
  memcpy(candidate, model, sizeof(*candidate));
  for (unsigned g = 0u; g < model->groups; ++g)
    if ((participants & (1u << g)) != 0u &&
        !expert_apply(&candidate->expert[g], head, &gradients[g], rate)) {
      free(candidate);
      return C_INVALID;
    }
  if (!model_valid(candidate)) {
    free(candidate);
    return C_INVALID;
  }
  memcpy(model, candidate, sizeof(*model));
  free(candidate);
  return C_OK;
}

c_status c_model_apply_shared(c_model *model, c_head head,
                              unsigned participants,
                              const c_gradient gradients[C_MAX_GROUPS],
                              const double shared[C_FEATURES], double rate) {
  if (!model || !model->shared_enabled || !shared ||
      participants != (1u << model->groups) - 1u ||
      model->shared_clock == UINT64_MAX)
    return C_INVALID;
  c_model *candidate = malloc(sizeof(*candidate));
  if (!candidate)
    return C_NOMEM;
  memcpy(candidate, model, sizeof(*candidate));
  c_status s = c_model_apply(candidate, head, participants, gradients, rate);
  uint64_t clock = model->shared_clock + 1;
  for (size_t j = 0; s == C_OK && j < C_FEATURES; ++j)
    if (!c_scalar_adam(&candidate->shared_scale[j], shared[j], rate * 0.1,
                       1.0 - pow(0.9, (double)clock),
                       1.0 - pow(0.999, (double)clock)))
      s = C_INVALID;
  candidate->shared_clock = clock;
  if (s == C_OK && !model_valid(candidate))
    s = C_INVALID;
  if (s == C_OK)
    memcpy(model, candidate, sizeof(*model));
  free(candidate);
  return s;
}

uint64_t c_model_group_clock(const c_model *model, unsigned group) {
  return model != NULL && group < model->groups && model->groups <= C_MAX_GROUPS
             ? model->expert[group].clock
             : 0u;
}

static void put_scalar(c_writer *writer, const c_scalar *scalar) {
  c_put_double(writer, scalar->value);
  c_put_double(writer, scalar->first);
  c_put_double(writer, scalar->second);
}

static void put_expert(c_writer *writer, const c_expert *expert) {
  c_put_u64(writer, expert->uid);
  c_put_u64(writer, expert->clock);
  for (size_t j = 0u; j < C_FEATURES; ++j)
    put_scalar(writer, &expert->centroid[j]);
  for (size_t a = 0u; a < C_CODE_ACTIONS; ++a)
    for (size_t j = 0u; j < C_FEATURES; ++j)
      put_scalar(writer, &expert->code[a][j]);
  for (size_t a = 0u; a < C_TEXT_ACTIONS; ++a)
    for (size_t j = 0u; j < C_FEATURES; ++j)
      put_scalar(writer, &expert->text[a][j]);
}

c_status c_model_fingerprint(const c_model *model, unsigned group,
                             char digest[C_DIGEST_HEX]) {
  c_writer writer = {0};
  if (!model_valid(model) || digest == NULL || group >= model->groups)
    return C_INVALID;
  c_put_bytes(&writer, C_RECIPE, sizeof(C_RECIPE));
  c_put_u32(&writer, model->groups);
  c_put_u64(&writer, model->seed);
  c_put_u32(&writer, group);
  c_put_u32(&writer, model->shared_enabled);
  c_put_u64(&writer, model->shared_clock);
  for (size_t j = 0; j < C_FEATURES; ++j)
    put_scalar(&writer, &model->shared_scale[j]);
  put_expert(&writer, &model->expert[group]);
  if (writer.status == C_OK)
    c_hash(writer.data, writer.length, digest);
  free(writer.data);
  return writer.status;
}
