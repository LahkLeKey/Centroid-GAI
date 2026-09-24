/** @file model_random.h @brief Private random operations. */
#ifndef CGAI_MODEL_RANDOM_H
#define CGAI_MODEL_RANDOM_H
#include <stdint.h>

/**
 * @brief Advance a caller-owned deterministic state and return its next mixed word.
 *
 * The stored state is advanced by a fixed increment; the return value is a mixed view of that
 * advanced state. Keeping state in a caller-owned variable lets independent calls reproduce or
 * separate their sequences without global random state. Unsigned wraparound is intentional.
 *
 * @param state Non-NULL writable 64-bit state initialized by the caller.
 * @return Next pseudo-random word; the pointed-to state has also advanced.
 */
uint64_t cgai_random_next(uint64_t *state);
#endif
