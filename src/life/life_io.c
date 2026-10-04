/** @file life_io.c @brief Canonical state bundles and native text trace artifacts. */
#include "life_io.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

struct life_trace {
    FILE *text;
    FILE *tsv;
    size_t frames;
    int failed;
};

static char *path_suffix(const char *path, const char *suffix) {
    const size_t a = strlen(path);
    const size_t b = strlen(suffix);
    char *result;
    if (a > SIZE_MAX - b - 1U)
        return NULL;
    result = (char *)malloc(a + b + 1U);
    if (result != NULL) {
        memcpy(result, path, a);
        memcpy(result + a, suffix, b + 1U);
    }
    return result;
}

/* Strict bounded tokens avoid scanf's silent integer overflow and trailing junk. */
static int token(FILE *file, char text[128]) {
    size_t n = 0U;
    int c;
    if (ferror(file) || feof(file))
        return 0;
    do {
        c = fgetc(file);
    } while (c != EOF && isspace((unsigned char)c));
    if (c == EOF)
        return 0;
    do {
        if (n >= 127U || c == 0)
            return 0;
        text[n++] = (char)c;
        c = fgetc(file);
    } while (c != EOF && !isspace((unsigned char)c));
    text[n] = '\0';
    return !ferror(file);
}

static int label(FILE *file, const char *expected) {
    char text[128];
    return token(file, text) && strcmp(text, expected) == 0;
}

static int unsigned_value(FILE *file, uint64_t limit, uint64_t *value) {
    char text[128];
    char *end = NULL;
    unsigned long long result;
    if (!token(file, text) || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    result = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || result > limit)
        return 0;
    *value = (uint64_t)result;
    return 1;
}

static int read_u32(FILE *file, uint32_t limit, uint32_t *value) {
    uint64_t result;
    if (!unsigned_value(file, limit, &result))
        return 0;
    *value = (uint32_t)result;
    return 1;
}

static int read_u8(FILE *file, uint8_t limit, uint8_t *value) {
    uint64_t result;
    if (!unsigned_value(file, limit, &result))
        return 0;
    *value = (uint8_t)result;
    return 1;
}

static int read_double(FILE *file, double *value) {
    char text[128];
    char *end = NULL;
    double result;
    if (!token(file, text))
        return 0;
    const char *start = text[0] == '-' ? text + 1 : text;
    if (start[0] != '0' || start[1] != 'x')
        return 0;
    errno = 0;
    result = strtod(text, &end);
    if ((errno != 0 && errno != ERANGE) || *end != '\0' || !isfinite(result) || result < 0.0)
        return 0;
    *value = result;
    return 1;
}

static int write_entities(FILE *file, const life_world *world) {
    for (size_t i = 0U; i < world->group_count; ++i) {
        const life_entity *entity = &world->entities[i];
        if (fprintf(file, "ENTITY %" PRIu32 " %u %u\n", entity->uid, (unsigned int)entity->ancestry,
                    (unsigned int)entity->active) < 0)
            return 0;
    }
    return 1;
}
static int write_conflicts(FILE *file, const life_world *world) {
    for (size_t i = 0U; i < LIFE_MAX_CONFLICTS; ++i) {
        const life_conflict *conflict = &world->conflicts[i];
        if (fprintf(file, "CONFLICT %" PRIu32 " %" PRIu32 " %" PRIu32 " %u %u %u %u %u\n",
                    conflict->id, conflict->age, conflict->last_tick,
                    (unsigned int)conflict->module_mask, (unsigned int)conflict->active,
                    (unsigned int)conflict->contact_streak,
                    (unsigned int)conflict->separation_streak, (unsigned int)conflict->outcome) < 0)
            return 0;
    }
    return 1;
}
static int write_world_stats(FILE *file, const life_world *world) {
    return fprintf(file,
                   "WORLD_STATS %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32
                   " %" PRIu32 " %" PRIu32 " %" PRIu32 "\nCELLS",
                   world->stats.collisions, world->stats.reopened, world->stats.separated,
                   world->stats.coupled, world->stats.absorbed, world->stats.extinct,
                   world->stats.merged, world->stats.edits) >= 0;
}
static int write_cells(FILE *file, const life_world *world) {
    for (size_t i = 0U; i < LIFE_CELLS; ++i)
        if (fprintf(file, " %u", (unsigned int)world->cells[i]) < 0)
            return 0;
    return fputc('\n', file) != EOF;
}
static int write_world(FILE *file, const life_world *world) {
    return fprintf(file, "WORLD %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n", world->tick,
                   world->seed, world->next_uid, world->next_conflict_id) >= 0 &&
           write_entities(file, world) && write_conflicts(file, world) &&
           write_world_stats(file, world) && write_cells(file, world);
}

static int read_entity(FILE *file, life_entity *entity, uint8_t group_mask) {
    return label(file, "ENTITY") && read_u32(file, UINT32_MAX, &entity->uid) &&
           read_u8(file, group_mask, &entity->ancestry) && read_u8(file, 1U, &entity->active);
}
static int read_conflict(FILE *file, life_conflict *conflict, uint32_t tick, uint8_t group_mask) {
    return label(file, "CONFLICT") && read_u32(file, UINT32_MAX, &conflict->id) &&
           read_u32(file, UINT32_MAX, &conflict->age) &&
           read_u32(file, tick, &conflict->last_tick) &&
           read_u8(file, group_mask, &conflict->module_mask) &&
           read_u8(file, 1U, &conflict->active) &&
           read_u8(file, LIFE_RESOLUTION_TICKS, &conflict->contact_streak) &&
           read_u8(file, LIFE_RESOLUTION_TICKS, &conflict->separation_streak) &&
           read_u8(file, LIFE_MERGED, &conflict->outcome);
}
static int read_world_stats(FILE *file, life_world *world) {
    return label(file, "WORLD_STATS") && read_u32(file, UINT32_MAX, &world->stats.collisions) &&
           read_u32(file, UINT32_MAX, &world->stats.reopened) &&
           read_u32(file, UINT32_MAX, &world->stats.separated) &&
           read_u32(file, UINT32_MAX, &world->stats.coupled) &&
           read_u32(file, UINT32_MAX, &world->stats.absorbed) &&
           read_u32(file, UINT32_MAX, &world->stats.extinct) &&
           read_u32(file, UINT32_MAX, &world->stats.merged) &&
           read_u32(file, UINT32_MAX, &world->stats.edits);
}
static int read_cells(FILE *file, life_world *world) {
    if (!label(file, "CELLS"))
        return 0;
    for (size_t i = 0U; i < LIFE_CELLS; ++i)
        if (!read_u8(file, life_world_group_mask(world), &world->cells[i]))
            return 0;
    return 1;
}
static int read_world(FILE *file, life_world *world, uint32_t groups) {
    world->group_count = groups;
    if (!label(file, "WORLD") || !read_u32(file, UINT32_MAX, &world->tick) ||
        !read_u32(file, UINT32_MAX, &world->seed) ||
        !read_u32(file, UINT32_MAX, &world->next_uid) ||
        !read_u32(file, UINT32_MAX, &world->next_conflict_id))
        return 0;
    for (size_t i = 0U; i < groups; ++i)
        if (!read_entity(file, &world->entities[i], life_world_group_mask(world)))
            return 0;
    for (size_t i = 0U; i < LIFE_MAX_CONFLICTS; ++i)
        if (!read_conflict(file, &world->conflicts[i], world->tick, life_world_group_mask(world)))
            return 0;
    return read_world_stats(file, world) && read_cells(file, world);
}

static unsigned int bit_count(uint8_t bits) {
    unsigned int count = 0U;
    while (bits != 0U) {
        count += bits & 1U;
        bits = (uint8_t)(bits >> 1U);
    }
    return count;
}
static int unique_entity(const life_world *world, size_t index) {
    const life_entity *entity = &world->entities[index];
    for (size_t i = 0U; i < index; ++i)
        if (entity->active != 0U && world->entities[i].active != 0U &&
            entity->uid == world->entities[i].uid)
            return 0;
    return 1;
}
static int unused_entities_zero(const life_run *run, const life_policy_info *info) {
    for (size_t i = run->config.group_count; i < LIFE_MODULES; ++i)
        if (run->world.entities[i].uid != 0U || run->world.entities[i].ancestry != 0U ||
            run->world.entities[i].active != 0U || info->module_mass[i] != 0.0)
            return 0;
    return 1;
}
static int validate_entities(const life_run *run, const life_policy_info *info) {
    uint32_t active_mask = 0U;
    uint8_t ancestry = 0U;
    for (size_t i = 0U; i < run->config.group_count; ++i) {
        const life_entity *entity = &run->world.entities[i];
        if (entity->uid == 0U || entity->uid >= run->world.next_uid || entity->ancestry == 0U ||
            !unique_entity(&run->world, i))
            return 0;
        if (entity->active != 0U) {
            if ((ancestry & entity->ancestry) != 0U ||
                info->module_mass[i] != (double)bit_count(entity->ancestry))
                return 0;
            ancestry = (uint8_t)(ancestry | entity->ancestry);
            active_mask |= 1U << i;
        }
    }
    return unused_entities_zero(run, info) && active_mask == info->active_mask &&
           ancestry == life_world_group_mask(&run->world);
}
static int empty_conflict(const life_conflict *conflict) {
    return conflict->age == 0U && conflict->last_tick == 0U && conflict->module_mask == 0U &&
           conflict->active == 0U && conflict->contact_streak == 0U &&
           conflict->separation_streak == 0U && conflict->outcome == 0U;
}
static int validate_conflict(const life_world *world, size_t index, uint32_t active) {
    const life_conflict *conflict = &world->conflicts[index];
    if (conflict->id == 0U)
        return empty_conflict(conflict);
    if (conflict->id >= world->next_conflict_id || conflict->age > world->tick ||
        bit_count(conflict->module_mask) < 2U)
        return 0;
    if (conflict->active != 0U &&
        ((conflict->outcome != LIFE_UNRESOLVED && conflict->outcome != LIFE_COUPLED) ||
         ((uint32_t)conflict->module_mask & ~active) != 0U))
        return 0;
    for (size_t i = 0U; i < index; ++i)
        if (conflict->id == world->conflicts[i].id)
            return 0;
    return 1;
}
static int validate_world(const life_run *run) {
    const life_world *world = &run->world;
    life_policy_info info;
    if (run->config.group_count < LIFE_MIN_MODULES || run->config.group_count > LIFE_MODULES ||
        world->group_count != run->config.group_count || world->next_uid == 0U ||
        world->next_conflict_id == 0U || world->seed != (uint32_t)run->config.seed ||
        run->last_merge_attempt > world->tick ||
        life_policy_inspect(run->policy, &info) != CGAI_STATUS_OK ||
        info.group_count != run->config.group_count || info.seed != run->config.seed ||
        info.updates < run->stats.training_updates || !validate_entities(run, &info))
        return 0;
    for (size_t i = 0U; i < LIFE_CELLS; ++i)
        if (((uint32_t)world->cells[i] & ~info.active_mask) != 0U)
            return 0;
    for (size_t i = 0U; i < LIFE_MAX_CONFLICTS; ++i)
        if (!validate_conflict(world, i, info.active_mask))
            return 0;
    return 1;
}

static int write_contract(FILE *file, const life_run_config *config) {
    return fprintf(file,
                   "LIFE_SNAPSHOT 3\nSHAPE %u %u %u %u %u\n"
                   "CONFIG %" PRIu64 " %" PRIu32 " %" PRIu32 " %u %u %" PRIu32 "\n",
                   (unsigned int)LIFE_WIDTH, (unsigned int)LIFE_HEIGHT, (unsigned int)LIFE_MODULES,
                   (unsigned int)LIFE_REPLAY_CAPACITY, (unsigned int)CGAI_GAMEPLAY_MAX_FEATURES,
                   config->seed, config->scenario, config->training_epochs,
                   (unsigned int)config->mode, (unsigned int)config->enable_merges,
                   config->group_count) >= 0;
}
static int write_run_stats(FILE *file, const life_run *run) {
    return fprintf(file,
                   "RUN_STATS %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
                   " %" PRIu64 " %" PRIu64 " %" PRIu64 " %a %a\nREPLAY %zu %zu %" PRIu64 "\n",
                   run->stats.collision_records, run->stats.training_updates,
                   run->stats.edited_cells, run->stats.fallback_calls, run->stats.merge_attempts,
                   run->stats.accepted_merges, run->stats.rejected_merges,
                   run->stats.teacher_agreements, run->stats.policy_decisions,
                   run->stats.loss_before, run->stats.loss_after, run->record_count,
                   run->record_cursor, run->last_merge_attempt) >= 0;
}
static int write_record(FILE *file, const life_record *record) {
    if (fprintf(file, "RECORD %" PRIu32 " %" PRIu32, record->target, record->module_mask) < 0)
        return 0;
    for (size_t n = 0U; n < CGAI_GAMEPLAY_MAX_FEATURES; ++n)
        if (fprintf(file, " %" PRIu32, record->state.values[n]) < 0)
            return 0;
    return fputc('\n', file) != EOF;
}
static int write_records(FILE *file, const life_run *run) {
    for (size_t i = 0U; i < run->record_count; ++i)
        if (!write_record(file, &run->records[i]))
            return 0;
    return 1;
}

typedef struct life_atomic_file {
    FILE *stream;
    char *path;
} life_atomic_file;

static uint32_t process_id(void) {
#ifdef _WIN32
    return (uint32_t)GetCurrentProcessId();
#else
    return (uint32_t)getpid();
#endif
}

static char *temporary_path(const char *path, uint64_t stamp, unsigned int attempt) {
    char suffix[96];
    const int size = snprintf(suffix, sizeof(suffix), ".tmp.%" PRIu32 ".%" PRIu64 ".%u",
                              process_id(), stamp, attempt);
    if (size < 0 || (size_t)size >= sizeof(suffix))
        return NULL;
    return path_suffix(path, suffix);
}

static int try_temporary(const char *path, uint64_t stamp, unsigned int attempt,
                         life_atomic_file *temporary) {
    temporary->path = temporary_path(path, stamp, attempt);
    if (temporary->path == NULL)
        return -1;
    temporary->stream = fopen(temporary->path, "wbx");
    if (temporary->stream != NULL)
        return 1;
    const int error = errno;
    free(temporary->path);
    temporary->path = NULL;
    return error == EEXIST ? 0 : -1;
}

static int open_temporary(const char *path, life_atomic_file *temporary) {
    struct timespec time = {0};
    (void)timespec_get(&time, TIME_UTC);
    const uint64_t stamp = (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
    for (unsigned int attempt = 0U; attempt < 128U; ++attempt) {
        const int result = try_temporary(path, stamp, attempt, temporary);
        if (result != 0)
            return result > 0;
    }
    return 0;
}

static int replace_checkpoint(const char *temporary, const char *path) {
#ifdef _WIN32
    return MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, path) == 0;
#endif
}

static int write_snapshot(FILE *file, const life_run *run) {
    return write_contract(file, &run->config) && write_world(file, &run->world) &&
           write_run_stats(file, run) && write_records(file, run) && fputs("POLICY\n", file) >= 0 &&
           life_policy_checkpoint_write(run->policy, file) == CGAI_STATUS_OK &&
           fprintf(file, "HASH %" PRIu64 "\nEND\n", life_run_hash(run)) >= 0;
}

int life_snapshot_write(FILE *file, const life_run *run) {
    return file != NULL && run != NULL && run->policy != NULL && write_snapshot(file, run);
}

int life_checkpoint_publish(const char *path, const void *owner,
                            int (*write_owner)(FILE *, const void *)) {
    life_atomic_file temporary = {0};
    int ok;
    if (path == NULL || path[0] == '\0' || owner == NULL || write_owner == NULL)
        return 0;
    if (!open_temporary(path, &temporary))
        return 0;
    ok = write_owner(temporary.stream, owner);
    if (fclose(temporary.stream) != 0)
        ok = 0;
    if (ok)
        ok = replace_checkpoint(temporary.path, path);
    if (!ok)
        (void)remove(temporary.path);
    free(temporary.path);
    return ok;
}

static int write_life_owner(FILE *file, const void *owner) {
    return life_snapshot_write(file, (const life_run *)owner);
}

int life_snapshot_save(const char *path, const life_run *run) {
    return run != NULL && run->policy != NULL &&
           life_checkpoint_publish(path, run, write_life_owner);
}

static int read_shape(FILE *file, uint32_t *version) {
    uint64_t shape[] = {LIFE_WIDTH, LIFE_HEIGHT, LIFE_MODULES, LIFE_REPLAY_CAPACITY,
                        CGAI_GAMEPLAY_MAX_FEATURES};
    uint64_t number = 0U;
    if (!label(file, "LIFE_SNAPSHOT") || !read_u32(file, 3U, version) || *version == 0U ||
        !label(file, "SHAPE"))
        return 0;
    if (*version < 3U)
        shape[2] = LIFE_DEFAULT_MODULES;
    for (size_t i = 0U; i < sizeof(shape) / sizeof(shape[0]); ++i)
        if (!unsigned_value(file, shape[i], &number) || number != shape[i])
            return 0;
    return 1;
}
static int read_config(FILE *file, life_run_config *config, uint32_t version) {
    uint64_t mode = 0U, merges = 0U;
    if (!label(file, "CONFIG") || !unsigned_value(file, UINT32_MAX, &config->seed) ||
        !read_u32(file, 3U, &config->scenario) || !read_u32(file, 64U, &config->training_epochs) ||
        !unsigned_value(file, 2U, &mode) || !unsigned_value(file, 1U, &merges))
        return 0;
    config->mode = (life_mode)mode;
    config->enable_merges = (int)merges;
    config->group_count = LIFE_DEFAULT_MODULES;
    return version < 3U || (read_u32(file, LIFE_MODULES, &config->group_count) &&
                            config->group_count >= LIFE_MIN_MODULES);
}
static int read_run_stats(FILE *file, life_run_stats *stats) {
    return label(file, "RUN_STATS") &&
           unsigned_value(file, UINT64_MAX, &stats->collision_records) &&
           unsigned_value(file, UINT64_MAX, &stats->training_updates) &&
           unsigned_value(file, UINT64_MAX, &stats->edited_cells) &&
           unsigned_value(file, UINT64_MAX, &stats->fallback_calls) &&
           unsigned_value(file, UINT64_MAX, &stats->merge_attempts) &&
           unsigned_value(file, UINT64_MAX, &stats->accepted_merges) &&
           unsigned_value(file, UINT64_MAX, &stats->rejected_merges) &&
           unsigned_value(file, UINT64_MAX, &stats->teacher_agreements) &&
           unsigned_value(file, UINT64_MAX, &stats->policy_decisions) &&
           read_double(file, &stats->loss_before) && read_double(file, &stats->loss_after);
}
static int read_record(FILE *file, life_record *record, uint32_t groups) {
    if (!label(file, "RECORD") || !read_u32(file, LIFE_OUTPUTS - 1U, &record->target) ||
        !read_u32(file, (1U << groups) - 1U, &record->module_mask) || record->module_mask == 0U)
        return 0;
    for (size_t n = 0U; n < CGAI_GAMEPLAY_MAX_FEATURES; ++n) {
        const uint32_t maximum = n == 15U ? groups : (n < 6U ? 54U : (n < 12U ? 4U : 7U));
        if (!read_u32(file, maximum, &record->state.values[n]))
            return 0;
    }
    return 1;
}
static int read_records(FILE *file, life_run *run) {
    uint64_t count = 0U, cursor = 0U;
    if (!label(file, "REPLAY") || !unsigned_value(file, LIFE_REPLAY_CAPACITY, &count) ||
        !unsigned_value(file, LIFE_REPLAY_CAPACITY - 1U, &cursor) ||
        !unsigned_value(file, UINT32_MAX, &run->last_merge_attempt) ||
        (count < LIFE_REPLAY_CAPACITY && cursor != count))
        return 0;
    run->record_count = (size_t)count;
    run->record_cursor = (size_t)cursor;
    for (size_t i = 0U; i < run->record_count; ++i)
        if (!read_record(file, &run->records[i], run->config.group_count))
            return 0;
    return 1;
}
static int trailing_space(FILE *file) {
    int c = EOF;
    if (ferror(file))
        return 0;
    while (!feof(file)) {
        c = fgetc(file);
        if (c == EOF)
            break;
        if (!isspace((unsigned char)c))
            return 0;
    }
    return !ferror(file);
}
static int read_snapshot_file(FILE *file, life_run *run, uint64_t *hash, uint32_t *version) {
    if (!read_shape(file, version) || !read_config(file, &run->config, *version) ||
        !read_world(file, &run->world, run->config.group_count) ||
        !read_run_stats(file, &run->stats) || !read_records(file, run))
        return 0;
    if (*version >= 2U) {
        if (!label(file, "POLICY"))
            return 0;
        run->policy = life_policy_checkpoint_read(file);
        if (run->policy == NULL)
            return 0;
    }
    return label(file, "HASH") && unsigned_value(file, UINT64_MAX, hash) && label(file, "END");
}

static int read_snapshot_path(const char *path, life_run *run, uint64_t *hash, uint32_t *version) {
    FILE *file = fopen(path, "rb");
    int ok;
    if (file == NULL)
        return 0;
    ok = read_snapshot_file(file, run, hash, version) && trailing_space(file);
    if (fclose(file) != 0)
        ok = 0;
    return ok;
}
static life_policy *load_policy_sidecar(const char *path) {
    char *policy_path = path_suffix(path, ".policy");
    life_policy *policy;
    if (policy_path == NULL)
        return NULL;
    policy = life_policy_checkpoint_load(policy_path);
    free(policy_path);
    return policy;
}
static int coherent_stats(const life_run *run) {
    const life_run_stats *stats = &run->stats;
    const size_t count = stats->collision_records < LIFE_REPLAY_CAPACITY
                             ? (size_t)stats->collision_records
                             : LIFE_REPLAY_CAPACITY;
    return stats->teacher_agreements <= stats->policy_decisions &&
           stats->fallback_calls <= stats->policy_decisions &&
           stats->accepted_merges <= stats->merge_attempts &&
           stats->rejected_merges == stats->merge_attempts - stats->accepted_merges &&
           stats->accepted_merges == run->world.stats.merged &&
           stats->policy_decisions ==
               (run->config.mode == LIFE_MODE_LEARNED ? stats->collision_records : 0U) &&
           run->record_count == count &&
           run->record_cursor == stats->collision_records % LIFE_REPLAY_CAPACITY;
}
static int coherent_replay(const life_run *run) {
    const uint32_t active = life_policy_active_mask(run->policy);
    for (size_t i = 0U; i < run->record_count; ++i)
        if ((run->records[i].module_mask & ~active) != 0U)
            return 0;
    return coherent_stats(run);
}

int life_snapshot_read(FILE *file, life_run *run) {
    life_run candidate = {0};
    uint64_t expected_hash = 0U;
    uint32_t version = 0U;
    int ok;
    if (file == NULL || run == NULL)
        return 0;
    ok = read_snapshot_file(file, &candidate, &expected_hash, &version) && version >= 2U;
    ok = ok && candidate.policy != NULL && validate_world(&candidate) &&
         coherent_replay(&candidate) && life_run_hash(&candidate) == expected_hash;
    if (!ok) {
        life_run_destroy(&candidate);
        return 0;
    }
    *run = candidate;
    return 1;
}
int life_snapshot_load(const char *path, life_run *run) {
    life_run candidate = {0};
    uint64_t expected_hash = 0U;
    uint32_t version = 0U;
    int ok;
    if (path == NULL || run == NULL)
        return 0;
    ok = read_snapshot_path(path, &candidate, &expected_hash, &version);
    if (ok && version == 1U)
        candidate.policy = load_policy_sidecar(path);
    ok = ok && candidate.policy != NULL && validate_world(&candidate) &&
         coherent_replay(&candidate) && life_run_hash(&candidate) == expected_hash;
    if (!ok) {
        life_run_destroy(&candidate);
        return 0;
    }
    *run = candidate;
    return 1;
}

static FILE *open_suffix(const char *prefix, const char *suffix) {
    char *path = path_suffix(prefix, suffix);
    FILE *file;
    if (path == NULL)
        return NULL;
    file = fopen(path, "wb");
    free(path);
    return file;
}

static int write_trace_header(life_trace *trace, const life_run *run) {
    const unsigned int version = run->config.group_count > LIFE_DEFAULT_MODULES ? 2U : 1U;
    if (fprintf(trace->text,
                "CGAI_LIFE_TRACE %u\nSHAPE %u %u %" PRIu32 "\nCONFIG %" PRIu64 " %" PRIu32
                " %u\n# GRID rows use hexadecimal lineage claims.\n",
                version, (unsigned int)LIFE_WIDTH, (unsigned int)LIFE_HEIGHT,
                run->config.group_count, run->config.seed, run->config.scenario,
                (unsigned int)run->config.mode) < 0 ||
        (version == 2U && fputs("CLAIM_ENCODING HEX2\n", trace->text) < 0))
        return 0;
    return fputs("generation\tpopulation\thash\tactive_conflicts\tcollisions\tseparated\t"
                 "coupled\tabsorbed\textinct\tmerged\tedited_cells\ttraining_updates\t"
                 "loss_before\tloss_after\tteacher_agreements\tpolicy_decisions\n",
                 trace->tsv) >= 0;
}

life_trace *life_trace_open(const char *prefix, const life_run *run) {
    if (prefix == NULL || prefix[0] == '\0' || run == NULL || run->policy == NULL)
        return NULL;
    life_trace *trace = calloc(1U, sizeof(*trace));
    if (trace == NULL)
        return NULL;
    trace->text = open_suffix(prefix, ".trace");
    trace->tsv = open_suffix(prefix, ".tsv");
    if (trace->text == NULL || trace->tsv == NULL || !write_trace_header(trace, run)) {
        trace->failed = 1;
        life_trace_close(trace);
        return NULL;
    }
    return trace;
}

static int write_grid(FILE *file, const life_world *world) {
    static const char claims[] = "0123456789abcdef";
    if (fputs("GRID\n", file) < 0)
        return 0;
    for (size_t y = 0U; y < LIFE_HEIGHT; ++y) {
        for (size_t x = 0U; x < LIFE_WIDTH; ++x) {
            const uint8_t cell = world->cells[y * LIFE_WIDTH + x];
            if ((cell & (uint8_t)~life_world_group_mask(world)) != 0U ||
                (world->group_count > LIFE_DEFAULT_MODULES &&
                 fputc(claims[cell >> 4U], file) == EOF) ||
                fputc(claims[cell & 15U], file) == EOF)
                return 0;
        }
        if (fputc('\n', file) == EOF)
            return 0;
    }
    return 1;
}

static int write_entity(FILE *file, const life_run *run, uint32_t module) {
    const life_entity *entity = &run->world.entities[module];
    double vector[LIFE_POLICY_HIDDEN];
    if (!life_policy_module_vector(run->policy, module, vector) ||
        fprintf(file, "ENTITY %" PRIu32 " %" PRIu32 " %u %u %" PRIu32 "\nCENTER", module,
                entity->uid, (unsigned int)entity->active, (unsigned int)entity->ancestry,
                life_population(&run->world, (uint8_t)(1U << module))) < 0)
        return 0;
    for (size_t i = 0U; i < LIFE_POLICY_HIDDEN; ++i)
        if (!isfinite(vector[i]) || fprintf(file, " %a", vector[i]) < 0)
            return 0;
    return fputc('\n', file) != EOF;
}

static int write_trace_entities(FILE *file, const life_run *run) {
    for (uint32_t module = 0U; module < run->config.group_count; ++module)
        if (!write_entity(file, run, module))
            return 0;
    return 1;
}

static int write_conflict(FILE *file, const life_conflict *conflict) {
    return fprintf(file, "CONFLICT %" PRIu32 " %" PRIu32 " %u %u %u %u %u\n", conflict->id,
                   conflict->age, (unsigned int)conflict->module_mask,
                   (unsigned int)conflict->active, (unsigned int)conflict->contact_streak,
                   (unsigned int)conflict->separation_streak, (unsigned int)conflict->outcome) >= 0;
}

static int write_trace_conflicts(FILE *file, const life_world *world) {
    for (size_t i = 0U; i < LIFE_MAX_CONFLICTS; ++i)
        if (world->conflicts[i].id != 0U && !write_conflict(file, &world->conflicts[i]))
            return 0;
    return 1;
}

static int write_patch(FILE *file, const life_run *run, size_t index) {
    const life_patch *patch = &run->last_frame.patches[index];
    if (fprintf(file, "PATCH %" PRIu32 " %u %" PRIu32 " %u", patch->conflict_id,
                (unsigned int)patch->module_mask, run->last_outputs[index],
                (unsigned int)patch->cell_count) < 0)
        return 0;
    for (size_t i = 0U; i < patch->cell_count; ++i)
        if (fprintf(file, " %u", (unsigned int)patch->cells[i]) < 0)
            return 0;
    return fputc('\n', file) != EOF;
}

static int write_patches(FILE *file, const life_run *run) {
    for (size_t i = 0U; i < run->last_frame.patch_count; ++i)
        if (!write_patch(file, run, i))
            return 0;
    return 1;
}

int life_inspect(FILE *file, const life_run *run) {
    if (file == NULL || run == NULL || run->policy == NULL)
        return 0;
    return fprintf(file, "FRAME %" PRIu32 " %016" PRIx64 "\nPOLICY %016" PRIx64 "\n",
                   run->world.tick, life_run_hash(run), life_policy_hash(run->policy)) >= 0 &&
           write_grid(file, &run->world) && write_trace_entities(file, run) &&
           write_trace_conflicts(file, &run->world) && write_patches(file, run) &&
           fputs("END_FRAME\n", file) >= 0;
}

static int write_metrics(FILE *file, const life_run *run) {
    unsigned int active = 0U;
    for (size_t i = 0U; i < LIFE_MAX_CONFLICTS; ++i)
        active += run->world.conflicts[i].active != 0U ? 1U : 0U;
    return fprintf(file,
                   "%" PRIu32 "\t%" PRIu32 "\t%016" PRIx64 "\t%u\t%" PRIu32 "\t%" PRIu32
                   "\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu64 "\t%" PRIu64
                   "\t%.17g\t%.17g\t%" PRIu64 "\t%" PRIu64 "\n",
                   run->world.tick,
                   life_population(&run->world, life_world_group_mask(&run->world)),
                   life_run_hash(run), active, run->world.stats.collisions,
                   run->world.stats.separated, run->world.stats.coupled, run->world.stats.absorbed,
                   run->world.stats.extinct, run->world.stats.merged, run->stats.edited_cells,
                   run->stats.training_updates, run->stats.loss_before, run->stats.loss_after,
                   run->stats.teacher_agreements, run->stats.policy_decisions) >= 0;
}

int life_trace_append(life_trace *trace, const life_run *run) {
    if (trace == NULL || run == NULL || trace->failed)
        return 0;
    const int okay = life_inspect(trace->text, run) && write_metrics(trace->tsv, run);
    if (okay)
        ++trace->frames;
    else
        trace->failed = 1;
    return okay;
}

int life_trace_close(life_trace *trace) {
    if (trace == NULL)
        return 0;
    int okay = !trace->failed;
    if (trace->text != NULL) {
        if (okay && fprintf(trace->text, "END_TRACE %zu\n", trace->frames) < 0)
            okay = 0;
        if (fclose(trace->text) != 0)
            okay = 0;
    }
    if (trace->tsv != NULL && fclose(trace->tsv) != 0)
        okay = 0;
    free(trace);
    return okay;
}
