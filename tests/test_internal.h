/** @file test_internal.h @brief Declarations for internal module tests. */

#ifndef CGAI_TEST_INTERNAL_H
#define CGAI_TEST_INTERNAL_H

/** Runs tokenizer behavior and ownership tests. */
int test_tokenizer(void);

/** Runs vocabulary lookup, insertion, and count-storage tests. */
int test_vocabulary(void);

/** Runs embedding, centroid-selection, and random-state tests. */
int test_model_math(void);

/** Runs model encoding, decoding, and truncation tests. */
int test_model_io(void);

/** Runs greedy and temperature-weighted token selection tests. */
int test_sampling(void);

/** Runs whole-file read/write and argument validation tests. */
int test_file_utils(void);

#endif