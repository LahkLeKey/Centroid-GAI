/** @file context_repository.h @brief Bounded native source and context preparation. */
#ifndef CGAI_CONTEXT_REPOSITORY_H
#define CGAI_CONTEXT_REPOSITORY_H

#include "centroid_life.h"

/** Prepared exact source chunks; no caller model is changed during preparation. */
typedef struct context_repository_batch context_repository_batch;

/** Preparation coverage and deterministic identity; hash is not cryptographic. */
typedef struct context_repository_report {
    uint32_t files;
    uint32_t records;
    uint32_t skipped;
    uint64_t bytes;
    uint64_t source_hash;
    char error[512];
} context_repository_report;

/** Scan source/configuration/documentation in lexical relative-path order.
 * Hidden/generated/dependency/model/test trees are excluded. Links and Windows
 * reparse points are never followed. Files are bounded to 2 MiB and exact chunks
 * to the public text limit; an overlong line fails rather than truncating bytes.
 * Output must start NULL. Failure keeps it NULL and explains the error in report. */
int context_repository_scan(const char *root, context_repository_batch **output,
                            context_repository_report *report);

/** Prepare one explicit LLM/activity context file with the same byte/line limits.
 * Explicit files need no repository extension, but cannot be links/reparse points.
 * The supplied path is the source attribution; its slashes are normalized. */
int context_repository_read_context(const char *path, cgai_life_context_kind kind,
                                    context_repository_batch **output,
                                    context_repository_report *report);

/** Admit a prepared batch as reviewed train bytes, eligible for all four groups.
 * Review here authorizes lexical fitting, not the factual truth of LLM proposals.
 * Capacity is checked before admission; callers publish only after full success. */
cgai_life_status context_repository_apply(const context_repository_batch *batch,
                                          cgai_life_context *owner);

/** Release all prepared chunks; NULL is accepted. */
void context_repository_destroy(context_repository_batch *batch);

#endif
