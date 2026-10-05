#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "context_internal.h"
#include <errno.h>
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

#define C_SCAN_DEPTH 64u
#define C_SCAN_ENTRIES 65536u
#define C_PATH_BYTES (C_CONTEXT_PROVENANCE_BYTES + 1u)

typedef struct {
  c_context *context;
  c_scan_report *report;
  c_status status;
  char root[C_PATH_BYTES];
  char **paths;
  size_t count, visited;
} scan_state;

static c_status scan_failure(scan_state *state, c_status status) {
  ++state->report->failed;
  if (state->status == C_OK)
    state->status = status;
  return status;
}

static c_status join_path(char out[C_PATH_BYTES], const char *a,
                          const char *b) {
  int length =
      snprintf(out, C_PATH_BYTES, "%s%s%s", a, a[0] != '\0' ? "/" : "", b);
  return length >= 0 && (size_t)length < C_PATH_BYTES ? C_OK : C_LIMIT;
}

static c_status path_type(const char *path, int directory) {
#ifdef _WIN32
  DWORD attributes = GetFileAttributesA(path);
  if (attributes == INVALID_FILE_ATTRIBUTES)
    return C_IO;
  if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    return C_INVALID;
  int is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  return directory < 0 || is_directory == directory ? C_OK : C_INVALID;
#else
  struct stat info;
  if (lstat(path, &info) != 0)
    return C_IO;
  if (S_ISLNK(info.st_mode) ||
      (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)))
    return C_INVALID;
  return directory < 0 ||
                 (directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode))
             ? C_OK
             : C_INVALID;
#endif
}

c_status c_context_no_links(const char *absolute, int directory) {
  char path[C_PATH_BYTES];
  size_t length = strlen(absolute);
  if (length >= sizeof(path))
    return C_LIMIT;
  memcpy(path, absolute, length + 1);
  for (size_t i = 1; i < length; ++i) {
    if (path[i] != '/' || path[i - 1] == ':' || path[i - 1] == '/')
      continue;
    path[i] = '\0';
    c_status status = path_type(path, 1);
    path[i] = '/';
    if (status != C_OK)
      return status;
  }
  return path_type(path, directory);
}

c_status c_context_absolute(const char *path, char absolute[C_PATH_BYTES]) {
  if (path == NULL || path[0] == '\0')
    return C_INVALID;
#ifdef _WIN32
  DWORD length = GetFullPathNameA(path, C_PATH_BYTES, absolute, NULL);
  if (length == 0)
    return C_IO;
  if (length >= C_PATH_BYTES)
    return C_LIMIT;
  for (size_t i = 0; i < length; ++i)
    if (absolute[i] == '\\')
      absolute[i] = '/';
  c_status status = c_context_no_links(absolute, -1);
  if (status != C_OK)
    return status;
  /* Known split locations must not be hidden by an 8.3 spelling. Validate
   * every ancestor before expanding names, then revalidate the long path.
   */
  char expanded[C_PATH_BYTES];
  length = GetLongPathNameA(absolute, expanded, C_PATH_BYTES);
  if (length == 0)
    return C_IO;
  if (length >= C_PATH_BYTES)
    return C_LIMIT;
  for (size_t i = 0; i < length; ++i)
    if (expanded[i] == '\\')
      expanded[i] = '/';
  status = c_context_no_links(expanded, -1);
  if (status == C_OK)
    memcpy(absolute, expanded, (size_t)length + 1);
  return status;
#else
  char raw[C_PATH_BYTES], working[C_PATH_BYTES];
  c_status status;
  if (path[0] == '/')
    status = join_path(raw, "", path);
  else if (getcwd(working, sizeof(working)) != NULL)
    status = join_path(raw, working, path);
  else
    return C_IO;
  if (status != C_OK || (status = c_context_no_links(raw, -1)) != C_OK)
    return status;
  char *resolved = realpath(raw, NULL);
  if (resolved == NULL)
    return C_IO;
  size_t length = strlen(resolved);
  status = length < C_PATH_BYTES ? C_OK : C_LIMIT;
  if (status == C_OK)
    memcpy(absolute, resolved, length + 1);
  free(resolved);
  return status;
#endif
}

static int component_equal(const char *begin, size_t length, const char *word) {
  if (length != strlen(word))
    return 0;
  for (size_t i = 0; i < length; ++i) {
    char byte = begin[i] >= 'A' && begin[i] <= 'Z'
                    ? (char)(begin[i] + ('a' - 'A'))
                    : begin[i];
    if (byte != word[i])
      return 0;
  }
  return 1;
}

int c_context_reserved_training_path(const char *absolute) {
  size_t position = 0, previous_begin = 0, previous_length = 0;
  if (absolute == NULL)
    return 1;
  while (absolute[position] != '\0') {
    while (absolute[position] == '/')
      ++position;
    size_t begin = position;
    while (absolute[position] != '\0' && absolute[position] != '/')
      ++position;
    size_t length = position - begin;
    if (component_equal(absolute + begin, length, "audit") ||
        (component_equal(absolute + previous_begin, previous_length, "data") &&
         component_equal(absolute + begin, length, "development")) ||
        (component_equal(absolute + previous_begin, previous_length, "tests") &&
         component_equal(absolute + begin, length, "fixtures")))
      return 1;
    previous_begin = begin;
    previous_length = length;
  }
  return 0;
}

static FILE *regular_file(const char *path, c_status *status) {
#ifdef _WIN32
  HANDLE handle =
      CreateFileA(path, GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  BY_HANDLE_FILE_INFORMATION info;
  if (handle == INVALID_HANDLE_VALUE)
    return NULL;
  if (!GetFileInformationByHandle(handle, &info) ||
      (info.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    (void)CloseHandle(handle);
    return NULL;
  }
  if (info.nFileSizeHigh != 0 || info.nFileSizeLow > C_CONTEXT_RECORD_BYTES) {
    *status = C_LIMIT;
    (void)CloseHandle(handle);
    return NULL;
  }
  int descriptor = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
  if (descriptor < 0) {
    (void)CloseHandle(handle);
    return NULL;
  }
  FILE *file = _fdopen(descriptor, "rb");
  if (file == NULL)
    (void)_close(descriptor);
#else
  int descriptor = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  struct stat info;
  if (descriptor < 0)
    return NULL;
  if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)) {
    (void)close(descriptor);
    return NULL;
  }
  if (info.st_size < 0 || (uint64_t)info.st_size > C_CONTEXT_RECORD_BYTES) {
    *status = C_LIMIT;
    (void)close(descriptor);
    return NULL;
  }
  FILE *file = fdopen(descriptor, "rb");
  if (file == NULL)
    (void)close(descriptor);
#endif
  return file;
}

c_status c_context_read_regular(const char *path, unsigned char **bytes,
                                size_t *length) {
  char absolute[C_PATH_BYTES];
  c_status status = c_context_absolute(path, absolute);
  if (status != C_OK || (status = c_context_no_links(absolute, 0)) != C_OK)
    return status;
  status = C_IO;
  FILE *file = regular_file(absolute, &status);
  if (file == NULL)
    return status;
  unsigned char *data = malloc((size_t)C_CONTEXT_RECORD_BYTES + 1);
  if (data == NULL) {
    (void)fclose(file);
    return C_NOMEM;
  }
  size_t size = fread(data, 1, (size_t)C_CONTEXT_RECORD_BYTES + 1, file);
  status = size > C_CONTEXT_RECORD_BYTES ? C_LIMIT : ferror(file) ? C_IO : C_OK;
  if (fclose(file) != 0 && status == C_OK)
    status = C_IO;
  if (status != C_OK)
    free(data);
  else {
    *bytes = data;
    *length = size;
  }
  return status;
}

static int excluded_name(const char *name, int directory) {
  static const char *const directories[] = {
      "models",    "checkpoints", "data",         "audit",     "tests",
      "research",  "experiments", "node_modules", "deps",      "dependencies",
      "vendor",    "third_party", "third-party",  "generated", "dist",
      "target",    "coverage",    "output",       "outputs",   "runs",
      "artifacts", "__pycache__"};
  if (name[0] == '.' || strstr(name, "secret") != NULL ||
      strstr(name, "credential") != NULL ||
      strstr(name, "private-key") != NULL ||
      strstr(name, ".checkpoint") != NULL || strstr(name, ".tmp") != NULL ||
      strcmp(name, "checkpoint.json") == 0 || strcmp(name, "model.json") == 0 ||
      strcmp(name, "weights.json") == 0)
    return 1;
  if (directory &&
      (strcmp(name, "build") == 0 || strncmp(name, "build-", 6) == 0 ||
       strncmp(name, "cmake-build-", 12) == 0))
    return 1;
  if (directory)
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i)
      if (strcmp(name, directories[i]) == 0)
        return 1;
  return 0;
}

static int source_extension(const char *name) {
  static const char *const extensions[] = {
      ".c",  ".h",   ".cc",    ".cpp",  ".hpp", ".py",  ".js",   ".jsx",
      ".ts", ".tsx", ".mjs",   ".cjs",  ".rs",  ".go",  ".java", ".cs",
      ".rb", ".lua", ".swift", ".kt",   ".sh",  ".ps1", ".md",   ".cmake",
      ".in", ".txt", ".yml",   ".yaml", ".json"};
  const char *extension = strrchr(name, '.');
  if (extension == NULL)
    return 0;
  for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
    if (strcmp(extension, extensions[i]) == 0)
      return 1;
  return 0;
}

static c_status collect_file(scan_state *state, const char *relative) {
  if (state->count == C_CONTEXT_RECORD_LIMIT)
    return scan_failure(state, C_LIMIT);
  size_t length = strlen(relative) + 1;
  char *copy = malloc(length);
  if (copy == NULL)
    return scan_failure(state, C_NOMEM);
  char **paths = realloc(state->paths, (state->count + 1) * sizeof(*paths));
  if (paths == NULL) {
    free(copy);
    return scan_failure(state, C_NOMEM);
  }
  memcpy(copy, relative, length);
  state->paths = paths;
  state->paths[state->count++] = copy;
  return C_OK;
}

static c_status walk_directory(scan_state *state, const char *relative,
                               unsigned depth);

static c_status process_entry(scan_state *state, const char *parent,
                              const char *name, unsigned depth, int directory,
                              int excluded_type) {
  char normalized[C_PATH_BYTES], relative[C_PATH_BYTES];
  size_t length = strlen(name);
  if (++state->visited > C_SCAN_ENTRIES || length >= sizeof(normalized))
    return scan_failure(state, C_LIMIT);
  for (size_t i = 0; i <= length; ++i)
    normalized[i] = name[i] >= 'A' && name[i] <= 'Z'
                        ? (char)(name[i] + ('a' - 'A'))
                        : name[i];
  if (excluded_type || excluded_name(normalized, directory) ||
      (!directory && !source_extension(normalized))) {
    ++state->report->excluded;
    return C_OK;
  }
  if (join_path(relative, parent, name) != C_OK)
    return scan_failure(state, C_LIMIT);
  return directory ? walk_directory(state, relative, depth + 1)
                   : collect_file(state, relative);
}

#ifdef _WIN32
static c_status walk_entries(scan_state *state, const char *relative,
                             unsigned depth, HANDLE search,
                             WIN32_FIND_DATAA *entry) {
  do {
    if (strcmp(entry->cFileName, ".") == 0 ||
        strcmp(entry->cFileName, "..") == 0)
      continue;
    DWORD attributes = entry->dwFileAttributes;
    (void)process_entry(state, relative, entry->cFileName, depth,
                        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
                        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
    if (state->visited > C_SCAN_ENTRIES)
      return C_LIMIT;
  } while (FindNextFileA(search, entry));
  return GetLastError() == ERROR_NO_MORE_FILES ? C_OK
                                               : scan_failure(state, C_IO);
}

static c_status walk_directory(scan_state *state, const char *relative,
                               unsigned depth) {
  char absolute[C_PATH_BYTES], pattern[C_PATH_BYTES];
  WIN32_FIND_DATAA entry;
  if (depth > C_SCAN_DEPTH ||
      join_path(absolute, state->root, relative) != C_OK ||
      join_path(pattern, absolute, "*") != C_OK)
    return scan_failure(state, C_LIMIT);
  c_status status = c_context_no_links(absolute, 1);
  if (status != C_OK)
    return scan_failure(state, status);
  HANDLE search = FindFirstFileA(pattern, &entry);
  if (search == INVALID_HANDLE_VALUE)
    return GetLastError() == ERROR_FILE_NOT_FOUND ? C_OK
                                                  : scan_failure(state, C_IO);
  status = walk_entries(state, relative, depth, search, &entry);
  (void)FindClose(search);
  return status;
}
#else
static void inspect_entry(scan_state *state, const char *relative,
                          const char *name, unsigned depth) {
  char child[C_PATH_BYTES], absolute[C_PATH_BYTES];
  struct stat info;
  if (join_path(child, relative, name) != C_OK ||
      join_path(absolute, state->root, child) != C_OK) {
    (void)scan_failure(state, C_LIMIT);
    return;
  }
  if (lstat(absolute, &info) != 0) {
    (void)scan_failure(state, C_IO);
    return;
  }
  (void)process_entry(state, relative, name, depth, S_ISDIR(info.st_mode),
                      !S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode));
}

static c_status walk_entries(scan_state *state, const char *relative,
                             unsigned depth, DIR *stream) {
  struct dirent *entry;
  for (;;) {
    errno = 0;
    entry = readdir(stream);
    if (entry == NULL)
      return errno == 0 ? C_OK : scan_failure(state, C_IO);
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
      inspect_entry(state, relative, entry->d_name, depth);
    if (state->visited > C_SCAN_ENTRIES)
      return C_LIMIT;
  }
}

static c_status walk_directory(scan_state *state, const char *relative,
                               unsigned depth) {
  char absolute[C_PATH_BYTES];
  if (depth > C_SCAN_DEPTH ||
      join_path(absolute, state->root, relative) != C_OK)
    return scan_failure(state, C_LIMIT);
  c_status status = c_context_no_links(absolute, 1);
  if (status != C_OK)
    return scan_failure(state, status);
  int descriptor = open(absolute, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (descriptor < 0)
    return scan_failure(state, C_IO);
  DIR *stream = fdopendir(descriptor);
  if (stream == NULL) {
    (void)close(descriptor);
    return scan_failure(state, C_IO);
  }
  status = walk_entries(state, relative, depth, stream);
  if (closedir(stream) != 0)
    return scan_failure(state, C_IO);
  return status;
}
#endif

static int compare_paths(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int path_separator(char byte) {
#ifdef _WIN32
  return byte == '/' || byte == '\\';
#else
  return byte == '/';
#endif
}

/* Only explicit absolute filesystem names can alias a scanned source. Relative
 * SOURCE names may describe virtual observations, so scanning does not
 * reinterpret them. Dot components are rejected rather than resolved across
 * possible links. Normalization is lexical: it never reads an arbitrary
 * admitted record path. */
static int absolute_source_name(const char *path, char out[C_PATH_BYTES]) {
  size_t length = strlen(path), position = 0, used = 0;
  if (!length || length >= C_PATH_BYTES)
    return 0;
#ifdef _WIN32
  if (((path[0] >= 'a' && path[0] <= 'z') ||
       (path[0] >= 'A' && path[0] <= 'Z')) &&
      length >= 3 && path[1] == ':' && path_separator(path[2])) {
    out[used++] = path[0] >= 'a' && path[0] <= 'z'
                      ? (char)(path[0] - ('a' - 'A'))
                      : path[0];
    out[used++] = ':';
    out[used++] = '/';
    position = 3;
  } else if (length >= 2 && path_separator(path[0]) &&
             path_separator(path[1])) {
    out[used++] = '/';
    out[used++] = '/';
    position = 2;
  } else
    return 0;
#else
  if (path[0] != '/')
    return 0;
  out[used++] = '/';
  position = 1;
#endif
  while (position < length) {
    while (position < length && path_separator(path[position]))
      ++position;
    size_t begin = position;
    while (position < length && !path_separator(path[position]))
      ++position;
    size_t component = position - begin;
    if (!component)
      break;
    if ((component == 1 && path[begin] == '.') ||
        (component == 2 && path[begin] == '.' && path[begin + 1] == '.'))
      return 0;
    if (out[used - 1] != '/')
      out[used++] = '/';
    if (component >= C_PATH_BYTES - used)
      return 0;
    memcpy(out + used, path + begin, component);
    used += component;
  }
  out[used] = '\0';
  return 1;
}

static int same_absolute_source_name(const char *physical,
                                     const char *admitted) {
  if (!strcmp(physical, admitted))
    return 1;
#ifdef _WIN32
  /* Case folding alone is insufficient: Windows directories may opt into
   * case-sensitive lookup. Restrict the alternate lookup to lexical case
   * variants of this allowed source, then verify its native file identity.
   */
  for (size_t i = 0;; ++i) {
    unsigned char first = (unsigned char)physical[i];
    unsigned char second = (unsigned char)admitted[i];
    if (first >= 'A' && first <= 'Z')
      first = (unsigned char)(first + ('a' - 'A'));
    if (second >= 'A' && second <= 'Z')
      second = (unsigned char)(second + ('a' - 'A'));
    if (first != second)
      return 0;
    if (!first)
      break;
  }
  if (c_context_no_links(admitted, 0) != C_OK)
    return 0;
  HANDLE first =
      CreateFileA(physical, FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  HANDLE second =
      CreateFileA(admitted, FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  BY_HANDLE_FILE_INFORMATION a, b;
  int same = first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE &&
             GetFileInformationByHandle(first, &a) &&
             GetFileInformationByHandle(second, &b) &&
             !(a.dwFileAttributes &
               (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
             !(b.dwFileAttributes &
               (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
             a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
             a.nFileIndexHigh == b.nFileIndexHigh &&
             a.nFileIndexLow == b.nFileIndexLow;
  if (first != INVALID_HANDLE_VALUE)
    (void)CloseHandle(first);
  if (second != INVALID_HANDLE_VALUE)
    (void)CloseHandle(second);
  return same;
#else
  return 0;
#endif
}

static void refresh_source_aliases(scan_state *state, const char *absolute,
                                   const unsigned char *bytes, size_t length,
                                   size_t original_count) {
  char physical[C_PATH_BYTES], admitted[C_PATH_BYTES];
  if (!absolute_source_name(absolute, physical))
    return;
  /* This is the same allowed physical path already read by the scanner, never
   * an alias-directed traversal or an extra source-byte read. */
  c_status status = c_context_no_links(absolute, 0);
  if (status != C_OK) {
    (void)scan_failure(state, status);
    return;
  }
  for (size_t i = 0; i < original_count; ++i) {
    const c_record *alias = &state->context->records[i]->view;
    if (!alias->current || alias->kind != C_SOURCE || alias->split != C_TRAIN ||
        !absolute_source_name(alias->path, admitted) ||
        !same_absolute_source_name(physical, admitted))
      continue;
    size_t before = state->context->count;
    status = c_context_admit(state->context, C_SOURCE, C_TRAIN, alias->path,
                             alias->attribution, bytes, length, NULL);
    if (status != C_OK)
      (void)scan_failure(state, status);
    else if (state->context->count != before)
      ++state->report->admitted;
    else
      ++state->report->unchanged;
  }
}

static void admit_paths(scan_state *state) {
  qsort(state->paths, state->count, sizeof(*state->paths), compare_paths);
  for (size_t i = 0; i < state->count; ++i) {
    char absolute[C_PATH_BYTES];
    unsigned char *bytes = NULL;
    size_t length = 0, before = state->context->count;
    uint64_t id = 0;
    c_status status = join_path(absolute, state->root, state->paths[i]);
    if (status == C_OK)
      status = c_context_read_regular(absolute, &bytes, &length);
    if (status == C_OK) {
      state->report->bytes += length;
      status =
          c_context_admit(state->context, C_SOURCE, C_TRAIN, state->paths[i],
                          state->root, bytes, length, &id);
    }
    if (status != C_OK)
      (void)scan_failure(state, status);
    else {
      if (state->context->count != before)
        ++state->report->admitted;
      else
        ++state->report->unchanged;
      refresh_source_aliases(state, absolute, bytes, length, before);
    }
    free(bytes);
  }
}

c_status c_context_scan(c_context *context, const char *root,
                        c_scan_report *report) {
  if (context == NULL || report == NULL)
    return C_INVALID;
  memset(report, 0, sizeof(*report));
  scan_state state = {0};
  state.context = context;
  state.report = report;
  c_status status = c_context_absolute(root, state.root);
  if (status == C_OK)
    status = c_context_no_links(state.root, 1);
  if (status == C_OK && c_context_reserved_training_path(state.root))
    status = C_INVALID;
  if (status != C_OK)
    return scan_failure(&state, status);
  (void)walk_directory(&state, "", 0);
  admit_paths(&state);
  for (size_t i = 0; i < state.count; ++i)
    free(state.paths[i]);
  free(state.paths);
  return state.status;
}
