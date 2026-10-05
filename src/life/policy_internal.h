#ifndef CENTROID_POLICY_INTERNAL_H
#define CENTROID_POLICY_INTERNAL_H
#include "internal.h"
typedef struct {
  unsigned prediction, teacher, x, y;
  double probabilities[4], utility[4], loss;
  uint64_t lookahead_ns, optimizer_ns;
  unsigned char evolved[C_WORLD_CELLS], edited[C_WORLD_CELLS];
} c_policy_trace;
c_status c_policy_inspect(const c_trainer *trainer, c_policy_trace *trace);
#endif
