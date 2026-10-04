/** @file test_neural.h @brief Shared fixtures and scenarios for neural centroid tests. */
#ifndef CGAI_TEST_NEURAL_H
#define CGAI_TEST_NEURAL_H

#include "internal/neural_math.h"
#include "test_utils.h"
/** @brief Check reusable generation, deterministic work budgets and resource accounting. */
void cgai_test_neural_session(void);

/** @brief Create an owned small deterministic model for numerical checks.
 *
 * The compact shape keeps a full parameter-by-parameter gradient check inexpensive.
 * The vocabulary includes controls and three normalized ordinary spellings.
 * @return Owned initialized model; assertions terminate on allocation failure. */
cgai_neural_model *cgai_test_neural_fixture(void);

/** @brief Check gradients, normalized mixtures, masking, and positional encoding.
 *
 * One compact fixture and one independent workspace are shared across numerical
 * checks, then released. Assertions terminate before unsafe follow-up operations. */
void cgai_test_neural_math(void);

/** @brief Check neural artifact round trips and malformed artifact rejection.
 *
 * The successful artifact is reused for corruption tests through a separately
 * owned byte copy. Every successful path releases models, buffers, and files. */
void cgai_test_neural_io(void);

#endif
