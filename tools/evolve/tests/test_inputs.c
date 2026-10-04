/** @file test_inputs.c @brief Candidate bytes and configured input membership integrity. */
#include "build_config.h"
#include "evolve_run.h"
#include "test_utils.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CHECK(condition) TEST_CHECK(condition, "Evolution input integrity check failed")
#define PATH_BYTES (EVOLVE_PROCESS_MAX_PATH_BYTES + 1U)

typedef struct inputs_fixture {
    evolve_run run;
    evolve_work work;
    char child_directory[PATH_BYTES];
    char child_manifest[PATH_BYTES];
} inputs_fixture;

static unsigned int current_pid(void) {
#ifdef _WIN32
    return (unsigned int)GetCurrentProcessId();
#else
    return (unsigned int)getpid();
#endif
}

static void remove_directory(const char *path) {
#ifdef _WIN32
    CHECK(_rmdir(path) == 0);
#else
    CHECK(rmdir(path) == 0);
#endif
}

static void init_fixture(inputs_fixture *fixture, const char *source, const char *manifest) {
    char directory[128];
    memset(fixture, 0, sizeof(*fixture));
    const int length =
        snprintf(directory, sizeof(directory), "test-evolve-inputs-%u", current_pid());
    CHECK(length > 0 && (size_t)length < sizeof(directory));
    CHECK(evolve_manifest_hash(manifest, &fixture->run.inputs_checksum));
    CHECK(evolve_inputs_unchanged(&fixture->run));
    CHECK(evolve_read_source(source, &fixture->run.parent, &fixture->run.parent_storage));
    CHECK(evolve_create_directory(directory));
    CHECK(evolve_absolute_path(directory, fixture->run.output, sizeof(fixture->run.output)));
    CHECK(evolve_work_create(&fixture->run, "candidate", fixture->run.parent.bytes,
                             fixture->run.parent.size, &fixture->work));
    CHECK(fixture->work.source_checksum == fixture->run.parent.checksum);
    CHECK(fixture->work.configured == 0);
    CHECK(evolve_work_unchanged(&fixture->run, &fixture->work));
}

static void restore_candidate(const inputs_fixture *fixture) {
    CHECK(remove(fixture->work.source) == 0);
    CHECK(evolve_write_exclusive(fixture->work.source, fixture->run.parent.bytes,
                                 fixture->run.parent.size));
    CHECK(evolve_work_unchanged(&fixture->run, &fixture->work));
}

static void alter_catalog_literal(const inputs_fixture *fixture) {
    const evolve_mutation_source *parent = &fixture->run.parent;
    const uint32_t choice = (parent->profile.choices[EVOLVE_MUTATION_EMBEDDINGS] + 1U) % 3U;
    const char *literal = evolve_mutation_literal(EVOLVE_MUTATION_EMBEDDINGS, choice);
    const size_t offset = parent->literal_offsets[EVOLVE_MUTATION_EMBEDDINGS] + 3U;
    CHECK(offset <= LONG_MAX && literal != NULL && strlen(literal) == 4U);
    FILE *file = fopen(fixture->work.source, "r+b");
    CHECK(file != NULL && fseek(file, (long)offset, SEEK_SET) == 0);
    CHECK(fwrite(literal + 3U, 1U, 1U, file) == 1U);
    CHECK(fclose(file) == 0);
    evolve_mutation_source parsed;
    char *storage = NULL;
    CHECK(evolve_read_source(fixture->work.source, &parsed, &storage));
    CHECK(parsed.profile.choices[EVOLVE_MUTATION_EMBEDDINGS] == choice);
    CHECK(parsed.checksum != fixture->work.source_checksum);
    CHECK(!evolve_work_unchanged(&fixture->run, &fixture->work));
    free(storage);
}

static void candidate_bytes(inputs_fixture *fixture) {
    alter_catalog_literal(fixture);
    restore_candidate(fixture);
    CHECK(fixture->run.parent.size > 32U);
    CHECK(remove(fixture->work.source) == 0);
    CHECK(evolve_write_exclusive(fixture->work.source, fixture->run.parent.bytes, 32U));
    CHECK(!evolve_work_unchanged(&fixture->run, &fixture->work));
    restore_candidate(fixture);
    evolve_work duplicate = {0};
    CHECK(!evolve_work_create(&fixture->run, "candidate", "replacement", 11U, &duplicate));
    CHECK(evolve_work_unchanged(&fixture->run, &fixture->work));
}

static void prepare_child(inputs_fixture *fixture) {
    CHECK(evolve_create_directory(fixture->work.build));
    CHECK(evolve_path_join(fixture->child_directory, sizeof(fixture->child_directory),
                           fixture->work.build, "evolve"));
    CHECK(evolve_create_directory(fixture->child_directory));
    CHECK(evolve_path_join(fixture->child_manifest, sizeof(fixture->child_manifest),
                           fixture->child_directory, "inputs.txt"));
    fixture->work.configured = 1;
    CHECK(!evolve_work_unchanged(&fixture->run, &fixture->work));
}

static void copy_manifest(const char *source, const char *destination, int omit_first,
                          const char *extra_path) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(destination, "wbx");
    char line[PATH_BYTES];
    size_t count = 0U;
    CHECK(input != NULL && output != NULL);
    while (fgets(line, sizeof(line), input) != NULL) {
        CHECK(strchr(line, '\n') != NULL);
        if (!omit_first || count != 0U)
            CHECK(fputs(line, output) >= 0);
        ++count;
    }
    CHECK(count > 1U && !ferror(input));
    if (extra_path != NULL)
        CHECK(fprintf(output, "%s\n", extra_path) >= 0);
    CHECK(fclose(input) == 0);
    CHECK(fclose(output) == 0);
}

static void membership_addition(inputs_fixture *fixture, const char *manifest) {
    CHECK(remove(fixture->child_manifest) == 0);
    copy_manifest(manifest, fixture->child_manifest, 0, fixture->work.source);
    uint64_t changed;
    CHECK(evolve_manifest_hash(fixture->child_manifest, &changed));
    CHECK(changed != fixture->run.inputs_checksum);
    CHECK(!evolve_work_unchanged(&fixture->run, &fixture->work));
}

static void membership_omission(inputs_fixture *fixture, const char *manifest) {
    CHECK(remove(fixture->child_manifest) == 0);
    copy_manifest(manifest, fixture->child_manifest, 1, NULL);
    uint64_t changed;
    CHECK(evolve_manifest_hash(fixture->child_manifest, &changed));
    CHECK(changed != fixture->run.inputs_checksum);
    CHECK(!evolve_work_unchanged(&fixture->run, &fixture->work));
}

static void configured_membership(inputs_fixture *fixture, const char *manifest) {
    prepare_child(fixture);
    copy_manifest(manifest, fixture->child_manifest, 0, NULL);
    uint64_t exact;
    CHECK(evolve_manifest_hash(fixture->child_manifest, &exact));
    CHECK(exact == fixture->run.inputs_checksum);
    CHECK(evolve_work_unchanged(&fixture->run, &fixture->work));
    membership_addition(fixture, manifest);
    membership_omission(fixture, manifest);
    CHECK(remove(fixture->child_manifest) == 0);
    copy_manifest(manifest, fixture->child_manifest, 0, NULL);
    CHECK(evolve_work_unchanged(&fixture->run, &fixture->work));
}

static void original_source_preserved(const inputs_fixture *fixture, const char *path) {
    evolve_mutation_source original;
    char *storage = NULL;
    CHECK(evolve_read_source(path, &original, &storage));
    CHECK(original.size == fixture->run.parent.size);
    CHECK(memcmp(original.bytes, fixture->run.parent.bytes, original.size) == 0);
    CHECK(evolve_inputs_unchanged(&fixture->run));
    free(storage);
}

static void cleanup_fixture(inputs_fixture *fixture) {
    CHECK(remove(fixture->child_manifest) == 0);
    remove_directory(fixture->child_directory);
    remove_directory(fixture->work.build);
    CHECK(remove(fixture->work.source) == 0);
    remove_directory(fixture->work.directory);
    remove_directory(fixture->run.output);
    free(fixture->run.parent_storage);
}

int main(int argc, char **argv) {
    CHECK(argc == 2 || argc == 3);
    const char *manifest = argc == 3 ? argv[2] : EVOLVE_INPUT_MANIFEST;
    CHECK(!evolve_output_allowed(NULL));
    CHECK(!evolve_output_allowed(EVOLVE_SOURCE_DIRECTORY));
    CHECK(!evolve_output_allowed(EVOLVE_SOURCE_DIRECTORY "/evolution-run"));
    CHECK(!evolve_output_allowed(EVOLVE_SOURCE_DIRECTORY "/build-other/run"));
    CHECK(evolve_output_allowed(EVOLVE_SOURCE_DIRECTORY "/build/run"));
    CHECK(evolve_output_allowed(EVOLVE_SOURCE_DIRECTORY "-other/run"));
    inputs_fixture fixture;
    init_fixture(&fixture, argv[1], manifest);
    candidate_bytes(&fixture);
    configured_membership(&fixture, manifest);
    original_source_preserved(&fixture, argv[1]);
    cleanup_fixture(&fixture);
    puts("Candidate source integrity and exact configured input membership checks passed");
    return 0;
}
