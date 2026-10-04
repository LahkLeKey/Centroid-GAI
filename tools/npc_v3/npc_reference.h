/** @file npc_reference.h @brief Authoritative replay of pinned observation-limited comparator
 * actions. */
#ifndef CGAI_NPC_V3_REFERENCE_H
#define CGAI_NPC_V3_REFERENCE_H
#include "npc_evaluation.h"

/** Immutable artifact identity for the v3 world and shared reference replay. */
#define NPC_REFERENCE_PROFILE 3

/** @brief Replay every captured comparator decision against its own visible history.
 * @param capture_path Complete canonical raw capture, retained outside release bundles.
 * @param report_path New compact authoritative outcome report.
 * @param split Development or sealed audit partition.
 * @param comparator_sha Lowercase SHA256 of the wrapper's pinned comparator descriptor.
 * @return OK only after every declared episode and observation validates. */
cgai_status npc_reference_files(const char *capture_path, const char *report_path, npc_split split,
                                const char *comparator_sha);

/** @brief Emit a development-only planner capture for reference integration and validation.
 * @param capture_path New raw capture destination under an ignored work directory.
 * @param report_path New compact authoritative outcome report.
 * @param split Must be development; audit templates are rejected.
 * @param comparator_sha Lowercase SHA256 of the pinned observation-planner descriptor.
 * @return OK after a complete own-history development capture and report. */
cgai_status npc_reference_template(const char *capture_path, const char *report_path,
                                   npc_split split, const char *comparator_sha);
#endif
