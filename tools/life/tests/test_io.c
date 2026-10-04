/** @file test_io.c @brief Exact learned continuation and malformed bundle rejection. */
#include "life_io.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <sys/resource.h>
#endif

#define CHECK(condition) TEST_CHECK(condition, "Life snapshot check failed")

static int copy_stream(FILE *input, FILE *output) {
    unsigned char buffer[4096];
    while (!feof(input) && !ferror(input)) {
        const size_t count = fread(buffer, 1U, sizeof(buffer), input);
        if (count == 0U)
            break;
        if (fwrite(buffer, 1U, count, output) != count)
            return 0;
    }
    return !ferror(input);
}
static int copy_file(const char *source, const char *destination) {
    FILE *input = fopen(source, "rb");
    FILE *output;
    int ok;
    if (input == NULL)
        return 0;
    output = fopen(destination, "wb");
    if (output == NULL) {
        fclose(input);
        return 0;
    }
    ok = copy_stream(input, output);
    if (fclose(input) != 0)
        ok = 0;
    if (fclose(output) != 0)
        ok = 0;
    return ok;
}

static char *read_snapshot_text(size_t *count) {
    FILE *file = fopen("test-life-io.snapshot", "rb");
    char *buffer = NULL;
    if (file == NULL)
        return NULL;
    buffer = calloc(2097152U, 1U);
    if (buffer != NULL) {
        *count = fread(buffer, 1U, 2097151U, file);
        if (ferror(file) || !feof(file)) {
            free(buffer);
            buffer = NULL;
        }
    }
    CHECK(fclose(file) == 0);
    return buffer;
}
static int write_bad_text(const char *buffer, size_t count) {
    FILE *file = fopen("test-life-io.bad.snapshot", "wb");
    int ok;
    if (file == NULL)
        return 0;
    ok = fwrite(buffer, 1U, count, file) == count;
    if (fclose(file) != 0)
        ok = 0;
    return ok;
}
static int altered_snapshot(const char *needle, const char *replacement) {
    size_t count = 0U;
    char *buffer = read_snapshot_text(&count);
    if (buffer == NULL)
        return 0;
    char *at = strstr(buffer, needle);
    int ok = at != NULL && strlen(needle) == strlen(replacement);
    if (ok) {
        memcpy(at, replacement, strlen(replacement));
        ok = write_bad_text(buffer, count);
    }
    free(buffer);
    return ok;
}

static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    CHECK(fclose(file) == 0);
    return 1;
}

static void equal_runs(const life_run *original, const life_run *restored) {
    CHECK(life_run_hash(original) == life_run_hash(restored));
    CHECK(life_policy_hash(original->policy) == life_policy_hash(restored->policy));
    CHECK(original->stats.training_updates == restored->stats.training_updates);
    CHECK(memcmp(original->last_outputs, restored->last_outputs, sizeof(original->last_outputs)) ==
          0);
}
static void advance_together(life_run *original, life_run *restored, unsigned int ticks) {
    for (unsigned int i = 0U; i < ticks; ++i) {
        CHECK(life_run_tick(original));
        CHECK(life_run_tick(restored));
        equal_runs(original, restored);
    }
}
static void full_ring(life_run *original, life_run *restored) {
    for (size_t i = original->record_count; i < LIFE_REPLAY_CAPACITY; ++i)
        original->records[i] = original->records[0];
    original->record_count = LIFE_REPLAY_CAPACITY;
    original->record_cursor = 7U;
    original->stats.collision_records = LIFE_REPLAY_CAPACITY + 7U;
    original->stats.policy_decisions = original->stats.collision_records;
    CHECK(life_snapshot_save("test-life-io.snapshot", original));
    CHECK(life_snapshot_load("test-life-io.snapshot", restored));
    CHECK(life_run_hash(original) == life_run_hash(restored));
    advance_together(original, restored, 1U);
}

static void legacy_contract(char *buffer, size_t *count) {
    char *shape = strstr(buffer, "SHAPE 32 32 8 ");
    char *config = strstr(buffer, "\nCONFIG ");
    CHECK(shape != NULL && config != NULL);
    shape[strlen("SHAPE 32 32 ")] = '4';
    char *end = strchr(config + 1, '\n');
    CHECK(end != NULL && end[-2] == ' ' && end[-1] == '4');
    memmove(end - 2, end, *count - (size_t)(end - buffer) + 1U);
    *count -= 2U;
    buffer[strlen("LIFE_SNAPSHOT ")] = '1';
}

static void legacy_checkpoint(const life_run *original) {
    size_t count = 0U;
    char *buffer = read_snapshot_text(&count);
    CHECK(buffer != NULL && count != 0U);
    legacy_contract(buffer, &count);
    char *policy = strstr(buffer, "\nPOLICY\n");
    char *hash = strstr(buffer, "\nHASH ");
    CHECK(policy != NULL && hash != NULL && policy < hash);
    FILE *file = fopen("test-life-io.legacy.snapshot", "wb");
    CHECK(file != NULL);
    CHECK(fwrite(buffer, 1U, (size_t)(policy - buffer) + 1U, file) ==
          (size_t)(policy - buffer) + 1U);
    CHECK(fputs(hash + 1, file) >= 0 && fclose(file) == 0);
    CHECK(life_policy_checkpoint_save(original->policy, "test-life-io.legacy.snapshot.policy"));
    life_run restored = {0};
    CHECK(life_snapshot_load("test-life-io.legacy.snapshot", &restored));
    CHECK(life_run_hash(original) == life_run_hash(&restored));
    life_run_destroy(&restored);
    free(buffer);
}

static void restore_checkpoint(const life_run *original, life_run *restored) {
    CHECK(life_snapshot_save("test-life-io.snapshot", original));
    CHECK(!file_exists("test-life-io.snapshot.policy"));
    CHECK(life_snapshot_load("test-life-io.snapshot", restored));
    CHECK(life_run_hash(original) == life_run_hash(restored));
    legacy_checkpoint(original);
}

static int exact_continuation(void) {
    const life_run_config config = {42U, 3U, 1U, LIFE_MODE_LEARNED, 0, LIFE_DEFAULT_MODULES};
    life_run original = {0};
    life_run restored = {0};
    CHECK(life_run_init(&original, &config));
    for (unsigned int i = 0U; i < 3U; ++i)
        CHECK(life_run_tick(&original));
    CHECK(original.record_count != 0U);
    CHECK(original.stats.training_updates != 0U);
    restore_checkpoint(&original, &restored);
    advance_together(&original, &restored, 5U);
    life_run_destroy(&restored);
    full_ring(&original, &restored);
    life_run_destroy(&original);
    life_run_destroy(&restored);
    return 1;
}

static void rejected_bundle(void) {
    life_run rejected = {0};
    CHECK(!life_snapshot_load("test-life-io.bad.snapshot", &rejected));
    CHECK(rejected.policy == NULL);
}
static void malformed_headers(void) {
    CHECK(altered_snapshot("LIFE_SNAPSHOT 3", "LIFE_SNAPSHOT 9"));
    rejected_bundle();
    CHECK(altered_snapshot("SHAPE 32 32", "SHAPE 31 32"));
    rejected_bundle();
    CHECK(altered_snapshot("CONFIG 42 3 1 2 0", "CONFIG 42 3 1 9 0"));
    rejected_bundle();
}
static void malformed_text(void) {
    FILE *file;
    CHECK(copy_file("test-life-io.snapshot", "test-life-io.bad.snapshot"));
    file = fopen("test-life-io.bad.snapshot", "ab");
    CHECK(file != NULL);
    CHECK(fputs("unexpected trailing bytes\n", file) >= 0);
    CHECK(fclose(file) == 0);
    rejected_bundle();
    file = fopen("test-life-io.bad.snapshot", "wb");
    CHECK(file != NULL);
    CHECK(fputs("LIFE_SNAPSHOT 1\nSHAPE 32 32 4 256 16\nCONFIG 184467440737095516160", file) >= 0);
    CHECK(fclose(file) == 0);
    rejected_bundle();
}
static int malformed_bundles(void) {
    malformed_headers();
    malformed_text();
    CHECK(altered_snapshot("CGAI_LIFE_POLICY 2", "CGAI_LIFE_POLICY 9"));
    rejected_bundle();
    CHECK(altered_snapshot("\nPOLICY\n", "\nABSENT\n"));
    rejected_bundle();
    return 1;
}

static void truncated_policy(void) {
    size_t count = 0U;
    char *buffer = read_snapshot_text(&count);
    CHECK(buffer != NULL && count != 0U);
    char *policy = strstr(buffer, "CGAI_LIFE_POLICY");
    CHECK(policy != NULL);
    CHECK(write_bad_text(buffer, (size_t)(policy - buffer) + strlen("CGAI_LIFE_POLICY 1\n")));
    rejected_bundle();
    free(buffer);
}

static void corrupt_policy_value(void) {
    size_t count = 0U;
    char *buffer = read_snapshot_text(&count);
    CHECK(buffer != NULL);
    char *policy = strstr(buffer, "CGAI_LIFE_POLICY");
    CHECK(policy != NULL);
    char *value = strstr(policy, "0x");
    CHECK(value != NULL);
    memcpy(value, "nan", 3U);
    CHECK(write_bad_text(buffer, count));
    rejected_bundle();
    free(buffer);
}

static int failed_save(const life_run *run) {
#ifdef _WIN32
    HANDLE locked = CreateFileA("test-life-io.snapshot", GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(locked != INVALID_HANDLE_VALUE);
    const int saved = life_snapshot_save("test-life-io.snapshot", run);
    CHECK(CloseHandle(locked));
    return saved;
#else
    struct rlimit before;
    CHECK(getrlimit(RLIMIT_FSIZE, &before) == 0);
    struct rlimit limited = before;
    limited.rlim_cur = 1024U;
    void (*handler)(int) = signal(SIGXFSZ, SIG_IGN);
    CHECK(handler != SIG_ERR);
    CHECK(setrlimit(RLIMIT_FSIZE, &limited) == 0);
    const int saved = life_snapshot_save("test-life-io.snapshot", run);
    CHECK(setrlimit(RLIMIT_FSIZE, &before) == 0);
    CHECK(signal(SIGXFSZ, handler) != SIG_ERR);
    return saved;
#endif
}

static int atomic_replacement(void) {
    const life_run_config config = {42U, 3U, 1U, LIFE_MODE_LEARNED, 0, LIFE_DEFAULT_MODULES};
    life_run original = {0};
    life_run restored = {0};
    CHECK(life_run_init(&original, &config));
    CHECK(life_snapshot_save("test-life-io.snapshot", &original));
    const uint64_t before = life_run_hash(&original);
    CHECK(life_run_tick(&original));
    CHECK(!failed_save(&original));
    CHECK(life_snapshot_load("test-life-io.snapshot", &restored));
    CHECK(life_run_hash(&restored) == before);
    life_run_destroy(&restored);
    CHECK(life_snapshot_save("test-life-io.snapshot", &original));
    CHECK(life_snapshot_load("test-life-io.snapshot", &restored));
    CHECK(life_run_hash(&restored) == life_run_hash(&original));
    life_run_destroy(&original);
    life_run_destroy(&restored);
    return 1;
}

static int invalid_category_with_matching_hash(void) {
    const life_run_config config = {42U, 3U, 1U, LIFE_MODE_LEARNED, 0, LIFE_DEFAULT_MODULES};
    life_run original = {0};
    life_run rejected = {0};
    CHECK(life_run_init(&original, &config));
    CHECK(life_run_tick(&original));
    CHECK(original.record_count != 0U);
    original.records[0].state.values[6] = 5U; /* This field's cardinality is five. */
    CHECK(life_snapshot_save("test-life-io.bad.snapshot", &original));
    CHECK(!life_snapshot_load("test-life-io.bad.snapshot", &rejected));
    CHECK(rejected.policy == NULL);
    life_run_destroy(&original);
    return 1;
}

int main(void) {
    remove("test-life-io.snapshot.policy");
    const int ok =
        exact_continuation() && malformed_bundles() && invalid_category_with_matching_hash();
    truncated_policy();
    corrupt_policy_value();
    CHECK(atomic_replacement());
    remove("test-life-io.snapshot");
    remove("test-life-io.snapshot.policy");
    remove("test-life-io.bad.snapshot");
    remove("test-life-io.bad.snapshot.policy");
    remove("test-life-io.legacy.snapshot");
    remove("test-life-io.legacy.snapshot.policy");
    if (ok)
        puts("Life snapshot continuation and malformed input checks passed");
    return ok ? 0 : 1;
}
