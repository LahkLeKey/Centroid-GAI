/** @file gameplay_tool.h @brief Seeded composed fixtures and publication measurements. */
#ifndef CGAI_GAMEPLAY_TOOL_H
#define CGAI_GAMEPLAY_TOOL_H
#include "gameplay_evaluation.h"

/** @brief Seed aligned centroid prototypes from the complete training split only.
 * @return Owned initialized model with no optimizer/progress, or NULL with a diagnostic. */
cgai_gameplay_model *gameplay_tool_create(void);
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
