/** @file cli_neural.h @brief Shared neural CLI text, numeric, and metric helpers. */
#ifndef CGAI_CLI_NEURAL_H
#define CGAI_CLI_NEURAL_H
#include "centroid_gai_neural.h"

/**
 * @brief Parse decimal digits with an explicit upper bound before multiplication.
 *
 * Manual decimal accumulation rejects signs, whitespace, suffixes, and overflow uniformly
 * across platforms whose unsigned long long or size_t widths may differ.
 * @param text Borrowed terminated numeric argument, never NULL.
 * @param maximum Largest accepted result.
 * @param value Borrowed writable destination, changed only on success.
 * @return Nonzero for a nonempty valid decimal argument, otherwise zero.
 */
int cgai_cli_neural_unsigned(const char *text, uint64_t maximum, uint64_t *value);
/**
 * @brief Parse a fully consumed nonnegative finite floating-point value.
 *
 * Range errors, nonfinite spellings, signs, and leading whitespace are rejected. Scientific
 * notation remains available for small learning rates such as 1e-3.
 * @param text Borrowed terminated numeric argument, never NULL.
 * @param maximum Inclusive upper bound.
 * @param value Borrowed writable destination, unchanged on failure.
 * @return Nonzero after an accepted conversion, otherwise zero.
 */
int cgai_cli_neural_real(const char *text, double maximum, double *value);
/**
 * @brief Read a text file into owned storage that can safely be passed to string APIs.
 *
 * The shared reader appends a terminator. Embedded NUL bytes would silently truncate training
 * or evaluation, so this helper rejects them and releases the allocation.
 * @param path Borrowed terminated corpus path.
 * @return Owned terminated text to free(), or NULL after a stderr diagnostic.
 */
char *cgai_cli_neural_text(const char *path);
/**
 * @brief Score one sequence and display its next-token quality and vocabulary coverage.
 *
 * Metrics include the final EOS target. Unknown coverage counts only input tokens, making it
 * visible when a held-out score primarily reflects mapping unfamiliar words to UNK.
 * @param model Borrowed immutable initialized model.
 * @param text Borrowed nonempty sequence to evaluate.
 * @param label Borrowed display label identifying the dataset and training stage.
 * @return Nonzero after successful scoring, otherwise zero with the API diagnostic preserved.
 */
int cgai_cli_neural_report(const cgai_neural_model *model, const char *text, const char *label);
#endif
