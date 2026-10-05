#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "centroid_algorithms.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                  \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define PATH 4096u
static const char baseline[] =
    "#include \"centroid_algorithms.h\"\n"
    "c_status c_size_affine(size_t count,size_t stride,size_t extra,size_t "
    "*out){"
    "size_t product;if(!out){return C_INVALID;}"
    "if(stride && count>SIZE_MAX/stride){return C_LIMIT;}product=count*stride;"
    "if(extra>SIZE_MAX-product){return C_LIMIT;}*out=product+extra;return "
    "C_OK;}\n"
    "size_t c_lower_bound(const int *values,size_t count,int key,size_t "
    "*comparisons){"
    "size_t index=0,operations=0;if(!values && count){"
    "if(comparisons){*comparisons=0;}return SIZE_MAX;}"
    "while(index<count){++operations;if(values[index]>=key){break;}++index;}"
    "if(comparisons){*comparisons=operations;}return index;}\n"
    "uint64_t c_u64_saturating_add(uint64_t left,uint64_t right){"
    "return right>UINT64_MAX-left?UINT64_MAX:left+right;}\n";
static void join(char out[PATH], const char *left, const char *right) {
  int n = snprintf(out, PATH, "%s/%s", left, right);
  CHECK(n > 0 && (size_t)n < PATH);
}
static unsigned char *read_artifact(const char *dir, const char *name,
                                    size_t *size) {
  char path[PATH];
  unsigned char *bytes = NULL;
  join(path, dir, name);
  CHECK(c_read_file(path, &bytes, size) == C_OK);
  unsigned char *grown = realloc(bytes, *size + 1);
  CHECK(grown);
  grown[*size] = 0;
  return grown;
}

#include "contact_contract.h"

static void no_contact_deferred(const char *directory, const char *project) {
  char output[PATH];
  int n = snprintf(output, sizeof(output), "%s-no-contact", directory);
  CHECK(n > 0 && (size_t)n < sizeof(output));
  c_trainer *trainer = NULL;
  CHECK(c_trainer_create(2u, 42u, &trainer) == C_OK);
  CHECK(c_context_admit(trainer->context, C_ACTIVITY, C_TRAIN, "request.txt",
                        "preflight fixture", (const unsigned char *)"size", 4u,
                        NULL) == C_OK);
  memset(trainer->cells, 0, sizeof(trainer->cells));
  c_trainer *before = malloc(sizeof(*before));
  c_model *model = malloc(sizeof(*model));
  CHECK(before && model);
  memcpy(before, trainer, sizeof(*before));
  memcpy(model, trainer->model, sizeof(*model));
  unsigned char *context_before = NULL, *context_after = NULL;
  size_t first = 0, second = 0;
  CHECK(c_context_pack(trainer->context, &context_before, &first) == C_OK);
  CHECK(c_code_suite_run(trainer, "centroid-no-such-code-suite-compiler",
                         project, output) == C_DEFERRED);
  CHECK(!memcmp(before, trainer, sizeof(*before)) &&
        !memcmp(model, trainer->model, sizeof(*model)));
  CHECK(c_context_pack(trainer->context, &context_after, &second) == C_OK &&
        first == second && !memcmp(context_before, context_after, first));
#ifdef _WIN32
  CHECK(GetFileAttributesA(output) == INVALID_FILE_ATTRIBUTES);
#else
  struct stat info;
  CHECK(lstat(output, &info) != 0);
#endif
  free(context_before);
  free(context_after);
  free(before);
  free(model);
  c_trainer_destroy(trainer);
}
static void write_artifact(const char *dir, const char *name, const void *bytes,
                           size_t size) {
  char path[PATH];
  join(path, dir, name);
  CHECK(c_write_atomic(path, bytes, size) == C_OK);
}
static void copy_header(const char *source, const char *target,
                        const char *name) {
  char relative[128];
  snprintf(relative, sizeof(relative), "include/%s", name);
  size_t size;
  unsigned char *bytes = read_artifact(source, relative, &size);
  write_artifact(target, relative, bytes, size);
  free(bytes);
}
static void check_algorithms(void) {
  size_t out = 91;
  CHECK(c_size_affine(7, 5, 3, &out) == C_OK && out == 38);
  CHECK(c_size_affine(SIZE_MAX, 2, 0, &out) == C_LIMIT && out == 38);
  CHECK(c_size_affine(SIZE_MAX, 1, 1, &out) == C_LIMIT && out == 38);
  CHECK(c_size_affine(0, SIZE_MAX, 17, &out) == C_OK && out == 17);
  CHECK(c_size_affine(1, 1, 0, NULL) == C_INVALID);
  const int values[] = {-7, -4, -4, 0, 9};
  size_t comparisons = 99;
  CHECK(c_lower_bound(values, 5, -4, &comparisons) == 1 && comparisons > 0);
  CHECK(c_lower_bound(values, 5, 10, NULL) == 5);
  CHECK(c_lower_bound(NULL, 0, 0, &comparisons) == 0 && comparisons == 0);
  CHECK(c_lower_bound(NULL, 1, 0, &comparisons) == SIZE_MAX &&
        comparisons == 0);
  CHECK(c_u64_saturating_add(UINT64_MAX - 2, 2) == UINT64_MAX);
  CHECK(c_u64_saturating_add(UINT64_MAX - 2, 3) == UINT64_MAX);
  CHECK(c_u64_saturating_add(18, 12) == 30);
}
static void check_quarantine(c_trainer *t) {
  size_t audits = 0, devs = 0, measurements = 0;
  for (size_t i = 0; i < c_context_count(c_trainer_context(t)); ++i) {
    c_record r;
    CHECK(c_context_record(c_trainer_context(t), i, &r) == C_OK);
    if (r.kind == C_AUDIT) {
      ++audits;
      CHECK(r.split == C_HOLDOUT);
    }
    if (r.kind == C_DEVELOPMENT) {
      ++devs;
      CHECK(r.split == C_DEV);
    }
    if (r.kind == C_TRAIN_MEASUREMENT) {
      ++measurements;
      CHECK(r.split == C_TRAIN);
    }
  }
  CHECK(audits == 14 && devs == 12 && measurements == 12);
  CHECK(t->receipt_count == 6);
  unsigned targets[4] = {0};
  for (size_t i = 0; i < t->task_count; ++i) {
    CHECK(t->tasks[i].head == C_CODE && t->tasks[i].done);
    ++targets[t->tasks[i].target];
  }
  CHECK(targets[1] == 2 && targets[2] == 2 && targets[3] == 2);
  for (size_t i = 0; i < t->receipt_count; ++i)
    CHECK(t->receipt_consumed[i] == 1);
}
static void check_inputs(const char *directory) {
  static const char *const domains[] = {"affine", "lower-bound", "saturation"};
  for (unsigned d = 0; d < 3; ++d) {
    char name[128];
    size_t size;
    snprintf(name, sizeof(name), "%s/input.bin", domains[d]);
    unsigned char *bytes = read_artifact(directory, name, &size);
    CHECK(!strstr((char *)bytes, "FORBIDDEN_AUDIT") &&
          !strstr((char *)bytes, "STALE_EXPERIMENT_SOURCE"));
    CHECK(strstr((char *)bytes, "Input role=SOURCE id="));
    CHECK(strstr((char *)bytes, "Input role=LLM_PROPOSAL id="));
    CHECK(strstr((char *)bytes, "Unverified native C catalog hypothesis"));
    free(bytes);
    snprintf(name, sizeof(name), "%s/generalization-input.bin", domains[d]);
    bytes = read_artifact(directory, name, &size);
    CHECK(strstr((char *)bytes, "Independent use:"));
    free(bytes);
    for (unsigned a = 0; a < 4; ++a) {
      static const char *const split[] = {"train", "dev", "audit"};
      for (unsigned s = 0; s < 3; ++s) {
        snprintf(name, sizeof(name), "%s/action-%u/%s.log", domains[d], a,
                 split[s]);
        bytes = read_artifact(directory, name, &size);
        CHECK(strstr((char *)bytes, "ROW 0 0 "));
        CHECK(strstr((char *)bytes, s == 1 ? "total=4" : "total=8"));
        free(bytes);
      }
    }
  }
}
static void assert_target(const char *root, const void *expected,
                          size_t length) {
  size_t size;
  unsigned char *actual = read_artifact(root, "src/domain/algorithms.c", &size);
  CHECK(size == length && !memcmp(actual, expected, size));
  free(actual);
}
static void check_apply(const char *directory, const char *project) {
  char digest[C_DIGEST_HEX];
  c_hash(baseline, sizeof(baseline) - 1, digest);
  char wrong[C_DIGEST_HEX];
  memset(wrong, '0', 64);
  wrong[64] = 0;
  CHECK(c_patch_apply(directory, project, wrong) == C_CORRUPT);
  assert_target(project, baseline, sizeof(baseline) - 1);
  size_t size;
  unsigned char *winner = read_artifact(directory, "winner.c", &size);
  CHECK(size > 0);
  winner[0] ^= 1;
  write_artifact(directory, "winner.c", winner, size);
  size_t compile_size = 0;
  unsigned char *compile_log = read_artifact(
      directory, "lower-bound/action-1/compile.log", &compile_size);
  unsigned char *tampered_log = malloc(compile_size + 1);
  CHECK(tampered_log);
  memcpy(tampered_log, compile_log, compile_size);
  tampered_log[compile_size] = 'x';
  write_artifact(directory, "lower-bound/action-1/compile.log", tampered_log,
                 compile_size + 1);
  CHECK(c_patch_apply(directory, project, digest) == C_CORRUPT);
  assert_target(project, baseline, sizeof(baseline) - 1);
  write_artifact(directory, "lower-bound/action-1/compile.log", compile_log,
                 compile_size);
  free(tampered_log);
  free(compile_log);
  CHECK(c_patch_apply(directory, project, digest) == C_CORRUPT);
  assert_target(project, baseline, sizeof(baseline) - 1);
  winner[0] ^= 1;
  write_artifact(directory, "winner.c", winner, size);
  char changed[sizeof(baseline) + 64];
  int n = snprintf(changed, sizeof(changed),
                   "%s\n/* dirty conflicting parent */\n", baseline);
  CHECK(n > 0 && (size_t)n < sizeof(changed));
  write_artifact(project, "src/domain/algorithms.c", changed, (size_t)n);
  CHECK(c_patch_apply(directory, project, digest) == C_INVALID);
  assert_target(project, changed, (size_t)n);
  write_artifact(project, "src/domain/algorithms.c", baseline,
                 sizeof(baseline) - 1);
  size_t manifest_size;
  unsigned char *manifest =
      read_artifact(directory, "patch.manifest", &manifest_size);
  char *target = strstr((char *)manifest, "target=src/domain/algorithms.c");
  CHECK(target);
  target[7] = '.';
  char forged[C_DIGEST_HEX];
  c_hash(manifest, manifest_size, forged);
  write_artifact(directory, "patch.manifest", manifest, manifest_size);
  write_artifact(directory, "accepted.sha256", forged, 64);
  CHECK(c_patch_apply(directory, project, digest) == C_CORRUPT);
  assert_target(project, baseline, sizeof(baseline) - 1);
  target[7] = 's';
  c_hash(manifest, manifest_size, forged);
  write_artifact(directory, "patch.manifest", manifest, manifest_size);
  write_artifact(directory, "accepted.sha256", forged, 64);
  free(manifest);
  static const char *const live_headers[] = {"include/centroid_algorithms.h",
                                             "include/centroid.h"};
  for (unsigned i = 0; i < 2u; ++i) {
    size_t header_size = 0;
    unsigned char *header =
        read_artifact(project, live_headers[i], &header_size);
    CHECK(header_size > 0);
    header[0] ^= 1u;
    write_artifact(project, live_headers[i], header, header_size);
    CHECK(c_patch_apply(directory, project, digest) == C_CORRUPT);
    assert_target(project, baseline, sizeof(baseline) - 1);
    header[0] ^= 1u;
    write_artifact(project, live_headers[i], header, header_size);
    free(header);
  }
#ifndef _WIN32
  char linked[PATH];
  int link_count = snprintf(linked, sizeof(linked), "%s-linked", directory);
  CHECK(link_count > 0 && (size_t)link_count < sizeof(linked));
  CHECK(symlink(directory, linked) == 0);
  CHECK(c_patch_apply(linked, project, digest) == C_IO);
  CHECK(unlink(linked) == 0);
  char domain_path[PATH], saved_path[PATH];
  join(domain_path, project, "src/domain");
  join(saved_path, project, "src/domain-saved");
  CHECK(rename(domain_path, saved_path) == 0);
  CHECK(symlink(saved_path, domain_path) == 0);
  CHECK(c_patch_apply(directory, project, digest) == C_IO);
  CHECK(unlink(domain_path) == 0);
  CHECK(rename(saved_path, domain_path) == 0);
  assert_target(project, baseline, sizeof(baseline) - 1);
#endif
  CHECK(c_patch_apply(directory, project, digest) == C_OK);
  assert_target(project, winner, size);
  CHECK(c_patch_apply(directory, project, digest) == C_INVALID);
  assert_target(project, winner, size);
  free(winner);
}
static void check_infrastructure(const char *directory, const char *project,
                                 c_trainer *trainer) {
  char failed[PATH], before[C_DIGEST_HEX], after[C_DIGEST_HEX];
  int n = snprintf(failed, sizeof(failed), "%s-failed", directory);
  CHECK(n > 0 && (size_t)n < sizeof(failed));
  c_model *original_model = malloc(sizeof(*original_model));
  unsigned char original_cells[C_WORLD_CELLS];
  CHECK(original_model);
  memcpy(original_model, trainer->model, sizeof(*original_model));
  memcpy(original_cells, trainer->cells, sizeof(original_cells));
  const uint64_t original_generation = trainer->generation;
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0, before) == C_OK);
  size_t receipts = trainer->receipt_count;
  CHECK(c_code_suite_run(trainer, "centroid-no-such-code-suite-compiler",
                         project, failed) == C_IO);
  CHECK(c_model_fingerprint(c_trainer_model(trainer), 0, after) == C_OK &&
        !strcmp(before, after));
  CHECK(trainer->receipt_count == receipts);
  size_t size = 0;
  unsigned char *report = read_artifact(failed, "report.md", &size);
  CHECK(strstr((char *)report,
               "Attempted 12/12; verified TRAIN candidates 0/12"));
  CHECK(strstr((char *)report, "No accepted patch is published"));
  free(report);
  check_contact_artifacts(failed, original_model, original_cells,
                          original_generation, 0u);
  free(original_model);
  char path[PATH];
  join(path, failed, "accepted.sha256");
  FILE *f = fopen(path, "rb");
  CHECK(!f);
  static const char *const domains[] = {"affine", "lower-bound", "saturation"};
  for (unsigned d = 0; d < 3; ++d)
    for (unsigned a = 0; a < 4; ++a) {
      char name[128];
      snprintf(name, sizeof(name), "%s/action-%u/candidate.c", domains[d], a);
      unsigned char *bytes = read_artifact(failed, name, &size);
      CHECK(size);
      free(bytes);
      snprintf(name, sizeof(name), "%s/action-%u/compile.log", domains[d], a);
      bytes = read_artifact(failed, name, &size);
      free(bytes);
    }
}
int main(int argc, char **argv) {
  CHECK(argc == 4);
  check_algorithms();
  char root[PATH], project[PATH], directory[PATH], path[PATH], name[128];
#ifdef _WIN32
  CHECK(_fullpath(root, argv[1], sizeof(root)));
  unsigned pid = GetCurrentProcessId();
#else
  CHECK(realpath(argv[1], root));
  unsigned pid = (unsigned)getpid();
#endif
  snprintf(name, sizeof(name), "code-suite-%u-%llu", pid,
           (unsigned long long)c_monotonic_ms());
  join(directory, root, name);
  snprintf(name, sizeof(name), "code-project-%u-%llu", pid,
           (unsigned long long)c_monotonic_ms());
  join(project, root, name);
  CHECK(c_make_directory(project) == C_OK);
  join(path, project, "src");
  CHECK(c_make_directory(path) == C_OK);
  join(path, project, "src/domain");
  CHECK(c_make_directory(path) == C_OK);
  join(path, project, "include");
  CHECK(c_make_directory(path) == C_OK);
  write_artifact(project, "src/domain/algorithms.c", baseline,
                 sizeof(baseline) - 1);
  copy_header(argv[3], project, "centroid.h");
  copy_header(argv[3], project, "centroid_algorithms.h");
  no_contact_deferred(directory, project);
  c_trainer *t = NULL;
  CHECK(c_trainer_create(2, 42, &t) == C_OK);
  static const unsigned char proposal[] =
      "Unverified native C catalog hypothesis: choose action0 checked "
      "allocation lower bound saturation. Independent measurement required.";
  static const unsigned char audit[] =
      "FORBIDDEN_AUDIT checked allocation lower bound saturation action=0";
  static const unsigned char stale[] =
      "STALE_EXPERIMENT_SOURCE checked allocation lower bound saturation";
  CHECK(c_context_admit(c_trainer_context(t), C_LLM_PROPOSAL, C_TRAIN,
                        "proposal.txt", "explicit mistaken LLM suggestion",
                        proposal, sizeof(proposal) - 1, NULL) == C_OK);
  CHECK(c_context_admit(c_trainer_context(t), C_AUDIT, C_HOLDOUT, "heldout.txt",
                        "quarantined", audit, sizeof(audit) - 1, NULL) == C_OK);
  CHECK(c_context_admit(c_trainer_context(t), C_SOURCE, C_TRAIN,
                        "old-experiment/winner.c", "historical fixture", stale,
                        sizeof(stale) - 1, NULL) == C_OK);
  c_model *original_model = malloc(sizeof(*original_model));
  unsigned char original_cells[C_WORLD_CELLS];
  CHECK(original_model);
  memcpy(original_model, t->model, sizeof(*original_model));
  memcpy(original_cells, t->cells, sizeof(original_cells));
  CHECK(c_code_suite_run(t, argv[2], project, directory) == C_OK);
  check_contact_artifacts(directory, original_model, original_cells, 0u, 6u);
  free(original_model);
  c_training_report report;
  CHECK(c_trainer_report(t, &report) == C_OK);
  CHECK(report.updates == 6 && report.completed == 6 && report.queued == 0 &&
        report.contacts >= 6);
  check_inputs(directory);
  check_quarantine(t);
  size_t size;
  unsigned char *r = read_artifact(directory, "report.md", &size);
  CHECK(strstr((char *)r, "Publication gate: accepted"));
  CHECK(strstr((char *)r, "affine selected action=3"));
  CHECK(strstr((char *)r, "lower-bound selected action=1"));
  CHECK(strstr((char *)r, "saturation selected action=2"));
  free(r);
  char checkpoint[PATH];
  join(checkpoint, directory, "memory.centroid");
  c_trainer *loaded = NULL;
  CHECK(c_trainer_load(checkpoint, &loaded) == C_OK);
  check_quarantine(loaded);
  c_trainer_destroy(loaded);
  CHECK(c_code_suite_run(t, argv[2], project, directory) == C_INVALID);
  check_infrastructure(directory, project, t);
  check_apply(directory, project);
  c_trainer_destroy(t);
  printf("Three-domain TRAIN-only learning, independent gates, immutable "
         "context and fixed-site verified patch passed: %s\n",
         directory);
  return 0;
}
