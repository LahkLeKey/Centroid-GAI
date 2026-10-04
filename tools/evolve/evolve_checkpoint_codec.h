/** @file evolve_checkpoint_codec.h @brief Canonical bounded checkpoint payload codec. */
#ifndef CGAI_EVOLVE_CHECKPOINT_CODEC_H
#define CGAI_EVOLVE_CHECKPOINT_CODEC_H
#include "evolve_run.h"
int evolve_checkpoint_state_write(const char *path, const evolve_run *run);
int evolve_checkpoint_state_read(const char *path, evolve_run *run);
uint64_t evolve_checkpoint_receipt_hash(const evolve_checkpoint_receipt *receipt,
                                        uint64_t previous);
#endif
