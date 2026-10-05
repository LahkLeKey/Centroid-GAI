#include "internal.h"
#include "policy_internal.h"
#include <math.h>
#include <string.h>

/* Cell actions have their own contract and teacher, unrelated to code choices.
 * Candidate interventions touch at most eight cells. Lookahead uses only world
 * geometry and physical claims, never task inputs, labels, or domain outcomes.
 */
enum cell_action { CELL_KEEP, CELL_INSERT, CELL_REMOVE, CELL_RENEW };
static size_t at(unsigned x, unsigned y) {
  return (y % C_WORLD_SIDE) * C_WORLD_SIDE + x % C_WORLD_SIDE;
}
static void intervene(unsigned char *cells, unsigned action, unsigned x,
                      unsigned y) {
  if (action == CELL_INSERT)
    cells[at(x, y)] = 1;
  else if (action == CELL_REMOVE)
    cells[at(x, y)] = 0;
  else if (action == CELL_RENEW)
    for (unsigned g = 0; g < 2; g++)
      for (unsigned dy = 0; dy < 2; dy++)
        for (unsigned dx = 0; dx < 2; dx++)
          cells[at(x + 2 * g + dx, y + dy)] = (unsigned char)(1u << g);
}
static unsigned bits(unsigned x) {
  unsigned n = 0;
  while (x) {
    n += x & 1u;
    x >>= 1;
  }
  return n;
}
static double utility(const unsigned char *cells) {
  unsigned char current[C_WORLD_CELLS], next[C_WORLD_CELLS];
  double score = 0;
  memcpy(current, cells, sizeof(current));
  for (unsigned step = 0; step < 3; step++) {
    unsigned contacts, claims = 0;
    c_life_evolve(current, next, &contacts);
    for (size_t i = 0; i < C_WORLD_CELLS; i++)
      claims |= current[i];
    score += 8.0 * bits(contacts) + bits(claims);
    memcpy(current, next, sizeof(current));
  }
  return score;
}
static c_status prepare(const c_trainer *t,
                        const unsigned char evolved[C_WORLD_CELLS],
                        unsigned contacts, c_policy *candidate,
                        unsigned char edited[C_WORLD_CELLS],
                        c_policy_trace *trace) {
  double input[C_FEATURES], p[4], max = -INFINITY, sum = 0;
  unsigned char frame[C_WORLD_CELLS + 8], worlds[4][C_WORLD_CELLS];
  if (!t || !candidate || !edited || !evolved || !t->policy.enabled)
    return C_INVALID;
  unsigned x = (unsigned)((t->generation * 7u + t->rng) % C_WORLD_SIDE);
  unsigned y =
      (unsigned)((t->generation * 11u + (t->rng >> 8u)) % C_WORLD_SIDE);
  memcpy(frame, t->cells, C_WORLD_CELLS);
  for (unsigned i = 0; i < 4; i++) {
    frame[C_WORLD_CELLS + i] = (unsigned char)(x >> (8u * i));
    frame[C_WORLD_CELLS + 4 + i] = (unsigned char)(y >> (8u * i));
  }
  c_status s = c_encode(frame, sizeof(frame), input);
  if (s != C_OK)
    return s;
  unsigned choice = 0, teacher = 0;
  double best = -INFINITY;
  for (unsigned a = 0; a < 4; a++) {
    memcpy(worlds[a], evolved, C_WORLD_CELLS);
    intervene(worlds[a], a, x, y);
    uint64_t start = trace ? c_monotonic_ns() : 0;
    double score = utility(worlds[a]) - (a == CELL_KEEP ? 0.0 : 0.25);
    if (trace)
      trace->lookahead_ns += c_monotonic_ns() - start;
    if (trace)
      trace->utility[a] = score;
    if (score > best) {
      best = score;
      teacher = a;
    }
    double logit = 0;
    for (size_t j = 0; j < C_FEATURES; j++)
      logit += t->policy.readout[a][j].value * input[j];
    if (!isfinite(logit))
      return C_INVALID;
    p[a] = logit;
    if (logit > max) {
      max = logit;
      choice = a;
    }
  }
  for (unsigned a = 0; a < 4; a++) {
    p[a] = exp(p[a] - max);
    sum += p[a];
  }
  if (trace) {
    trace->teacher = teacher;
    trace->prediction = choice;
    trace->x = x;
    trace->y = y;
    for (unsigned a = 0; a < 4; a++)
      trace->probabilities[a] = p[a] / sum;
    trace->loss = -log(trace->probabilities[teacher]);
    memcpy(trace->evolved, evolved, C_WORLD_CELLS);
  }
  *candidate = t->policy;
  uint64_t optimizer_start = trace ? c_monotonic_ns() : 0;
  if (contacts) {
    if (candidate->clock == UINT64_MAX)
      return C_LIMIT;
    uint64_t clock = candidate->clock + 1;
    for (unsigned a = 0; a < 4; a++)
      for (size_t j = 0; j < C_FEATURES; j++)
        if (!c_scalar_adam(&candidate->readout[a][j],
                           (p[a] / sum - (a == teacher ? 1.0 : 0.0)) * input[j],
                           0.03, 1.0 - pow(0.9, (double)clock),
                           1.0 - pow(0.999, (double)clock)))
          return C_INVALID;
    candidate->clock = clock;
  }
  if (trace)
    trace->optimizer_ns = c_monotonic_ns() - optimizer_start;
  if (!t->policy.editing_enabled)
    choice = CELL_KEEP;
  if (memcmp(worlds[choice], evolved, C_WORLD_CELLS)) {
    if (candidate->interventions == UINT64_MAX)
      return C_LIMIT;
    candidate->interventions++;
  }
  unsigned char next[C_WORLD_CELLS];
  unsigned original_contacts, edited_contacts;
  c_life_evolve(evolved, next, &original_contacts);
  c_life_evolve(worlds[choice], next, &edited_contacts);
  if (original_contacts != edited_contacts) {
    if (candidate->contact_changes == UINT64_MAX)
      return C_LIMIT;
    candidate->contact_changes++;
  }
  memcpy(edited, worlds[choice], C_WORLD_CELLS);
  if (trace)
    memcpy(trace->edited, edited, C_WORLD_CELLS);
  return C_OK;
}
c_status c_policy_prepare(const c_trainer *t,
                          const unsigned char evolved[C_WORLD_CELLS],
                          unsigned contacts, c_policy *candidate,
                          unsigned char edited[C_WORLD_CELLS]) {
  return prepare(t, evolved, contacts, candidate, edited, NULL);
}
c_status c_policy_inspect(const c_trainer *t, c_policy_trace *trace) {
  if (!t || !trace || !t->policy.enabled)
    return C_INVALID;
  unsigned char evolved[C_WORLD_CELLS], edited[C_WORLD_CELLS];
  unsigned contacts;
  c_policy candidate;
  c_life_evolve(t->cells, evolved, &contacts);
  c_policy_trace result = {0};
  c_status s = prepare(t, evolved, contacts, &candidate, edited, &result);
  if (s == C_OK)
    *trace = result;
  return s;
}
