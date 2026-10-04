/** @file life_io.h @brief Exact simulation bundles and native text traces. */
#ifndef CGAI_LIFE_IO_H
#define CGAI_LIFE_IO_H
#include "life_training.h"
#include <stdio.h>

typedef struct life_trace life_trace;

/** Atomically replace PATH with a self-contained version-two exact checkpoint.
 * Failed writes/replacements preserve any existing destination. */
int life_snapshot_save(const char *path, const life_run *run);
/** Load into an uninitialized owner; failure leaves it untouched. Accept version two
 * embedded policies and legacy version one with its adjacent PATH.policy file. */
int life_snapshot_load(const char *path, life_run *run);
/** Validated embedded v2 stream codec; read leaves following tokens unread. */
int life_snapshot_write(FILE *file, const life_run *run);
int life_snapshot_read(FILE *file, life_run *run);
/** Same-directory staged atomic publication shared by native domain bundles. */
int life_checkpoint_publish(const char *path, const void *owner,
                            int (*write_owner)(FILE *, const void *));
/** Open PREFIX.tsv metrics and PREFIX.trace versioned ASCII world frames. */
life_trace *life_trace_open(const char *prefix, const life_run *run);
/** Append a current world and its most recent collision diagnostics. */
int life_trace_append(life_trace *trace, const life_run *run);
/** Complete both artifacts and release the trace; return nonzero on success. */
int life_trace_close(life_trace *trace);
/** Print a world frame to a borrowed stream without changing simulation or policy state. */
int life_inspect(FILE *file, const life_run *run);
#endif
