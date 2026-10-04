/** @file test_repository.c @brief Native bounded repository ingestion fixture checks. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "centroid_life.h"
#include "context_repository.h"
#include "life_context.h"
#include "test_utils.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CHECK(condition) TEST_CHECK(condition, "Native repository scanner check failed")
#define PATH_BYTES 4096U

typedef struct repository_fixture {
    char root[PATH_BYTES];
    int linked_file;
    int linked_directory;
} repository_fixture;

static void fixture_path(char *output, const char *root, const char *name) {
    const int written = snprintf(output, PATH_BYTES, "%s/%s", root, name);
    CHECK(written > 0 && (size_t)written < PATH_BYTES);
}

static int make_directory(const char *path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0700);
#endif
}

static void remove_directory(const char *path) {
#ifdef _WIN32
    CHECK(_rmdir(path) == 0);
#else
    CHECK(rmdir(path) == 0);
#endif
}

static unsigned long process_id(void) {
#ifdef _WIN32
    return (unsigned long)_getpid();
#else
    return (unsigned long)getpid();
#endif
}

static void create_root(repository_fixture *fixture, const char *directory) {
    char name[128];
    memset(fixture, 0, sizeof(*fixture));
    for (unsigned int attempt = 0U; attempt < 16U; ++attempt) {
        const int length =
            snprintf(name, sizeof(name), "test-context-repository-%lu-%u", process_id(), attempt);
        CHECK(length > 0 && (size_t)length < sizeof(name));
        fixture_path(fixture->root, directory, name);
        if (make_directory(fixture->root) == 0)
            return;
        CHECK(errno == EEXIST);
    }
    CHECK(0);
}

static void create_directory(const repository_fixture *fixture, const char *name) {
    char path[PATH_BYTES];
    fixture_path(path, fixture->root, name);
    CHECK(make_directory(path) == 0);
}

static void write_file(const repository_fixture *fixture, const char *name, const char *text) {
    char path[PATH_BYTES];
    fixture_path(path, fixture->root, name);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(text, 1U, strlen(text), file) == strlen(text));
    CHECK(fclose(file) == 0);
}

static void remove_file(const repository_fixture *fixture, const char *name) {
    char path[PATH_BYTES];
    fixture_path(path, fixture->root, name);
    CHECK(remove(path) == 0);
}

static void write_chunks(const repository_fixture *fixture) {
    char path[PATH_BYTES];
    char line[901];
    fixture_path(path, fixture->root, "src/alpha.h");
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    memset(line, 'a', sizeof(line));
    line[900] = '\n';
    for (size_t i = 0U; i < 5U; ++i)
        CHECK(fwrite(line, 1U, sizeof(line), file) == sizeof(line));
    CHECK(fclose(file) == 0);
}

static void excluded_fixtures(const repository_fixture *fixture) {
    static const char *const directories[] = {".hidden", "build-debug", "data", "tests"};
    static const char *const files[] = {".hidden/nested.c", "build-debug/generated.c",
                                        "data/private.c", "tests/test.c", ".secret.c"};
    for (size_t i = 0U; i < sizeof(directories) / sizeof(directories[0]); ++i)
        create_directory(fixture, directories[i]);
    for (size_t i = 0U; i < sizeof(files) / sizeof(files[0]); ++i)
        write_file(fixture, files[i], "excluded secret fixture\n");
}

static int make_link(const char *path, const char *target, int directory) {
#ifdef _WIN32
    const DWORD flags = (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0U) |
                        SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    return CreateSymbolicLinkA(path, target, flags) != 0;
#else
    (void)directory;
    return symlink(target, path) == 0;
#endif
}

static void linked_fixtures(repository_fixture *fixture) {
    char path[PATH_BYTES];
    char target[PATH_BYTES];
    fixture_path(path, fixture->root, "linked.c");
    fixture_path(target, fixture->root, "zeta.c");
    fixture->linked_file = make_link(path, target, 0);
    fixture_path(path, fixture->root, "linked-src");
    fixture_path(target, fixture->root, "src");
    fixture->linked_directory = make_link(path, target, 1);
    if (!fixture->linked_file || !fixture->linked_directory)
        puts("Native symbolic link fixtures unavailable for one or more paths");
}

static void create_fixture(repository_fixture *fixture, const char *directory) {
    create_root(fixture, directory);
    create_directory(fixture, "src");
    write_file(fixture, "README.md", "repository source");
    write_file(fixture, "zeta.c", "line one\nline two\n");
    write_file(fixture, "notes.session", "attributed assistant context\nlocal activity note\n");
    write_chunks(fixture);
    excluded_fixtures(fixture);
    linked_fixtures(fixture);
}

static cgai_life_context *new_owner(void) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life_context *owner = NULL;
    config.enable_merges = 0U;
    CHECK(cgai_life_context_create(&config, &owner) == CGAI_LIFE_OK);
    return owner;
}

static void check_chunk(const life_context_record *record, uint32_t first, uint32_t last) {
    CHECK(strcmp(record->source, "src/alpha.h") == 0);
    CHECK(record->first_line == first && record->last_line == last);
    const size_t expected = (size_t)(last - first + 1U) * 901U;
    CHECK(strlen(record->text) == expected);
    for (size_t i = 0U; i < expected; ++i)
        CHECK(record->text[i] == (i % 901U == 900U ? '\n' : 'a'));
}

static void check_records(const cgai_life_context *owner) {
    CHECK(owner->count == 5U);
    CHECK(strcmp(owner->records[0].source, "README.md") == 0);
    CHECK(strcmp(owner->records[0].text, "repository source") == 0);
    CHECK(owner->records[0].first_line == 1U && owner->records[0].last_line == 1U);
    check_chunk(&owner->records[1], 1U, 2U);
    check_chunk(&owner->records[2], 3U, 4U);
    check_chunk(&owner->records[3], 5U, 5U);
    CHECK(strcmp(owner->records[4].source, "zeta.c") == 0);
    CHECK(strcmp(owner->records[4].text, "line one\nline two\n") == 0);
    CHECK(owner->records[4].first_line == 1U && owner->records[4].last_line == 2U);
    for (size_t i = 0U; i < owner->count; ++i)
        CHECK(owner->records[i].kind == CGAI_LIFE_CONTEXT_SOURCE &&
              owner->records[i].learned_mask == 0U);
}

static context_repository_batch *scan(const repository_fixture *fixture,
                                      context_repository_report *report) {
    context_repository_batch *batch = NULL;
    CHECK(context_repository_scan(fixture->root, &batch, report));
    CHECK(batch != NULL && report->error[0] == '\0');
    CHECK(report->files == 3U && report->records == 5U);
    CHECK(report->source_hash != 0U);
    return batch;
}

static void deterministic_scan(const repository_fixture *fixture) {
    context_repository_report first, second;
    context_repository_batch *batch = scan(fixture, &first);
    context_repository_batch *again = scan(fixture, &second);
    cgai_life_context *owner = new_owner();
    CHECK(first.bytes == second.bytes && first.source_hash == second.source_hash);
    CHECK(first.skipped == second.skipped && first.skipped >= 5U);
    CHECK(context_repository_apply(batch, owner) == CGAI_LIFE_OK);
    check_records(owner);
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(context_repository_apply(again, owner) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_hash(owner) == before);
    context_repository_destroy(batch);
    context_repository_destroy(again);
    cgai_life_context_destroy(owner);
}

static void attributed_context(const repository_fixture *fixture, cgai_life_context_kind kind) {
    char path[PATH_BYTES];
    context_repository_report report;
    context_repository_batch *batch = NULL;
    fixture_path(path, fixture->root, "notes.session");
    CHECK(context_repository_read_context(path, kind, &batch, &report));
    CHECK(report.files == 1U && report.records == 1U);
    cgai_life_context *owner = new_owner();
    CHECK(context_repository_apply(batch, owner) == CGAI_LIFE_OK);
    CHECK(owner->count == 1U && owner->records[0].kind == (uint32_t)kind);
    CHECK(owner->records[0].first_line == 1U && owner->records[0].last_line == 2U);
    CHECK(strcmp(owner->records[0].text, "attributed assistant context\nlocal activity note\n") ==
          0);
    CHECK(owner->records[0].reviewed == 1U && owner->records[0].learned_mask == 0U);
    context_repository_destroy(batch);
    cgai_life_context_destroy(owner);
}

static void write_long_line(const repository_fixture *fixture) {
    char text[CGAI_LIFE_CONTEXT_TEXT_BYTES + 3U];
    memset(text, 'x', sizeof(text));
    text[sizeof(text) - 2U] = '\n';
    text[sizeof(text) - 1U] = '\0';
    write_file(fixture, "bad.c", text);
}

static void failed_scan(const repository_fixture *fixture) {
    context_repository_report report;
    context_repository_batch *batch = NULL;
    cgai_life_context *owner = new_owner();
    const uint64_t before = cgai_life_context_hash(owner);
    write_long_line(fixture);
    CHECK(!context_repository_scan(fixture->root, &batch, &report));
    CHECK(batch == NULL && report.error[0] != '\0');
    CHECK(cgai_life_context_hash(owner) == before);
    remove_file(fixture, "bad.c");
    cgai_life_context_destroy(owner);
}

static void invalid_operations(const repository_fixture *fixture) {
    char path[PATH_BYTES];
    context_repository_report report;
    context_repository_batch *batch = NULL;
    fixture_path(path, fixture->root, "no-such-file");
    CHECK(!context_repository_scan(NULL, &batch, &report));
    CHECK(batch == NULL);
    CHECK(!context_repository_scan(path, &batch, &report));
    CHECK(batch == NULL && report.error[0] != '\0');
    CHECK(!context_repository_scan(fixture->root, &batch, NULL));
    CHECK(!context_repository_scan(fixture->root, NULL, &report));
    CHECK(!context_repository_read_context(path, CGAI_LIFE_CONTEXT_LLM, &batch, &report));
    CHECK(!context_repository_read_context(path, CGAI_LIFE_CONTEXT_SOURCE, &batch, &report));
    CHECK(context_repository_apply(NULL, NULL) == CGAI_LIFE_INVALID_ARGUMENT);
    context_repository_destroy(NULL);
}

static void remove_links(const repository_fixture *fixture) {
    char path[PATH_BYTES];
    if (fixture->linked_file)
        remove_file(fixture, "linked.c");
    if (fixture->linked_directory) {
        fixture_path(path, fixture->root, "linked-src");
#ifdef _WIN32
        remove_directory(path);
#else
        CHECK(remove(path) == 0);
#endif
    }
}

static void cleanup_fixture(const repository_fixture *fixture) {
    static const char *const files[] = {
        "README.md",      "zeta.c",           "notes.session",
        "src/alpha.h",    ".hidden/nested.c", "build-debug/generated.c",
        "data/private.c", "tests/test.c",     ".secret.c"};
    static const char *const directories[] = {"src", ".hidden", "build-debug", "data", "tests"};
    char path[PATH_BYTES];
    remove_links(fixture);
    for (size_t i = 0U; i < sizeof(files) / sizeof(files[0]); ++i)
        remove_file(fixture, files[i]);
    for (size_t i = 0U; i < sizeof(directories) / sizeof(directories[0]); ++i) {
        fixture_path(path, fixture->root, directories[i]);
        remove_directory(path);
    }
    remove_directory(fixture->root);
}

int main(int argc, char **argv) {
    repository_fixture fixture;
    CHECK(argc == 2);
    create_fixture(&fixture, argv[1]);
    deterministic_scan(&fixture);
    attributed_context(&fixture, CGAI_LIFE_CONTEXT_LLM);
    attributed_context(&fixture, CGAI_LIFE_CONTEXT_ACTIVITY);
    failed_scan(&fixture);
    invalid_operations(&fixture);
    cleanup_fixture(&fixture);
    puts("Native repository chunks, exclusions and attributed context checks passed");
    return 0;
}
