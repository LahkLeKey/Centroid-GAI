/** @file evolve_io.h @brief Bounded source and provenance artifact access. */
#ifndef CGAI_EVOLVE_IO_H
#define CGAI_EVOLVE_IO_H
#include "mutation.h"

int evolve_read_source(const char *path, evolve_mutation_source *source, char **storage);
int evolve_write_exclusive(const char *path, const void *bytes, size_t size);
int evolve_manifest_hash(const char *path, uint64_t *hash);
uint64_t evolve_bytes_hash(const void *bytes, size_t size);
#endif
