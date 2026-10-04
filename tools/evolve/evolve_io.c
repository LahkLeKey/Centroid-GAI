/** @file evolve_io.c @brief Immutable inputs and exclusive candidate artifacts. */
#include "evolve_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVOLVE_SOURCE_LIMIT (1024U * 1024U)
#define EVOLVE_PATH_LIMIT 4096U
#define EVOLVE_INPUT_LIMIT 4096U

static uint64_t hash_bytes(uint64_t hash, const void *bytes, size_t size) {
    const unsigned char *data = bytes;
    for (size_t i = 0U; i < size; ++i) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t evolve_bytes_hash(const void *bytes, size_t size) {
    return hash_bytes(UINT64_C(14695981039346656037), bytes, size);
}

int evolve_write_exclusive(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "wbx");
    if (file == NULL)
        return 0;
    int okay = fwrite(bytes, 1U, size, file) == size && fflush(file) == 0;
    if (fclose(file) != 0)
        okay = 0;
    if (!okay)
        (void)remove(path);
    return okay;
}

static int read_source_bytes(FILE *file, char **storage, size_t *size) {
    char *bytes = malloc(EVOLVE_SOURCE_LIMIT + 1U);
    if (bytes == NULL)
        return 0;
    const size_t used = fread(bytes, 1U, EVOLVE_SOURCE_LIMIT + 1U, file);
    if (ferror(file) || used == 0U || used > EVOLVE_SOURCE_LIMIT || !feof(file)) {
        free(bytes);
        return 0;
    }
    bytes[used] = '\0';
    *storage = bytes;
    *size = used;
    return 1;
}

static int read_source_file(const char *path, char **storage, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    int okay = read_source_bytes(file, storage, size);
    if (fclose(file) != 0 && okay) {
        free(*storage);
        *storage = NULL;
        okay = 0;
    }
    return okay;
}

int evolve_read_source(const char *path, evolve_mutation_source *source, char **storage) {
    char *bytes = NULL;
    size_t size = 0U;
    if (!read_source_file(path, &bytes, &size))
        return 0;
    if (evolve_mutation_validate(bytes, size, source) != EVOLVE_MUTATION_OK) {
        free(bytes);
        return 0;
    }
    *storage = bytes;
    return 1;
}

static int file_hash(const char *path, uint64_t *hash) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    unsigned char buffer[8192];
    size_t used;
    uint64_t result = UINT64_C(14695981039346656037);
    do {
        used = fread(buffer, 1U, sizeof(buffer), file);
        result = hash_bytes(result, buffer, used);
    } while (!ferror(file) && !feof(file) && used != 0U);
    int okay = !ferror(file);
    if (fclose(file) != 0)
        okay = 0;
    if (okay)
        *hash = result;
    return okay;
}

static uint64_t hash_word(uint64_t hash, uint64_t value) {
    for (size_t i = 0U; i < 8U; ++i) {
        const unsigned char byte = (unsigned char)(value & UINT64_C(255));
        hash = hash_bytes(hash, &byte, 1U);
        value >>= 8U;
    }
    return hash;
}

static int manifest_line(FILE *file, uint64_t *hash) {
    char path[EVOLVE_PATH_LIMIT];
    if (fgets(path, sizeof(path), file) == NULL)
        return feof(file) && !ferror(file) ? 0 : -1;
    const size_t length = strcspn(path, "\r\n");
    if (length == 0U || length >= sizeof(path) - 1U)
        return -1;
    path[length] = '\0';
    uint64_t contents;
    if (!file_hash(path, &contents))
        return -1;
    *hash = hash_bytes(*hash, path, length + 1U);
    *hash = hash_word(*hash, contents);
    return 1;
}

int evolve_manifest_hash(const char *path, uint64_t *hash) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    uint64_t result = UINT64_C(14695981039346656037);
    size_t count = 0U;
    int line;
    while ((line = manifest_line(file, &result)) > 0 && count < EVOLVE_INPUT_LIMIT)
        ++count;
    int okay = line == 0 && count != 0U && count < EVOLVE_INPUT_LIMIT;
    if (fclose(file) != 0)
        okay = 0;
    if (okay)
        *hash = result;
    return okay;
}
