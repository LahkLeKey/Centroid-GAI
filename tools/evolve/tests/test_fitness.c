/** @file test_fitness.c @brief Independent-pack determinism and strict fitness admission. */
#include "fitness.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, "Fixed Life fitness check failed")
#define REPORT_PATH "test-life-source-fitness.tsv"

static life_fitness_report valid_report(void) {
    const life_fitness_report report = {.version = LIFE_FITNESS_VERSION,
                                        .split = LIFE_FITNESS_DEV,
                                        .complete = 1U,
                                        .pack_hash = 123U,
                                        .pack_records = LIFE_FITNESS_PACK_RECORDS,
                                        .records = 384U,
                                        .correct = 96U,
                                        .mean_loss = 2.0,
                                        .model_count = LIFE_FITNESS_MODELS,
                                        .training_epochs = LIFE_FITNESS_TRAINING_EPOCHS,
                                        .training_generations = LIFE_FITNESS_TRAINING_GENERATIONS,
                                        .training_updates = 1024U,
                                        .forward_passes = 768U,
                                        .active_modules = 1152U,
                                        .active_centroids = 4608U};
    return report;
}

static void quality_gate(void) {
    const life_fitness_report baseline = valid_report();
    life_fitness_report candidate = baseline;
    CHECK(life_fitness_report_valid(&baseline));
    CHECK(!life_fitness_admits(&baseline, &candidate));
    candidate.mean_loss -= 2e-6;
    CHECK(life_fitness_admits(&baseline, &candidate));
    candidate.correct = baseline.correct - 1U;
    CHECK(!life_fitness_admits(&baseline, &candidate));
    candidate.correct = baseline.correct;
    candidate.mean_loss = baseline.mean_loss - 0.5e-6;
    CHECK(!life_fitness_admits(&baseline, &candidate));
}

static void invalid_losses(void) {
    const life_fitness_report baseline = valid_report();
    static const double invalid[] = {NAN, INFINITY, -INFINITY, -1.0};
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        life_fitness_report candidate = baseline;
        candidate.mean_loss = invalid[i];
        CHECK(!life_fitness_report_valid(&candidate));
        CHECK(!life_fitness_admits(&baseline, &candidate));
        CHECK(!life_fitness_admits(&candidate, &baseline));
    }
    CHECK(!life_fitness_admits(NULL, &baseline) && !life_fitness_admits(&baseline, NULL));
}

static void incomplete_gate(void) {
    const life_fitness_report baseline = valid_report();
    life_fitness_report candidate = baseline;
    candidate.mean_loss = 1.0;
    candidate.complete = 0U;
    CHECK(!life_fitness_admits(&baseline, &candidate));
    candidate.complete = 1U;
    candidate.records = baseline.records - 1U;
    CHECK(!life_fitness_admits(&baseline, &candidate));
    candidate.records = baseline.records;
    candidate.pack_records -= 1U;
    CHECK(!life_fitness_admits(&baseline, &candidate));
    candidate.pack_records = baseline.pack_records;
    candidate.model_count -= 1U;
    CHECK(!life_fitness_admits(&baseline, &candidate));
}

static void resource_gate(void) {
    const life_fitness_report baseline = valid_report();
    life_fitness_report candidate = baseline;
    candidate.mean_loss = 1.0;
    candidate.training_updates += 1U;
    CHECK(life_fitness_report_valid(&candidate) && !life_fitness_admits(&baseline, &candidate));
    candidate.training_updates = baseline.training_updates;
    candidate.active_modules += 1U;
    candidate.active_centroids += 4U;
    CHECK(life_fitness_report_valid(&candidate) && !life_fitness_admits(&baseline, &candidate));
    candidate.active_modules = baseline.active_modules;
    candidate.active_centroids = baseline.active_centroids;
    candidate.pack_hash += 1U;
    CHECK(!life_fitness_admits(&baseline, &candidate));
    candidate.pack_hash = baseline.pack_hash;
    candidate.split = LIFE_FITNESS_CONFIRM;
    CHECK(!life_fitness_admits(&baseline, &candidate));
}

static void read_preserves_on_error(void) {
    life_fitness_report report;
    unsigned char before[sizeof(report)];
    memset(&report, 0xa5, sizeof(report));
    memcpy(before, &report, sizeof(before));
    CHECK(!life_fitness_read(REPORT_PATH, &report));
    CHECK(memcmp(before, &report, sizeof(before)) == 0);
}

static void write_text(const char *text) {
    FILE *file = fopen(REPORT_PATH, "wb");
    CHECK(file != NULL);
    CHECK(fputs(text, file) >= 0);
    CHECK(fclose(file) == 0);
}

static void read_text(char text[1024]) {
    FILE *file = fopen(REPORT_PATH, "rb");
    CHECK(file != NULL);
    const size_t count = fread(text, 1U, 1023U, file);
    CHECK(count < 1023U && !ferror(file) && feof(file));
    text[count] = '\0';
    CHECK(fclose(file) == 0);
}

static void replace_text(const char *old, const char *replacement) {
    char text[1024], revised[2048];
    read_text(text);
    const char *found = strstr(text, old);
    CHECK(found != NULL);
    const size_t prefix = (size_t)(found - text), length = strlen(replacement);
    const char *suffix = found + strlen(old);
    CHECK(prefix + length + strlen(suffix) + 1U <= sizeof(revised));
    memcpy(revised, text, prefix);
    memcpy(revised + prefix, replacement, length);
    memcpy(revised + prefix + length, suffix, strlen(suffix) + 1U);
    write_text(revised);
}

static void corrupt_report(const char *old, const char *replacement) {
    const life_fitness_report report = valid_report();
    CHECK(life_fitness_write(REPORT_PATH, &report));
    replace_text(old, replacement);
    read_preserves_on_error();
    CHECK(remove(REPORT_PATH) == 0);
}

static void strict_reader(void) {
    corrupt_report("LIFE_FITNESS\t1", "LIFE_FITNESS\t2");
    corrupt_report("split\tdev", "split\tunknown");
    corrupt_report("complete\t1", "complete\t0");
    corrupt_report("records\t384", "records\t383");
    corrupt_report("records\t384", "records\t-384");
    corrupt_report("records\t384", "records\t18446744073709551616");
    corrupt_report("correct\t96", "Correct\t96");
    corrupt_report("records\t384", "records\t384\nrecords\t384");
    corrupt_report("mean_loss\t2", "mean_loss\tNaN");
    corrupt_report("mean_loss\t2", "mean_loss\tInf");
    corrupt_report("mean_loss\t2", "mean_loss\t-1");
    corrupt_report("mean_loss\t2", "mean_loss\t2junk");
    corrupt_report("END\n", "");
    corrupt_report("END\n", "END\njunk\n");
}

static void bounded_reader(void) {
    char long_value[192];
    memset(long_value, '0', sizeof(long_value));
    long_value[sizeof(long_value) - 1U] = '\0';
    corrupt_report("384", long_value);
    char trailing[196];
    memcpy(trailing, "END\n", 4U);
    memset(trailing + 4U, ' ', sizeof(trailing) - 5U);
    trailing[sizeof(trailing) - 1U] = '\0';
    corrupt_report("END\n", trailing);
}

static void report_roundtrip(void) {
    const life_fitness_report original = valid_report();
    life_fitness_report restored;
    char before[1024], after[1024];
    CHECK(life_fitness_write(REPORT_PATH, &original));
    read_text(before);
    CHECK(!life_fitness_write(REPORT_PATH, &original));
    read_text(after);
    CHECK(strcmp(before, after) == 0);
    CHECK(life_fitness_read(REPORT_PATH, &restored));
    CHECK(restored.pack_hash == original.pack_hash && restored.correct == original.correct);
    CHECK(restored.mean_loss == original.mean_loss && !life_fitness_admits(&original, &restored));
    replace_text("END\n", "END\n \r\n\t");
    CHECK(life_fitness_read(REPORT_PATH, &restored));
    CHECK(remove(REPORT_PATH) == 0);
}

static void invalid_paths(void) {
    const life_fitness_report report = valid_report();
    life_fitness_report output;
    CHECK(!life_fitness_write(NULL, &report) && !life_fitness_write("", &report));
    CHECK(!life_fitness_write(REPORT_PATH, NULL));
    CHECK(!life_fitness_read(NULL, &output) && !life_fitness_read("", &output));
    CHECK(!life_fitness_read(REPORT_PATH, NULL));
    read_preserves_on_error();
}

static int same_record(const life_fitness_pack *first, const life_fitness_pack *second,
                       size_t index) {
    return first->records[index].target == second->records[index].target &&
           first->records[index].module_mask == second->records[index].module_mask &&
           first->allowed_outputs[index] == second->allowed_outputs[index] &&
           memcmp(first->records[index].state.values, second->records[index].state.values,
                  sizeof(first->records[index].state.values)) == 0;
}

static void runtime_packs(void) {
    life_fitness_pack first, repeat, confirm, train;
    CHECK(life_fitness_build_pack(LIFE_FITNESS_DEV, &first));
    CHECK(life_fitness_build_pack(LIFE_FITNESS_DEV, &repeat));
    CHECK(life_fitness_build_pack(LIFE_FITNESS_CONFIRM, &confirm));
    CHECK(life_fitness_build_pack(LIFE_FITNESS_TRAIN, &train));
    CHECK(first.count == LIFE_FITNESS_PACK_RECORDS && first.hash == repeat.hash);
    CHECK(confirm.count == first.count && confirm.hash != first.hash);
    CHECK(train.count == first.count && train.hash != first.hash && train.hash != confirm.hash);
    unsigned int different = 0U;
    for (size_t i = 0U; i < first.count; ++i) {
        CHECK(same_record(&first, &repeat, i));
        CHECK(first.records[i].target != 0U && first.records[i].target < LIFE_OUTPUTS);
        CHECK((first.allowed_outputs[i] & (1U << first.records[i].target)) != 0U);
        different += !same_record(&first, &confirm, i) ? 1U : 0U;
    }
    CHECK(different != 0U);
}

static void invalid_split(void) {
    life_fitness_split split = LIFE_FITNESS_CONFIRM;
    CHECK(!life_fitness_split_parse("Dev", &split) && split == LIFE_FITNESS_CONFIRM);
    CHECK(!life_fitness_split_parse(NULL, &split) && !life_fitness_split_parse("dev", NULL));
    CHECK(life_fitness_split_parse("dev", &split) && split == LIFE_FITNESS_DEV);
    CHECK(life_fitness_split_parse("confirm", &split) && split == LIFE_FITNESS_CONFIRM);
    CHECK(life_fitness_split_parse("train", &split) && split == LIFE_FITNESS_TRAIN);
    CHECK(!life_fitness_build_pack(LIFE_FITNESS_DEV, NULL));
    CHECK(!life_fitness_measure(LIFE_FITNESS_DEV, NULL));
}

int main(void) {
    quality_gate();
    invalid_losses();
    incomplete_gate();
    resource_gate();
    strict_reader();
    bounded_reader();
    report_roundtrip();
    invalid_paths();
    invalid_split();
    runtime_packs();
    puts("Fixed physical packs, strict reports and source-candidate admission checks passed");
    return 0;
}
