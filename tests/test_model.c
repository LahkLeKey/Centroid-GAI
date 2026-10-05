#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "model check failed at %s:%d: %s\n", __FILE__, __LINE__, \
              #condition);                                                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static c_model *create_model(unsigned groups, uint64_t seed) {
  c_model *model = NULL;
  CHECK(c_model_create(groups, seed, &model) == C_OK && model != NULL);
  return model;
}

static c_model *copy_model(const c_model *model) {
  c_model *copy = malloc(sizeof(*copy));
  CHECK(copy != NULL);
  memcpy(copy, model, sizeof(*copy));
  return copy;
}

static c_gradient *create_gradients(void) {
  c_gradient *gradients = calloc(C_MAX_GROUPS, sizeof(*gradients));
  CHECK(gradients != NULL);
  return gradients;
}

static void test_encoder(void) {
  static const unsigned char original[] = {'a', 'b', 'c', 0u, 255u};
  static const unsigned char reordered[] = {'b', 'a', 'c', 0u, 255u};
  double first[C_FEATURES], second[C_FEATURES], empty[C_FEATURES];
  CHECK(c_encode(original, sizeof(original), first) == C_OK);
  CHECK(c_encode(original, sizeof(original), second) == C_OK);
  CHECK(memcmp(first, second, sizeof(first)) == 0);
  CHECK(c_encode(reordered, sizeof(reordered), second) == C_OK);
  CHECK(memcmp(first, second, sizeof(first)) != 0);
  double norm = 0.0;
  for (size_t j = 0u; j < C_FEATURES; ++j)
    norm += first[j] * first[j];
  CHECK(fabs(norm - 1.0) < 1e-14);
  CHECK(c_encode(NULL, 0u, empty) == C_OK && empty[0] == 1.0);
  memcpy(second, first, sizeof(second));
  CHECK(c_encode(NULL, 1u, second) == C_INVALID);
  CHECK(memcmp(first, second, sizeof(first)) == 0);
  CHECK(c_encode(original, (size_t)C_MAX_FILE_BYTES + 1u, second) == C_LIMIT);
  CHECK(memcmp(first, second, sizeof(first)) == 0);
}

static void dense_input(double input[C_FEATURES]) {
  double norm = 0.0;
  for (size_t j = 0u; j < C_FEATURES; ++j) {
    input[j] = (double)(j + 1u);
    norm += input[j] * input[j];
  }
  norm = sqrt(norm);
  for (size_t j = 0u; j < C_FEATURES; ++j)
    input[j] /= norm;
}

static double prediction_loss(const c_model *model, c_head head,
                              const double input[C_FEATURES], unsigned eligible,
                              const double mass[C_MAX_GROUPS],
                              unsigned target) {
  double probabilities[C_TEXT_ACTIONS];
  CHECK(c_model_predict(model, head, input, eligible, mass, probabilities,
                        C_TEXT_ACTIONS) == C_OK);
  CHECK(probabilities[target] > 0.0 && isfinite(probabilities[target]));
  return -log(probabilities[target]);
}

static void finite_difference(c_model *model, c_head head,
                              const double input[C_FEATURES],
                              const double mass[C_MAX_GROUPS], unsigned target,
                              double *parameter, double analytic) {
  const double original = *parameter, epsilon = 1e-6;
  *parameter = original + epsilon;
  const double positive = prediction_loss(model, head, input, 3u, mass, target);
  *parameter = original - epsilon;
  const double negative = prediction_loss(model, head, input, 3u, mass, target);
  *parameter = original;
  const double measured = (positive - negative) / (2.0 * epsilon);
  CHECK(fabs(measured - analytic) < 2e-7 * (1.0 + fabs(analytic)));
}

static void test_gradient(c_head head) {
  c_model *model = create_model(2u, 2345u);
  c_gradient *gradients = create_gradients();
  double input[C_FEATURES], loss = 0.0;
  const double mass[C_MAX_GROUPS] = {2.0, 0.5, 0.0, 0.0};
  const unsigned target = head == C_CODE ? 2u : C_EOS;
  dense_input(input);
  model->expert[0].centroid[0].value = -0.8;
  model->expert[1].centroid[0].value = 0.6;
  for (unsigned g = 0u; g < 2u; ++g) {
    c_scalar *row = head == C_CODE ? model->expert[g].code[target]
                                   : model->expert[g].text[target];
    row[0].value = g == 0u ? 1.5 : -1.0;
  }
  CHECK(c_model_gradient(model, head, input, 3u, mass, target, gradients,
                         &loss) == C_OK);
  CHECK(fabs(loss - prediction_loss(model, head, input, 3u, mass, target)) <
        1e-13);
  for (unsigned g = 0u; g < 2u; ++g) {
    for (size_t j = 0u; j < C_FEATURES; j += 7u) {
      finite_difference(model, head, input, mass, target,
                        &model->expert[g].centroid[j].value,
                        gradients[g].centroid[j]);
      c_scalar *target_row = head == C_CODE ? model->expert[g].code[target]
                                            : model->expert[g].text[target];
      c_scalar *other_row =
          head == C_CODE ? model->expert[g].code[0] : model->expert[g].text[0];
      finite_difference(model, head, input, mass, target, &target_row[j].value,
                        gradients[g].readout[target][j]);
      finite_difference(model, head, input, mass, target, &other_row[j].value,
                        gradients[g].readout[0][j]);
    }
  }
  CHECK(gradients[2].centroid[0] == 0.0 &&
        gradients[3].readout[target][0] == 0.0);
  free(gradients);
  c_model_destroy(model);
}

static void test_masked_frozen_prediction(void) {
  c_model *model = create_model(4u, 99u);
  c_model *before = copy_model(model);
  const double sparse_mass[C_MAX_GROUPS] = {1.0, 0.0, 0.0, 0.0};
  const double full_mass[C_MAX_GROUPS] = {1.0, 1.0, 1.0, 1.0};
  const double zero_mass[C_MAX_GROUPS] = {0.0, 0.0, 0.0, 0.0};
  double input[C_FEATURES], first[C_TEXT_ACTIONS], second[C_TEXT_ACTIONS];
  dense_input(input);
  CHECK(c_model_predict(model, C_TEXT, input, 15u, sparse_mass, first,
                        C_TEXT_ACTIONS) == C_OK);
  CHECK(c_model_predict(model, C_TEXT, input, 1u, full_mass, second,
                        C_TEXT_ACTIONS) == C_OK);
  CHECK(memcmp(first, second, sizeof(first)) == 0);
  CHECK(memcmp(model, before, sizeof(*model)) == 0);
  CHECK(c_model_predict(model, C_TEXT, input, 15u, zero_mass, second,
                        C_TEXT_ACTIONS) == C_DEFERRED);
  CHECK(memcmp(first, second, sizeof(first)) == 0);
  CHECK(c_model_predict(model, C_TEXT, input, 0u, full_mass, second,
                        C_TEXT_ACTIONS) == C_DEFERRED);
  CHECK(memcmp(first, second, sizeof(first)) == 0);
  CHECK(c_model_predict(model, C_TEXT, input, 16u, full_mass, second,
                        C_TEXT_ACTIONS) == C_INVALID);
  CHECK(memcmp(first, second, sizeof(first)) == 0);
  input[0] = NAN;
  CHECK(c_model_predict(model, C_TEXT, input, 1u, full_mass, second,
                        C_TEXT_ACTIONS) == C_INVALID);
  CHECK(memcmp(first, second, sizeof(first)) == 0);
  CHECK(memcmp(model, before, sizeof(*model)) == 0);
  free(before);
  c_model_destroy(model);
}

static void test_update_isolation(void) {
  c_model *model = create_model(4u, 11u);
  c_gradient *gradients = create_gradients();
  double input[C_FEATURES], loss;
  const double mass[C_MAX_GROUPS] = {1.0, 1.0, 1.0, 1.0};
  dense_input(input);
  model->expert[0].text[17][5].first = 0.5;
  model->expert[0].text[17][5].second = 0.25;
  c_model *before = copy_model(model);
  CHECK(c_model_gradient(model, C_CODE, input, 3u, mass, 1u, gradients,
                         &loss) == C_OK);
  CHECK(c_model_apply(model, C_CODE, 1u, gradients, 0.01) == C_OK);
  CHECK(c_model_group_clock(model, 0u) == 1u);
  CHECK(memcmp(model->expert[0].text, before->expert[0].text,
               sizeof(model->expert[0].text)) == 0);
  for (unsigned g = 1u; g < C_MAX_GROUPS; ++g)
    CHECK(memcmp(&model->expert[g], &before->expert[g], sizeof(c_expert)) == 0);
  memcpy(before, model, sizeof(*before));
  CHECK(c_model_gradient(model, C_TEXT, input, 2u, mass, C_EOS, gradients,
                         &loss) == C_OK);
  CHECK(c_model_apply(model, C_TEXT, 2u, gradients, 0.01) == C_OK);
  CHECK(c_model_group_clock(model, 1u) == 1u);
  CHECK(memcmp(model->expert[1].code, before->expert[1].code,
               sizeof(model->expert[1].code)) == 0);
  CHECK(memcmp(&model->expert[0], &before->expert[0], sizeof(c_expert)) == 0);
  memcpy(before, model, sizeof(*before));
  gradients[1].readout[C_EOS][C_FEATURES - 1u] = NAN;
  CHECK(c_model_apply(model, C_TEXT, 3u, gradients, 0.01) == C_INVALID);
  CHECK(memcmp(model, before, sizeof(*model)) == 0);
  gradients[1].readout[C_EOS][C_FEATURES - 1u] = 0.0;
  gradients[0].centroid[0] = 1e308;
  CHECK(c_model_apply(model, C_CODE, 1u, gradients, 0.01) == C_INVALID);
  CHECK(memcmp(model, before, sizeof(*model)) == 0);
  CHECK(c_model_apply(model, C_CODE, 0u, gradients, 0.01) == C_DEFERRED);
  CHECK(memcmp(model, before, sizeof(*model)) == 0);
  free(before);
  free(gradients);
  c_model_destroy(model);
}

static void test_adam(void) {
  c_model *model = create_model(2u, 44u);
  c_gradient *gradients = create_gradients();
  const double original = model->expert[0].code[0][0].value;
  gradients[0].readout[0][0] = 0.2;
  CHECK(c_model_apply(model, C_CODE, 1u, gradients, 0.01) == C_OK);
  const c_scalar *scalar = &model->expert[0].code[0][0];
  CHECK(fabs(scalar->first - 0.02) < 1e-15);
  CHECK(fabs(scalar->second - 0.00004) < 1e-15);
  CHECK(fabs(scalar->value - (original - 0.01 * 0.2 / (0.2 + 1e-8))) < 1e-14);
  CHECK(c_model_group_clock(model, 0u) == 1u &&
        c_model_group_clock(model, 1u) == 0u);
  free(gradients);
  c_model_destroy(model);
}

static void test_fingerprint(void) {
  c_model *first = create_model(2u, 567u), *second = create_model(2u, 567u);
  char original[C_DIGEST_HEX], copy[C_DIGEST_HEX];
  CHECK(c_model_fingerprint(first, 0u, original) == C_OK);
  CHECK(c_model_fingerprint(second, 0u, copy) == C_OK &&
        strcmp(original, copy) == 0);
  second->expert[0].text[C_EOS][31].first = 0.25;
  CHECK(c_model_fingerprint(second, 0u, copy) == C_OK &&
        strcmp(original, copy) != 0);
  second->expert[0].text[C_EOS][31].first = 0.0;
  second->expert[0].clock = 1u;
  CHECK(c_model_fingerprint(second, 0u, copy) == C_OK &&
        strcmp(original, copy) != 0);
  c_model_destroy(first);
  c_model_destroy(second);
}

int main(void) {
  test_encoder();
  test_gradient(C_CODE);
  test_gradient(C_TEXT);
  test_masked_frozen_prediction();
  test_update_isolation();
  test_adam();
  test_fingerprint();
  puts("ordered byte encoder, mixture derivatives, frozen inference and owned "
       "Adam passed");
  return 0;
}
