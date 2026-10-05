#include "centroid_algorithms.h"
c_status c_size_affine(size_t count, size_t stride, size_t extra, size_t *out) {
  size_t product;
  if (!out) {
    return C_INVALID;
  }
  if (stride && count > SIZE_MAX / stride) {
    return C_LIMIT;
  }
  product = count * stride;
  if (extra > SIZE_MAX - product) {
    return C_LIMIT;
  }
  *out = product + extra;
  return C_OK;
}

size_t c_lower_bound(const int *values, size_t count, int key,
                     size_t *comparisons) {
  size_t first = 0, last = count, operations = 0;
  if (!values && count) {
    if (comparisons) {
      *comparisons = 0;
    }
    return SIZE_MAX;
  }
  while (first < last) {
    size_t middle = first + (last - first) / 2;
    ++operations;
    if (values[middle] < key) {
      first = middle + 1;
    } else {
      last = middle;
    }
  }
  if (comparisons) {
    *comparisons = operations;
  }
  return first;
}

uint64_t c_u64_saturating_add(uint64_t left, uint64_t right) {
  return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}
