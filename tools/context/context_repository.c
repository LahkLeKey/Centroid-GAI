/** @file context_repository.c @brief Exact local codebase ingestion without scripts. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "context_repository.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CONTEXT_FILE_BYTES (2U * 1024U * 1024U)
#define CONTEXT_PATH_BYTES 4096U
#define CONTEXT_DEPTH 64U
#define CONTEXT_HASH_START UINT64_C(14695981039346656037)
#define CONTEXT_HASH_PRIME UINT64_C(1099511628211)

typedef struct prepared_record {
    char source[CGAI_LIFE_CONTEXT_SOURCE_BYTES + 1U];
    char text[CGAI_LIFE_CONTEXT_TEXT_BYTES + 1U];
    uint32_t first_line;
    uint32_t last_line;
    uint32_t family;
    cgai_life_context_kind kind;
} prepared_record;

struct context_repository_batch {
    prepared_record *records;
    size_t count;
    size_t capacity;
};

typedef struct source_paths {
    char (*paths)[CGAI_LIFE_CONTEXT_SOURCE_BYTES + 1U];
    size_t count;
    const char *root;
    context_repository_report *report;
} source_paths;

typedef struct chunk_source {
    const char *source;
    const char *text;
    size_t bytes;
    cgai_life_context_kind kind;
    uint32_t family;
} chunk_source;

static int failure(context_repository_report *report, const char *reason, const char *path) {
    (void)snprintf(report->error, sizeof(report->error), "%s: %.400s", reason,
                   path != NULL ? path : "(null)");
    return 0;
}

static uint64_t hash_bytes(uint64_t hash, const char *bytes, size_t length) {
    for (size_t i = 0U; i < length; ++i)
        hash = (hash ^ (uint64_t)(unsigned char)bytes[i]) * CONTEXT_HASH_PRIME;
    return hash;
}

static uint64_t hash_length(uint64_t hash, uint64_t length) {
    for (size_t i = 0U; i < 8U; ++i) {
        const char byte = (char)(length & UINT64_C(255));
        hash = hash_bytes(hash, &byte, 1U);
        length >>= 8U;
    }
    return hash;
}

static uint32_t family_id(const char *source) {
    const uint64_t hash = hash_bytes(CONTEXT_HASH_START, source, strlen(source));
    const uint32_t result = (uint32_t)(hash ^ (hash >> 32U));
    return result != 0U ? result : 1U;
}

static int safe_segment(const char *name) {
    if (name[0] == '\0')
        return 0;
    for (size_t i = 0U; name[i] != '\0'; ++i)
        if ((unsigned char)name[i] < 32U || (unsigned char)name[i] == 127U ||
            strchr("/\\:", name[i]) != NULL)
            return 0;
    return 1;
}

static int normalized_name(const char *name, char *output) {
    const size_t length = strlen(name);
    if (length > CGAI_LIFE_CONTEXT_SOURCE_BYTES)
        return 0;
    for (size_t i = 0U; i < length; ++i)
        output[i] = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] + ('a' - 'A')) : name[i];
    output[length] = '\0';
    return 1;
}

static int excluded_name(const char *name) {
    static const char *const excluded[] = {
        "models",       "data",      "tests",        "neural-workflow", "persistence",
        "node_modules", "dist",      "coverage",     "vendor",          "__pycache__",
        "target",       "generated", "dependencies", "third_party",     "third-party"};
    if (name[0] == '.' || strncmp(name, "build", 5U) == 0)
        return 1;
    for (size_t i = 0U; i < sizeof(excluded) / sizeof(excluded[0]); ++i)
        if (strcmp(name, excluded[i]) == 0)
            return 1;
    return 0;
}

static int source_extension(const char *name) {
    static const char *const extensions[] = {".c",   ".h",   ".md",   ".cmake", ".in",
                                             ".txt", ".yml", ".yaml", ".json"};
    const char *extension = strrchr(name, '.');
    if (extension == NULL || strstr(name, "secret") != NULL || strstr(name, "credential") != NULL ||
        strstr(name, "private-key") != NULL)
        return 0;
    for (size_t i = 0U; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
        if (strcmp(extension, extensions[i]) == 0)
            return 1;
    return 0;
}

static int join_path(char *output, size_t capacity, const char *left, const char *right) {
    const int length =
        snprintf(output, capacity, "%s%s%s", left, left[0] != '\0' ? "/" : "", right);
    return length >= 0 && (size_t)length < capacity;
}

static int collect_path(source_paths *paths, const char *relative) {
    if (paths->count >= CGAI_LIFE_CONTEXT_MAX_RECORDS)
        return failure(paths->report, "source file capacity exceeded", relative);
    memcpy(paths->paths[paths->count], relative, strlen(relative) + 1U);
    ++paths->count;
    return 1;
}

static int scan_directory(source_paths *paths, const char *relative, uint32_t depth);

static int process_entry(source_paths *paths, const char *parent, const char *name, uint32_t depth,
                         int directory, int linked) {
    char relative[CGAI_LIFE_CONTEXT_SOURCE_BYTES + 1U];
    char normalized[CGAI_LIFE_CONTEXT_SOURCE_BYTES + 1U];
    if (!normalized_name(name, normalized))
        return failure(paths->report, "source name limit exceeded", name);
    if (linked || excluded_name(normalized) || (!directory && !source_extension(normalized))) {
        ++paths->report->skipped;
        return 1;
    }
    if (!safe_segment(name) || !join_path(relative, sizeof(relative), parent, name))
        return failure(paths->report, "invalid or overlong relative source path", name);
    return directory ? scan_directory(paths, relative, depth + 1U) : collect_path(paths, relative);
}

#ifdef _WIN32
static int scan_entries(source_paths *paths, const char *relative, uint32_t depth, HANDLE search,
                        WIN32_FIND_DATAA *entry) {
    int okay = 1;
    do {
        const DWORD attributes = entry->dwFileAttributes;
        if (strcmp(entry->cFileName, ".") != 0 && strcmp(entry->cFileName, "..") != 0)
            okay = process_entry(
                paths, relative, entry->cFileName, depth,
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U,
                (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN)) != 0U);
    } while (okay && FindNextFileA(search, entry));
    return okay && GetLastError() == ERROR_NO_MORE_FILES;
}

static int scan_directory(source_paths *paths, const char *relative, uint32_t depth) {
    char directory[CONTEXT_PATH_BYTES];
    char pattern[CONTEXT_PATH_BYTES];
    WIN32_FIND_DATAA entry;
    HANDLE search;
    int okay;
    if (depth > CONTEXT_DEPTH || !join_path(directory, sizeof(directory), paths->root, relative) ||
        !join_path(pattern, sizeof(pattern), directory, "*"))
        return failure(paths->report, "directory depth/path limit exceeded", relative);
    search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND
                   ? 1
                   : failure(paths->report, "cannot enumerate source directory", directory);
    okay = scan_entries(paths, relative, depth, search, &entry);
    (void)FindClose(search);
    return okay ? 1
           : paths->report->error[0] != '\0'
               ? 0
               : failure(paths->report, "source directory enumeration failed", directory);
}

static int root_directory(const char *path, char *output, context_repository_report *report) {
    const DWORD length = GetFullPathNameA(path, CONTEXT_PATH_BYTES, output, NULL);
    if (length == 0U || length >= CONTEXT_PATH_BYTES)
        return failure(report, "cannot resolve root path", path);
    for (size_t i = 0U; output[i] != '\0'; ++i)
        if (output[i] == '\\')
            output[i] = '/';
    return 1;
}

static int valid_root(const char *path) {
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
}

static FILE *open_source(const char *path) {
    HANDLE handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    BY_HANDLE_FILE_INFORMATION info;
    int descriptor;
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0U ||
        info.nFileSizeHigh != 0U || info.nFileSizeLow > CONTEXT_FILE_BYTES) {
        (void)CloseHandle(handle);
        return NULL;
    }
    descriptor = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
    if (descriptor == -1) {
        (void)CloseHandle(handle);
        return NULL;
    }
    FILE *file = _fdopen(descriptor, "rb");
    if (file == NULL)
        (void)_close(descriptor);
    return file;
}
#else
static int posix_entry(source_paths *paths, const char *relative, const char *name,
                       uint32_t depth) {
    char child[CGAI_LIFE_CONTEXT_SOURCE_BYTES + 1U];
    char absolute[CONTEXT_PATH_BYTES];
    struct stat info;
    if (!join_path(child, sizeof(child), relative, name) ||
        !join_path(absolute, sizeof(absolute), paths->root, child))
        return failure(paths->report, "source path limit exceeded", name);
    if (lstat(absolute, &info) != 0)
        return failure(paths->report, "cannot inspect source entry", child);
    return process_entry(paths, relative, name, depth, S_ISDIR(info.st_mode),
                         !S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode));
}

static int scan_posix_entries(source_paths *paths, const char *relative, uint32_t depth,
                              DIR *stream) {
    struct dirent *entry;
    int okay = 1;
    while (okay) {
        errno = 0;
        entry = readdir(stream);
        if (entry == NULL)
            return errno == 0
                       ? 1
                       : failure(paths->report, "source directory enumeration failed", relative);
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            okay = posix_entry(paths, relative, entry->d_name, depth);
    }
    return 0;
}

static int scan_directory(source_paths *paths, const char *relative, uint32_t depth) {
    char directory[CONTEXT_PATH_BYTES];
    DIR *stream;
    int okay;
    if (depth > CONTEXT_DEPTH || !join_path(directory, sizeof(directory), paths->root, relative))
        return failure(paths->report, "directory depth/path limit exceeded", relative);
    stream = opendir(directory);
    if (stream == NULL)
        return failure(paths->report, "cannot enumerate source directory", directory);
    okay = scan_posix_entries(paths, relative, depth, stream);
    return closedir(stream) == 0 && okay;
}

static int root_directory(const char *path, char *output, context_repository_report *report) {
    char current[CONTEXT_PATH_BYTES];
    if (path[0] == '/') {
        if (strlen(path) >= CONTEXT_PATH_BYTES)
            return failure(report, "root path limit exceeded", path);
        memcpy(output, path, strlen(path) + 1U);
        return 1;
    }
    return getcwd(current, sizeof(current)) != NULL &&
                   join_path(output, CONTEXT_PATH_BYTES, current, path)
               ? 1
               : failure(report, "cannot resolve root path", path);
}

static int valid_root(const char *path) {
    struct stat info;
    return lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static FILE *open_source(const char *path) {
    const int descriptor = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    struct stat info;
    FILE *file;
    if (descriptor == -1)
        return NULL;
    if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        (uint64_t)info.st_size > CONTEXT_FILE_BYTES) {
        (void)close(descriptor);
        return NULL;
    }
    file = fdopen(descriptor, "rb");
    if (file == NULL)
        (void)close(descriptor);
    return file;
}
#endif

static int root_ancestors(char *path, context_repository_report *report) {
    for (size_t i = 1U; path[i] != '\0'; ++i)
        if (path[i] == '/' && path[i - 1U] != ':' && path[i - 1U] != '/') {
            path[i] = '\0';
            const int okay = valid_root(path);
            path[i] = '/';
            if (!okay)
                return failure(report, "root ancestor is unavailable or a link", path);
        }
    return valid_root(path) ? 1 : failure(report, "root is unavailable or a link", path);
}

static int source_ancestors(const char *path, context_repository_report *report) {
    char absolute[CONTEXT_PATH_BYTES];
    char *separator;
    if (!root_directory(path, absolute, report))
        return 0;
    separator = strrchr(absolute, '/');
    if (separator == NULL)
        return failure(report, "source has no resolved parent directory", path);
    if (separator == absolute || separator[-1] == ':')
        ++separator;
    *separator = '\0';
    return root_ancestors(absolute, report);
}

static int reserve_record(context_repository_batch *batch, context_repository_report *report) {
    size_t capacity;
    prepared_record *records;
    if (batch->count >= CGAI_LIFE_CONTEXT_MAX_RECORDS)
        return failure(report, "source chunk capacity exceeded", "4096 records");
    if (batch->count < batch->capacity)
        return 1;
    capacity = batch->capacity != 0U ? batch->capacity * 2U : 16U;
    records = realloc(batch->records, capacity * sizeof(*records));
    if (records == NULL)
        return failure(report, "cannot allocate source chunks", "memory");
    batch->records = records;
    batch->capacity = capacity;
    return 1;
}

static int append_chunk(context_repository_batch *batch, const chunk_source *source, size_t begin,
                        size_t end, uint32_t first, uint32_t last,
                        context_repository_report *report) {
    prepared_record *record;
    if (!reserve_record(batch, report) || batch->records == NULL)
        return 0;
    record = &batch->records[batch->count];
    memset(record, 0, sizeof(*record));
    memcpy(record->source, source->source, strlen(source->source) + 1U);
    memcpy(record->text, source->text + begin, end - begin);
    record->first_line = first;
    record->last_line = last;
    record->family = source->family;
    record->kind = source->kind;
    ++batch->count;
    return 1;
}

static size_t line_end(const chunk_source *source, size_t position) {
    while (position < source->bytes && source->text[position] != '\n')
        ++position;
    return position < source->bytes ? position + 1U : position;
}

static int chunk_lines(context_repository_batch *batch, const chunk_source *source,
                       context_repository_report *report) {
    size_t begin = 0U, position = 0U;
    uint32_t first = 1U, line = 1U;
    while (position < source->bytes) {
        const size_t end = line_end(source, position);
        if (end - position > CGAI_LIFE_CONTEXT_TEXT_BYTES)
            return failure(report, "source line exceeds 2048 bytes", source->source);
        if (end - begin > CGAI_LIFE_CONTEXT_TEXT_BYTES) {
            if (!append_chunk(batch, source, begin, position, first, line - 1U, report))
                return 0;
            begin = position;
            first = line;
        }
        position = end;
        ++line;
    }
    return begin == source->bytes ||
           append_chunk(batch, source, begin, position, first, line - 1U, report);
}

static int read_bytes(const char *path, char *text, size_t *bytes) {
    FILE *file = open_source(path);
    int okay = 0;
    if (file != NULL) {
        *bytes = fread(text, 1U, (size_t)CONTEXT_FILE_BYTES + 1U, file);
        okay = *bytes <= CONTEXT_FILE_BYTES && !ferror(file) && memchr(text, '\0', *bytes) == NULL;
    }
    if (file != NULL && fclose(file) != 0)
        okay = 0;
    return okay;
}

static void source_identity(context_repository_report *report, const chunk_source *source) {
    report->source_hash = hash_length(report->source_hash, (uint64_t)source->kind);
    report->source_hash = hash_length(report->source_hash, strlen(source->source));
    report->source_hash =
        hash_bytes(report->source_hash, source->source, strlen(source->source) + 1U);
    report->source_hash = hash_length(report->source_hash, source->bytes);
    report->source_hash = hash_bytes(report->source_hash, source->text, source->bytes);
    report->bytes += source->bytes;
    ++report->files;
}

static int read_source(context_repository_batch *batch, const char *path, const char *source,
                       cgai_life_context_kind kind, context_repository_report *report) {
    char *text = malloc((size_t)CONTEXT_FILE_BYTES + 1U);
    size_t bytes = 0U;
    int okay = 0;
    if (text != NULL && source_ancestors(path, report) && read_bytes(path, text, &bytes)) {
        const chunk_source input = {source, text, bytes, kind, family_id(source)};
        okay = chunk_lines(batch, &input, report);
        if (okay)
            source_identity(report, &input);
    } else if (report->error[0] == '\0')
        (void)failure(report, "cannot read bounded regular text source", path);
    free(text);
    return okay;
}

static int compare_paths(const void *left, const void *right) {
    return strcmp((const char *)left, (const char *)right);
}

static int read_paths(context_repository_batch *batch, source_paths *paths) {
    qsort(paths->paths, paths->count, sizeof(paths->paths[0]), compare_paths);
    for (size_t i = 0U; i < paths->count; ++i) {
        char absolute[CONTEXT_PATH_BYTES];
        if (!join_path(absolute, sizeof(absolute), paths->root, paths->paths[i]) ||
            !read_source(batch, absolute, paths->paths[i], CGAI_LIFE_CONTEXT_SOURCE, paths->report))
            return 0;
    }
    return 1;
}

static int prepare(context_repository_batch **output, context_repository_report *report,
                   context_repository_batch **batch) {
    if (report == NULL)
        return 0;
    memset(report, 0, sizeof(*report));
    report->source_hash = CONTEXT_HASH_START;
    if (output == NULL || *output != NULL)
        return failure(report, "output must start NULL", "batch");
    *batch = calloc(1U, sizeof(**batch));
    return *batch != NULL ? 1 : failure(report, "cannot allocate source batch", "memory");
}

static int finish(context_repository_batch *batch, context_repository_batch **output,
                  context_repository_report *report, int okay) {
    report->records = (uint32_t)batch->count;
    if (okay)
        *output = batch;
    else
        context_repository_destroy(batch);
    return okay;
}

int context_repository_scan(const char *root, context_repository_batch **output,
                            context_repository_report *report) {
    context_repository_batch *batch = NULL;
    char absolute[CONTEXT_PATH_BYTES];
    source_paths paths = {NULL, 0U, absolute, report};
    int okay = 0;
    if (!prepare(output, report, &batch))
        return 0;
    if (root == NULL || root[0] == '\0')
        return finish(batch, output, report, failure(report, "missing repository root", root));
    paths.paths = calloc(CGAI_LIFE_CONTEXT_MAX_RECORDS, sizeof(paths.paths[0]));
    if (paths.paths != NULL && root_directory(root, absolute, report) &&
        root_ancestors(absolute, report))
        okay = scan_directory(&paths, "", 0U) && read_paths(batch, &paths);
    if (paths.paths == NULL)
        (void)failure(report, "cannot allocate source path list", "memory");
    free(paths.paths);
    return finish(batch, output, report, okay);
}

static int context_source_name(const char *path, char *source, context_repository_report *report) {
    if (path == NULL || path[0] == '\0' || strlen(path) > CGAI_LIFE_CONTEXT_SOURCE_BYTES)
        return failure(report, "invalid or overlong context source attribution", path);
    memcpy(source, path, strlen(path) + 1U);
    for (size_t i = 0U; source[i] != '\0'; ++i) {
        if ((unsigned char)source[i] < 32U || (unsigned char)source[i] == 127U)
            return failure(report, "context source attribution contains controls", path);
        if (source[i] == '\\')
            source[i] = '/';
    }
    return 1;
}

int context_repository_read_context(const char *path, cgai_life_context_kind kind,
                                    context_repository_batch **output,
                                    context_repository_report *report) {
    context_repository_batch *batch = NULL;
    char source[CGAI_LIFE_CONTEXT_SOURCE_BYTES + 1U];
    int okay;
    if (!prepare(output, report, &batch))
        return 0;
    okay = kind == CGAI_LIFE_CONTEXT_LLM || kind == CGAI_LIFE_CONTEXT_ACTIVITY;
    if (!okay)
        (void)failure(report, "context kind must be LLM or activity", path);
    if (okay)
        okay = context_source_name(path, source, report) &&
               read_source(batch, path, source, kind, report);
    return finish(batch, output, report, okay);
}

static cgai_life_status admit_record(const prepared_record *record, cgai_life_context *owner,
                                     uint32_t group_count) {
    const cgai_life_context_input input = {
        record->source,          record->text, record->first_line,      record->last_line,
        record->family,          record->kind, CGAI_LIFE_CONTEXT_TRAIN, 1U,
        (1U << group_count) - 1U};
    return cgai_life_context_add(owner, &input);
}

cgai_life_status context_repository_apply(const context_repository_batch *batch,
                                          cgai_life_context *owner) {
    cgai_life_context_stats stats;
    if (batch == NULL || owner == NULL)
        return CGAI_LIFE_INVALID_ARGUMENT;
    cgai_life_status status = cgai_life_context_get_stats(owner, &stats);
    if (status != CGAI_LIFE_OK)
        return status;
    if (batch->count > CGAI_LIFE_CONTEXT_MAX_RECORDS - stats.records)
        return CGAI_LIFE_LIMIT_REACHED;
    for (size_t i = 0U; i < batch->count; ++i) {
        status = admit_record(&batch->records[i], owner, stats.group_count);
        if (status != CGAI_LIFE_OK)
            return status;
    }
    return CGAI_LIFE_OK;
}

void context_repository_destroy(context_repository_batch *batch) {
    if (batch != NULL) {
        free(batch->records);
        free(batch);
    }
}
