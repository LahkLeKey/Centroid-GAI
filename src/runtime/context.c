#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "centroid_context.h"
#include "runtime_internal.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
  cr_record_view view;
  double centroid[CR_FEATURES];
} owned_record;
struct cr_context {
  cr_context_options limits;
  owned_record **records;
  size_t count, total;
};
static int same_word(const char *a, size_t n, const char *b) {
  if (strlen(b) != n)
    return 0;
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)a[i];
    if (c >= 'A' && c <= 'Z')
      c = (unsigned char)(c + 'a' - 'A');
    if (c != (unsigned char)b[i])
      return 0;
  }
  return 1;
}
static int excluded_component(const char *name, size_t n) {
  static const char *const names[] = {
      "data",         "tests",        "research",
      "audit",        "dev",          "holdout",
      "experiments",  "models",       "checkpoints",
      "runs",         "vendor",       "third_party",
      "third-party",  "node_modules", "deps",
      "dependencies", "generated",    "dist",
      "target",       "output",       "outputs",
      "artifacts",    "coverage",     "source-package-verification"};
  if (n && name[0] == '.')
    return 1;
  if ((n >= 5 && same_word(name, 5, "build")) ||
      (n >= 11 && same_word(name, 11, "cmake-build")))
    return 1;
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    if (same_word(name, n, names[i]))
      return 1;
  return 0;
}
static int reserved_path(const char *path) {
  size_t start = 0, n = strlen(path);
  for (size_t i = 0; i <= n; ++i) {
    if (i == n || path[i] == '/' || path[i] == '\\') {
      if (excluded_component(path + start, i - start))
        return 1;
      start = i + 1;
    }
  }
  return 0;
}
static char *copy_string(const char *text) {
  size_t n = strlen(text) + 1;
  char *copy = malloc(n);
  if (copy)
    memcpy(copy, text, n);
  return copy;
}
void CR_CALL cr_context_options_init(cr_context_options *o) {
  if (!o)
    return;
  memset(o, 0, sizeof(*o));
  o->struct_size = sizeof(*o);
  o->api_version = CR_API_VERSION;
  o->max_records = 512;
  o->max_record_bytes = CR_MAX_INPUT_BYTES;
  o->max_total_bytes = UINT64_C(64) * 1024 * 1024;
}
void CR_CALL cr_query_options_init(cr_query_options *o) {
  if (!o)
    return;
  memset(o, 0, sizeof(*o));
  o->struct_size = sizeof(*o);
  o->api_version = CR_API_VERSION;
  o->max_hits = 4;
  o->kind_mask = 1;
  o->byte_budget = 8192;
  o->excerpt_bytes = 2048;
}
cr_status CR_CALL cr_context_create(const cr_context_options *o,
                                    cr_context **out) {
  if (!o || !out || *out || o->struct_size < sizeof(*o) ||
      o->api_version != CR_API_VERSION || o->reserved || !o->max_records ||
      o->max_records > CR_CONTEXT_MAX_RECORDS || !o->max_record_bytes ||
      o->max_record_bytes > CR_MAX_INPUT_BYTES || !o->max_total_bytes ||
      o->max_total_bytes > SIZE_MAX)
    return CR_INVALID;
  cr_context *c = calloc(1, sizeof(*c));
  if (!c)
    return CR_NOMEM;
  c->records = calloc(o->max_records, sizeof(*c->records));
  if (!c->records) {
    free(c);
    return CR_NOMEM;
  }
  c->limits = *o;
  *out = c;
  return CR_OK;
}
void CR_CALL cr_context_destroy(cr_context *c) {
  if (!c)
    return;
  for (size_t i = 0; i < c->count; ++i) {
    free((void *)c->records[i]->view.path);
    free((void *)c->records[i]->view.attribution);
    free((void *)c->records[i]->view.bytes);
    free(c->records[i]);
  }
  free(c->records);
  free(c);
}
cr_status CR_CALL cr_context_admit(cr_context *c, cr_record_kind kind,
                                   cr_split split, const char *path,
                                   const char *attribution,
                                   const unsigned char *bytes, size_t length,
                                   uint64_t *id) {
  if (!c || kind > CR_ACTIVITY || split > CR_AUDIT || !path || !*path ||
      !attribution || !*attribution || (!bytes && length) ||
      strlen(path) > CR_CONTEXT_MAX_PATH ||
      strlen(attribution) > CR_CONTEXT_MAX_ATTRIBUTION ||
      (split == CR_TRAIN && reserved_path(path)))
    return CR_INVALID;
  if (length > c->limits.max_record_bytes)
    return CR_LIMIT;
  owned_record *previous = NULL;
  for (size_t i = 0; i < c->count; ++i) {
    owned_record *r = c->records[i];
    if (r->view.kind == kind && r->view.split == split &&
        !strcmp(r->view.path, path))
      previous = r;
  }
  if (previous && previous->view.current && previous->view.length == length &&
      !strcmp(previous->view.attribution, attribution) &&
      (!length || !memcmp(previous->view.bytes, bytes, length))) {
    if (id)
      *id = previous->view.id;
    return CR_OK;
  }
  if (c->count == c->limits.max_records ||
      length > c->limits.max_total_bytes - c->total)
    return CR_LIMIT;
  owned_record *r = calloc(1, sizeof(*r));
  if (!r)
    return CR_NOMEM;
  r->view.path = copy_string(path);
  r->view.attribution = copy_string(attribution);
  unsigned char *copy = malloc(length ? length : 1);
  r->view.bytes = copy;
  if (!r->view.path || !r->view.attribution || !copy) {
    free((void *)r->view.path);
    free((void *)r->view.attribution);
    free(copy);
    free(r);
    return CR_NOMEM;
  }
  if (length)
    memcpy(copy, bytes, length);
  cr_status s = cr_encode(copy, length, r->centroid);
  if (s != CR_OK) {
    free((void *)r->view.path);
    free((void *)r->view.attribution);
    free(copy);
    free(r);
    return s;
  }
  r->view.id = c->count + 1;
  r->view.version = previous ? previous->view.version + 1 : 1;
  r->view.kind = kind;
  r->view.split = split;
  r->view.current = 1;
  r->view.length = length;
  cr_hash_bytes(copy, length, r->view.digest);
  if (previous)
    previous->view.current = 0;
  c->records[c->count++] = r;
  c->total += length;
  if (id)
    *id = r->view.id;
  return CR_OK;
}
size_t CR_CALL cr_context_count(const cr_context *c) {
  return c ? c->count : 0;
}
cr_status CR_CALL cr_context_record(const cr_context *c, size_t i,
                                    cr_record_view *out) {
  if (!c || !out)
    return CR_INVALID;
  if (i >= c->count)
    return CR_NOT_FOUND;
  *out = c->records[i]->view;
  return CR_OK;
}
cr_status CR_CALL cr_context_forget_source(cr_context *c, const char *path) {
  if (!c || !path)
    return CR_INVALID;
  int found = 0;
  for (size_t i = 0; i < c->count; ++i)
    if (c->records[i]->view.kind == CR_SOURCE && c->records[i]->view.current &&
        !strcmp(c->records[i]->view.path, path)) {
      c->records[i]->view.current = 0;
      found = 1;
    }
  return found ? CR_OK : CR_NOT_FOUND;
}
static int word_byte(unsigned char b) {
  return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') ||
         (b >= '0' && b <= '9') || b == '_';
}
static int has_word(const unsigned char *b, size_t n, const unsigned char *w,
                    size_t m, size_t *offset) {
  for (size_t i = 0; i < n;) {
    while (i < n && !word_byte(b[i]))
      ++i;
    size_t begin = i;
    while (i < n && word_byte(b[i]))
      ++i;
    if (i - begin == m && !memcmp(b + begin, w, m)) {
      if (offset)
        *offset = begin;
      return 1;
    }
  }
  return 0;
}
static int overlap(const unsigned char *q, size_t n, const cr_record_view *r,
                   size_t *offset) {
  int path_match = 0;
  for (size_t i = 0; i < n;) {
    while (i < n && !word_byte(q[i]))
      ++i;
    size_t begin = i;
    int alphabetic = 0;
    while (i < n && word_byte(q[i])) {
      if ((q[i] >= 'a' && q[i] <= 'z') || (q[i] >= 'A' && q[i] <= 'Z'))
        alphabetic = 1;
      ++i;
    }
    if (i - begin < 3 || !alphabetic)
      continue;
    if (has_word(r->bytes, r->length, q + begin, i - begin, offset))
      return 1;
    if (has_word((const unsigned char *)r->path, strlen(r->path), q + begin,
                 i - begin, NULL))
      path_match = 1;
  }
  if (path_match)
    *offset = 0;
  return path_match;
}
static double similarity(const double *a, const double *b) {
  double dot = 0, aa = 0, bb = 0;
  for (size_t i = 0; i < CR_FEATURES; ++i) {
    dot += a[i] * b[i];
    aa += a[i] * a[i];
    bb += b[i] * b[i];
  }
  if (!aa || !bb)
    return 0;
  return fmax(0, fmin(1, (dot / sqrt(aa * bb) + 1) * 0.5));
}
cr_status CR_CALL cr_context_query(const cr_context *c, const unsigned char *q,
                                   size_t n, const cr_query_options *o,
                                   cr_evidence *out, size_t capacity,
                                   cr_query_report *report) {
  if (!c || (!q && n) || !o || !out || !report || o->struct_size < sizeof(*o) ||
      o->api_version != CR_API_VERSION || !o->max_hits ||
      o->max_hits > CR_CONTEXT_MAX_HITS || !o->kind_mask ||
      (o->kind_mask & ~7u) || !o->excerpt_bytes ||
      o->excerpt_bytes > CR_MAX_INPUT_BYTES || o->byte_budget > SIZE_MAX)
    return CR_INVALID;
  if (capacity < o->max_hits || n > CR_MAX_INPUT_BYTES)
    return CR_LIMIT;
  cr_query_report result = {0};
  if (!n || !o->byte_budget) {
    result.abstained = 1;
    *report = result;
    return CR_NOT_FOUND;
  }
  double encoded[CR_FEATURES];
  cr_status s = cr_encode(q, n, encoded);
  if (s != CR_OK)
    return s;
  cr_evidence hits[CR_CONTEXT_MAX_HITS];
  size_t used = 0;
  for (size_t i = 0; i < c->count; ++i) {
    owned_record *r = c->records[i];
    size_t offset = 0;
    if (!r->view.current || r->view.split != CR_TRAIN || !r->view.length ||
        !(o->kind_mask & (1u << r->view.kind)) ||
        !overlap(q, n, &r->view, &offset))
      continue;
    ++result.matched;
    cr_evidence hit;
    hit.record = r->view;
    hit.score = similarity(encoded, r->centroid);
    size_t margin = (size_t)o->excerpt_bytes / 4;
    hit.offset = offset > margin ? offset - margin : 0;
    hit.length = r->view.length - hit.offset;
    if (hit.length > o->excerpt_bytes)
      hit.length = (size_t)o->excerpt_bytes;
    size_t pos = 0;
    while (pos < used && (hits[pos].score > hit.score ||
                          (hits[pos].score == hit.score &&
                           hits[pos].record.id < hit.record.id)))
      ++pos;
    if (pos >= o->max_hits)
      continue;
    if (used < o->max_hits)
      ++used;
    for (size_t j = used - 1; j > pos; --j)
      hits[j] = hits[j - 1];
    hits[pos] = hit;
  }
  size_t budget = (size_t)o->byte_budget;
  for (size_t i = 0; i < used && budget; ++i) {
    if (hits[i].length > budget)
      hits[i].length = budget;
    out[result.returned++] = hits[i];
    result.excerpt_bytes += hits[i].length;
    budget -= hits[i].length;
  }
  result.omitted = result.matched - result.returned;
  result.abstained = result.returned == 0;
  *report = result;
  return result.returned ? CR_OK : CR_NOT_FOUND;
}
static int evidence_header(char *out, size_t cap, const cr_evidence *e) {
  static const char *const kinds[] = {"SOURCE", "LLM_PROPOSAL", "ACTIVITY"};
  return snprintf(out, cap,
                  "%s %s id=%llu version=%llu offset=%zu length=%zu kind=%u "
                  "attribution=%s sha256=%s score=%.17g\n",
                  kinds[e->record.kind], e->record.path,
                  (unsigned long long)e->record.id,
                  (unsigned long long)e->record.version, e->offset, e->length,
                  (unsigned)e->record.kind, e->record.attribution,
                  e->record.digest, e->score);
}
static int bounded_string(const char *text, size_t maximum) {
  if (!text)
    return 0;
  for (size_t i = 0; i <= maximum; ++i)
    if (!text[i])
      return 1;
  return 0;
}
static int add_size(size_t *total, size_t amount) {
  if (amount > SIZE_MAX - *total)
    return 0;
  *total += amount;
  return 1;
}
cr_status CR_CALL cr_evidence_format(const cr_evidence *e, size_t count,
                                     unsigned char *out, size_t capacity,
                                     size_t *required) {
  if ((!e && count) || !required || count > CR_CONTEXT_MAX_HITS)
    return CR_INVALID;
  size_t total = 1;
  char header[CR_CONTEXT_MAX_PATH + CR_CONTEXT_MAX_ATTRIBUTION + 256];
  for (size_t i = 0; i < count; ++i) {
    if (!bounded_string(e[i].record.path, CR_CONTEXT_MAX_PATH) ||
        !bounded_string(e[i].record.attribution, CR_CONTEXT_MAX_ATTRIBUTION) ||
        e[i].record.kind > CR_ACTIVITY ||
        e[i].record.length > CR_MAX_INPUT_BYTES ||
        e[i].record.digest[64] != 0 ||
        (!e[i].record.bytes && e[i].record.length) ||
        e[i].offset > e[i].record.length ||
        e[i].length > e[i].record.length - e[i].offset || !isfinite(e[i].score))
      return CR_INVALID;
    int n = evidence_header(header, sizeof(header), &e[i]);
    if (n < 0 || (size_t)n >= sizeof(header) || !add_size(&total, (size_t)n) ||
        !add_size(&total, e[i].length) || !add_size(&total, 1))
      return CR_LIMIT;
  }
  *required = total;
  if (!out || capacity < total)
    return CR_LIMIT;
  size_t position = 0;
  for (size_t i = 0; i < count; ++i) {
    int n = evidence_header(header, sizeof(header), &e[i]);
    memcpy(out + position, header, (size_t)n);
    position += (size_t)n;
    if (e[i].length)
      memcpy(out + position, e[i].record.bytes + e[i].offset, e[i].length);
    position += e[i].length;
    out[position++] = '\n';
  }
  out[position] = 0;
  return CR_OK;
}

typedef struct {
  char **paths;
  size_t count, capacity, visited;
  cr_scan_report *report;
} scan_list;
static cr_status path_type(const char *p, int *directory) {
#ifdef _WIN32
  DWORD a = GetFileAttributesA(p);
  if (a == INVALID_FILE_ATTRIBUTES)
    return CR_IO;
  if (a & FILE_ATTRIBUTE_REPARSE_POINT)
    return CR_INVALID;
  *directory = !!(a & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  if (lstat(p, &st))
    return CR_IO;
  if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
    return CR_INVALID;
  *directory = S_ISDIR(st.st_mode);
#endif
  return CR_OK;
}
static cr_status canonical_root(const char *root,
                                char out[CR_CONTEXT_MAX_PATH + 1]) {
  if (!root || !*root)
    return CR_INVALID;
#ifdef _WIN32
  DWORD n = GetFullPathNameA(root, CR_CONTEXT_MAX_PATH + 1, out, NULL);
  if (!n)
    return CR_IO;
  if (n > CR_CONTEXT_MAX_PATH)
    return CR_LIMIT;
  for (size_t i = 0; i < n; ++i)
    if (out[i] == '\\')
      out[i] = '/';
#else
  char raw[CR_CONTEXT_MAX_PATH + 1];
  if (root[0] == '/') {
    if (strlen(root) > CR_CONTEXT_MAX_PATH)
      return CR_LIMIT;
    strcpy(raw, root);
  } else {
    if (!getcwd(raw, sizeof(raw)))
      return CR_IO;
    size_t n = strlen(raw), m = strlen(root);
    if (m + n + 1 > CR_CONTEXT_MAX_PATH)
      return CR_LIMIT;
    raw[n++] = '/';
    memcpy(raw + n, root, m + 1);
  }
  /* Reject links before realpath removes their spelling. */
  for (size_t i = 1; i <= strlen(raw); ++i)
    if (!raw[i] || raw[i] == '/') {
      char saved = raw[i];
      raw[i] = 0;
      int directory = 0;
      cr_status s = path_type(raw, &directory);
      raw[i] = saved;
      if (s != CR_OK)
        return s;
    }
  if (!realpath(raw, out))
    return CR_IO;
#endif
  for (size_t i = 1; i <= strlen(out); ++i)
    if (!out[i] || out[i] == '/') {
      if (i == 2 && out[1] == ':')
        continue;
      char saved = out[i];
      out[i] = 0;
      int directory = 0;
      cr_status s = path_type(out, &directory);
      out[i] = saved;
      if (s != CR_OK || !directory)
        return s == CR_OK ? CR_INVALID : s;
    }
#ifdef _WIN32
  /* Resolve aliases through the directory handle. GetLongPathName requires
   * ancestor enumeration rights even when the caller can read this source. */
  HANDLE handle = CreateFileA(
      out, FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      NULL);
  if (handle == INVALID_HANDLE_VALUE)
    return CR_IO;
  char expanded[CR_CONTEXT_MAX_PATH + 8];
  BY_HANDLE_FILE_INFORMATION info;
  int ordinary = GetFileInformationByHandle(handle, &info) &&
                 (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                 !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
  DWORD expanded_length =
      ordinary
          ? GetFinalPathNameByHandleA(handle, expanded, sizeof(expanded),
                                      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)
          : 0;
  CloseHandle(handle);
  if (!ordinary)
    return CR_INVALID;
  if (!expanded_length)
    return CR_IO;
  if (expanded_length >= sizeof(expanded))
    return CR_LIMIT;
  if (expanded_length < 7 || strncmp(expanded, "\\\\?\\", 4) ||
      expanded[5] != ':')
    return CR_UNSUPPORTED;
  if (expanded_length - 4 > CR_CONTEXT_MAX_PATH)
    return CR_LIMIT;
  for (size_t i = 4; i < expanded_length; ++i)
    if (expanded[i] == '\\')
      expanded[i] = '/';
  memcpy(out, expanded + 4, (size_t)expanded_length - 4 + 1);
#endif
  return reserved_path(out) ? CR_INVALID : CR_OK;
}
static int source_name(const char *p) {
  static const char *const extensions[] = {".c",     ".h",   ".md",
                                           ".cmake", ".txt", ".in"};
  const char *e = strrchr(p, '.');
  if (!e)
    return 0;
  for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
    if (!strcmp(e, extensions[i]))
      return 1;
  return 0;
}
static cr_status scan_directory(scan_list *list, const char *root,
                                unsigned depth);
static cr_status scan_entry(scan_list *list, const char *root, const char *name,
                            unsigned depth) {
  if (++list->visited > 65536)
    return CR_LIMIT;
  if (excluded_component(name, strlen(name))) {
    ++list->report->excluded;
    return CR_OK;
  }
  char path[CR_CONTEXT_MAX_PATH + 1];
  int n = snprintf(path, sizeof(path), "%s/%s", root, name);
  if (n < 0 || (size_t)n >= sizeof(path))
    return CR_LIMIT;
  int directory = 0;
  cr_status s = path_type(path, &directory);
  if (s == CR_INVALID) {
    ++list->report->excluded;
    return CR_OK;
  }
  if (s != CR_OK)
    return s;
  if (directory)
    return scan_directory(list, path, depth + 1);
  if (!source_name(name)) {
    ++list->report->excluded;
    return CR_OK;
  }
  if (list->count == list->capacity)
    return CR_LIMIT;
  list->paths[list->count] = copy_string(path);
  if (!list->paths[list->count])
    return CR_NOMEM;
  ++list->count;
  return CR_OK;
}
static cr_status scan_directory(scan_list *list, const char *root,
                                unsigned depth) {
  if (depth > 64)
    return CR_LIMIT;
  cr_status status = CR_OK;
#ifdef _WIN32
  char pattern[CR_CONTEXT_MAX_PATH + 1];
  int n = snprintf(pattern, sizeof(pattern), "%s/*", root);
  if (n < 0 || (size_t)n >= sizeof(pattern))
    return CR_LIMIT;
  WIN32_FIND_DATAA data;
  HANDLE handle = FindFirstFileA(pattern, &data);
  if (handle == INVALID_HANDLE_VALUE)
    return GetLastError() == ERROR_FILE_NOT_FOUND ? CR_OK : CR_IO;
  do {
    if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, ".."))
      continue;
    status = scan_entry(list, root, data.cFileName, depth);
    if (status != CR_OK)
      break;
  } while (FindNextFileA(handle, &data));
  if (status == CR_OK && GetLastError() != ERROR_NO_MORE_FILES)
    status = CR_IO;
  FindClose(handle);
#else
  DIR *dir = opendir(root);
  if (!dir)
    return CR_IO;
  struct dirent *entry;
  for (;;) {
    errno = 0;
    entry = readdir(dir);
    if (!entry) {
      if (errno)
        status = CR_IO;
      break;
    }
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    status = scan_entry(list, root, entry->d_name, depth);
    if (status != CR_OK)
      break;
  }
  closedir(dir);
#endif
  return status;
}
static int path_compare(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}
cr_status CR_CALL cr_context_scan(cr_context *c, const char *root,
                                  cr_scan_report *report) {
  if (!c || !report)
    return CR_INVALID;
  memset(report, 0, sizeof(*report));
  char canonical[CR_CONTEXT_MAX_PATH + 1];
  cr_status status = canonical_root(root, canonical);
  if (status != CR_OK) {
    report->failed = 1;
    return status;
  }
  scan_list list = {0};
  list.capacity = CR_CONTEXT_MAX_RECORDS;
  list.report = report;
  list.paths = calloc(list.capacity, sizeof(*list.paths));
  if (!list.paths)
    return CR_NOMEM;
  status = scan_directory(&list, canonical, 0);
  if (status != CR_OK)
    ++report->failed;
  qsort(list.paths, list.count, sizeof(*list.paths), path_compare);
  for (size_t i = 0; i < list.count; ++i) {
    int directory = 0;
    cr_status s = path_type(list.paths[i], &directory);
    FILE *file = s == CR_OK && !directory ? fopen(list.paths[i], "rb") : NULL;
    if (!file)
      s = s == CR_OK ? CR_IO : s;
    unsigned char *bytes = NULL;
    size_t length = 0;
    if (file) {
      bytes = malloc((size_t)c->limits.max_record_bytes + 1);
      if (!bytes)
        s = CR_NOMEM;
      else {
        length = fread(bytes, 1, (size_t)c->limits.max_record_bytes + 1, file);
        if (ferror(file))
          s = CR_IO;
        else if (length > c->limits.max_record_bytes)
          s = CR_LIMIT;
      }
      if (fclose(file) && s == CR_OK)
        s = CR_IO;
    }
    size_t previous = c->count;
    if (s == CR_OK)
      s = cr_context_admit(c, CR_SOURCE, CR_TRAIN, list.paths[i],
                           "native-source-scan", bytes, length, NULL);
    free(bytes);
    if (s == CR_OK) {
      if (c->count == previous)
        ++report->unchanged;
      else {
        ++report->admitted;
        report->bytes += length;
      }
    } else {
      ++report->failed;
      if (status == CR_OK)
        status = s;
    }
    free(list.paths[i]);
  }
  free(list.paths);
  return status;
}
