/** @file fitness.c @brief Candidate-independent physical samples and frozen objective. */
#include "fitness.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FITNESS_WORLD_SAMPLES 64U
#define FITNESS_SAMPLE_GENERATIONS 32U
#define FITNESS_CONFIRM_WARMUP 8U
#define FITNESS_MAX_UPDATES                                                                        \
    (LIFE_FITNESS_MODELS * LIFE_FITNESS_TRAINING_GENERATIONS * LIFE_FITNESS_TRAINING_EPOCHS * 16U)

static int valid_split(life_fitness_split split) {
    return split >= LIFE_FITNESS_DEV && split <= LIFE_FITNESS_TRAIN;
}

const char *life_fitness_split_name(life_fitness_split split) {
    if (!valid_split(split))
        return NULL;
    static const char *const names[] = {"dev", "confirm", "train"};
    return names[(size_t)split];
}

int life_fitness_split_parse(const char *text, life_fitness_split *output) {
    if (text == NULL || output == NULL)
        return 0;
    if (strcmp(text, "dev") == 0)
        *output = LIFE_FITNESS_DEV;
    else if (strcmp(text, "confirm") == 0)
        *output = LIFE_FITNESS_CONFIRM;
    else if (strcmp(text, "train") == 0)
        *output = LIFE_FITNESS_TRAIN;
    else
        return 0;
    return 1;
}

static uint64_t hash_word(uint64_t hash, uint64_t value) {
    for (unsigned int byte = 0U; byte < 8U; ++byte) {
        hash ^= value & UINT64_C(255);
        hash *= UINT64_C(1099511628211);
        value >>= 8U;
    }
    return hash;
}

static uint64_t hash_record(uint64_t hash, const life_record *record, uint32_t allowed) {
    hash = hash_word(hash, record->module_mask);
    hash = hash_word(hash, record->target);
    hash = hash_word(hash, allowed);
    for (size_t i = 0U; i < LIFE_POLICY_FEATURES; ++i)
        hash = hash_word(hash, record->state.values[i]);
    return hash;
}

static int warm_world(life_world *world, uint32_t generations) {
    for (uint32_t i = 0U; i < generations; ++i) {
        life_frame frame;
        if (life_prepare(world, &frame) != LIFE_OK || life_apply(world, &frame, NULL) != LIFE_OK)
            return 0;
    }
    return 1;
}

static int sample_world(life_world *world, life_fitness_split split, uint32_t sample) {
    static const uint32_t seeds[] = {1009U, 10007U, 30011U};
    static const uint32_t warmups[] = {0U, FITNESS_CONFIRM_WARMUP, 4U};
    const uint32_t seed = seeds[(size_t)split] + sample * 37U;
    const uint32_t warmup = warmups[(size_t)split];
    return life_world_init(world, seed, sample % 4U) == LIFE_OK && warm_world(world, warmup);
}

static int append_patch(life_fitness_pack *pack, const life_world *world, const life_frame *frame,
                        size_t index) {
    const life_patch *patch = &frame->patches[index];
    life_record record = {0};
    int64_t teacher_score = 0;
    const uint32_t allowed = life_allowed_outputs(patch);
    life_encode(world, patch, &record.state);
    record.module_mask = patch->module_mask;
    if (!life_teach(world, frame, index, &record.target, &teacher_score) ||
        record.target >= LIFE_OUTPUTS || (allowed & (UINT32_C(1) << record.target)) == 0U)
        return 0;
    pack->records[pack->count] = record;
    pack->allowed_outputs[pack->count++] = allowed;
    pack->hash = hash_word(pack->hash, life_world_hash(world));
    for (size_t i = 0U; i < patch->cell_count; ++i)
        pack->hash = hash_word(pack->hash, patch->cells[i]);
    pack->hash = hash_record(pack->hash, &record, allowed);
    return 1;
}

static int collect_generation(life_fitness_pack *pack, life_world *world) {
    life_frame frame;
    if (life_prepare(world, &frame) != LIFE_OK)
        return 0;
    for (size_t i = 0U; i < frame.patch_count && pack->count < LIFE_FITNESS_PACK_RECORDS; ++i)
        if (!append_patch(pack, world, &frame, i))
            return 0;
    return life_apply(world, &frame, NULL) == LIFE_OK;
}

static int collect_sample(life_fitness_pack *pack, life_fitness_split split, uint32_t sample) {
    life_world world;
    if (!sample_world(&world, split, sample))
        return 0;
    for (uint32_t i = 0U; i < FITNESS_SAMPLE_GENERATIONS && pack->count < LIFE_FITNESS_PACK_RECORDS;
         ++i)
        if (!collect_generation(pack, &world))
            return 0;
    return 1;
}

int life_fitness_build_pack(life_fitness_split split, life_fitness_pack *output) {
    life_fitness_pack pack = {0};
    if (!valid_split(split) || output == NULL)
        return 0;
    pack.hash = hash_word(UINT64_C(14695981039346656037), (uint64_t)split);
    for (uint32_t sample = 0U;
         sample < FITNESS_WORLD_SAMPLES && pack.count < LIFE_FITNESS_PACK_RECORDS; ++sample)
        if (!collect_sample(&pack, split, sample))
            return 0;
    if (pack.count != LIFE_FITNESS_PACK_RECORDS || pack.hash == 0U)
        return 0;
    *output = pack;
    return 1;
}

static int score_record(life_policy *policy, const life_fitness_pack *pack, size_t index,
                        life_fitness_report *report) {
    const life_record *record = &pack->records[index];
    cgai_gameplay_result selected;
    double loss = 0.0;
    if (!life_policy_evaluate(policy, record, &loss) || !isfinite(loss) || loss < 0.0 ||
        !life_policy_select(policy, &record->state, record->module_mask,
                            pack->allowed_outputs[index], &selected))
        return 0;
    ++report->records;
    report->correct += selected.output == record->target ? 1U : 0U;
    report->mean_loss += loss;
    report->forward_passes += 1U + selected.forward_passes;
    report->active_modules += selected.active_modules;
    report->active_centroids += selected.active_centroids;
    return isfinite(report->mean_loss);
}

static int score_model(life_policy *policy, const life_fitness_pack *pack,
                       life_fitness_report *report) {
    const uint64_t before = life_policy_hash(policy);
    for (size_t i = 0U; i < pack->count; ++i)
        if (!score_record(policy, pack, i, report))
            return 0;
    return before != 0U && life_policy_hash(policy) == before;
}

static int train_model(life_run *run) {
    for (uint32_t i = 0U; i < LIFE_FITNESS_TRAINING_GENERATIONS; ++i)
        if (!life_run_tick(run))
            return 0;
    return 1;
}

static int measure_model(life_fitness_split split, size_t model, const life_fitness_pack *pack,
                         life_fitness_report *report) {
    static const uint32_t seeds[3][LIFE_FITNESS_MODELS] = {
        {42U, 71U, 113U}, {173U, 211U, 257U}, {307U, 347U, 389U}};
    const life_run_config config = {seeds[(size_t)split][model], 3U, LIFE_FITNESS_TRAINING_EPOCHS,
                                    LIFE_MODE_LEARNED,           0,  LIFE_DEFAULT_MODULES};
    life_run run = {0};
    const int ok =
        life_run_init(&run, &config) && train_model(&run) && score_model(run.policy, pack, report);
    if (ok) {
        report->training_updates += run.stats.training_updates;
        ++report->model_count;
    }
    life_run_destroy(&run);
    return ok;
}

static life_fitness_report report_recipe(life_fitness_split split, const life_fitness_pack *pack) {
    life_fitness_report report = {0};
    report.version = LIFE_FITNESS_VERSION;
    report.split = split;
    report.pack_hash = pack->hash;
    report.pack_records = pack->count;
    report.training_epochs = LIFE_FITNESS_TRAINING_EPOCHS;
    report.training_generations = LIFE_FITNESS_TRAINING_GENERATIONS;
    return report;
}

int life_fitness_measure(life_fitness_split split, life_fitness_report *output) {
    life_fitness_pack pack;
    if (output == NULL || !life_fitness_build_pack(split, &pack))
        return 0;
    life_fitness_report report = report_recipe(split, &pack);
    for (size_t model = 0U; model < LIFE_FITNESS_MODELS; ++model)
        if (!measure_model(split, model, &pack, &report))
            return 0;
    report.mean_loss /= (double)report.records;
    report.complete = 1U;
    if (!life_fitness_report_valid(&report))
        return 0;
    *output = report;
    return 1;
}

static int valid_recipe(const life_fitness_report *report) {
    return report->version == LIFE_FITNESS_VERSION && valid_split(report->split) &&
           report->complete == 1U && report->pack_hash != 0U &&
           report->pack_records == LIFE_FITNESS_PACK_RECORDS &&
           report->model_count == LIFE_FITNESS_MODELS &&
           report->training_epochs == LIFE_FITNESS_TRAINING_EPOCHS &&
           report->training_generations == LIFE_FITNESS_TRAINING_GENERATIONS;
}

static int valid_measurements(const life_fitness_report *report) {
    return report->records == (uint64_t)LIFE_FITNESS_PACK_RECORDS * LIFE_FITNESS_MODELS &&
           report->correct <= report->records && isfinite(report->mean_loss) &&
           report->mean_loss >= 0.0 && report->training_updates != 0U &&
           report->training_updates <= FITNESS_MAX_UPDATES &&
           report->forward_passes == report->records * 2U &&
           report->active_modules >= report->records * 2U &&
           report->active_modules <= report->records * LIFE_POLICY_MODULES &&
           report->active_centroids == report->active_modules * 4U;
}

int life_fitness_report_valid(const life_fitness_report *report) {
    return report != NULL && valid_recipe(report) && valid_measurements(report);
}

static int same_scoring(const life_fitness_report *baseline, const life_fitness_report *candidate) {
    return baseline->split == candidate->split && baseline->pack_hash == candidate->pack_hash &&
           baseline->records == candidate->records &&
           baseline->pack_records == candidate->pack_records &&
           baseline->forward_passes == candidate->forward_passes &&
           baseline->active_modules == candidate->active_modules &&
           baseline->active_centroids == candidate->active_centroids;
}

int life_fitness_admits(const life_fitness_report *baseline, const life_fitness_report *candidate) {
    return life_fitness_report_valid(baseline) && life_fitness_report_valid(candidate) &&
           same_scoring(baseline, candidate) && candidate->correct >= baseline->correct &&
           candidate->training_updates <= baseline->training_updates &&
           baseline->mean_loss - candidate->mean_loss >= LIFE_FITNESS_MINIMUM_IMPROVEMENT;
}

static int write_quality(FILE *file, const life_fitness_report *report) {
    return fprintf(file,
                   "LIFE_FITNESS\t%" PRIu32 "\nsplit\t%s\ncomplete\t%" PRIu32
                   "\npack_hash\t%" PRIu64 "\npack_records\t%" PRIu32 "\nrecords\t%" PRIu64
                   "\ncorrect\t%" PRIu64 "\nmean_loss\t%.17g\n",
                   report->version, life_fitness_split_name(report->split), report->complete,
                   report->pack_hash, report->pack_records, report->records, report->correct,
                   report->mean_loss) >= 0;
}

static int write_work(FILE *file, const life_fitness_report *report) {
    return fprintf(file,
                   "model_count\t%" PRIu32 "\ntraining_epochs\t%" PRIu32
                   "\ntraining_generations\t%" PRIu32 "\ntraining_updates\t%" PRIu64
                   "\nforward_passes\t%" PRIu64 "\nactive_modules\t%" PRIu64
                   "\nactive_centroids\t%" PRIu64 "\nEND\n",
                   report->model_count, report->training_epochs, report->training_generations,
                   report->training_updates, report->forward_passes, report->active_modules,
                   report->active_centroids) >= 0;
}

int life_fitness_write(const char *path, const life_fitness_report *report) {
    if (path == NULL || path[0] == '\0' || !life_fitness_report_valid(report))
        return 0;
    FILE *file = fopen(path, "wbx");
    if (file == NULL)
        return 0;
    int ok = write_quality(file, report) && write_work(file, report);
    if (fclose(file) != 0)
        ok = 0;
    if (!ok)
        (void)remove(path);
    return ok;
}

static int read_line(FILE *file, char line[128]) {
    if (fgets(line, 128, file) == NULL)
        return 0;
    size_t length = strlen(line);
    if (length == 0U || line[length - 1U] != '\n')
        return 0;
    line[--length] = '\0';
    if (length != 0U && line[length - 1U] == '\r')
        line[length - 1U] = '\0';
    return 1;
}

static int read_value(FILE *file, const char *key, char value[128]) {
    char line[128];
    const size_t prefix = strlen(key);
    if (!read_line(file, line) || strncmp(line, key, prefix) != 0 || line[prefix] != '\t' ||
        line[prefix + 1U] == '\0')
        return 0;
    const size_t length = strlen(line + prefix + 1U);
    memcpy(value, line + prefix + 1U, length + 1U);
    return 1;
}

static int unsigned_value(const char *text, uint64_t *output) {
    uint64_t value = 0U;
    if (text[0] == '\0')
        return 0;
    for (const char *point = text; *point != '\0'; ++point) {
        if (*point < '0' || *point > '9')
            return 0;
        const uint64_t digit = (uint64_t)(*point - '0');
        if (value > (UINT64_MAX - digit) / 10U)
            return 0;
        value = value * 10U + digit;
    }
    *output = value;
    return 1;
}

static int read_u64(FILE *file, const char *key, uint64_t *output) {
    char value[128];
    return read_value(file, key, value) && unsigned_value(value, output);
}

static int read_u32(FILE *file, const char *key, uint32_t *output) {
    uint64_t value = 0U;
    if (!read_u64(file, key, &value) || value > UINT32_MAX)
        return 0;
    *output = (uint32_t)value;
    return 1;
}

static int read_loss(FILE *file, double *output) {
    char text[128], *end = NULL;
    if (!read_value(file, "mean_loss", text) || isspace((unsigned char)text[0]))
        return 0;
    errno = 0;
    const double value = strtod(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE || !isfinite(value) || value < 0.0)
        return 0;
    *output = value;
    return 1;
}

static int read_split(FILE *file, life_fitness_split *output) {
    char text[128];
    return read_value(file, "split", text) && life_fitness_split_parse(text, output);
}

static int read_quality(FILE *file, life_fitness_report *report) {
    return read_u32(file, "LIFE_FITNESS", &report->version) && read_split(file, &report->split) &&
           read_u32(file, "complete", &report->complete) &&
           read_u64(file, "pack_hash", &report->pack_hash) &&
           read_u32(file, "pack_records", &report->pack_records) &&
           read_u64(file, "records", &report->records) &&
           read_u64(file, "correct", &report->correct) && read_loss(file, &report->mean_loss);
}

static int read_work(FILE *file, life_fitness_report *report) {
    return read_u32(file, "model_count", &report->model_count) &&
           read_u32(file, "training_epochs", &report->training_epochs) &&
           read_u32(file, "training_generations", &report->training_generations) &&
           read_u64(file, "training_updates", &report->training_updates) &&
           read_u64(file, "forward_passes", &report->forward_passes) &&
           read_u64(file, "active_modules", &report->active_modules) &&
           read_u64(file, "active_centroids", &report->active_centroids);
}

static int read_end(FILE *file) {
    char line[128];
    if (!read_line(file, line) || strcmp(line, "END") != 0)
        return 0;
    for (unsigned int i = 0U; i < 128U; ++i) {
        const int value = fgetc(file);
        if (value == EOF)
            return !ferror(file);
        if (!isspace((unsigned char)value))
            return 0;
    }
    return fgetc(file) == EOF && !ferror(file);
}

int life_fitness_read(const char *path, life_fitness_report *output) {
    life_fitness_report report = {0};
    if (path == NULL || path[0] == '\0' || output == NULL)
        return 0;
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    int ok = read_quality(file, &report) && read_work(file, &report) && read_end(file) &&
             life_fitness_report_valid(&report);
    if (fclose(file) != 0)
        ok = 0;
    if (ok)
        *output = report;
    return ok;
}
