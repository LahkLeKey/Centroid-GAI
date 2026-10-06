#include "centroid_code_helper.h"
#include <math.h>
#include <string.h>

static cr_status array_check(const int32_t *values, size_t count) {
  if (!values && count)
    return CR_INVALID;
  if (count > CR_CODE_MAX_VALUES)
    return CR_LIMIT;
  if (count && values[0] > values[count - 1u])
    return CR_INVALID;
  return CR_OK;
}
cr_status CR_CALL cr_code_observe(const int32_t *values, size_t count,
                                  int32_t key, cr_code_observation *out) {
  cr_code_observation result;
  cr_status status;
  if (!out || out->struct_size != sizeof(*out) ||
      out->api_version != CR_API_VERSION)
    return CR_INVALID;
  status = array_check(values, count);
  if (status != CR_OK)
    return status;
  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.api_version = CR_API_VERSION;
  result.count = count;
  if (!count)
    result.band = 0u;
  else if (key <= values[0])
    result.band = 1u;
  else if (key > values[count - 1u]) {
    result.band = 2u;
    result.estimated_rank = count;
  } else {
    const uint64_t numerator = (uint64_t)((int64_t)key - values[0]);
    const uint64_t denominator =
        (uint64_t)((int64_t)values[count - 1u] - values[0]) + 1u;
    result.estimated_rank = ((uint64_t)count * numerator) / denominator;
    if (result.estimated_rank <= 3u)
      result.band = 3u;
    else if (result.estimated_rank <= 24u)
      result.band = 4u;
    else if ((uint64_t)count - result.estimated_rank <= 8u)
      result.band = 6u;
    else if (result.estimated_rank >= 64u && result.estimated_rank <= 256u)
      result.band = 7u;
    else
      result.band = 5u;
  }
  result.bytes[0] = 1u;
  result.bytes[1] = (unsigned char)result.band;
  status = cr_encode(result.bytes, CR_CODE_INPUT_BYTES, result.features);
  if (status == CR_OK)
    *out = result;
  return status;
}
cr_status CR_CALL cr_code_recommend(const cr_model *model,
                                    const int32_t *values, size_t count,
                                    int32_t key, uint32_t legal_mask,
                                    cr_code_recommendation *out) {
  cr_model_info info;
  cr_code_observation observation;
  cr_code_recommendation result;
  double mass[CR_MAX_GROUPS] = {1.0, 1.0, 1.0, 1.0}, sum = 0.0;
  cr_status status;
  if (!out || out->struct_size != sizeof(*out) ||
      out->api_version != CR_API_VERSION || !legal_mask || (legal_mask & ~15u))
    return CR_INVALID;
  memset(&info, 0, sizeof(info));
  info.struct_size = sizeof(info);
  info.api_version = CR_API_VERSION;
  status = cr_model_info_get(model, &info);
  if (status != CR_OK)
    return status;
  if (strcmp(info.metadata.task_profile, CR_CODE_HELPER_PROFILE) ||
      strcmp(info.metadata.observation_schema, CR_CODE_OBSERVATION_SCHEMA) ||
      strcmp(info.metadata.action_catalog, CR_CODE_ACTION_CATALOG))
    return CR_UNSUPPORTED;
  if (info.metadata.qualification != CR_QUALIFIED)
    return CR_DEFERRED;
  memset(&observation, 0, sizeof(observation));
  observation.struct_size = sizeof(observation);
  observation.api_version = CR_API_VERSION;
  status = cr_code_observe(values, count, key, &observation);
  if (status != CR_OK)
    return status;
  memset(&result, 0, sizeof(result));
  result.struct_size = sizeof(result);
  result.api_version = CR_API_VERSION;
  result.legal_mask = legal_mask;
  status = cr_model_predict(model, CR_CODE, observation.features,
                            (1u << info.groups) - 1u, mass, result.scores,
                            CR_CODE_ACTIONS);
  if (status != CR_OK)
    return status;
  for (uint32_t action = 0; action < CR_CODE_ACTIONS; ++action) {
    if (!(legal_mask & (1u << action)))
      result.scores[action] = 0.0;
    else {
      if (!isfinite(result.scores[action]) || result.scores[action] < 0.0)
        return CR_CORRUPT;
      sum += result.scores[action];
    }
  }
  if (!isfinite(sum) || sum <= 0.0)
    return CR_DEFERRED;
  result.action = CR_CODE_ACTIONS;
  for (uint32_t action = 0; action < CR_CODE_ACTIONS; ++action) {
    result.scores[action] /= sum;
    if ((legal_mask & (1u << action)) &&
        (result.action == CR_CODE_ACTIONS ||
         result.scores[action] > result.scores[result.action]))
      result.action = action;
  }
  memcpy(result.model_digest, info.metadata.model_digest,
         sizeof(result.model_digest));
  *out = result;
  return CR_OK;
}
cr_status CR_CALL cr_code_execute(uint32_t action, const int32_t *values,
                                  size_t count, int32_t key, size_t *index,
                                  uint64_t *comparisons) {
  size_t first = 0u, last = count;
  uint64_t operations = 0u;
  cr_status status;
  if (!index || !comparisons || action >= CR_CODE_ACTIONS)
    return CR_INVALID;
  status = array_check(values, count);
  if (status != CR_OK)
    return status;
  if (action == CR_CODE_FORWARD) {
    while (first < count) {
      ++operations;
      if (values[first] >= key)
        break;
      ++first;
    }
  } else if (action == CR_CODE_REVERSE) {
    first = count;
    while (first) {
      ++operations;
      if (values[first - 1u] < key)
        break;
      --first;
    }
  } else {
    if (action == CR_CODE_GALLOP && count) {
      ++operations;
      if (values[0] >= key)
        last = 0u;
      else {
        size_t high = 1u;
        first = 1u;
        while (high < count) {
          ++operations;
          if (values[high] >= key)
            break;
          first = high + 1u;
          high *= 2u; /* count <= 4096, so doubling is bounded. */
        }
        last = high < count ? high : count;
      }
    }
    while (first < last) {
      const size_t middle = first + (last - first) / 2u;
      ++operations;
      if (values[middle] < key)
        first = middle + 1u;
      else
        last = middle;
    }
  }
  *index = first;
  *comparisons = operations;
  return CR_OK;
}
