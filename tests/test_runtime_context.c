#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include "centroid_context.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>

#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "context %d: %s\n", __LINE__, #x);                       \
      return 1;                                                                \
    }                                                                          \
  } while (0)

#define MEASURE_RECORDS 64u
#define MEASURE_QUERIES 16u
#define MEASURE_REPEATS 100u
static uint64_t monotonic_ns(void) {
#ifdef _WIN32
  LARGE_INTEGER ticks, frequency;
  if (!QueryPerformanceCounter(&ticks) ||
      !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    return 0u;
  uint64_t tick = (uint64_t)ticks.QuadPart, rate = (uint64_t)frequency.QuadPart;
  return (tick / rate) * UINT64_C(1000000000) +
         ((tick % rate) * UINT64_C(1000000000)) / rate;
#else
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time))
    return 0u;
  return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
#endif
}
static uint64_t peak_memory(void) {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS memory;
  memset(&memory, 0, sizeof(memory));
  memory.cb = sizeof(memory);
  return K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))
             ? (uint64_t)memory.PeakWorkingSetSize
             : 0u;
#else
  struct rusage usage;
  return getrusage(RUSAGE_SELF, &usage) == 0 ? (uint64_t)usage.ru_maxrss * 1024u
                                             : 0u;
#endif
}
static int compare_time(const void *a, const void *b) {
  uint64_t left = *(const uint64_t *)a, right = *(const uint64_t *)b;
  return left > right ? 1 : left < right ? -1 : 0;
}
static int measure_context(void) {
  char content[MEASURE_RECORDS][128], paths[MEASURE_RECORDS][64];
  static const char attribution[] = "native-M4-fixture";
  static const unsigned positive[] = {0u, 7u, 15u, 23u, 31u, 39u, 47u, 63u};
  static const char *const negative[] = {
      "missing_function", "", "7", "...?!", "dev_only_symbol",
      "audit_only_symbol"};
  cr_context_options limits;
  cr_context_options_init(&limits);
  limits.max_records = 128u;
  limits.max_record_bytes = 1024u;
  limits.max_total_bytes = 128u * 1024u;
  cr_context *context = NULL, *independent = NULL;
  CHECK(cr_context_create(&limits, &context) == CR_OK);
  CHECK(cr_context_create(&limits, &independent) == CR_OK);
  uint64_t raw_bytes = 0u, provenance_bytes = 0u;
  for (unsigned i = 0u; i < MEASURE_RECORDS; ++i) {
    int bytes = snprintf(content[i], sizeof(content[i]),
                         "int sdk_function_%02u(int shared_marker) { return "
                         "shared_marker + %u; }\n",
                         i, i);
    int named =
        snprintf(paths[i], sizeof(paths[i]), "src/m4_fixture_%02u.c", i);
    CHECK(bytes > 0 && (size_t)bytes < sizeof(content[i]) && named > 0 &&
          (size_t)named < sizeof(paths[i]));
    CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, paths[i], attribution,
                           (const unsigned char *)content[i], (size_t)bytes,
                           NULL) == CR_OK);
    raw_bytes += (uint64_t)bytes;
    provenance_bytes += strlen(paths[i]) + 1u + sizeof(attribution);
  }
  static const unsigned char dev[] = "int dev_only_symbol(void);";
  static const unsigned char audit[] = "int audit_only_symbol(void);";
  /* Quarantine-only negative controls are never TRAIN records or target labels.
   */
  CHECK(cr_context_admit(context, CR_SOURCE, CR_DEV, "dev/m4.c",
                         "quarantine-control", dev, sizeof(dev),
                         NULL) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_AUDIT, "audit/m4.c",
                         "quarantine-control", audit, sizeof(audit),
                         NULL) == CR_OK);
  raw_bytes += sizeof(dev) + sizeof(audit);
  provenance_bytes += sizeof("dev/m4.c") + sizeof("audit/m4.c") +
                      2u * sizeof("quarantine-control");
  static const unsigned char private_source[] =
      "int private_only_symbol(void) { return 9; }";
  CHECK(cr_context_admit(independent, CR_SOURCE, CR_TRAIN, paths[0],
                         "independent-fixture", private_source,
                         sizeof(private_source), NULL) == CR_OK);
  raw_bytes += sizeof(private_source);
  provenance_bytes += strlen(paths[0]) + 1u + sizeof("independent-fixture");
  char query_text[MEASURE_QUERIES][64];
  cr_query_options options[MEASURE_QUERIES];
  for (unsigned i = 0; i < MEASURE_QUERIES; ++i) {
    cr_query_options_init(&options[i]);
    options[i].excerpt_bytes = 48u;
    options[i].byte_budget = 96u;
    if (i < 8u)
      CHECK(snprintf(query_text[i], sizeof(query_text[i]), "sdk_function_%02u",
                     positive[i]) > 0);
    else if (i < 14u)
      CHECK(snprintf(query_text[i], sizeof(query_text[i]), "%s",
                     negative[i - 8u]) >= 0);
    else {
      CHECK(snprintf(query_text[i], sizeof(query_text[i]), "shared_marker") >
            0);
      options[i].max_hits = i == 14u ? 4u : 2u;
      options[i].excerpt_bytes = i == 14u ? 12u : 32u;
      options[i].byte_budget = i == 14u ? 19u : 8u;
    }
  }
  cr_evidence hits[CR_CONTEXT_MAX_HITS];
  cr_query_report report;
  uint64_t latency[MEASURE_REPEATS * MEASURE_QUERIES], elapsed_total = 0u;
  for (unsigned repetition = 0; repetition < MEASURE_REPEATS + 2u;
       ++repetition) {
    for (unsigned i = 0; i < MEASURE_QUERIES; ++i) {
      uint64_t started = monotonic_ns();
      cr_status status = cr_context_query(
          context, (const unsigned char *)query_text[i], strlen(query_text[i]),
          &options[i], hits, CR_CONTEXT_MAX_HITS, &report);
      uint64_t ended = monotonic_ns();
      CHECK(started && ended >= started);
      if (repetition >= 2u) {
        uint64_t elapsed = ended - started;
        latency[(repetition - 2u) * MEASURE_QUERIES + i] = elapsed;
        elapsed_total += elapsed;
      }
      if (i < 8u) {
        CHECK(status == CR_OK && report.returned == 1u &&
              report.matched == 1u && !report.omitted && !report.abstained);
        CHECK(!strcmp(hits[0].record.path, paths[positive[i]]) &&
              !strcmp(hits[0].record.attribution, attribution) &&
              hits[0].record.version == 1u && hits[0].record.split == CR_TRAIN);
      } else if (i < 14u) {
        CHECK(status == CR_NOT_FOUND && report.abstained && !report.returned &&
              !report.matched && !report.excerpt_bytes);
      } else {
        CHECK(status == CR_OK && report.matched == MEASURE_RECORDS &&
              report.returned == (i == 14u ? 2u : 1u) &&
              report.omitted == MEASURE_RECORDS - report.returned &&
              report.excerpt_bytes == options[i].byte_budget);
      }
      CHECK(report.excerpt_bytes <= options[i].byte_budget);
      uint64_t counted = 0u;
      for (size_t hit = 0; hit < report.returned; ++hit) {
        CHECK(hits[hit].record.kind == CR_SOURCE && hits[hit].record.current &&
              hits[hit].record.split == CR_TRAIN &&
              hits[hit].offset <= hits[hit].record.length &&
              hits[hit].length <= hits[hit].record.length - hits[hit].offset);
        CHECK(strlen(hits[hit].record.digest) == 64u);
        counted += hits[hit].length;
      }
      CHECK(counted == report.excerpt_bytes);
    }
  }
  cr_record_view old;
  CHECK(cr_context_record(context, 0u, &old) == CR_OK);
  static const unsigned char refreshed[] =
      "int sdk_refreshed_00(int shared_marker) { return shared_marker + 100; "
      "}\n";
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, paths[0], attribution,
                         refreshed, sizeof(refreshed), NULL) == CR_OK);
  CHECK(!memcmp(old.bytes, content[0], old.length));
  cr_query_options query;
  cr_query_options_init(&query);
  CHECK(cr_context_query(context, (const unsigned char *)"sdk_function_00", 15u,
                         &query, hits, CR_CONTEXT_MAX_HITS,
                         &report) == CR_NOT_FOUND);
  CHECK(cr_context_query(context, (const unsigned char *)"sdk_refreshed_00",
                         16u, &query, hits, CR_CONTEXT_MAX_HITS,
                         &report) == CR_OK &&
        hits[0].record.version == 2u && hits[0].record.id != old.id);
  CHECK(cr_context_query(context, (const unsigned char *)"private_only_symbol",
                         19u, &query, hits, CR_CONTEXT_MAX_HITS,
                         &report) == CR_NOT_FOUND);
  CHECK(cr_context_query(independent,
                         (const unsigned char *)"private_only_symbol", 19u,
                         &query, hits, CR_CONTEXT_MAX_HITS, &report) == CR_OK &&
        hits[0].record.version == 1u && cr_context_count(independent) == 1u &&
        hits[0].record.length == sizeof(private_source) &&
        !memcmp(hits[0].record.bytes, private_source, sizeof(private_source)));
  raw_bytes += sizeof(refreshed);
  provenance_bytes += strlen(paths[0]) + 1u + sizeof(attribution);
  qsort(latency, MEASURE_REPEATS * MEASURE_QUERIES, sizeof(latency[0]),
        compare_time);
  size_t calls = MEASURE_REPEATS * MEASURE_QUERIES;
  uint64_t median = (latency[calls / 2u - 1u] + latency[calls / 2u]) / 2u;
  size_t caller_bytes = sizeof(content) + sizeof(paths) + sizeof(query_text) +
                        sizeof(options) + sizeof(hits) + sizeof(report) +
                        sizeof(latency);
  uint64_t memory = peak_memory();
  CHECK(memory);
  printf("M4 measurement profile=%s records=%zu independent_records=%zu "
         "positive=8/8 negative=6/6 bounded=2/2 refresh=2/2 independence=2/2 "
         "calls=%zu warmup_calls=%u mean_ns=%.3f median_ns=%" PRIu64 " "
         "raw_bytes=%" PRIu64 " provenance_bytes=%" PRIu64 " "
         "caller_buffers=%zu peak_process_bytes=%" PRIu64 "\n",
         CR_CONTEXT_PROFILE, cr_context_count(context),
         cr_context_count(independent), calls, 2u * MEASURE_QUERIES,
         (double)elapsed_total / (double)calls, median, raw_bytes,
         provenance_bytes, caller_bytes, memory);
  cr_context_destroy(independent);
  cr_context_destroy(context);
  return 0;
}
int main(int argc, char **argv) {
  cr_context_options limits;
  cr_context_options_init(&limits);
  cr_context *context = NULL;
  CHECK(cr_context_create(&limits, &context) == CR_OK);
  static const unsigned char first[] =
      "int boundary_search(int key) { return key; }\0exact";
  static const unsigned char second[] =
      "int boundary_search(int key) { return key + 1; }";
  uint64_t old_id = 0, new_id = 0;
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "src/search.c",
                         "host-source", first, sizeof(first),
                         &old_id) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "src/search.c",
                         "host-source", first, sizeof(first),
                         &new_id) == CR_OK);
  CHECK(old_id == new_id && cr_context_count(context) == 1);
  cr_record_view original;
  CHECK(cr_context_record(context, 0, &original) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "src/search.c",
                         "host-source", second, sizeof(second),
                         &new_id) == CR_OK);
  CHECK(new_id != old_id && !memcmp(original.bytes, first, sizeof(first)));
  cr_record_view older;
  CHECK(cr_context_record(context, 0, &older) == CR_OK);
  CHECK(!older.current && older.version == 1);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_DEV, "dev/search.c",
                         "evaluation", second, sizeof(second), NULL) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "research/hidden.c",
                         "source", second, sizeof(second), NULL) == CR_INVALID);
  CHECK(cr_context_admit(context, CR_LLM_PROPOSAL, CR_TRAIN, "llm:search",
                         "host-llm", first, sizeof(first), NULL) == CR_OK);
  cr_query_options query;
  cr_query_options_init(&query);
  query.byte_budget = 17;
  cr_evidence hits[CR_CONTEXT_MAX_HITS];
  cr_query_report report;
  CHECK(cr_context_query(context, (const unsigned char *)"boundary_search", 15,
                         &query, hits, CR_CONTEXT_MAX_HITS, &report) == CR_OK);
  CHECK(report.returned == 1 && report.matched == 1 &&
        report.excerpt_bytes == 17);
  CHECK(hits[0].record.id == new_id && hits[0].record.version == 2 &&
        hits[0].record.split == CR_TRAIN);
  CHECK(hits[0].length == 17 && !strcmp(hits[0].record.path, "src/search.c"));
  unsigned char formatted[8192];
  memset(formatted, 0xaa, sizeof(formatted));
  size_t required = 0;
  CHECK(cr_evidence_format(hits, 1, formatted, 1, &required) == CR_LIMIT);
  CHECK(formatted[0] == 0xaa && required > hits[0].length);
  CHECK(cr_evidence_format(hits, 1, formatted, sizeof(formatted), &required) ==
        CR_OK);
  CHECK(strstr((const char *)formatted, "attribution=host-source") != NULL);
  CHECK(cr_context_query(context, (const unsigned char *)"unrelated_symbol", 16,
                         &query, hits, CR_CONTEXT_MAX_HITS,
                         &report) == CR_NOT_FOUND &&
        report.abstained);
  CHECK(cr_context_forget_source(context, "src/search.c") == CR_OK);
  CHECK(cr_context_query(context, (const unsigned char *)"boundary_search", 15,
                         &query, hits, CR_CONTEXT_MAX_HITS,
                         &report) == CR_NOT_FOUND);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "src/search.c",
                         "host-source", second, sizeof(second),
                         &new_id) == CR_OK);
  CHECK(cr_context_record(context, cr_context_count(context) - 1, &older) ==
            CR_OK &&
        older.version == 3);
  CHECK(cr_context_query(context, (const unsigned char *)"boundary_search", 15,
                         &query, hits, CR_CONTEXT_MAX_HITS, &report) == CR_OK);
  cr_evidence invalid = hits[0];
  memset(invalid.record.digest, 'x', sizeof(invalid.record.digest));
  CHECK(cr_evidence_format(&invalid, 1, formatted, sizeof(formatted),
                           &required) == CR_INVALID);
  query.kind_mask = 2;
  CHECK(cr_context_query(context, (const unsigned char *)"boundary_search", 15,
                         &query, hits, CR_CONTEXT_MAX_HITS, &report) == CR_OK);
  CHECK(hits[0].record.kind == CR_LLM_PROPOSAL);
  query.byte_budget = 0;
  CHECK(cr_context_query(context, first, sizeof(first), &query, hits,
                         CR_CONTEXT_MAX_HITS, &report) == CR_NOT_FOUND &&
        report.abstained);
  cr_context_destroy(context);
  context = NULL;
  limits.max_records = 1;
  limits.max_total_bytes = sizeof(first);
  CHECK(cr_context_create(&limits, &context) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "a.c", "source", first,
                         sizeof(first), NULL) == CR_OK);
  CHECK(cr_context_admit(context, CR_SOURCE, CR_TRAIN, "a.c", "source", second,
                         sizeof(second), NULL) == CR_LIMIT);
  CHECK(cr_context_record(context, 0, &original) == CR_OK && original.current &&
        original.version == 1);
  cr_context_destroy(context);
  context = NULL;
  if (argc == 2) {
    char directory[4096], file[4096], forbidden[4096];
    CHECK(snprintf(directory, sizeof(directory), "%s/sdk-source-fixture",
                   argv[1]) > 0);
    CHECK(snprintf(file, sizeof(file), "%s/native.c", directory) > 0);
    CHECK(snprintf(forbidden, sizeof(forbidden), "%s/tests", directory) > 0);
#ifdef _WIN32
    CHECK(CreateDirectoryA(directory, NULL));
    CHECK(CreateDirectoryA(forbidden, NULL));
#else
    CHECK(mkdir(directory, 0700) == 0);
    CHECK(mkdir(forbidden, 0700) == 0);
#endif
    FILE *f = fopen(file, "wb");
    CHECK(f);
    CHECK(fwrite(first, 1, sizeof(first), f) == sizeof(first));
    CHECK(fclose(f) == 0);
    cr_context_options_init(&limits);
    CHECK(cr_context_create(&limits, &context) == CR_OK);
    cr_scan_report scan;
    CHECK(cr_context_scan(context, directory, &scan) == CR_OK &&
          scan.admitted == 1 && scan.excluded == 1);
    CHECK(cr_context_scan(context, directory, &scan) == CR_OK &&
          scan.unchanged == 1);
    f = fopen(file, "wb");
    CHECK(f);
    CHECK(fwrite(second, 1, sizeof(second), f) == sizeof(second));
    CHECK(fclose(f) == 0);
    CHECK(cr_context_scan(context, directory, &scan) == CR_OK &&
          scan.admitted == 1 && cr_context_count(context) == 2);
    CHECK(cr_context_scan(context, forbidden, &scan) == CR_INVALID &&
          scan.failed == 1);
    cr_context_destroy(context);
    CHECK(remove(file) == 0);
#ifdef _WIN32
    CHECK(RemoveDirectoryA(forbidden));
    CHECK(RemoveDirectoryA(directory));
#else
    CHECK(rmdir(forbidden) == 0);
    CHECK(rmdir(directory) == 0);
#endif
  }
  puts("runtime context: versions, quarantine, budgets, provenance and "
       "capacity pass");
  return measure_context();
}
