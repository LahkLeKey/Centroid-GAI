/** @file life_events.h @brief Observation of successful transactional generations. */
#ifndef CGAI_LIFE_EVENTS_H
#define CGAI_LIFE_EVENTS_H

#include "centroid_life.h"
#include "life_training.h"

/** Capture a successful result while its preceding owner is still alive.
 * Source and result are borrowed, distinct runs; output is caller-owned.
 * This infallible observer neither mutates nor recomputes either run's decisions. */
void life_events_capture(const life_run *source, const life_run *result, int evaluate,
                         cgai_life_events *output);

#endif
