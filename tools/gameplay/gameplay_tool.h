/** @file gameplay_tool.h @brief Reusable native composed training and publication measurements. */
#ifndef CGAI_GAMEPLAY_TOOL_H
#define CGAI_GAMEPLAY_TOOL_H
#include "gameplay_evaluation.h"

/** @brief Seed aligned centroid prototypes from the complete training split only.
 * @return Owned initialized model with no optimizer/progress, or NULL with a diagnostic. */
cgai_gameplay_model *gameplay_tool_create(void);
/** @brief Initialize aligned specialist parameters from the frozen task registry.
 * @param path New exact checkpoint destination.
 * @return OK after complete write, ERROR otherwise. */
cgai_status gameplay_tool_init(const char *path);
/** @brief Resume the balanced complete-epoch joint recipe.
 * @param input Existing exact checkpoint.
 * @param output Different checkpoint destination.
 * @param epochs Additional full balanced passes.
 * @param rate Positive finite learning rate, at most one.
 * @return OK after complete save, ERROR otherwise. */
cgai_status gameplay_tool_step(const char *input, const char *output, size_t epochs, double rate);
/** @brief Reconstruct all recorded balanced updates from seeded initialization.
 * @param input Reference checkpoint.
 * @param output Different replay destination.
 * @param rate Recorded constant learning rate.
 * @return OK after complete replay write, ERROR otherwise. */
cgai_status gameplay_tool_replay(const char *input, const char *output, double rate);
/** @brief Export weights without Adam state in the distinct gameplay inference format.
 * @param input Existing exact checkpoint.
 * @param output Inference artifact destination.
 * @return OK after complete export, ERROR otherwise. */
cgai_status gameplay_tool_export(const char *input, const char *output);
/** @brief Report complete per-task quality, composition and measured workload latency.
 * @param candidate Candidate inference weights.
 * @param incumbent Incumbent inference weights.
 * @param report New compact TSV destination.
 * @param hardware Bounded one-line hardware description.
 * @return OK after complete write, ERROR otherwise; report records the promotion decision. */
cgai_status gameplay_tool_report(const char *candidate, const char *incumbent, const char *report,
                                 const char *hardware);
/** @brief Check absolute frozen task, simulator, composition and memory quality.
 * @param path Trusted inference artifact.
 * @return OK after complete passing score, ERROR otherwise. */
cgai_status gameplay_tool_score(const char *path);
#endif
