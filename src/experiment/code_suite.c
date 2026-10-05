#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "centroid_algorithms.h"
#include "internal.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "../../data/audit/code_suite_fixtures.h"

#define SUITE_PATH 4096u
#define SUITE_INPUT 65536u
#define SUITE_LOG 1048576u
#define DOMAINS 3u
static const char *const names[DOMAINS] = {"affine", "lower-bound",
                                           "saturation"};
static const char *const affine_sources[C_CODE_ACTIONS] = {
    "c_status c_size_affine(size_t count,size_t stride,size_t extra,size_t "
    "*out){"
    "if(!out){return C_INVALID;}*out=count*stride+extra;return C_OK;}\n",
    "c_status c_size_affine(size_t count,size_t stride,size_t extra,size_t "
    "*out){"
    "if(!out){return C_INVALID;}if(stride && count>SIZE_MAX/stride){return "
    "C_LIMIT;}"
    "*out=count*stride+extra;return C_OK;}\n",
    "c_status c_size_affine(size_t count,size_t stride,size_t extra,size_t "
    "*out){"
    "size_t product=count*stride;if(!out){return C_INVALID;}"
    "if(extra>SIZE_MAX-product){return C_LIMIT;}*out=product+extra;return "
    "C_OK;}\n",
    "c_status c_size_affine(size_t count,size_t stride,size_t extra,size_t "
    "*out){"
    "size_t product;if(!out){return C_INVALID;}"
    "if(stride && count>SIZE_MAX/stride){return C_LIMIT;}product=count*stride;"
    "if(extra>SIZE_MAX-product){return C_LIMIT;}*out=product+extra;return "
    "C_OK;}\n"};
static const char *const lower_sources[C_CODE_ACTIONS] = {
    "size_t c_lower_bound(const int *values,size_t count,int key,size_t "
    "*comparisons){"
    "size_t index=0,operations=0;if(!values && count){"
    "if(comparisons){*comparisons=0;}return SIZE_MAX;}"
    "while(index<count){++operations;if(values[index]>=key){break;}++index;}"
    "if(comparisons){*comparisons=operations;}return index;}\n",
    "size_t c_lower_bound(const int *values,size_t count,int key,size_t "
    "*comparisons){"
    "size_t first=0,last=count,operations=0;if(!values && count){"
    "if(comparisons){*comparisons=0;}return SIZE_MAX;}"
    "while(first<last){size_t middle=first+(last-first)/2;++operations;"
    "if(values[middle]<key){first=middle+1;}else{last=middle;}}"
    "if(comparisons){*comparisons=operations;}return first;}\n",
    "size_t c_lower_bound(const int *values,size_t count,int key,size_t "
    "*comparisons){"
    "size_t first=0,last=count,operations=0;if(!values && count){"
    "if(comparisons){*comparisons=0;}return SIZE_MAX;}"
    "while(first<last){size_t middle=first+(last-first)/2;++operations;"
    "if(values[middle]<=key){first=middle+1;}else{last=middle;}}"
    "if(comparisons){*comparisons=operations;}return first;}\n",
    "size_t c_lower_bound(const int *values,size_t count,int key,size_t "
    "*comparisons){"
    "size_t first=0,last=count,operations=0;if(!values && count){"
    "if(comparisons){*comparisons=0;}return SIZE_MAX;}"
    "while(first<last){size_t middle=first+(last-first)/2;++operations;"
    "if(values[middle]==key){first=middle;break;}"
    "if(values[middle]<key){first=middle+1;}else{last=middle;}}"
    "if(comparisons){*comparisons=operations;}return first;}\n"};
static const char *const sat_sources[C_CODE_ACTIONS] = {
    "uint64_t c_u64_saturating_add(uint64_t left,uint64_t right){return "
    "left+right;}\n",
    "uint64_t c_u64_saturating_add(uint64_t left,uint64_t right){"
    "return left>right?left:right;}\n",
    "uint64_t c_u64_saturating_add(uint64_t left,uint64_t right){"
    "return right>UINT64_MAX-left?UINT64_MAX:left+right;}\n",
    "uint64_t c_u64_saturating_add(uint64_t left,uint64_t right){"
    "return right>=UINT64_MAX-left?0:left+right;}\n"};

typedef struct {
  unsigned failures, rows, family_failures[2], family_rows[2];
  size_t cost, family_cost[2];
  char digest[C_DIGEST_HEX];
} measure;
typedef struct {
  unsigned char input[SUITE_INPUT], fresh[SUITE_INPUT];
  size_t input_length, fresh_length;
  double features[C_FEATURES], fresh_features[C_FEATURES];
  double before[4], after[4], fresh_before[4], fresh_after[4];
  char input_digest[C_DIGEST_HEX], parent_digest[C_DIGEST_HEX];
  measure result[4][3];
  c_status status[4];
  unsigned winner;
} domain;
typedef struct {
  char output[SUITE_PATH], compiler[SUITE_PATH], project[SUITE_PATH];
  char source_path[SUITE_PATH], evaluator_digest[C_DIGEST_HEX];
  char project_parent[C_DIGEST_HEX], winner_digest[C_DIGEST_HEX];
  unsigned char *parent;
  size_t parent_length;
  uint64_t source_id, started;
  unsigned eligible, attempted, verified;
  c_training_report start, finish;
  c_experiment_contact contact;
  domain task[DOMAINS];
} suite;

static c_status join(char out[SUITE_PATH], const char *left,
                     const char *right) {
  int n = snprintf(out, SUITE_PATH, "%s/%s", left, right);
  return n > 0 && (size_t)n < SUITE_PATH ? C_OK : C_LIMIT;
}
static c_status put(const char *directory, const char *name, const void *data,
                    size_t size) {
  char path[SUITE_PATH];
  c_status status = join(path, directory, name);
  return status == C_OK ? c_write_atomic(path, data, size) : status;
}
static c_status new_dir(const char *path) {
#ifdef _WIN32
  return !_mkdir(path) ? C_OK : (errno == EEXIST ? C_INVALID : C_IO);
#else
  return !mkdir(path, 0700) ? C_OK : (errno == EEXIST ? C_INVALID : C_IO);
#endif
}
static c_status absolute(const char *path, char out[SUITE_PATH]) {
#ifdef _WIN32
  DWORD n = GetFullPathNameA(path, SUITE_PATH, out, NULL);
  return n && n < SUITE_PATH ? C_OK : C_IO;
#else
  return realpath(path, out) ? C_OK : C_IO;
#endif
}
/* Every existing ancestor is checked before any target read or replacement. */
static c_status regular_chain(const char *path, int directory) {
  char part[SUITE_PATH];
  size_t length = strlen(path);
  if (!length || length >= sizeof(part))
    return C_INVALID;
  memcpy(part, path, length + 1);
  for (size_t i = 0; i <= length; ++i) {
    if (i && (i == length || part[i] == '/' || part[i] == '\\')) {
      char saved = part[i];
      int final = i == length;
#ifdef _WIN32
      if (i == 2 && part[1] == ':')
        continue;
#endif
      part[i] = 0;
#ifdef _WIN32
      DWORD attr = GetFileAttributesA(part);
      if (attr == INVALID_FILE_ATTRIBUTES ||
          (attr & FILE_ATTRIBUTE_REPARSE_POINT) ||
          ((!!(attr & FILE_ATTRIBUTE_DIRECTORY)) != (!final || directory)))
        return C_IO;
#else
      struct stat info;
      if (lstat(part, &info) ||
          ((!final || directory) ? !S_ISDIR(info.st_mode)
                                 : !S_ISREG(info.st_mode)))
        return C_IO;
#endif
      part[i] = saved;
    }
  }
  return C_OK;
}
static c_status module(unsigned d, unsigned action, char out[8192],
                       size_t *size) {
  const char *aff = affine_sources[d == 0 ? action : 3];
  const char *low = lower_sources[d == 1 ? action : 0];
  const char *sat = sat_sources[d == 2 ? action : 2];
  int n = snprintf(out, 8192, "#include \"centroid_algorithms.h\"\n%s\n%s\n%s",
                   aff, low, sat);
  if (n < 0 || n >= 8192)
    return C_LIMIT;
  *size = (size_t)n;
  return C_OK;
}
/* Production parent must be exactly the declared correct implementation,
 * allowing formatting whitespace. This is a fixed-site catalog, not arbitrary
 * source transformation. There are no string literals except the include. */
static int same_tokens(const unsigned char *a, size_t n, const char *b,
                       size_t m) {
  size_t i = 0, j = 0;
  for (;;) {
    while (i < n && isspace(a[i]))
      ++i;
    while (j < m && isspace((unsigned char)b[j]))
      ++j;
    if (i == n || j == m)
      return i == n && j == m;
    if (a[i++] != (unsigned char)b[j++])
      return 0;
  }
}
static c_status copy_header(suite *s, const char *name) {
  char path[SUITE_PATH], relative[128];
  unsigned char *bytes = NULL;
  size_t size = 0;
  int n = snprintf(relative, sizeof(relative), "include/%s", name);
  if (n < 0 || (size_t)n >= sizeof(relative))
    return C_LIMIT;
  c_status status = join(path, s->project, relative);
  if (status == C_OK)
    status = regular_chain(path, 0);
  if (status == C_OK)
    status = c_read_file(path, &bytes, &size);
  if (status == C_OK)
    status = put(s->output, name, bytes, size);
  free(bytes);
  return status;
}
static c_status append(unsigned char *out, size_t *length, const void *bytes,
                       size_t size) {
  if (size > SUITE_INPUT - *length)
    return C_LIMIT;
  memcpy(out + *length, bytes, size);
  *length += size;
  return C_OK;
}
static c_status record_input(unsigned char *out, size_t *length,
                             const c_record *record, const char *role) {
  char header[384];
  int n = snprintf(
      header, sizeof(header),
      "\nInput role=%s id=%llu version=%llu digest=%s span=[0,%zu) "
      "path-bytes=%zu attribution-bytes=%zu\n",
      role, (unsigned long long)record->id, (unsigned long long)record->version,
      record->digest, record->length, strlen(record->path),
      strlen(record->attribution));
  if (n < 0 || (size_t)n >= sizeof(header))
    return C_LIMIT;
  c_status st = append(out, length, header, (size_t)n);
  if (st == C_OK)
    st = append(out, length, record->path, strlen(record->path));
  if (st == C_OK)
    st = append(out, length, "\n", 1);
  if (st == C_OK)
    st = append(out, length, record->attribution, strlen(record->attribution));
  if (st == C_OK)
    st = append(out, length, "\nExact bytes:\n", 14);
  if (st == C_OK)
    st = append(out, length, record->bytes, record->length);
  return st;
}
static c_status freeze(suite *s, c_trainer *trainer, unsigned d) {
  domain *t = &s->task[d];
  static const char *const requests[] = {
      "Checked allocation size=count*stride+extra. Reject overflow without "
      "output mutation.",
      "Sorted integer lower bound: first index with value >= key; minimize "
      "comparisons.",
      "Unsigned 64-bit addition saturates at maximum, preserving all smaller "
      "exact sums."};
  static const char *const fresh_requests[] = {
      "Independent use: combine record width and object count with a trailing "
      "allocation header.",
      "Independent use: locate earliest sorted dictionary insertion position, "
      "including duplicate values.",
      "Independent use: combine unsigned counters without wrapping near the "
      "representable limit."};
  char catalog[8192], heading[512];
  size_t catalog_size = 0;
  int n =
      snprintf(heading, sizeof(heading),
               "Native C catalog=%s. %s\nFour explicit action alternatives; "
               "measurement required. No answer labels are context.\n",
               names[d], requests[d]);
  if (n < 0 || (size_t)n >= sizeof(heading))
    return C_LIMIT;
  c_status st = append(t->input, &t->input_length, heading, (size_t)n);
  if (st == C_OK)
    st = module(d, 0, catalog, &catalog_size);
  if (st == C_OK) {
    c_hash(catalog, catalog_size, t->parent_digest);
    st = append(t->input, &t->input_length, catalog, catalog_size);
  }
  c_record real_source;
  int found = 0;
  for (size_t i = 0; i < c_context_count(c_trainer_context(trainer)); ++i) {
    st = c_context_record(c_trainer_context(trainer), i, &real_source);
    if (st != C_OK)
      return st;
    if (real_source.id == s->source_id) {
      found = 1;
      break;
    }
  }
  if (!found)
    return C_CORRUPT;
  if (st == C_OK)
    st = record_input(t->input, &t->input_length, &real_source, "SOURCE");
  for (unsigned k = C_LLM_PROPOSAL; k <= C_ACTIVITY && st == C_OK; ++k) {
    c_record r;
    double score;
    c_status got = c_context_retrieve_kind(
        c_trainer_context(trainer), (c_record_kind)k,
        (const unsigned char *)heading, (size_t)n, &r, &score);
    const char *role = k == C_LLM_PROPOSAL ? "LLM_PROPOSAL" : "ACTIVITY";
    if (got == C_OK)
      st = record_input(t->input, &t->input_length, &r, role);
    else if (got == C_NOT_FOUND || got == C_DEFERRED) {
      char unavailable[128];
      int z = snprintf(unavailable, sizeof(unavailable),
                       "\nInput role=%s no supported TRAIN evidence.\n", role);
      st = z > 0 && (size_t)z < sizeof(unavailable)
               ? append(t->input, &t->input_length, unavailable, (size_t)z)
               : C_LIMIT;
    } else
      st = got;
  }
  for (unsigned a = 0; a < 4 && st == C_OK; ++a) {
    st = module(d, a, catalog, &catalog_size);
    if (st == C_OK) {
      int z = snprintf(heading, sizeof(heading),
                       "\nAction=%u complete module:\n", a);
      st = z > 0 && (size_t)z < sizeof(heading)
               ? append(t->input, &t->input_length, heading, (size_t)z)
               : C_LIMIT;
    }
    if (st == C_OK)
      st = append(t->input, &t->input_length, catalog, catalog_size);
  }
  if (st == C_OK)
    st = append(t->fresh, &t->fresh_length, fresh_requests[d],
                strlen(fresh_requests[d]));
  if (st == C_OK)
    st = append(t->fresh, &t->fresh_length, "\nFrozen catalog and evidence:\n",
                30);
  if (st == C_OK)
    st = append(t->fresh, &t->fresh_length, t->input, t->input_length);
  if (st == C_OK) {
    c_hash(t->input, t->input_length, t->input_digest);
    st = c_encode(t->input, t->input_length, t->features);
  }
  if (st == C_OK)
    st = c_encode(t->fresh, t->fresh_length, t->fresh_features);
  double mass[4] = {1, 1, 1, 1};
  if (st == C_OK)
    st = c_model_predict(c_trainer_model(trainer), C_CODE, t->features,
                         s->eligible, mass, t->before, 4);
  if (st == C_OK)
    st = c_model_predict(c_trainer_model(trainer), C_CODE, t->fresh_features,
                         s->eligible, mass, t->fresh_before, 4);
  return st;
}
static int msvc(const char *path) {
  const char *b = path;
  for (const char *p = path; *p; ++p)
    if (*p == '/' || *p == '\\')
      b = p + 1;
  char lower[32];
  const size_t length = strlen(b);
  if (length >= sizeof(lower))
    return 0;
  for (size_t i = 0; i <= length; ++i)
    lower[i] = b[i] >= 'A' && b[i] <= 'Z' ? (char)(b[i] + ('a' - 'A')) : b[i];
  return !strcmp(lower, "cl") || !strcmp(lower, "cl.exe") ||
         !strcmp(lower, "clang-cl") || !strcmp(lower, "clang-cl.exe");
}
static c_status compile(suite *s, const char *dir, const char *source,
                        const char *evaluator, const char *exe) {
  char output[SUITE_PATH], log[SUITE_PATH];
  const char *args[16] = {0};
  if (msvc(s->compiler)) {
    int n = snprintf(output, sizeof(output), "/Fe:%s", exe);
    if (n < 0 || (size_t)n >= sizeof(output))
      return C_LIMIT;
    const char *a[] = {s->compiler, "/nologo", "/std:c11", "/W4",
                       "/WX",       "/O1",     "/I",       s->output,
                       source,      evaluator, output,     NULL};
    for (size_t i = 0; i < sizeof(a) / sizeof(a[0]); ++i)
      args[i] = a[i];
  } else {
    const char *a[] = {s->compiler, "-std=c11", "-Wall",   "-Wextra", "-Werror",
                       "-O1",       "-I",       s->output, source,    evaluator,
                       "-o",        exe,        NULL};
    for (size_t i = 0; i < sizeof(a) / sizeof(a[0]); ++i)
      args[i] = a[i];
  }
  c_process_options opt = {s->compiler, args, dir, 60000, SUITE_LOG};
  c_process_result r = {0};
  c_status st = join(log, dir, "compile.log");
  if (st == C_OK)
    st = c_process_capture_file(&opt, log, &r);
  if (st == C_OK && (r.timed_out || r.exit_code || r.output_truncated))
    st = C_IO;
  c_process_dispose(&r);
  return st;
}
static c_status parse_measure(const unsigned char *data, size_t length,
                              unsigned expected_rows, measure *out) {
  if (!data || memchr(data, 0, length))
    return C_CORRUPT;
  char *copy = malloc(length + 1);
  if (!copy)
    return C_NOMEM;
  memcpy(copy, data, length);
  copy[length] = 0;
  measure result = {0};
  const char *p = copy;
  c_status status = C_OK;
  while (!strncmp(p, "ROW ", 4)) {
    unsigned family, index, fail;
    size_t cost;
    const char *end = strchr(p, '\n');
    if (!end ||
        sscanf(p, "ROW %u %u %u %zu", &family, &index, &fail, &cost) != 4 ||
        family > 1 || index != result.rows || fail > 1 ||
        !c_checked_add(result.cost, cost, &result.cost) ||
        !c_checked_add(result.family_cost[family], cost,
                       &result.family_cost[family])) {
      status = C_CORRUPT;
      break;
    }
    ++result.rows;
    ++result.family_rows[family];
    result.failures += fail;
    result.family_failures[family] += fail;
    p = end + 1;
  }
  unsigned rows, failures;
  size_t cost;
  int consumed = 0;
  if (status == C_OK &&
      (sscanf(p, "SUMMARY failures=%u total=%u cost=%zu%n", &failures, &rows,
              &cost, &consumed) != 3 ||
       rows != expected_rows || rows != result.rows ||
       failures != result.failures || cost != result.cost ||
       (strcmp(p + consumed, "\n") && strcmp(p + consumed, "\r\n")) ||
       (expected_rows == 8 && result.family_rows[1] &&
        (result.family_rows[0] != 4 || result.family_rows[1] != 4))))
    status = C_CORRUPT;
  if (status == C_OK) {
    c_hash(data, length, result.digest);
    *out = result;
  }
  free(copy);
  return status;
}
static c_status evaluate(suite *s, unsigned d, unsigned a, unsigned split,
                         const char *dir, const char *exe) {
  static const char *const splits[] = {"train", "dev", "audit"};
  char dom[16], log[SUITE_PATH], name[32];
  snprintf(dom, sizeof(dom), "%u", d);
  snprintf(name, sizeof(name), "%s.log", splits[split]);
  const char *argv[] = {exe, dom, splits[split], NULL};
  c_process_options opt = {exe, argv, dir, 10000, SUITE_LOG};
  c_process_result r = {0};
  c_status st = join(log, dir, name);
  if (st == C_OK)
    st = c_process_capture_file(&opt, log, &r);
  if (st == C_OK && (r.timed_out || r.output_truncated ||
                     (r.exit_code != 0 && r.exit_code != 1)))
    st = C_IO;
  if (st == C_OK)
    st = parse_measure(r.output, r.length, split == 1 ? 4 : 8,
                       &s->task[d].result[a][split]);
  if (st == C_OK &&
      r.exit_code != (s->task[d].result[a][split].failures ? 1 : 0))
    st = C_CORRUPT;
  c_process_dispose(&r);
  return st;
}
static c_status trial(suite *s, unsigned d, unsigned a) {
  char name[64], dir[SUITE_PATH], source[SUITE_PATH], evaluator[SUITE_PATH],
      exe[SUITE_PATH];
  char bytes[8192];
  size_t size = 0;
  snprintf(name, sizeof(name), "%s/action-%u", names[d], a);
  c_status st = join(dir, s->output, name);
  if (st == C_OK)
    st = new_dir(dir);
  if (st == C_OK)
    st = module(d, a, bytes, &size);
  if (st == C_OK)
    st = put(dir, "candidate.c", bytes, size);
  if (st == C_OK)
    st = join(source, dir, "candidate.c");
  if (st == C_OK)
    st = join(evaluator, s->output, "evaluator.c");
#ifdef _WIN32
  if (st == C_OK)
    st = join(exe, dir, "evaluate.exe");
#else
  if (st == C_OK)
    st = join(exe, dir, "evaluate");
#endif
  if (st == C_OK)
    st = compile(s, dir, source, evaluator, exe);
  /* Freeze fitting before revealing evaluation: all TRAIN results are obtained
   * first by caller; DEV/AUDIT run only after receipts consumed. */
  if (st == C_OK)
    st = evaluate(s, d, a, 0, dir, exe);
  return st;
}
static int better(const measure *a, const measure *b) {
  return a->failures < b->failures ||
         (a->failures == b->failures && a->cost < b->cost);
}
static c_status train(suite *s, c_trainer *trainer) {
  char receipts[8192];
  size_t used = 0;
  for (unsigned d = 0; d < DOMAINS; ++d) {
    domain *t = &s->task[d];
    for (unsigned a = 1; a < 4; ++a)
      if (better(&t->result[a][0], &t->result[t->winner][0]))
        t->winner = a;
    for (unsigned f = 0; f < 2; ++f) {
      char binding[1024], digest[C_DIGEST_HEX];
      int n = snprintf(
          binding, sizeof(binding),
          "TRAIN catalog=%s family=%u target=%u parent=%s "
          "evaluator=%s input=%s log=%s "
          "failures=%u rows=%u cost=%zu model-parent=%s contact=%s",
          names[d], f, t->winner, t->parent_digest, s->evaluator_digest,
          t->input_digest, t->result[t->winner][0].digest,
          t->result[t->winner][0].family_failures[f],
          t->result[t->winner][0].family_rows[f],
          t->result[t->winner][0].family_cost[f], s->contact.checkpoint_digest,
          s->contact.proof_digest);
      if (n < 0 || (size_t)n >= sizeof(binding))
        return C_LIMIT;
      c_hash(binding, (size_t)n, digest);
      int k = snprintf(receipts + used, sizeof(receipts) - used,
                       "receipt=%s %s\n", digest, binding);
      if (k < 0 || (size_t)k >= sizeof(receipts) - used)
        return C_LIMIT;
      used += (size_t)k;
      c_status st = c_trainer_enqueue_measurement(
          trainer, t->input, t->input_length, t->winner, s->eligible,
          t->parent_digest, s->evaluator_digest, digest);
      if (st != C_OK)
        return st;
    }
  }
  c_status st = put(s->output, "train-receipts.txt", receipts, used);
  if (st == C_OK)
    st = c_trainer_step(trainer, 512, &s->finish);
  if (st == C_OK &&
      (s->finish.updates - s->start.updates != 6 || s->finish.queued))
    st = C_DEFERRED;
  double mass[4] = {1, 1, 1, 1};
  for (unsigned d = 0; d < DOMAINS && st == C_OK; ++d) {
    domain *t = &s->task[d];
    st = c_model_predict(c_trainer_model(trainer), C_CODE, t->features,
                         s->eligible, mass, t->after, 4);
    if (st == C_OK)
      st = c_model_predict(c_trainer_model(trainer), C_CODE, t->fresh_features,
                           s->eligible, mass, t->fresh_after, 4);
  }
  return st;
}
static unsigned choice(const double p[4]) {
  unsigned result = 0;
  for (unsigned a = 1; a < 4; ++a)
    if (p[a] > p[result])
      result = a;
  return result;
}
static c_status quarantine(suite *s, c_trainer *trainer) {
  char path[SUITE_PATH], name[128];
  c_status st = join(path, s->output, "evaluator.c");
  if (st == C_OK)
    st = c_context_admit_file(
        c_trainer_context(trainer), C_AUDIT, C_HOLDOUT, path,
        "three-domain independent evaluator labels; never retrieval or fitting",
        NULL);
  for (unsigned d = 0; d < DOMAINS && st == C_OK; ++d)
    for (unsigned a = 0; a < 4 && st == C_OK; ++a)
      for (unsigned split = 0; split < 3 && st == C_OK; ++split) {
        static const char *const kinds[] = {"train", "dev", "audit"};
        snprintf(name, sizeof(name), "%s/action-%u/%s.log", names[d], a,
                 kinds[split]);
        st = join(path, s->output, name);
        if (st == C_OK)
          st = c_context_admit_file(
              c_trainer_context(trainer),
              split == 0 ? C_TRAIN_MEASUREMENT
                         : (split == 1 ? C_DEVELOPMENT : C_AUDIT),
              split == 0 ? C_TRAIN : (split == 1 ? C_DEV : C_HOLDOUT), path,
              "independent native case measurement; split-authorized only",
              NULL);
      }
  return st;
}
static c_status artifact_digest(const char *directory, const char *name,
                                char digest[C_DIGEST_HEX]) {
  char path[SUITE_PATH];
  unsigned char *bytes = NULL;
  size_t length = 0;
  c_status status = join(path, directory, name);
  if (status == C_OK)
    status = c_read_file(path, &bytes, &length);
  if (status == C_OK)
    c_hash(bytes, length, digest);
  free(bytes);
  return status;
}
static c_status publish(suite *s, c_trainer *trainer, c_status failure) {
  char report[32768], path[SUITE_PATH], winner[8192];
  size_t used = 0, size = 0;
  if (failure != C_OK) {
    int failed_length = snprintf(
        report, sizeof(report),
        "# Incomplete three-domain native code suite\n\n"
        "Infrastructure status: %s. Attempted %u/12; verified TRAIN candidates "
        "%u/12. No accepted patch is published. A compiler/measurement failure "
        "does not provide a target. Retain every source and complete raw "
        "log.\n\n"
        "Retained pretrial model-parent checkpoint SHA256: `%s`. Physical "
        "contact proof SHA256: `%s`. Any attempted tool trial requires both "
        "artifacts.\n\n"
        "Actual owned updates=%llu; pending=%llu.\n\n",
        c_status_string(failure), s->attempted, s->verified,
        s->contact.checkpoint_digest, s->contact.proof_digest,
        (unsigned long long)(s->finish.updates - s->start.updates),
        (unsigned long long)s->finish.queued);
    if (failed_length < 0 || (size_t)failed_length >= sizeof(report))
      return C_LIMIT;
    used = (size_t)failed_length;
    for (unsigned d = 0; d < DOMAINS; ++d)
      for (unsigned a = 0; a < 4; ++a) {
        failed_length =
            snprintf(report + used, sizeof(report) - used,
                     "%s action=%u status=%s observed-TRAIN-rows=%u\n",
                     names[d], a, c_status_string(s->task[d].status[a]),
                     s->task[d].result[a][0].rows);
        if (failed_length < 0 || (size_t)failed_length >= sizeof(report) - used)
          return C_LIMIT;
        used += (size_t)failed_length;
      }
    c_status saved = put(s->output, "report.md", report, used);
    if (saved == C_OK && s->finish.updates != s->start.updates) {
      saved = join(path, s->output, "memory.centroid");
      if (saved == C_OK)
        saved = c_trainer_save(trainer, path);
    }
    return saved == C_OK ? failure : saved;
  }
  int n = snprintf(
      report, sizeof(report),
      "# Three-domain independently verified native C suite\n\n"
      "Status: %s. Attempted %u/12; verified TRAIN candidates %u/12. "
      "Compiler: `%s`. Elapsed before publication: %llu ms.\n\n"
      "Project parent: `%s`. Evaluator: `%s`. The SOURCE role is the exact "
      "current declared project file, never an earlier experiment winner.\n\n"
      "Frozen pretrial model-parent checkpoint SHA256: `%s`. Physical contact "
      "proof SHA256: `%s`; generation=%llu eligible=%u participants=%u "
      "contact-graph=%u. Both immutable artifacts precede every tool trial and "
      "are bound into every TRAIN receipt.\n\n"
      "TRAIN targets are selected from aggregate independent TRAIN outcomes "
      "only; "
      "each of two family receipts records that family's support for the "
      "selected action. "
      "DEV and AUDIT were opened only after all six contact updates and cannot "
      "fit the model.\n\n"
      "| Catalog | Action | TRAIN failures/8 | TRAIN comparisons | DEV "
      "failures/4 | AUDIT failures/8 |\n"
      "| --- | --- | --- | --- | --- | --- |\n",
      c_status_string(failure), s->attempted, s->verified, s->compiler,
      (unsigned long long)(c_monotonic_ms() - s->started), s->project_parent,
      s->evaluator_digest, s->contact.checkpoint_digest,
      s->contact.proof_digest, (unsigned long long)s->contact.generation,
      s->contact.eligible, s->contact.participants, s->contact.graph);
  if (n < 0 || (size_t)n >= sizeof(report))
    return C_LIMIT;
  used = (size_t)n;
  for (unsigned d = 0; d < DOMAINS; ++d)
    for (unsigned a = 0; a < 4; ++a) {
      domain *t = &s->task[d];
      n = snprintf(report + used, sizeof(report) - used,
                   "| %s | %u | %u | %zu | %u | %u |\n", names[d], a,
                   t->result[a][0].failures, t->result[a][0].cost,
                   t->result[a][1].failures, t->result[a][2].failures);
      if (n < 0 || (size_t)n >= sizeof(report) - used)
        return C_LIMIT;
      used += (size_t)n;
    }
  int accepted = failure == C_OK;
  for (unsigned d = 0; d < DOMAINS; ++d) {
    domain *t = &s->task[d];
    accepted = accepted && !t->result[t->winner][1].failures &&
               !t->result[t->winner][2].failures;
    unsigned b = choice(t->fresh_before), a = choice(t->fresh_after);
    n = snprintf(
        report + used, sizeof(report) - used,
        "\n%s selected action=%u; TRAIN winner probability %.12g -> %.12g, "
        "cross entropy %.12g -> %.12g. Frozen independent-use input choice %u "
        "-> %u; "
        "winner probability %.12g -> %.12g; AUDIT failures of model choice %u "
        "-> %u. "
        "All fresh-input distributions were frozen before AUDIT consumption.\n",
        names[d], t->winner, t->before[t->winner], t->after[t->winner],
        -log(t->before[t->winner]), -log(t->after[t->winner]), b, a,
        t->fresh_before[t->winner], t->fresh_after[t->winner],
        t->result[b][2].failures, t->result[a][2].failures);
    if (n < 0 || (size_t)n >= sizeof(report) - used)
      return C_LIMIT;
    used += (size_t)n;
  }
  accepted = accepted && s->task[1].winner == 1 &&
             s->task[1].result[1][0].cost < s->task[1].result[0][0].cost;
  n = snprintf(report + used, sizeof(report) - used,
               "\nLife generations %llu -> %llu; authentic contacts=%llu; "
               "owned updates=%llu; "
               "completed=%llu; pending=%llu. Publication gate: %s. "
               "This demonstrates three authored finite C contracts and exact "
               "comparison-cost "
               "improvement, not general code generation or a wall-clock "
               "speedup. Fresh AUDIT "
               "families were not used in any prior range experiment. The "
               "independent-use "
               "model input is a new request over the same catalog, not a new "
               "unseen action language. "
               "Inspect every candidate, compile/train/dev/audit log, "
               "receipts, input.bin and "
               "generalization-input.bin. Applying accepted winner.c is a "
               "separate explicit action.\n",
               (unsigned long long)s->start.generation,
               (unsigned long long)s->finish.generation,
               (unsigned long long)(s->finish.contacts - s->start.contacts),
               (unsigned long long)(s->finish.updates - s->start.updates),
               (unsigned long long)(s->finish.completed - s->start.completed),
               (unsigned long long)s->finish.queued,
               accepted ? "accepted" : "deferred");
  if (n < 0 || (size_t)n >= sizeof(report) - used)
    return C_LIMIT;
  used += (size_t)n;
  c_status st = put(s->output, "report.md", report, used);
  if (st == C_OK && failure == C_OK) {
    st = join(path, s->output, "memory.centroid");
    if (st == C_OK)
      st = c_trainer_save(trainer, path);
  }
  if (st == C_OK) {
    char predictions[8192];
    size_t length = 0;
    static const char heading[] =
        "catalog\tinput\taction\tbefore\tafter\tTRAIN-selected-target\n";
    memcpy(predictions, heading, sizeof(heading) - 1);
    length = sizeof(heading) - 1;
    for (unsigned d = 0; d < DOMAINS; ++d)
      for (unsigned kind = 0; kind < 2; ++kind)
        for (unsigned a = 0; a < 4; ++a) {
          domain *t = &s->task[d];
          n = snprintf(predictions + length, sizeof(predictions) - length,
                       "%s\t%s\t%u\t%.17g\t%.17g\t%u\n", names[d],
                       kind ? "independent-use" : "TRAIN-request", a,
                       kind ? t->fresh_before[a] : t->before[a],
                       kind ? t->fresh_after[a] : t->after[a], t->winner);
          if (n < 0 || (size_t)n >= sizeof(predictions) - length)
            return C_LIMIT;
          length += (size_t)n;
        }
    st = put(s->output, "predictions.tsv", predictions, length);
  }
  if (st == C_OK && accepted)
    st = module(1, 1, winner, &size);
  if (st == C_OK && accepted) {
    c_hash(winner, size, s->winner_digest);
    st = put(s->output, "winner.c", winner, size);
  }
  if (st == C_OK && accepted) {
    char manifest[4096], report_digest[C_DIGEST_HEX],
        accepted_digest[C_DIGEST_HEX];
    char compiled[C_DIGEST_HEX], executable[C_DIGEST_HEX], header[C_DIGEST_HEX],
        api[C_DIGEST_HEX];
    st = artifact_digest(s->output, "lower-bound/action-1/compile.log",
                         compiled);
#ifdef _WIN32
    const char *executable_name = "lower-bound/action-1/evaluate.exe";
#else
    const char *executable_name = "lower-bound/action-1/evaluate";
#endif
    if (st == C_OK)
      st = artifact_digest(s->output, executable_name, executable);
    if (st == C_OK)
      st = artifact_digest(s->output, "centroid_algorithms.h", header);
    if (st == C_OK)
      st = artifact_digest(s->output, "centroid.h", api);
    if (st != C_OK)
      return st;
    c_hash(report, used, report_digest);
    n = snprintf(manifest, sizeof(manifest),
                 "centroid-fixed-patch/v1\ntarget=src/domain/"
                 "algorithms.c\nparent=%s\nwinner=%s\n"
                 "evaluator=%s\nreport=%s\ntrain=%s\ndev=%s\naudit=%s\n"
                 "compile=%s\nexecutable=%s\nheader=%s\napi=%s\n"
                 "executable-path=%s\ncompiled-exit=0\n"
                 "train-cost=%zu\nbaseline-cost=%zu\n",
                 s->project_parent, s->winner_digest, s->evaluator_digest,
                 report_digest, s->task[1].result[1][0].digest,
                 s->task[1].result[1][1].digest, s->task[1].result[1][2].digest,
                 compiled, executable, header, api, executable_name,
                 s->task[1].result[1][0].cost, s->task[1].result[0][0].cost);
    if (n < 0 || (size_t)n >= sizeof(manifest))
      return C_LIMIT;
    st = put(s->output, "patch.manifest", manifest, (size_t)n);
    c_hash(manifest, (size_t)n, accepted_digest);
    if (st == C_OK)
      st = put(s->output, "accepted.sha256", accepted_digest, 64);
  }
  if (st != C_OK)
    return st;
  return failure != C_OK ? failure : (accepted ? C_OK : C_DEFERRED);
}
c_status c_code_suite_run(c_trainer *trainer, const char *compiler,
                          const char *project_root,
                          const char *new_output_directory) {
  if (!trainer || !compiler || !*compiler || !project_root || !*project_root ||
      !new_output_directory || !*new_output_directory ||
      strlen(compiler) >= SUITE_PATH)
    return C_INVALID;
  const unsigned groups = c_model_group_count(c_trainer_model(trainer));
  if (groups < 2u || groups > C_MAX_GROUPS)
    return C_INVALID;
  c_experiment_contact contact;
  c_status st =
      c_experiment_contact_preflight(trainer, (1u << groups) - 1u, &contact);
  if (st != C_OK)
    return st;
  suite *s = calloc(1, sizeof(*s));
  if (!s)
    return C_NOMEM;
  s->started = c_monotonic_ms();
  s->contact = contact;
  for (unsigned d = 0; d < DOMAINS; ++d)
    for (unsigned a = 0; a < 4; ++a)
      s->task[d].status[a] = C_DEFERRED;
  st = c_trainer_report(trainer, &s->start);
  s->finish = s->start;
  if (st == C_OK && s->start.queued)
    st = C_DEFERRED;
  if (st == C_OK)
    st = regular_chain(project_root, 1);
  if (st == C_OK)
    st = absolute(project_root, s->project);
  if (st == C_OK)
    st = regular_chain(s->project, 1);
  if (st == C_OK)
    st = join(s->source_path, s->project, "src/domain/algorithms.c");
  if (st == C_OK)
    st = regular_chain(s->source_path, 0);
  if (st == C_OK)
    st = c_read_file(s->source_path, &s->parent, &s->parent_length);
  char parent[8192];
  size_t parent_size = 0;
  if (st == C_OK)
    st = module(1, 0, parent, &parent_size);
  if (st == C_OK &&
      !same_tokens(s->parent, s->parent_length, parent, parent_size))
    st = C_INVALID;
  if (st == C_OK) {
    c_hash(s->parent, s->parent_length, s->project_parent);
    c_hash(code_suite_evaluator, sizeof(code_suite_evaluator) - 1,
           s->evaluator_digest);
  }
  if (st == C_OK) {
    if (strchr(compiler, '/') || strchr(compiler, '\\'))
      st = absolute(compiler, s->compiler);
    else
      memcpy(s->compiler, compiler, strlen(compiler) + 1);
  }
  int created = 0;
  if (st == C_OK) {
    st = new_dir(new_output_directory);
    created = st == C_OK;
  }
  if (st == C_OK)
    st = regular_chain(new_output_directory, 1);
  if (st == C_OK)
    st = absolute(new_output_directory, s->output);
  if (st == C_OK)
    st = regular_chain(s->output, 1);
  if (st == C_OK)
    st = put(s->output, "parent.c", s->parent, s->parent_length);
  if (st == C_OK)
    st = put(s->output, "evaluator.c", code_suite_evaluator,
             sizeof(code_suite_evaluator) - 1);
  if (st == C_OK)
    st = copy_header(s, "centroid.h");
  if (st == C_OK)
    st = copy_header(s, "centroid_algorithms.h");
  if (st == C_OK)
    st = c_context_admit(
        c_trainer_context(trainer), C_SOURCE, C_TRAIN, s->source_path,
        "exact current declared production source for finite native code suite",
        s->parent, s->parent_length, &s->source_id);
  s->eligible = (1u << c_model_group_count(c_trainer_model(trainer))) - 1u;
  for (unsigned d = 0; d < DOMAINS && st == C_OK; ++d) {
    char dir[SUITE_PATH];
    st = join(dir, s->output, names[d]);
    if (st == C_OK)
      st = new_dir(dir);
    if (st == C_OK)
      st = freeze(s, trainer, d);
    if (st == C_OK)
      st = put(dir, "input.bin", s->task[d].input, s->task[d].input_length);
    if (st == C_OK)
      st = put(dir, "generalization-input.bin", s->task[d].fresh,
               s->task[d].fresh_length);
  }
  if (st == C_OK)
    st = c_experiment_contact_snapshot(trainer, s->output, &s->contact);
  if (st == C_OK) {
    c_status first = C_OK;
    for (unsigned d = 0; d < DOMAINS; ++d)
      for (unsigned a = 0; a < 4; ++a) {
        ++s->attempted;
        s->task[d].status[a] = trial(s, d, a);
        if (s->task[d].status[a] == C_OK)
          ++s->verified;
        else if (first == C_OK)
          first = s->task[d].status[a];
      }
    st = first;
  }
  if (st == C_OK)
    st = train(s, trainer);
  for (unsigned d = 0; d < DOMAINS && st == C_OK; ++d)
    for (unsigned a = 0; a < 4 && st == C_OK; ++a) {
      char name[64], dir[SUITE_PATH], exe[SUITE_PATH];
      snprintf(name, sizeof(name), "%s/action-%u", names[d], a);
      st = join(dir, s->output, name);
#ifdef _WIN32
      if (st == C_OK)
        st = join(exe, dir, "evaluate.exe");
#else
      if (st == C_OK)
        st = join(exe, dir, "evaluate");
#endif
      for (unsigned split = 1; split < 3 && st == C_OK; ++split)
        st = evaluate(s, d, a, split, dir, exe);
    }
  if (st == C_OK)
    st = quarantine(s, trainer);
  if (created && c_trainer_report(trainer, &s->finish) != C_OK)
    st = C_CORRUPT;
  if (created && s->output[0])
    st = publish(s, trainer, st);
  free(s->parent);
  free(s);
  return st;
}

static c_status read_artifact(const char *dir, const char *name,
                              unsigned char **out, size_t *length) {
  char path[SUITE_PATH];
  c_status st = join(path, dir, name);
  if (st == C_OK)
    st = regular_chain(path, 0);
  return st == C_OK ? c_read_file(path, out, length) : st;
}
static c_status verify_digest_file(const char *dir, const char *name,
                                   const char *digest) {
  unsigned char *bytes = NULL;
  size_t length = 0;
  c_status st = read_artifact(dir, name, &bytes, &length);
  char actual[C_DIGEST_HEX];
  if (st == C_OK) {
    c_hash(bytes, length, actual);
    if (strcmp(actual, digest))
      st = C_CORRUPT;
  }
  free(bytes);
  return st;
}
c_status c_patch_apply(const char *artifact_directory, const char *project_root,
                       const char *expected_parent_digest) {
  if (!artifact_directory || !project_root || !expected_parent_digest ||
      strlen(expected_parent_digest) != 64)
    return C_INVALID;
  char artifacts[SUITE_PATH], root[SUITE_PATH], target[SUITE_PATH];
  c_status st = regular_chain(artifact_directory, 1);
  if (st == C_OK)
    st = absolute(artifact_directory, artifacts);
  if (st == C_OK)
    st = regular_chain(artifacts, 1);
  if (st == C_OK)
    st = regular_chain(project_root, 1);
  if (st == C_OK)
    st = absolute(project_root, root);
  if (st == C_OK)
    st = regular_chain(root, 1);
  if (st == C_OK)
    st = join(target, root, "src/domain/algorithms.c");
  if (st == C_OK)
    st = regular_chain(target, 0);
  unsigned char *manifest = NULL, *accepted = NULL, *winner = NULL,
                *parent = NULL;
  size_t manifest_size = 0, accepted_size = 0, winner_size = 0, parent_size = 0;
  if (st == C_OK)
    st = read_artifact(artifacts, "patch.manifest", &manifest, &manifest_size);
  if (st == C_OK)
    st = read_artifact(artifacts, "accepted.sha256", &accepted, &accepted_size);
  char hash[C_DIGEST_HEX], p[C_DIGEST_HEX], w[C_DIGEST_HEX], e[C_DIGEST_HEX],
      r[C_DIGEST_HEX];
  char tr[C_DIGEST_HEX], dev[C_DIGEST_HEX], audit[C_DIGEST_HEX],
      destination[128], compiled[C_DIGEST_HEX], executable[C_DIGEST_HEX],
      header[C_DIGEST_HEX], api[C_DIGEST_HEX], executable_name[128];
  size_t cost = 0, baseline = 0;
  int consumed = 0;
  if (st == C_OK) {
    c_hash(manifest, manifest_size, hash);
    if (accepted_size != 64 || memcmp(accepted, hash, 64) ||
        memchr(manifest, 0, manifest_size))
      st = C_CORRUPT;
  }
  if (st == C_OK) {
    unsigned char *terminated = realloc(manifest, manifest_size + 1);
    if (!terminated)
      st = C_NOMEM;
    else {
      manifest = terminated;
      manifest[manifest_size] = 0;
      if (sscanf(
              (const char *)manifest,
              "centroid-fixed-patch/"
              "v1\ntarget=%127[^\n]\nparent=%64s\nwinner=%64s\n"
              "evaluator=%64s\nreport=%64s\ntrain=%64s\ndev=%64s\naudit=%64s\n"
              "compile=%64s\nexecutable=%64s\nheader=%64s\napi=%64s\n"
              "executable-path=%127[^\n]\ncompiled-exit=0\n"
              "train-cost=%zu\nbaseline-cost=%zu\n%n",
              destination, p, w, e, r, tr, dev, audit, compiled, executable,
              header, api, executable_name, &cost, &baseline,
              &consumed) != 15 ||
          (size_t)consumed != manifest_size ||
          strcmp(destination, "src/domain/algorithms.c") ||
          strcmp(p, expected_parent_digest) || cost >= baseline ||
          (strcmp(executable_name, "lower-bound/action-1/evaluate.exe") &&
           strcmp(executable_name, "lower-bound/action-1/evaluate")))
        st = C_CORRUPT;
    }
  }
  if (st == C_OK)
    st = verify_digest_file(artifacts, "parent.c", p);
  if (st == C_OK)
    st = verify_digest_file(artifacts, "evaluator.c", e);
  if (st == C_OK) {
    c_hash(code_suite_evaluator, sizeof(code_suite_evaluator) - 1, hash);
    if (strcmp(hash, e))
      st = C_CORRUPT;
  }
  if (st == C_OK)
    st = verify_digest_file(artifacts, "report.md", r);
  if (st == C_OK)
    st = verify_digest_file(artifacts, "lower-bound/action-1/compile.log",
                            compiled);
  if (st == C_OK)
    st = verify_digest_file(artifacts, executable_name, executable);
  if (st == C_OK)
    st = verify_digest_file(artifacts, "centroid_algorithms.h", header);
  if (st == C_OK)
    st = verify_digest_file(artifacts, "centroid.h", api);
  if (st == C_OK)
    st = verify_digest_file(artifacts, "lower-bound/action-1/candidate.c", w);
  static const char *const logs[] = {"lower-bound/action-1/train.log",
                                     "lower-bound/action-1/dev.log",
                                     "lower-bound/action-1/audit.log"};
  const char *digests[] = {tr, dev, audit};
  for (unsigned i = 0; i < 3 && st == C_OK; ++i) {
    st = verify_digest_file(artifacts, logs[i], digests[i]);
    unsigned char *data = NULL;
    size_t size = 0;
    measure result = {0};
    if (st == C_OK)
      st = read_artifact(artifacts, logs[i], &data, &size);
    if (st == C_OK)
      st = parse_measure(data, size, i == 1 ? 4 : 8, &result);
    if (st == C_OK && (result.failures || (i == 0 && result.cost != cost)))
      st = C_CORRUPT;
    free(data);
  }
  if (st == C_OK)
    st = read_artifact(artifacts, "winner.c", &winner, &winner_size);
  char canonical[8192];
  size_t canonical_size = 0;
  if (st == C_OK)
    st = module(1, 1, canonical, &canonical_size);
  if (st == C_OK) {
    c_hash(winner, winner_size, hash);
    if (strcmp(hash, w) || winner_size != canonical_size ||
        memcmp(winner, canonical, winner_size))
      st = C_CORRUPT;
  }
  if (st == C_OK)
    st = c_read_file(target, &parent, &parent_size);
  if (st == C_OK) {
    c_hash(parent, parent_size, hash);
    if (strcmp(hash, p))
      st = C_INVALID;
  }
  if (st == C_OK)
    st = module(1, 0, canonical, &canonical_size);
  if (st == C_OK &&
      !same_tokens(parent, parent_size, canonical, canonical_size))
    st = C_INVALID;
  if (st == C_OK) {
    /* The immutable artifact's parent.c is a complete rollback source. Check
     * lineage once again immediately before the same-directory atomic write. */
    unsigned char *current = NULL;
    size_t size = 0;
    st = regular_chain(target, 0);
    if (st == C_OK)
      st = c_read_file(target, &current, &size);
    if (st == C_OK && (size != parent_size || memcmp(current, parent, size)))
      st = C_INVALID;
    free(current);
    /* A winner was compiled against these exact contracts. Matching the target
     * module alone cannot authorize publication after a live header change. */
    if (st == C_OK)
      st = verify_digest_file(root, "include/centroid_algorithms.h", header);
    if (st == C_OK)
      st = verify_digest_file(root, "include/centroid.h", api);
    if (st == C_OK)
      st = c_write_atomic(target, winner, winner_size);
  }
  free(manifest);
  free(accepted);
  free(winner);
  free(parent);
  return st;
}
