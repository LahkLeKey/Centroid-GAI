/* Standalone public-Runtime resource conformance; no teacher or trainer. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include "centroid_code_helper.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>

#include <intrin.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/utsname.h>
#include <time.h>
#endif

#define BANDS 8u
#define CASES (BANDS * 2u)
#define WARMUP_CYCLES 2u
#define MEASURE_CYCLES 100u
#define MEASURE_CALLS (CASES * MEASURE_CYCLES)
#define EXPECTED_MODEL                                                         \
  "eb34dee4bb00ea02d5cef7a8eaa1214869d7fffe15c5356cabbdb96ef7c20e27"
_Static_assert(CR_PREDICT_SCRATCH_DOUBLES == 554u,
               "registered caller workspace must match the profile");
_Static_assert(sizeof(double) == 8u, "registered binary64 platform");
typedef struct {
  uint64_t start, end, elapsed;
  uint32_t head, band;
} observation;
static uint64_t frequency;

static int clock_init(void) {
#ifdef _WIN32
  LARGE_INTEGER rate;
  if (!QueryPerformanceFrequency(&rate) || rate.QuadPart <= 0 ||
      (uint64_t)rate.QuadPart > UINT64_MAX / UINT64_C(1000000000))
    return 0;
  frequency = (uint64_t)rate.QuadPart;
#else
  frequency = UINT64_C(1000000000);
#endif
  return 1;
}
static int tick(uint64_t *out) {
#ifdef _WIN32
  LARGE_INTEGER raw;
  if (!QueryPerformanceCounter(&raw) || raw.QuadPart < 0)
    return 0;
  *out = (uint64_t)raw.QuadPart;
#else
  struct timespec raw;
  if (clock_gettime(CLOCK_MONOTONIC, &raw) || raw.tv_sec < 0 ||
      (uint64_t)raw.tv_sec > UINT64_MAX / UINT64_C(1000000000))
    return 0;
  *out = (uint64_t)raw.tv_sec * UINT64_C(1000000000) + (uint64_t)raw.tv_nsec;
#endif
  return 1;
}
static uint64_t nanoseconds(uint64_t start, uint64_t end) {
  uint64_t difference = end - start;
  return (difference / frequency) * UINT64_C(1000000000) +
         ((difference % frequency) * UINT64_C(1000000000)) / frequency;
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
  return getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0
             ? (uint64_t)usage.ru_maxrss * 1024u
             : 0u;
#endif
}
static void host_info(void) {
#if defined(__clang__)
  printf("compiler=clang-%s\n", __clang_version__);
#elif defined(_MSC_VER)
  printf("compiler=MSVC-%u.%u.%u\n", (unsigned)(_MSC_VER / 100),
         (unsigned)(_MSC_VER % 100), (unsigned)(_MSC_FULL_VER % 100000));
#elif defined(__GNUC__)
  printf("compiler=GCC-%s\n", __VERSION__);
#else
  puts("compiler=unregistered");
#endif
#ifdef _WIN32
  SYSTEM_INFO system;
  GetNativeSystemInfo(&system);
  char brand[49] = {0};
  int words[4];
  __cpuid(words, (int)0x80000000u);
  if ((uint32_t)words[0] >= UINT32_C(0x80000004))
    for (unsigned i = 0; i < 3u; ++i) {
      __cpuid(words, (int)(UINT32_C(0x80000002) + i));
      memcpy(brand + i * 16u, words, 16u);
    }
  printf("os=Windows architecture=%u cpu=%s family=%u model=%u stepping=%u "
         "logical_processors=%lu registered_kernel=10.0.26200 "
         "registered_revision=9457 registered_display=25H2\n",
         (unsigned)system.wProcessorArchitecture, brand,
         (unsigned)system.wProcessorLevel,
         (unsigned)(system.wProcessorRevision >> 8u),
         (unsigned)(system.wProcessorRevision & 255u),
         (unsigned long)system.dwNumberOfProcessors);
  printf("clock=QueryPerformanceCounter raw_tick_frequency=%" PRIu64 " "
         "peak_counter=K32GetProcessMemoryInfo.PeakWorkingSetSize\n",
         frequency);
#else
  struct utsname system;
  if (uname(&system) == 0)
    printf("os=%s kernel=%s architecture=%s host=%s\n", system.sysname,
           system.release, system.machine, system.nodename);
  FILE *cpu = fopen("/proc/cpuinfo", "rb");
  if (cpu) {
    char line[256];
    while (fgets(line, sizeof(line), cpu))
      if (!strncmp(line, "model name", 10u)) {
        printf("cpu=%s", line);
        break;
      }
    fclose(cpu);
  }
  puts("registered_distribution=Ubuntu-24.04-WSL same_physical_host=i5-12400F "
       "clock=CLOCK_MONOTONIC raw_ticks=nanoseconds "
       "peak_counter=getrusage.RUSAGE_SELF.ru_maxrss_KiB");
#endif
}
static int valid_probabilities(const double *scores, size_t actions) {
  double sum = 0.0, maximum = 0.0;
  for (size_t i = 0; i < actions; ++i) {
    if (!isfinite(scores[i]) || scores[i] < 0.0)
      return 0;
    sum += scores[i];
    if (scores[i] > maximum)
      maximum = scores[i];
  }
  return maximum > 0.0 && isfinite(sum) && fabs(sum - 1.0) <= 1e-12;
}
static void print_records(const observation *records, size_t completed) {
  puts("call\thead\tband\tstart_raw_tick\tend_raw_tick\telapsed_ns");
  for (size_t i = 0u; i < completed; ++i)
    printf("%zu\t%s\t%u\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n", i,
           records[i].head == CR_CODE ? "CODE" : "TEXT",
           (unsigned)records[i].band, records[i].start, records[i].end,
           records[i].elapsed);
}
int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s QUALIFIED_BUNDLE QUALIFICATION_SIDECAR\n",
            argv[0]);
    return 2;
  }
  cr_model *model = NULL;
  unsigned char *before = NULL, *after = NULL;
  cr_model_info info = {0}, final = {0};
  info.struct_size = final.struct_size = sizeof(info);
  info.api_version = final.api_version = CR_API_VERSION;
  observation records[MEASURE_CALLS];
  double input[BANDS][CR_FEATURES], mass[CR_MAX_GROUPS] = {1.0, 1.0, 0.0, 0.0};
  double scratch[CR_PREDICT_SCRATCH_DOUBLES], scores[CR_TEXT_ACTIONS];
  uint64_t started = 0u, loaded = 0u, prepared = 0u, measured = 0u;
  uint64_t verified = 0u, logged = 0u, validation_ns = 0u;
  uint64_t total[2] = {0u, 0u}, maximum[2] = {0u, 0u}, count[2] = {0u, 0u};
  const char *stage = "native clock";
  cr_status status = CR_INVALID;
  size_t bundle_bytes = 0u, written = 0u, completed = 0u;
  int accepted = 0;
  int raw_printed = 0;
  if (!clock_init() || !tick(&started))
    goto done;
  host_info();
  stage = "complete frozen model and sidecar validation";
  status = cr_model_load_file(argv[1], &model);
  if (status == CR_OK)
    status = cr_model_info_get(model, &info);
  if (status == CR_OK)
    status = cr_model_check_qualification_file(model, argv[2]);
  if (status != CR_OK)
    goto done;
  if (info.groups != 2u || info.shared_enabled ||
      info.metadata.qualification != CR_QUALIFIED ||
      strcmp(info.metadata.model_digest, EXPECTED_MODEL) ||
      strcmp(info.metadata.task_profile, CR_CODE_HELPER_PROFILE) ||
      strcmp(info.metadata.observation_schema, CR_CODE_OBSERVATION_SCHEMA) ||
      strcmp(info.metadata.action_catalog, CR_CODE_ACTION_CATALOG)) {
    status = CR_UNSUPPORTED;
    goto done;
  }
  if (!tick(&loaded) || loaded < started) {
    status = CR_IO;
    goto done;
  }
  stage = "preallocate canonical identity buffers and encode observations";
  bundle_bytes = cr_model_bundle_size(model);
  if (!bundle_bytes || bundle_bytes > CR_MAX_BUNDLE_BYTES) {
    status = CR_CORRUPT;
    goto done;
  }
  before = malloc(bundle_bytes);
  after = malloc(bundle_bytes);
  if (!before || !after) {
    status = CR_NOMEM;
    goto done;
  }
  status = cr_model_write_bundle(model, before, bundle_bytes, &written);
  if (status != CR_OK || written != bundle_bytes)
    goto done;
  for (unsigned band = 0u; band < BANDS && status == CR_OK; ++band) {
    unsigned char bytes[2] = {1u, (unsigned char)band};
    status = cr_encode(bytes, sizeof(bytes), input[band]);
  }
  if (status != CR_OK)
    goto done;
  if (!tick(&prepared) || prepared < loaded) {
    status = CR_IO;
    goto done;
  }
  stage = "allocation-free caller-workspace prediction";
  for (unsigned cycle = 0u; cycle < WARMUP_CYCLES + MEASURE_CYCLES; ++cycle)
    for (unsigned band = 0u; band < BANDS; ++band)
      for (unsigned sequence = 0u; sequence < 2u; ++sequence) {
        uint32_t head = sequence == 0u ? CR_CODE : CR_TEXT;
        size_t actions = head == CR_CODE ? CR_CODE_ACTIONS : CR_TEXT_ACTIONS;
        uint64_t begin = 0u, end = 0u, checked = 0u;
        if (!tick(&begin)) {
          status = CR_IO;
          goto done;
        }
        status = cr_model_predict_with_scratch(
            model, head, input[band], 3u, mass, scratch,
            CR_PREDICT_SCRATCH_DOUBLES, scores, actions);
        if (!tick(&end) || end < begin) {
          status = CR_IO;
          goto done;
        }
        if (status != CR_OK)
          goto done;
        if (!valid_probabilities(scores, actions)) {
          status = CR_CORRUPT;
          goto done;
        }
        if (!tick(&checked) || checked < end) {
          status = CR_IO;
          goto done;
        }
        validation_ns += nanoseconds(end, checked);
        if (cycle >= WARMUP_CYCLES) {
          uint64_t elapsed = nanoseconds(begin, end);
          records[completed].start = begin;
          records[completed].end = end;
          records[completed].elapsed = elapsed;
          records[completed].head = head;
          records[completed].band = band;
          ++completed;
          if (elapsed > UINT64_MAX - total[head]) {
            status = CR_LIMIT;
            goto done;
          }
          total[head] += elapsed;
          ++count[head];
          if (elapsed > maximum[head])
            maximum[head] = elapsed;
        }
      }
  if (!tick(&measured) || measured < prepared) {
    status = CR_IO;
    goto done;
  }
  stage = "post-prediction immutable complete model check";
  status = cr_model_info_get(model, &final);
  if (status == CR_OK)
    status = cr_model_write_bundle(model, after, bundle_bytes, &written);
  if (status != CR_OK)
    goto done;
  if (written != bundle_bytes || memcmp(before, after, bundle_bytes) ||
      strcmp(info.metadata.model_digest, final.metadata.model_digest) ||
      completed != MEASURE_CALLS || count[CR_CODE] != 800u ||
      count[CR_TEXT] != 800u) {
    status = CR_CORRUPT;
    goto done;
  }
  if (!tick(&verified) || verified < measured) {
    status = CR_IO;
    goto done;
  }
  stage = "retain raw clock intervals";
  print_records(records, completed);
  raw_printed = 1;
  if (fflush(stdout) || ferror(stdout) || !tick(&logged) || logged < verified) {
    status = CR_IO;
    goto done;
  }
  uint64_t peak = peak_memory();
  accepted = peak && peak <= UINT64_C(128) * 1024u * 1024u &&
             total[CR_CODE] <= count[CR_CODE] * UINT64_C(10000000) &&
             total[CR_TEXT] <= count[CR_TEXT] * UINT64_C(10000000);
  printf("resource model=%s protocol=RESOURCE_PROTOCOL.md calls=%zu "
         "warmup_calls=%u "
         "CODE_calls=%" PRIu64 " CODE_mean_ns=%.3f CODE_max_ns=%" PRIu64
         " TEXT_calls=%" PRIu64 " TEXT_mean_ns=%.3f TEXT_max_ns=%" PRIu64
         " peak_process_bytes=%" PRIu64 " accepted=%d\n",
         final.metadata.model_digest, completed, CASES * WARMUP_CYCLES,
         count[CR_CODE], (double)total[CR_CODE] / (double)count[CR_CODE],
         maximum[CR_CODE], count[CR_TEXT],
         (double)total[CR_TEXT] / (double)count[CR_TEXT], maximum[CR_TEXT],
         peak, accepted);
  printf(
      "costs load_validate_host_ns=%" PRIu64 " preprocess_serialize_ns=%" PRIu64
      " warmup_and_measure_loop_ns=%" PRIu64 " result_validation_ns=%" PRIu64
      " identity_check_ns=%" PRIu64 " raw_log_ns=%" PRIu64 " whole_ns=%" PRIu64
      " scratch_doubles=%u scratch_bytes=%zu caller_buffers=%zu "
      "model_bytes=%" PRIu64 " bundle_bytes=%zu identity_buffer_bytes=%zu "
                             "framing=absent retrieval=absent "
      "callbacks=absent steady_state_allocations=none\n",
      nanoseconds(started, loaded), nanoseconds(loaded, prepared),
      nanoseconds(prepared, measured), validation_ns,
      nanoseconds(measured, verified), nanoseconds(verified, logged),
      nanoseconds(started, logged), (unsigned)CR_PREDICT_SCRATCH_DOUBLES,
      sizeof(scratch),
      sizeof(input) + sizeof(mass) + sizeof(scratch) + sizeof(scores) +
          sizeof(records),
      info.model_bytes, bundle_bytes, 2u * bundle_bytes);
  status = accepted ? CR_OK : CR_LIMIT;
  if (fflush(stdout) || ferror(stdout)) {
    status = CR_IO;
    accepted = 0;
  }
done:
  if (!raw_printed)
    print_records(records, completed);
  if (status != CR_OK)
    fprintf(stderr,
            "resource check failed: stage=%s status=%s completed=%zu "
            "peak_process_bytes=%" PRIu64 " (no new quality result)\n",
            stage, cr_status_string(status), completed, peak_memory());
  free(after);
  free(before);
  cr_model_destroy(model);
  return status == CR_OK && accepted ? 0 : 1;
}
