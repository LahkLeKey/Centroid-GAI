/** @file test_checkpoint.c @brief Synthetic search-state bundles and native process continuation.
 */
#include "context_repository.h"
#include "evolve_checkpoint.h"
#include "evolve_checkpoint_codec.h"
#include "evolve_run.h"
#include "life_context.h"
#include "test_utils.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CHECK(condition) TEST_CHECK(condition, "Synthetic search checkpoint check failed")
#define PATH_BYTES (EVOLVE_PROCESS_MAX_PATH_BYTES + 1U)
#define FILE_LIMIT (16U * 1024U * 1024U)

typedef struct checkpoint_fixture {
    evolve_run run;
    char directory[PATH_BYTES];
    char source_root[PATH_BYTES];
    char source[PATH_BYTES];
    char inputs_manifest[PATH_BYTES];
    char marker[PATH_BYTES];
} checkpoint_fixture;

static const char *const members[] = {
    "state.bin", "parent.c", "memory.context", "inputs.txt", "external-inputs.txt", "manifest.bin"};

static unsigned int process_id(void) {
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

static void join(char *output, const char *directory, const char *name) {
    CHECK(evolve_path_join(output, PATH_BYTES, directory, name));
}

static unsigned char *read_bytes(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    unsigned char *bytes = malloc(FILE_LIMIT + 1U);
    CHECK(file != NULL && bytes != NULL);
    *size = fread(bytes, 1U, FILE_LIMIT + 1U, file);
    CHECK(*size <= FILE_LIMIT && !ferror(file) && feof(file));
    CHECK(fclose(file) == 0);
    return bytes;
}

static void overwrite(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL && fwrite(bytes, 1U, size, file) == size);
    CHECK(fclose(file) == 0);
}

static void copy_file(const char *source, const char *destination) {
    size_t size;
    unsigned char *bytes = read_bytes(source, &size);
    CHECK(evolve_write_exclusive(destination, bytes, size));
    free(bytes);
}

static uint64_t file_hash(const char *path) {
    size_t size;
    unsigned char *bytes = read_bytes(path, &size);
    const uint64_t hash = evolve_bytes_hash(bytes, size);
    free(bytes);
    return hash;
}

static void bundle_copy(const char *source, const char *destination) {
    char input[PATH_BYTES], output[PATH_BYTES];
    CHECK(evolve_create_directory(destination));
    for (size_t i = 0U; i < sizeof(members) / sizeof(members[0]); ++i) {
        join(input, source, members[i]);
        join(output, destination, members[i]);
        copy_file(input, output);
    }
}

static void bundle_remove(const char *path) {
    char member[PATH_BYTES];
    for (size_t i = 0U; i < sizeof(members) / sizeof(members[0]); ++i) {
        join(member, path, members[i]);
        CHECK(remove(member) == 0);
    }
    remove_directory(path);
}

static uint64_t hash_word(uint64_t hash, uint64_t word) {
    for (size_t i = 0U; i < 8U; ++i) {
        hash = (hash ^ (word & UINT64_C(255))) * UINT64_C(1099511628211);
        word >>= 8U;
    }
    return hash;
}

static void write_word(FILE *file, uint64_t word) {
    unsigned char bytes[8];
    for (size_t i = 0U; i < sizeof(bytes); ++i)
        bytes[i] = (unsigned char)(word >> (8U * i));
    CHECK(fwrite(bytes, 1U, sizeof(bytes), file) == sizeof(bytes));
}

static void manifest_member(FILE *file, const char *bundle, const char *member, uint64_t *hash) {
    char path[PATH_BYTES];
    size_t size;
    join(path, bundle, member);
    unsigned char *bytes = read_bytes(path, &size);
    const uint64_t member_hash = evolve_bytes_hash(bytes, size);
    free(bytes);
    write_word(file, (uint64_t)size);
    write_word(file, member_hash);
    *hash = hash_word(hash_word(*hash, (uint64_t)size), member_hash);
}

static void rewrite_manifest(const char *bundle) {
    /* Deliberately preserve outer integrity around invalid synthetic payloads so
     * rejection must reach the native semantic validators. */
    char path[PATH_BYTES];
    join(path, bundle, "manifest.bin");
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    write_word(file, UINT64_C(0x314b48434c5645));
    write_word(file, 1U);
    uint64_t hash = hash_word(UINT64_C(14695981039346656037), 1U);
    for (size_t i = 0U; i < 5U; ++i)
        manifest_member(file, bundle, members[i], &hash);
    write_word(file, hash);
    CHECK(fclose(file) == 0);
}

static void rewrite_state(const char *bundle, const evolve_run *run) {
    char path[PATH_BYTES];
    join(path, bundle, "state.bin");
    CHECK(remove(path) == 0);
    CHECK(evolve_checkpoint_state_write(path, run));
}

static life_fitness_report synthetic_report(life_fitness_split split, double loss) {
    /* These deliberately synthetic measurements exercise persistence machinery;
     * no candidate compiler, test suite or production evaluator has produced them. */
    const life_fitness_report report = {.version = LIFE_FITNESS_VERSION,
                                        .split = split,
                                        .complete = 1U,
                                        .pack_hash = UINT64_C(10000) + (uint64_t)split,
                                        .pack_records = LIFE_FITNESS_PACK_RECORDS,
                                        .records = 384U,
                                        .correct = 96U,
                                        .mean_loss = loss,
                                        .model_count = LIFE_FITNESS_MODELS,
                                        .training_epochs = LIFE_FITNESS_TRAINING_EPOCHS,
                                        .training_generations = LIFE_FITNESS_TRAINING_GENERATIONS,
                                        .training_updates = 1024U,
                                        .forward_passes = 768U,
                                        .active_modules = 1152U,
                                        .active_centroids = 4608U};
    CHECK(life_fitness_report_valid(&report));
    return report;
}

static void release_run(evolve_run *run) {
    cgai_life_context_destroy(run->memory);
    free(run->parent_storage);
    if (run->report != NULL)
        CHECK(fclose(run->report) == 0);
    memset(run, 0, sizeof(*run));
}

static void source_fixture(checkpoint_fixture *fixture, const char *source) {
    char directory[PATH_BYTES];
    join(fixture->source_root, fixture->directory, "source");
    CHECK(evolve_create_directory(fixture->source_root));
    join(directory, fixture->source_root, "src");
    CHECK(evolve_create_directory(directory));
    join(directory, directory, "gameplay");
    CHECK(evolve_create_directory(directory));
    join(fixture->source, directory, "gameplay_model.c");
    copy_file(source, fixture->source);
    join(fixture->marker, fixture->directory, "synthetic-input.txt");
    static const char marker[] = "SYNTHETIC CHECKPOINT UNIT FIXTURE; NOT PRODUCTION MEASUREMENTS\n";
    CHECK(evolve_write_exclusive(fixture->marker, marker, sizeof(marker) - 1U));
    join(fixture->inputs_manifest, fixture->directory, "inputs-manifest.txt");
    FILE *file = fopen(fixture->inputs_manifest, "wbx");
    CHECK(file != NULL);
    CHECK(fprintf(file, "%s\n", fixture->source) > 0);
    CHECK(fclose(file) == 0);
}

static void create_context(checkpoint_fixture *fixture, uint32_t epochs) {
    cgai_life_config config = cgai_life_config_default();
    context_repository_batch *batch = NULL;
    context_repository_report report;
    config.enable_merges = 0U;
    config.training_epochs = epochs;
    CHECK(cgai_life_context_create(&config, &fixture->run.memory) == CGAI_LIFE_OK);
    fixture->run.initial_memory_checksum = cgai_life_context_hash(fixture->run.memory);
    CHECK(context_repository_scan(fixture->source_root, &batch, &report));
    CHECK(context_repository_apply(batch, fixture->run.memory) == CGAI_LIFE_OK);
    fixture->run.source_context_checksum = report.source_hash;
    context_repository_destroy(batch);
    fixture->run.prepared_memory_checksum = cgai_life_context_hash(fixture->run.memory);
}

static void external_context(checkpoint_fixture *fixture) {
    context_repository_batch *batch = NULL;
    context_repository_report report;
    CHECK(context_repository_read_context(fixture->marker, CGAI_LIFE_CONTEXT_LLM, &batch, &report));
    CHECK(context_repository_apply(batch, fixture->run.memory) == CGAI_LIFE_OK);
    fixture->run.llm_context_checksum = report.source_hash;
    context_repository_destroy(batch);
    join(fixture->run.external_manifest, fixture->directory, "external-manifest.txt");
    FILE *file = fopen(fixture->run.external_manifest, "wbx");
    CHECK(file != NULL && fprintf(file, "%s\n", fixture->marker) > 0);
    CHECK(fclose(file) == 0);
    CHECK(evolve_manifest_hash(fixture->run.external_manifest, &fixture->run.external_checksum));
    fixture->run.prepared_memory_checksum = cgai_life_context_hash(fixture->run.memory);
}

static void fixture_directory(checkpoint_fixture *fixture, const char *build_root,
                              const char *label) {
    char name[128], path[PATH_BYTES];
    const int written =
        snprintf(name, sizeof(name), "test-evolve-checkpoint-%u-%s", process_id(), label);
    CHECK(written > 0 && (size_t)written < sizeof(name));
    join(path, build_root, name);
    CHECK(evolve_create_directory(path));
    CHECK(evolve_absolute_path(path, fixture->directory, sizeof(fixture->directory)));
}

static void branch_directory(evolve_run *run, const char *directory, const char *label) {
    char path[PATH_BYTES];
    join(path, directory, label);
    CHECK(evolve_create_directory(path));
    CHECK(evolve_absolute_path(path, run->output, sizeof(run->output)));
    run->options.output = run->output;
}

static void stable_contact(cgai_life_context *memory) {
    const uint32_t generation = memory->run.world.tick;
    life_world_clear(&memory->run.world, 42U);
    memory->run.world.tick = generation;
    for (int y = 10; y < 12; ++y)
        for (int x = 10; x < 12; ++x)
            CHECK(life_world_set(&memory->run.world, x, y, 3U) == LIFE_OK);
}

static cgai_life_events contact(evolve_run *run) {
    cgai_life_events events;
    stable_contact(run->memory);
    CHECK(cgai_life_context_train_step(run->memory, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_get_events(run->memory, &events) == CGAI_LIFE_OK);
    CHECK(events.generation_valid == 1U && events.event_count == 1U);
    CHECK(events.events[0].participant_mask == 3U);
    return events;
}

static void baseline_reports(evolve_run *run) {
    run->training = synthetic_report(LIFE_FITNESS_TRAIN, 2.0);
    run->development = synthetic_report(LIFE_FITNESS_DEV, 2.0);
    run->confirmation = synthetic_report(LIFE_FITNESS_CONFIRM, 2.0);
    run->baseline_training = run->training;
    run->baseline_development = run->development;
    run->baseline_confirmation = run->confirmation;
}

static checkpoint_fixture *fixture_create_epochs(const char *source, const char *build_root,
                                                 const char *label, uint32_t epochs) {
    checkpoint_fixture *fixture = calloc(1U, sizeof(*fixture));
    CHECK(fixture != NULL);
    fixture_directory(fixture, build_root, label);
    source_fixture(fixture, source);
    branch_directory(&fixture->run, fixture->directory, "whole");
    CHECK(evolve_read_source(fixture->source, &fixture->run.parent, &fixture->run.parent_storage));
    CHECK(evolve_manifest_hash(fixture->inputs_manifest, &fixture->run.inputs_checksum));
    fixture->run.build_recipe_checksum = evolve_checkpoint_build_recipe();
    fixture->run.initial_parent_checksum = fixture->run.parent.checksum;
    fixture->run.options.candidates = 4U;
    fixture->run.options.generations = 8U;
    fixture->run.options.seed = 42U;
    create_context(fixture, epochs);
    external_context(fixture);
    baseline_reports(&fixture->run);
    CHECK(cgai_life_context_choice_get_stats(fixture->run.memory, &fixture->run.initial_choices) ==
          CGAI_LIFE_OK);
    return fixture;
}

static checkpoint_fixture *fixture_create(const char *source, const char *build_root,
                                          const char *label) {
    return fixture_create_epochs(source, build_root, label, 1U);
}

static void trial_proof(evolve_run *run, evolve_trial *trial) {
    const int written = snprintf(
        trial->feedback_proof, sizeof(trial->feedback_proof),
        "LIFE_SOURCE_TRAINING_FEEDBACK\t1\nsplit\ttrain\n"
        "choice_token\t%" PRIu64 "\ninput_hash\t%" PRIu64
        "\naction\t%u\nparent_source_checksum\t%" PRIu64 "\ncandidate_source_checksum\t%" PRIu64
        "\npack_hash\t%" PRIu64 "\nrecords\t%" PRIu64 "\nparent_loss\t%.17g\ncandidate_loss\t%.17g"
        "\nutility\t%.17g\nEND\n",
        trial->choice.token, trial->choice.input_hash, trial->choice.action,
        trial->candidate.source_checksum, trial->candidate.checksum, trial->training.pack_hash,
        trial->training.records, run->training.mean_loss, trial->training.mean_loss, 0.25);
    CHECK(written > 0 && (size_t)written < sizeof(trial->feedback_proof));
    trial->feedback_proof_bytes = (size_t)written;
    trial->feedback = (cgai_life_context_feedback){
        CGAI_LIFE_CONTEXT_TRAIN, 1U, 0U,
        evolve_bytes_hash(trial->feedback_proof, trial->feedback_proof_bytes), 0.25};
}

static void trial_begin(evolve_run *run, const cgai_life_events *events, evolve_trial *trial) {
    trial->event = &events->events[0];
    CHECK(evolve_mutation_alternatives(&run->parent, trial->event, trial->actions,
                                       &trial->action_count,
                                       &trial->fallback) == EVOLVE_MUTATION_OK);
    static const char input[] = "Synthetic checkpoint source initialization embeddings_base "
                                "encoder_base outer_base inner_base; no quality claim";
    memcpy(trial->input, input, sizeof(input));
    CHECK(cgai_life_context_choice_get_stats(run->memory, &trial->before) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_choice_begin(run->memory, 0U, trial->input, trial->actions,
                                         trial->action_count, trial->fallback,
                                         &trial->choice) == CGAI_LIFE_OK);
    CHECK(evolve_mutation_select(&run->parent, trial->event, trial->choice.action,
                                 &trial->candidate) == EVOLVE_MUTATION_OK);
}

static void trial_observe(evolve_run *run, evolve_trial *trial) {
    trial->training = synthetic_report(LIFE_FITNESS_TRAIN, run->training.mean_loss - 0.25);
    trial->development = synthetic_report(LIFE_FITNESS_DEV, run->development.mean_loss - 0.25);
    trial->confirmation =
        synthetic_report(LIFE_FITNESS_CONFIRM, run->confirmation.mean_loss - 0.25);
    trial->parent_training_loss = run->training.mean_loss;
    trial->audit_status = EVOLVE_CHECKPOINT_ACCEPTED;
    trial_proof(run, trial);
    CHECK(cgai_life_context_choice_observe(run->memory, &trial->choice, &trial->feedback) ==
          CGAI_LIFE_OK);
    CHECK(cgai_life_context_choice_get_stats(run->memory, &trial->after) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_choice_predict(
              run->memory, trial->input, trial->choice.participant_mask, trial->actions,
              trial->action_count, trial->fallback, &trial->prediction_after) == CGAI_LIFE_OK);
}

static void deferred_trial(evolve_run *run, evolve_trial *trial) {
    const int written =
        snprintf(trial->feedback_proof, sizeof(trial->feedback_proof),
                 "LIFE_SOURCE_DEFERRED_FEEDBACK\t1\nchoice_token\t%" PRIu64 "\ninput_hash\t%" PRIu64
                 "\naction\t%u\nparent_checksum\t%" PRIu64 "\ncandidate_checksum\t%" PRIu64
                 "\nstage\tbuild_rejected"
                 "\nutility_measured\t0\ndeferred\t1\nEND\n",
                 trial->choice.token, trial->choice.input_hash, trial->choice.action,
                 trial->candidate.source_checksum, trial->candidate.checksum);
    CHECK(written > 0 && (size_t)written < sizeof(trial->feedback_proof));
    trial->feedback_proof_bytes = (size_t)written;
    trial->audit_status = EVOLVE_CHECKPOINT_BUILD_REJECTED;
    trial->parent_training_loss = run->training.mean_loss;
    trial->feedback = (cgai_life_context_feedback){
        CGAI_LIFE_CONTEXT_TRAIN, 1U, 1U,
        evolve_bytes_hash(trial->feedback_proof, trial->feedback_proof_bytes), 0.0};
    CHECK(cgai_life_context_choice_observe(run->memory, &trial->choice, &trial->feedback) ==
          CGAI_LIFE_OK);
    CHECK(cgai_life_context_choice_get_stats(run->memory, &trial->after) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_choice_predict(
              run->memory, trial->input, trial->choice.participant_mask, trial->actions,
              trial->action_count, trial->fallback, &trial->prediction_after) == CGAI_LIFE_OK);
}

static void trial_activity(evolve_run *run, const evolve_trial *trial) {
    char source[128], text[512];
    const int named =
        snprintf(source, sizeof(source), "evolution/candidate-%04u/training", run->proposals);
    const int written =
        snprintf(text, sizeof(text),
                 "SYNTHETIC UNIT choice=%" PRIu64 " proof=%" PRIu64
                 " TRAIN measurement utility=%.17g; not production validation",
                 trial->choice.token, trial->feedback.evidence_hash, trial->feedback.utility);
    CHECK(named > 0 && (size_t)named < sizeof(source));
    CHECK(written > 0 && (size_t)written < sizeof(text));
    const cgai_life_context_input input = {
        source, text, 0U, 0U, 0U, CGAI_LIFE_CONTEXT_ACTIVITY, CGAI_LIFE_CONTEXT_TRAIN, 1U, 15U};
    CHECK(cgai_life_context_add(run->memory, &input) == CGAI_LIFE_OK);
}

static void trial_adopt(evolve_run *run, evolve_trial *trial) {
    evolve_mutation_source source;
    CHECK(evolve_mutation_validate(trial->candidate.bytes, trial->candidate.size, &source) ==
          EVOLVE_MUTATION_OK);
    free(run->parent_storage);
    run->parent_storage = trial->candidate.bytes;
    trial->candidate.bytes = NULL;
    run->parent = source;
    run->training = trial->training;
    run->development = trial->development;
    run->confirmation = trial->confirmation;
    ++run->accepted;
}

static void duplicate_record(evolve_run *run, const evolve_trial *trial) {
    evolve_run *before = malloc(sizeof(*before));
    CHECK(before != NULL);
    ++run->proposals;
    memcpy(before, run, sizeof(*before));
    CHECK(!evolve_checkpoint_record(run, trial, 1U));
    CHECK(memcmp(before, run, sizeof(*before)) == 0);
    --run->proposals;
    free(before);
}

static void advance_search(evolve_run *run) {
    evolve_trial *trial = calloc(1U, sizeof(*trial));
    CHECK(trial != NULL);
    const cgai_life_events events = contact(run);
    trial_begin(run, &events, trial);
    ++run->proposals;
    trial_observe(run, trial);
    trial_activity(run, trial);
    CHECK(evolve_checkpoint_record(run, trial, 1U));
    duplicate_record(run, trial);
    trial_adopt(run, trial);
    ++run->completed_generations;
    evolve_mutation_destroy(&trial->candidate);
    free(trial);
}

static void publish(checkpoint_fixture *fixture, evolve_run *run) {
    CHECK(evolve_checkpoint_publish(run, fixture->source_root, fixture->inputs_manifest));
    CHECK(run->checkpoint_hash != 0U && run->latest_checkpoint[0] != '\0');
}

static void restore(checkpoint_fixture *fixture, const char *path, evolve_run *run) {
    const char *output_option = run->options.output;
    char output[PATH_BYTES];
    memcpy(output, run->output, sizeof(output));
    FILE *report = run->report;
    CHECK(evolve_checkpoint_restore(path, fixture->source_root, fixture->inputs_manifest, run));
    CHECK(strcmp(output, run->output) == 0 && run->options.output == output_option);
    CHECK(run->report == report);
    cgai_life_events events;
    CHECK(cgai_life_context_get_events(run->memory, &events) == CGAI_LIFE_OK);
    CHECK(events.generation_valid == 0U && events.event_count == 0U);
}

static void same_reports(const life_fitness_report *first, const life_fitness_report *second) {
    CHECK(first->version == second->version && first->split == second->split);
    CHECK(first->pack_hash == second->pack_hash && first->records == second->records);
    CHECK(first->correct == second->correct && first->mean_loss == second->mean_loss);
    CHECK(first->training_updates == second->training_updates);
    CHECK(first->forward_passes == second->forward_passes);
    CHECK(first->active_modules == second->active_modules &&
          first->active_centroids == second->active_centroids);
}

static void same_search(const evolve_run *first, const evolve_run *second) {
    CHECK(first->parent.checksum == second->parent.checksum &&
          first->parent.size == second->parent.size);
    CHECK(memcmp(first->parent.bytes, second->parent.bytes, first->parent.size) == 0);
    CHECK(cgai_life_context_hash(first->memory) == cgai_life_context_hash(second->memory));
    CHECK(first->options.candidates == second->options.candidates &&
          first->options.generations == second->options.generations);
    CHECK(first->proposals == second->proposals && first->accepted == second->accepted);
    CHECK(first->completed_generations == second->completed_generations &&
          first->prepared_generation == second->prepared_generation);
    CHECK(first->ledger_count == second->ledger_count && first->finalized == second->finalized);
    CHECK(first->initial_parent_checksum == second->initial_parent_checksum);
    for (uint32_t i = 0U; i < first->ledger_count; ++i) {
        CHECK(first->ledger[i].token == second->ledger[i].token);
        CHECK(first->ledger[i].chain_hash == second->ledger[i].chain_hash);
        CHECK(strcmp(first->ledger[i].input, second->ledger[i].input) == 0);
        CHECK(first->ledger[i].evidence_hash == second->ledger[i].evidence_hash);
        CHECK(memcmp(first->ledger[i].proof, second->ledger[i].proof,
                     first->ledger[i].proof_bytes) == 0);
    }
    same_reports(&first->training, &second->training);
    same_reports(&first->development, &second->development);
    same_reports(&first->confirmation, &second->confirmation);
}

static void rejected_restore(checkpoint_fixture *fixture, const char *path, evolve_run *run) {
    evolve_run *before = malloc(sizeof(*before));
    CHECK(before != NULL);
    memcpy(before, run, sizeof(*before));
    const uint64_t memory_hash = cgai_life_context_hash(run->memory);
    const uint64_t parent_hash = evolve_bytes_hash(run->parent.bytes, run->parent.size);
    CHECK(!evolve_checkpoint_restore(path, fixture->source_root, fixture->inputs_manifest, run));
    CHECK(memcmp(before, run, sizeof(*before)) == 0);
    CHECK(cgai_life_context_hash(run->memory) == memory_hash);
    CHECK(evolve_bytes_hash(run->parent.bytes, run->parent.size) == parent_hash);
    free(before);
}

static void rejected_publish(checkpoint_fixture *fixture) {
    evolve_run *before = malloc(sizeof(*before));
    CHECK(before != NULL);
    memcpy(before, &fixture->run, sizeof(*before));
    CHECK(
        !evolve_checkpoint_publish(&fixture->run, fixture->source_root, fixture->inputs_manifest));
    CHECK(memcmp(before, &fixture->run, sizeof(*before)) == 0);
    free(before);
}

static void repeated_destination(checkpoint_fixture *fixture) {
    uint64_t hashes[sizeof(members) / sizeof(members[0])];
    char path[PATH_BYTES];
    for (size_t i = 0U; i < sizeof(members) / sizeof(members[0]); ++i) {
        join(path, fixture->run.latest_checkpoint, members[i]);
        hashes[i] = file_hash(path);
    }
    rejected_publish(fixture);
    for (size_t i = 0U; i < sizeof(members) / sizeof(members[0]); ++i) {
        join(path, fixture->run.latest_checkpoint, members[i]);
        CHECK(file_hash(path) == hashes[i]);
    }
}

static void member_corruption(checkpoint_fixture *fixture, const char *bundle, const char *member,
                              int truncate) {
    char bad[PATH_BYTES], path[PATH_BYTES];
    join(bad, fixture->directory, "malformed-bundle");
    bundle_copy(bundle, bad);
    join(path, bad, member);
    size_t size;
    unsigned char *bytes = read_bytes(path, &size);
    CHECK(size > 0U);
    if (truncate)
        overwrite(path, bytes, size / 2U);
    else {
        bytes[0] ^= 1U;
        overwrite(path, bytes, size);
    }
    free(bytes);
    rejected_restore(fixture, bad, &fixture->run);
    bundle_remove(bad);
}

static void overwrite_nonfinite(const char *path) {
    size_t size;
    unsigned char *bytes = read_bytes(path, &size);
    bytes[size] = '\0';
    char *model = strstr((char *)bytes, "MODEL ");
    CHECK(model != NULL);
    char *start = strchr(model, '\n');
    CHECK(start != NULL);
    ++start;
    char *end = strchr(start, ' ');
    CHECK(end != NULL && (size_t)(end - start) >= 3U);
    const size_t tail = size - (size_t)(end - (char *)bytes);
    memmove(start + 3U, end, tail);
    memcpy(start, "nan", 3U);
    overwrite(path, bytes, (size_t)(start - (char *)bytes) + 3U + tail);
    free(bytes);
}

static void nonfinite_member(checkpoint_fixture *fixture) {
    char bad[PATH_BYTES], path[PATH_BYTES];
    join(bad, fixture->directory, "nonfinite-bundle");
    bundle_copy(fixture->run.latest_checkpoint, bad);
    join(path, bad, "memory.context");
    overwrite_nonfinite(path);
    rewrite_manifest(bad);
    rejected_restore(fixture, bad, &fixture->run);
    bundle_remove(bad);
}

static void rechain_receipts(evolve_run *run) {
    uint64_t chain =
        hash_word(hash_word(UINT64_C(14695981039346656037), run->initial_parent_checksum),
                  run->prepared_memory_checksum);
    for (uint32_t i = 0U; i < run->ledger_count; ++i) {
        chain = evolve_checkpoint_receipt_hash(&run->ledger[i], chain);
        CHECK(chain != 0U);
        run->ledger[i].chain_hash = chain;
    }
}

static void forged_state(evolve_run *run, unsigned int fault) {
    CHECK(run->ledger_count == 2U && fault < 4U);
    evolve_checkpoint_receipt *receipt = &run->ledger[1];
    switch (fault) {
    case 0U:
        run->build_recipe_checksum ^= UINT64_C(1);
        break;
    case 1U:
        ++run->proposals;
        break;
    case 2U:
        receipt->token = run->ledger[0].token;
        receipt->choice.token = receipt->token;
        break;
    default:
        receipt->proof[0] ^= 1;
        receipt->evidence_hash = evolve_bytes_hash(receipt->proof, receipt->proof_bytes);
        break;
    }
    rechain_receipts(run);
}

static void forged_state_rejection(checkpoint_fixture *fixture, unsigned int fault) {
    char bad[PATH_BYTES];
    evolve_run *forged = malloc(sizeof(*forged));
    CHECK(forged != NULL);
    memcpy(forged, &fixture->run, sizeof(*forged));
    forged_state(forged, fault);
    join(bad, fixture->directory, "forged-state-bundle");
    bundle_copy(fixture->run.latest_checkpoint, bad);
    rewrite_state(bad, forged);
    rewrite_manifest(bad);
    rejected_restore(fixture, bad, &fixture->run);
    bundle_remove(bad);
    free(forged);
}

static void malformed_bundles(checkpoint_fixture *fixture) {
    char good[PATH_BYTES];
    memcpy(good, fixture->run.latest_checkpoint, sizeof(good));
    for (size_t i = 0U; i < sizeof(members) / sizeof(members[0]); ++i) {
        member_corruption(fixture, good, members[i], 0);
        member_corruption(fixture, good, members[i], 1);
    }
}

static void changed_external(checkpoint_fixture *fixture) {
    size_t size;
    unsigned char *bytes = read_bytes(fixture->marker, &size);
    static const char changed[] = "Changed external context identity\n";
    overwrite(fixture->marker, changed, sizeof(changed) - 1U);
    rejected_restore(fixture, fixture->run.latest_checkpoint, &fixture->run);
    overwrite(fixture->marker, bytes, size);
    free(bytes);
}

static void changed_runtime_source(checkpoint_fixture *fixture) {
    size_t size;
    unsigned char *bytes = read_bytes(fixture->source, &size);
    CHECK(size > 0U);
    const unsigned char original = bytes[0];
    bytes[0] ^= 1U;
    overwrite(fixture->source, bytes, size);
    rejected_restore(fixture, fixture->run.latest_checkpoint, &fixture->run);
    bytes[0] = original;
    overwrite(fixture->source, bytes, size);
    free(bytes);
}

static void changed_context_source(checkpoint_fixture *fixture) {
    char path[PATH_BYTES];
    join(path, fixture->source_root, "new-context.md");
    static const char text[] = "Additional source context outside runtime manifest\n";
    CHECK(evolve_write_exclusive(path, text, sizeof(text) - 1U));
    rejected_restore(fixture, fixture->run.latest_checkpoint, &fixture->run);
    CHECK(remove(path) == 0);
}

static void invalid_reports(checkpoint_fixture *fixture) {
    const life_fitness_report report = fixture->run.training;
    fixture->run.training.mean_loss = NAN;
    rejected_publish(fixture);
    fixture->run.training = report;
    fixture->run.baseline_development.complete = 0U;
    rejected_publish(fixture);
    fixture->run.baseline_development.complete = 1U;
}

static void invalid_counters(checkpoint_fixture *fixture) {
    ++fixture->run.completed_generations;
    rejected_publish(fixture);
    --fixture->run.completed_generations;
    ++fixture->run.proposals;
    rejected_publish(fixture);
    --fixture->run.proposals;
    ++fixture->run.accepted;
    rejected_publish(fixture);
    --fixture->run.accepted;
    ++fixture->run.inputs_checksum;
    rejected_publish(fixture);
    --fixture->run.inputs_checksum;
}

static void invalid_receipts(checkpoint_fixture *fixture) {
    CHECK(fixture->run.ledger_count == 2U);
    evolve_checkpoint_receipt *receipt = &fixture->run.ledger[1];
    const uint64_t token = receipt->token;
    receipt->token = fixture->run.ledger[0].token;
    rejected_publish(fixture);
    receipt->token = token;
    ++receipt->evidence_hash;
    rejected_publish(fixture);
    --receipt->evidence_hash;
    receipt->utility = NAN;
    rejected_publish(fixture);
    receipt->utility = 0.25;
    ++receipt->chain_hash;
    rejected_publish(fixture);
    --receipt->chain_hash;
}

static void invalid_state(checkpoint_fixture *fixture) {
    char previous[PATH_BYTES];
    memcpy(previous, fixture->run.output, sizeof(previous));
    branch_directory(&fixture->run, fixture->directory, "invalid-publication");
    invalid_reports(fixture);
    invalid_counters(fixture);
    invalid_receipts(fixture);
    const uint32_t action = fixture->run.ledger[0].action;
    const double original = fixture->run.memory->choice.models[0][action].readout[0];
    fixture->run.memory->choice.models[0][action].readout[0] = NAN;
    rejected_publish(fixture);
    fixture->run.memory->choice.models[0][action].readout[0] = original;
    remove_directory(fixture->run.output);
    memcpy(fixture->run.output, previous, sizeof(previous));
}

static void checkpoint_name(char *path, const char *directory, uint32_t generation, int final) {
    char name[64];
    const int written =
        snprintf(name, sizeof(name), "checkpoint-%04u%s", generation, final ? "-final" : "");
    CHECK(written > 0 && (size_t)written < sizeof(name));
    join(path, directory, name);
}

static void remove_checkpoints(const char *directory, uint32_t last) {
    char path[PATH_BYTES];
    for (uint32_t generation = 0U; generation <= last; ++generation) {
        checkpoint_name(path, directory, generation, 0);
        bundle_remove(path);
    }
    remove_directory(directory);
}

static void fixture_remove(checkpoint_fixture *fixture, uint32_t last) {
    char directory[PATH_BYTES];
    remove_checkpoints(fixture->run.output, last);
    CHECK(remove(fixture->source) == 0);
    join(directory, fixture->source_root, "src/gameplay");
    remove_directory(directory);
    join(directory, fixture->source_root, "src");
    remove_directory(directory);
    remove_directory(fixture->source_root);
    CHECK(remove(fixture->inputs_manifest) == 0);
    CHECK(remove(fixture->marker) == 0);
    CHECK(remove(fixture->run.external_manifest) == 0);
    release_run(&fixture->run);
    remove_directory(fixture->directory);
    free(fixture);
}

static void roundtrip_generations(checkpoint_fixture *fixture, evolve_run *restored) {
    publish(fixture, &fixture->run);
    restore(fixture, fixture->run.latest_checkpoint, restored);
    same_search(&fixture->run, restored);
    advance_search(&fixture->run);
    publish(fixture, &fixture->run);
    restore(fixture, fixture->run.latest_checkpoint, restored);
    same_search(&fixture->run, restored);
    advance_search(&fixture->run);
    advance_search(restored);
    same_search(&fixture->run, restored);
    publish(fixture, &fixture->run);
}

static void checkpoint_rejections(checkpoint_fixture *fixture) {
    repeated_destination(fixture);
    malformed_bundles(fixture);
    nonfinite_member(fixture);
    for (unsigned int fault = 0U; fault < 4U; ++fault)
        forged_state_rejection(fixture, fault);
    invalid_state(fixture);
    changed_external(fixture);
    changed_runtime_source(fixture);
    changed_context_source(fixture);
}

static void pending_bundle_rejection(checkpoint_fixture *fixture) {
    char bad[PATH_BYTES], path[PATH_BYTES];
    join(bad, fixture->directory, "pending-bundle");
    bundle_copy(fixture->run.latest_checkpoint, bad);
    rewrite_state(bad, &fixture->run);
    join(path, bad, "memory.context");
    CHECK(cgai_life_context_save(fixture->run.memory, path) == CGAI_LIFE_OK);
    rewrite_manifest(bad);
    rejected_restore(fixture, bad, &fixture->run);
    bundle_remove(bad);
}

static void source_search_roundtrip(const char *source, const char *build_root) {
    checkpoint_fixture *fixture = fixture_create(source, build_root, "roundtrip");
    evolve_run *restored = calloc(1U, sizeof(*restored));
    CHECK(restored != NULL);
    branch_directory(restored, fixture->directory, "restored");
    restored->report = tmpfile();
    CHECK(restored->report != NULL);
    roundtrip_generations(fixture, restored);
    checkpoint_rejections(fixture);
    release_run(restored);
    char restored_directory[PATH_BYTES];
    join(restored_directory, fixture->directory, "restored");
    remove_directory(restored_directory);
    free(restored);
    fixture_remove(fixture, 2U);
}

static void pending_rejection(const char *source, const char *build_root) {
    checkpoint_fixture *fixture = fixture_create(source, build_root, "pending");
    evolve_trial *trial = calloc(1U, sizeof(*trial));
    CHECK(trial != NULL);
    publish(fixture, &fixture->run);
    const cgai_life_events events = contact(&fixture->run);
    trial_begin(&fixture->run, &events, trial);
    ++fixture->run.completed_generations;
    rejected_publish(fixture);
    pending_bundle_rejection(fixture);
    CHECK(fixture->run.memory->choice.pending == 1U);
    const cgai_life_context_feedback deferred = {CGAI_LIFE_CONTEXT_TRAIN, 1U, 1U, UINT64_C(10001),
                                                 0.0};
    CHECK(cgai_life_context_choice_observe(fixture->run.memory, &trial->choice, &deferred) ==
          CGAI_LIFE_OK);
    evolve_mutation_destroy(&trial->candidate);
    free(trial);
    fixture_remove(fixture, 0U);
}

static void no_head_change(const cgai_life_context_choice_stats *before,
                           const cgai_life_context_choice_stats *after) {
    CHECK(before->version == after->version);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group) {
        CHECK(before->group_steps[group] == after->group_steps[group]);
        CHECK(before->group_hashes[group] == after->group_hashes[group]);
    }
}

static void advance_deferred(evolve_run *run, evolve_trial *trial) {
    const cgai_life_events events = contact(run);
    trial_begin(run, &events, trial);
    ++run->proposals;
    deferred_trial(run, trial);
    no_head_change(&trial->before, &trial->after);
    trial_activity(run, trial);
    CHECK(evolve_checkpoint_record(run, trial, 0U));
    ++run->completed_generations;
}

static void deferred_receipt(const char *source, const char *build_root) {
    checkpoint_fixture *fixture = fixture_create(source, build_root, "deferred");
    evolve_trial *trial = calloc(1U, sizeof(*trial));
    evolve_run *restored = calloc(1U, sizeof(*restored));
    CHECK(trial != NULL && restored != NULL);
    publish(fixture, &fixture->run);
    advance_deferred(&fixture->run, trial);
    publish(fixture, &fixture->run);
    restore(fixture, fixture->run.latest_checkpoint, restored);
    same_search(&fixture->run, restored);
    CHECK(restored->ledger[0].verified == 1U && restored->ledger[0].deferred == 1U);
    CHECK(restored->ledger[0].after.deferred == 1U && restored->ledger[0].after.observations == 0U);
    release_run(restored);
    free(restored);
    evolve_mutation_destroy(&trial->candidate);
    free(trial);
    fixture_remove(fixture, 1U);
}

static void zero_epochs(const char *source, const char *build_root) {
    checkpoint_fixture *fixture = fixture_create_epochs(source, build_root, "zero-epochs", 0U);
    evolve_run *restored = calloc(1U, sizeof(*restored));
    CHECK(restored != NULL);
    publish(fixture, &fixture->run);
    advance_search(&fixture->run);
    publish(fixture, &fixture->run);
    restore(fixture, fixture->run.latest_checkpoint, restored);
    same_search(&fixture->run, restored);
    const evolve_checkpoint_receipt *receipt = &restored->ledger[0];
    no_head_change(&receipt->before, &receipt->after);
    CHECK(receipt->after.observations == receipt->before.observations + 1U);
    CHECK(receipt->deferred == 0U && receipt->after.deferred == receipt->before.deferred);
    CHECK(restored->memory->run.config.training_epochs == 0U);
    release_run(restored);
    free(restored);
    fixture_remove(fixture, 1U);
}

static void import_choice_history(evolve_run *run) {
    const cgai_life_events events = contact(run);
    const uint32_t actions[] = {1U, 80U};
    cgai_life_context_choice choice;
    CHECK(cgai_life_context_choice_begin(run->memory, 0U, "Synthetic imported source context",
                                         actions, 2U, 80U, &choice) == CGAI_LIFE_OK);
    CHECK(choice.generation == events.generation);
    const cgai_life_context_feedback feedback = {CGAI_LIFE_CONTEXT_TRAIN, 1U, 0U, UINT64_C(10002),
                                                 0.75};
    CHECK(cgai_life_context_choice_observe(run->memory, &choice, &feedback) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_choice_get_stats(run->memory, &run->initial_choices) == CGAI_LIFE_OK);
    run->prepared_generation = events.generation;
    run->initial_memory_checksum = cgai_life_context_hash(run->memory);
    run->prepared_memory_checksum = run->initial_memory_checksum;
}

static void imported_history(const char *source, const char *build_root) {
    checkpoint_fixture *fixture = fixture_create(source, build_root, "imported");
    evolve_run *restored = calloc(1U, sizeof(*restored));
    CHECK(restored != NULL);
    import_choice_history(&fixture->run);
    CHECK(fixture->run.initial_choices.version == 1U);
    publish(fixture, &fixture->run);
    advance_search(&fixture->run);
    publish(fixture, &fixture->run);
    restore(fixture, fixture->run.latest_checkpoint, restored);
    same_search(&fixture->run, restored);
    CHECK(restored->initial_choices.version == 1U && restored->ledger_count == 1U);
    CHECK(restored->ledger[0].version_before == 1U && restored->ledger[0].version_after == 2U);
    release_run(restored);
    free(restored);
    fixture_remove(fixture, 1U);
}

static void terminal_bundle(const char *source, const char *build_root) {
    checkpoint_fixture *fixture = fixture_create(source, build_root, "terminal");
    evolve_run *restored = calloc(1U, sizeof(*restored));
    CHECK(restored != NULL);
    fixture->run.options.generations = 1U;
    publish(fixture, &fixture->run);
    advance_search(&fixture->run);
    publish(fixture, &fixture->run);
    CHECK(cgai_life_context_train_step(fixture->run.memory, EVOLVE_CONTEXT_ACTIVITY_GENERATIONS,
                                       NULL) == CGAI_LIFE_OK);
    fixture->run.finalized = 1U;
    publish(fixture, &fixture->run);
    CHECK(strstr(fixture->run.latest_checkpoint, "-final") != NULL);
    restore(fixture, fixture->run.latest_checkpoint, restored);
    same_search(&fixture->run, restored);
    repeated_destination(fixture);
    bundle_remove(fixture->run.latest_checkpoint);
    release_run(restored);
    free(restored);
    fixture_remove(fixture, 1U);
}

static int child_continue(int argc, char **argv) {
    CHECK(argc == 6);
    evolve_run *run = calloc(1U, sizeof(*run));
    CHECK(run != NULL && evolve_create_directory(argv[5]));
    CHECK(evolve_absolute_path(argv[5], run->output, sizeof(run->output)));
    run->options.output = run->output;
    CHECK(evolve_checkpoint_restore(argv[2], argv[3], argv[4], run));
    advance_search(run);
    CHECK(evolve_checkpoint_publish(run, argv[3], argv[4]));
    CHECK(run->completed_generations == 2U && run->ledger_count == 2U);
    release_run(run);
    free(run);
    puts("Synthetic checkpoint fresh-process continuation complete");
    return 0;
}

static void launch_child(const char *executable, checkpoint_fixture *fixture,
                         const char *checkpoint, char *branch) {
    char log[PATH_BYTES];
    join(branch, fixture->directory, "process-branch");
    join(log, fixture->directory, "process.log");
    const char *const arguments[] = {
        "--child", checkpoint, fixture->source_root, fixture->inputs_manifest, branch, NULL};
    int result = -1;
    CHECK(evolve_process_run(executable, arguments, log, 60U, &result));
    CHECK(result == 0);
    CHECK(remove(log) == 0);
}

static void fresh_process(const char *executable, const char *source, const char *build_root) {
    checkpoint_fixture *fixture = fixture_create(source, build_root, "process");
    evolve_run *restored = calloc(1U, sizeof(*restored));
    CHECK(restored != NULL);
    char boundary[PATH_BYTES], branch[PATH_BYTES], child_checkpoint[PATH_BYTES];
    publish(fixture, &fixture->run);
    advance_search(&fixture->run);
    publish(fixture, &fixture->run);
    memcpy(boundary, fixture->run.latest_checkpoint, sizeof(boundary));
    launch_child(executable, fixture, boundary, branch);
    advance_search(&fixture->run);
    publish(fixture, &fixture->run);
    checkpoint_name(child_checkpoint, branch, 2U, 0);
    restore(fixture, child_checkpoint, restored);
    same_search(&fixture->run, restored);
    CHECK(fixture->run.checkpoint_hash == restored->checkpoint_hash);
    bundle_remove(child_checkpoint);
    remove_directory(branch);
    release_run(restored);
    free(restored);
    fixture_remove(fixture, 2U);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--child") == 0)
        return child_continue(argc, argv);
    CHECK(argc == 3);
    char executable[PATH_BYTES];
    CHECK(evolve_absolute_path(argv[0], executable, sizeof(executable)));
    source_search_roundtrip(argv[1], argv[2]);
    pending_rejection(argv[1], argv[2]);
    deferred_receipt(argv[1], argv[2]);
    zero_epochs(argv[1], argv[2]);
    imported_history(argv[1], argv[2]);
    terminal_bundle(argv[1], argv[2]);
    fresh_process(executable, argv[1], argv[2]);
    puts("Synthetic source-search bundles, rejection and native restart checks passed");
    return 0;
}
