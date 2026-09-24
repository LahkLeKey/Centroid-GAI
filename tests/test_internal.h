/** @file test_internal.h @brief Declarations for internal module tests. */

#ifndef CGAI_TEST_INTERNAL_H
#define CGAI_TEST_INTERNAL_H

/**
 * @brief Check spelling normalization, punctuation, UTF-8 bytes, and empty input.
 *
 * The token list is destroyed and reset between scenarios, demonstrating ownership as well as
 * scanner behavior. The escaped byte sequence encodes an accented UTF-8 spelling explicitly.
 * Assertions compare copied token strings, so failures identify the exact boundary or spelling
 * rule.
 *
 * @return Zero after all tokenization scenarios pass.
 */
int test_tokenizer(void);

/**
 * @brief Check reserved IDs, lookup failure, deduplication, and ownership during growth.
 *
 * A freshly created model contains the three control spellings. Adding the same ordinary spelling
 * twice must return one stable ID, and its stored bytes must survive independently of input
 * storage. The growth helper then exercises reallocation and count-table expansion.
 *
 * @return Zero after all vocabulary invariants are checked.
 */
int test_vocabulary(void);

/**
 * @brief Build a small fixture for context, centroid, and RNG checks.
 *
 * Training initializes the centroid rows required by nearest-centroid lookup. The test helpers
 * borrow the model, while the runner retains responsibility for cleanup. RNG testing uses its own
 * state variables and does not alter model state.
 *
 * @return Zero after all mathematical checks pass.
 */
int test_model_math(void);

/**
 * @brief Exercise size-query encoding, exact buffers, and decoding failures.
 *
 * The encoder's size-query mode allows exact allocation without guessing. Invalid destination
 * combinations and a one-byte-short buffer must fail. The decode helper subsequently corrupts the
 * artifact on purpose, after which the buffer is only freed.
 *
 * @return Zero after all codec checks pass.
 */
int test_model_io(void);

/**
 * @brief Check greedy validity and fixed-seed reproducibility of weighted sampling.
 *
 * A trained small model supplies an initialized count row. The greedy case checks that an ID is
 * returned, while the weighted case initializes two independent RNG states equally and expects the
 * same chosen ID. The model remains unchanged throughout selection.
 *
 * @return Zero after both selection modes pass.
 */
int test_sampling(void);

/**
 * @brief Check binary file round trips, sentinel bytes, and invalid paths.
 *
 * The fixture contains both an embedded zero and 0xff, so comparing raw bytes verifies that file
 * I/O does not treat the payload as a text string. The returned extra NUL is checked separately
 * from the reported payload size. The helper owns the read buffer and removes its temporary file.
 *
 * @return Zero after all checks pass; failed assertions terminate the executable.
 */
int test_file_utils(void);

#endif