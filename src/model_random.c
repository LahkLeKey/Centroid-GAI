/** @file model_random.c @brief Deterministic splitmix64 random state. */

#include "internal/model_random.h"

static const uint64_t CGAI_MIX_MULTIPLIER_A = UINT64_C(0xbf58476d1ce4e5b9);
static const uint64_t CGAI_MIX_MULTIPLIER_B = UINT64_C(0x94d049bb133111eb);
static const uint64_t CGAI_MIX_INCREMENT = UINT64_C(0x9e3779b97f4a7c15);
static const unsigned CGAI_MIX_SHIFT_A = 30U;
static const unsigned CGAI_MIX_SHIFT_B = 27U;
static const unsigned CGAI_MIX_SHIFT_C = 31U;

/**
 * @brief Scramble a 64-bit input with the splitmix64 avalanche transform.
 *
 * XOR combines differing bits, right shifts bring high bits into lower positions, and multiplication
 * spreads changes across the word. Unsigned uint64_t overflow deliberately wraps modulo 2^64.
 * This is deterministic numerical mixing for model reproducibility, not a cryptographic primitive.
 *
 * @param value Input word, passed by value so the caller's storage is unchanged.
 * @return Mixed 64-bit word; equal inputs always produce equal outputs.
 */
static uint64_t mix64(uint64_t value) {
    /* XOR-shift, multiply, and XOR-shift steps avalanche nearby input bits. */
    /* Step 1: Fold high input bits downward with an XOR-shift. */
    value ^= value >> CGAI_MIX_SHIFT_A;
    /* The first odd multiplier spreads the high-quality bits across the word. */
    /* Step 2: Spread those bit changes with the first odd multiplier. */
    value *= CGAI_MIX_MULTIPLIER_A;
    /* A second shift/multiply pair removes remaining linear structure. */
    /* Step 3: Apply the second shift/multiply stage to further mix nearby inputs. */
    value ^= value >> CGAI_MIX_SHIFT_B;
    value *= CGAI_MIX_MULTIPLIER_B;
    /* The final shift makes all output bits depend on the complete state. */
    /* Step 4: Finish with a final XOR-shift and return the mixed value. */
    return value ^ (value >> CGAI_MIX_SHIFT_C);
}

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
uint64_t cgai_random_next(uint64_t *state) {
    /* Step 1: Advance the caller's state modulo 2^64 using the fixed increment. */
    *state += CGAI_MIX_INCREMENT;
    /* Step 2: Mix the advanced state to obtain the sample returned to the caller. */
    return mix64(*state);
}
