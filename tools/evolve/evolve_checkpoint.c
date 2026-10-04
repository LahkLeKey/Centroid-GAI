/** @file evolve_checkpoint.c @brief Staged immutable source-search boundary transactions. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "build_config.h"
#include "context_repository.h"
#include "evolve_checkpoint_codec.h"
#include "life_context.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CHECKPOINT_MEMBER_LIMIT (64U * 1024U * 1024U)
#define CHECKPOINT_MANIFEST_LIMIT (1024U * 1024U)
#define CHECKPOINT_HASH_START UINT64_C(14695981039346656037)

static const char *const members[] = {"state.bin", "parent.c", "memory.context", "inputs.txt",
                                      "external-inputs.txt"};

typedef struct checkpoint_manifest {
    uint64_t sizes[5];
    uint64_t hashes[5];
    uint64_t bundle_hash;
} checkpoint_manifest;

typedef struct checkpoint_replay {
    char *storage;
    evolve_mutation_source parent;
    life_fitness_report training;
    life_fitness_report development;
    life_fitness_report confirmation;
    cgai_life_context_choice_stats choices;
    uint64_t chain;
    uint32_t accepted;
} checkpoint_replay;

static int restore_candidate(const char *path, const char *root, const char *inputs,
                             evolve_run *run);

static void discard_candidate(evolve_run *run) {
    free(run->parent_storage);
    cgai_life_context_destroy(run->memory);
    free(run);
}

static uint64_t hash_word(uint64_t hash, uint64_t word) {
    for (size_t i = 0U; i < 8U; ++i) {
        hash = (hash ^ (word & UINT64_C(255))) * UINT64_C(1099511628211);
        word >>= 8U;
    }
    return hash;
}

uint64_t evolve_checkpoint_build_recipe(void) {
    const char *const fields[] = {"LIFE_SOURCE_BUILD_RECIPE\t1;Release;tests=1;evolution=1",
                                  EVOLVE_CMAKE_GENERATOR,
                                  EVOLVE_CMAKE_PLATFORM,
                                  EVOLVE_CMAKE_TOOLSET,
                                  EVOLVE_C_COMPILER,
                                  EVOLVE_CMAKE_PROGRAM,
                                  EVOLVE_CTEST_PROGRAM,
                                  EVOLVE_CLANG_TIDY,
                                  EVOLVE_CLANG_FORMAT};
    uint64_t hash = CHECKPOINT_HASH_START;
    for (size_t i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const size_t size = strlen(fields[i]) + 1U;
        for (size_t j = 0U; j < size; ++j)
            hash = (hash ^ (unsigned char)fields[i][j]) * UINT64_C(1099511628211);
    }
    return hash_word(hash, EVOLVE_SANITIZERS);
}

static int stats_equal(const cgai_life_context_choice_stats *first,
                       const cgai_life_context_choice_stats *second) {
    if (first->group_count != second->group_count || first->version != second->version ||
        first->decisions != second->decisions || first->observations != second->observations ||
        first->deferred != second->deferred || first->pending != second->pending)
        return 0;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i)
        if (first->group_steps[i] != second->group_steps[i] ||
            first->group_hashes[i] != second->group_hashes[i])
            return 0;
    return 1;
}

static int counters_valid(const evolve_checkpoint_receipt *receipt, uint32_t epochs) {
    const cgai_life_context_choice_stats *before = &receipt->before, *after = &receipt->after;
    const uint64_t observation = receipt->deferred == 0U ? 1U : 0U;
    const uint64_t learned = observation != 0U && epochs != 0U ? 1U : 0U;
    if (before->group_count < CGAI_LIFE_MIN_GROUPS || before->group_count > CGAI_LIFE_GROUPS ||
        after->group_count != before->group_count || before->pending != 0U ||
        after->pending != 0U || before->decisions == UINT64_MAX ||
        before->observations > UINT64_MAX - observation ||
        before->deferred > UINT64_MAX - receipt->deferred ||
        before->version > UINT64_MAX - learned || after->decisions != before->decisions + 1U ||
        after->observations != before->observations + observation ||
        after->deferred != before->deferred + receipt->deferred ||
        after->version != before->version + learned)
        return 0;
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group) {
        const uint64_t steps =
            learned != 0U && (receipt->participant_mask & (1U << group)) != 0U ? epochs : 0U;
        if (before->group_steps[group] > UINT64_MAX - steps ||
            after->group_steps[group] != before->group_steps[group] + steps ||
            (steps == 0U && before->group_hashes[group] != after->group_hashes[group]))
            return 0;
    }
    return 1;
}

static uint64_t receipt_token(const evolve_checkpoint_receipt *receipt) {
    const cgai_life_context_choice *choice = &receipt->choice;
    uint64_t prediction;
    memcpy(&prediction, &choice->predicted_utility, sizeof(prediction));
    const uint64_t fields[] = {receipt->before.decisions + 1U,
                               choice->input_hash,
                               choice->model_version,
                               choice->world_hash,
                               choice->generation,
                               choice->conflict_id,
                               choice->participant_mask,
                               choice->action,
                               choice->supported_groups,
                               prediction};
    uint64_t hash = CHECKPOINT_HASH_START;
    for (size_t i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i)
        hash = hash_word(hash, fields[i]);
    return hash == 0U ? UINT64_C(1) : hash;
}

static int receipt_identity_valid(const evolve_checkpoint_receipt *receipt) {
    const cgai_life_context_choice *choice = &receipt->choice;
    if (memchr(receipt->input, '\0', sizeof(receipt->input)) == NULL ||
        memchr(receipt->proof, '\0', sizeof(receipt->proof)) == NULL)
        return 0;
    return receipt->token != 0U && receipt->input[0] != '\0' && receipt->evidence_hash != 0U &&
           receipt->token == choice->token && receipt->token == receipt_token(receipt) &&
           receipt->input_hash == choice->input_hash &&
           receipt->input_hash == evolve_bytes_hash(receipt->input, strlen(receipt->input)) &&
           receipt->generation == choice->generation &&
           receipt->generation == receipt->event.generation &&
           receipt->participant_mask == choice->participant_mask &&
           receipt->participant_mask == receipt->event.participant_mask &&
           receipt->action == choice->action && choice->conflict_id == receipt->event.conflict_id &&
           choice->world_hash == receipt->event.source_world_hash &&
           receipt->version_before == choice->model_version &&
           receipt->version_before == receipt->before.version &&
           receipt->version_after == receipt->after.version &&
           isfinite(choice->predicted_utility) && fabs(choice->predicted_utility) <= 1.0 &&
           receipt->participant_mask != 0U && receipt->before.group_count >= CGAI_LIFE_MIN_GROUPS &&
           receipt->before.group_count <= CGAI_LIFE_GROUPS &&
           receipt->participant_mask < (1U << receipt->before.group_count) &&
           receipt->action < CGAI_LIFE_CONTEXT_ACTIONS && receipt->accepted <= 1U &&
           receipt->verified == 1U && receipt->deferred <= 1U && isfinite(receipt->utility) &&
           fabs(receipt->utility) <= 1.0 && isfinite(receipt->parent_training_loss) &&
           receipt->parent_training_loss >= 0.0;
}

static int prediction_valid(const evolve_checkpoint_receipt *receipt) {
    const cgai_life_context_choice *prediction = &receipt->prediction_after;
    if (receipt->action_count == 0U || receipt->action_count > EVOLVE_MUTATION_MAX_ALTERNATIVES ||
        receipt->edit_count > EVOLVE_MUTATION_MAX_EDITS || prediction->token != 0U ||
        prediction->generation != 0U || prediction->conflict_id != 0U ||
        prediction->world_hash != 0U || prediction->input_hash != receipt->input_hash ||
        prediction->model_version != receipt->version_after ||
        prediction->participant_mask != receipt->participant_mask ||
        (prediction->supported_groups & ~receipt->participant_mask) != 0U ||
        (receipt->choice.supported_groups & ~receipt->participant_mask) != 0U ||
        !isfinite(prediction->predicted_utility) || fabs(prediction->predicted_utility) > 1.0)
        return 0;
    int found = 0;
    for (size_t i = 0U; i < receipt->action_count; ++i)
        found = found || receipt->actions[i] == prediction->action;
    for (size_t i = 0U; i < receipt->edit_count; ++i)
        if (memchr(receipt->edits[i].before_literal, '\0',
                   sizeof(receipt->edits[i].before_literal)) == NULL ||
            memchr(receipt->edits[i].after_literal, '\0',
                   sizeof(receipt->edits[i].after_literal)) == NULL)
            return 0;
    return found;
}

static int training_proof(const evolve_checkpoint_receipt *receipt, char *proof, size_t capacity) {
    return snprintf(
        proof, capacity,
        "LIFE_SOURCE_TRAINING_FEEDBACK\t1\nsplit\ttrain\nchoice_token\t%" PRIu64
        "\ninput_hash\t%" PRIu64 "\naction\t%u\nparent_source_checksum\t%" PRIu64
        "\ncandidate_source_checksum\t%" PRIu64 "\npack_hash\t%" PRIu64 "\nrecords\t%" PRIu64
        "\nparent_loss\t%.17g\ncandidate_loss\t%.17g\nutility\t%.17g\nEND\n",
        receipt->token, receipt->input_hash, receipt->action, receipt->parent_checksum,
        receipt->candidate_checksum, receipt->training.pack_hash, receipt->training.records,
        receipt->parent_training_loss, receipt->training.mean_loss, receipt->utility);
}

static int deferred_proof(const evolve_checkpoint_receipt *receipt, char *proof, size_t capacity) {
    static const char *const stages[] = {"configure_rejected", "build_rejected", "tests_rejected",
                                         "checks_rejected", "measurement_rejected"};
    if (receipt->audit_status < EVOLVE_CHECKPOINT_CONFIGURE_REJECTED ||
        receipt->audit_status > EVOLVE_CHECKPOINT_FITNESS_REJECTED)
        return -1;
    return snprintf(proof, capacity,
                    "LIFE_SOURCE_DEFERRED_FEEDBACK\t1\nchoice_token\t%" PRIu64
                    "\ninput_hash\t%" PRIu64 "\naction\t%u\nparent_checksum\t%" PRIu64
                    "\ncandidate_checksum\t%" PRIu64
                    "\nstage\t%s\nutility_measured\t0\ndeferred\t1\nEND\n",
                    receipt->token, receipt->input_hash, receipt->action, receipt->parent_checksum,
                    receipt->candidate_checksum,
                    stages[receipt->audit_status - EVOLVE_CHECKPOINT_CONFIGURE_REJECTED]);
}

static int proof_valid(const evolve_checkpoint_receipt *receipt) {
    char expected[EVOLVE_CHECKPOINT_PROOF_BYTES];
    const int length = receipt->deferred != 0U
                           ? deferred_proof(receipt, expected, sizeof(expected))
                           : training_proof(receipt, expected, sizeof(expected));
    return length > 0 && (size_t)length < sizeof(expected) &&
           receipt->proof_bytes == (uint32_t)length && strlen(receipt->proof) == (size_t)length &&
           memcmp(receipt->proof, expected, (size_t)length) == 0 &&
           receipt->evidence_hash == evolve_bytes_hash(receipt->proof, receipt->proof_bytes);
}

static int reports_equal(const life_fitness_report *first, const life_fitness_report *second) {
    return first->version == second->version && first->split == second->split &&
           first->complete == second->complete && first->pack_hash == second->pack_hash &&
           first->pack_records == second->pack_records && first->records == second->records &&
           first->correct == second->correct && first->mean_loss == second->mean_loss &&
           first->model_count == second->model_count &&
           first->training_epochs == second->training_epochs &&
           first->training_generations == second->training_generations &&
           first->training_updates == second->training_updates &&
           first->forward_passes == second->forward_passes &&
           first->active_modules == second->active_modules &&
           first->active_centroids == second->active_centroids;
}

static int report_split_valid(const life_fitness_report *report, life_fitness_split split) {
    return life_fitness_report_valid(report) && report->split == split;
}

static int report_optional_valid(const life_fitness_report *report, life_fitness_split split) {
    const life_fitness_report empty = {0};
    return report_split_valid(report, split) || reports_equal(report, &empty);
}

static int measurements_valid(const evolve_checkpoint_receipt *receipt,
                              const life_fitness_report *training,
                              const life_fitness_report *development,
                              const life_fitness_report *confirmation) {
    if (receipt->parent_training_loss != training->mean_loss ||
        receipt->accepted != (uint32_t)(receipt->audit_status == EVOLVE_CHECKPOINT_ACCEPTED))
        return 0;
    if (receipt->deferred != 0U)
        return receipt->utility == 0.0 && receipt->accepted == 0U &&
               report_optional_valid(&receipt->training, LIFE_FITNESS_TRAIN) &&
               report_optional_valid(&receipt->development, LIFE_FITNESS_DEV) &&
               report_optional_valid(&receipt->confirmation, LIFE_FITNESS_CONFIRM);
    if (!report_split_valid(&receipt->training, LIFE_FITNESS_TRAIN) ||
        !report_split_valid(&receipt->development, LIFE_FITNESS_DEV) ||
        !report_split_valid(&receipt->confirmation, LIFE_FITNESS_CONFIRM) ||
        receipt->training.pack_hash != training->pack_hash ||
        receipt->training.records != training->records)
        return 0;
    const double difference = training->mean_loss - receipt->training.mean_loss;
    const double utility = difference < -1.0 ? -1.0 : difference > 1.0 ? 1.0 : difference;
    const int dev = life_fitness_admits(development, &receipt->development);
    const int confirm = life_fitness_admits(confirmation, &receipt->confirmation);
    return receipt->utility == utility &&
           receipt->audit_status == (uint32_t)(!dev       ? EVOLVE_CHECKPOINT_DEV_REJECTED
                                               : !confirm ? EVOLVE_CHECKPOINT_CONFIRM_REJECTED
                                                          : EVOLVE_CHECKPOINT_ACCEPTED);
}

static int edit_equal(const evolve_mutation_edit *first, const evolve_mutation_edit *second) {
    return first->site == second->site && first->before_choice == second->before_choice &&
           first->after_choice == second->after_choice &&
           first->source_offset == second->source_offset &&
           first->candidate_offset == second->candidate_offset &&
           strcmp(first->before_literal, second->before_literal) == 0 &&
           strcmp(first->after_literal, second->after_literal) == 0;
}

static int render_receipt(const evolve_checkpoint_receipt *receipt,
                          const evolve_mutation_source *parent,
                          evolve_mutation_candidate *candidate) {
    uint32_t actions[EVOLVE_MUTATION_MAX_ALTERNATIVES];
    size_t count = 0U;
    uint32_t fallback;
    if (receipt->parent_checksum != parent->checksum ||
        evolve_mutation_alternatives(parent, &receipt->event, actions, &count, &fallback) !=
            EVOLVE_MUTATION_OK ||
        count != receipt->action_count || fallback != receipt->fallback ||
        memcmp(actions, receipt->actions, count * sizeof(*actions)) != 0 ||
        evolve_mutation_select(parent, &receipt->event, receipt->action, candidate) !=
            EVOLVE_MUTATION_OK)
        return 0;
    if (candidate->checksum != receipt->candidate_checksum ||
        candidate->domain_frontier_bits != receipt->domain_frontier_bits ||
        candidate->edit_count != receipt->edit_count)
        return 0;
    for (size_t i = 0U; i < candidate->edit_count; ++i)
        if (!edit_equal(&candidate->edits[i], &receipt->edits[i]))
            return 0;
    return 1;
}

static uint64_t ledger_start(const evolve_run *run) {
    return hash_word(hash_word(CHECKPOINT_HASH_START, run->initial_parent_checksum),
                     run->prepared_memory_checksum);
}

static int receipt_valid(const evolve_run *run, const evolve_checkpoint_receipt *receipt,
                         const cgai_life_context_choice_stats *before) {
    return receipt_identity_valid(receipt) && prediction_valid(receipt) && proof_valid(receipt) &&
           stats_equal(before, &receipt->before) &&
           counters_valid(receipt, run->memory->run.config.training_epochs) &&
           receipt->generation > run->prepared_generation &&
           (uint64_t)receipt->generation <=
               (uint64_t)run->prepared_generation + run->completed_generations;
}

static int boundary_valid(const evolve_run *run) {
    cgai_life_context_choice_stats choices;
    cgai_life_context_stats context;
    if (run->memory == NULL || run->parent_storage == NULL || run->options.candidates == 0U ||
        run->options.candidates > 8U || run->options.generations == 0U ||
        run->options.generations > 256U || run->completed_generations > run->options.generations ||
        run->proposals != run->ledger_count || run->ledger_count > run->options.candidates ||
        run->accepted > run->proposals || run->finalized > 1U ||
        (run->finalized != 0U && run->completed_generations < run->options.generations &&
         run->proposals < run->options.candidates) ||
        run->initial_choices.pending != 0U || run->initial_parent_checksum == 0U ||
        run->inputs_checksum == 0U ||
        run->build_recipe_checksum != evolve_checkpoint_build_recipe() ||
        cgai_life_context_choice_get_stats(run->memory, &choices) != CGAI_LIFE_OK ||
        choices.pending != 0U || cgai_life_context_get_stats(run->memory, &context) != CGAI_LIFE_OK)
        return 0;
    const uint64_t generation = (uint64_t)run->prepared_generation + run->completed_generations +
                                (run->finalized != 0U ? EVOLVE_CONTEXT_ACTIVITY_GENERATIONS : 0U);
    return generation == context.generation &&
           run->initial_choices.group_count == context.group_count &&
           report_split_valid(&run->baseline_training, LIFE_FITNESS_TRAIN) &&
           report_split_valid(&run->baseline_development, LIFE_FITNESS_DEV) &&
           report_split_valid(&run->baseline_confirmation, LIFE_FITNESS_CONFIRM);
}

static int receipt_unique(const evolve_run *run, const evolve_checkpoint_receipt *receipt,
                          size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (receipt->token == run->ledger[i].token ||
            receipt->generation < run->ledger[i].generation)
            return 0;
    return 1;
}

static int replay_adopt(checkpoint_replay *replay, const evolve_checkpoint_receipt *receipt,
                        evolve_mutation_candidate *candidate) {
    free(replay->storage);
    replay->storage = candidate->bytes;
    candidate->bytes = NULL;
    replay->training = receipt->training;
    replay->development = receipt->development;
    replay->confirmation = receipt->confirmation;
    ++replay->accepted;
    return evolve_mutation_validate(replay->storage, candidate->size, &replay->parent) ==
           EVOLVE_MUTATION_OK;
}

static int replay_receipt(const evolve_run *run, checkpoint_replay *replay, size_t index) {
    evolve_mutation_candidate candidate = {0};
    const evolve_checkpoint_receipt *receipt = &run->ledger[index];
    replay->chain = evolve_checkpoint_receipt_hash(receipt, replay->chain);
    int okay = replay->chain != 0U && replay->chain == receipt->chain_hash &&
               receipt_valid(run, receipt, &replay->choices) &&
               measurements_valid(receipt, &replay->training, &replay->development,
                                  &replay->confirmation) &&
               render_receipt(receipt, &replay->parent, &candidate) &&
               receipt_unique(run, receipt, index);
    if (okay && receipt->accepted != 0U)
        okay = replay_adopt(replay, receipt, &candidate);
    replay->choices = receipt->after;
    evolve_mutation_destroy(&candidate);
    return okay;
}

static int replay_matches(const evolve_run *run, const checkpoint_replay *replay) {
    cgai_life_context_choice_stats current;
    return replay->parent.checksum == run->parent.checksum &&
           replay->parent.size == run->parent.size &&
           memcmp(replay->parent.bytes, run->parent.bytes, replay->parent.size) == 0 &&
           replay->accepted == run->accepted && reports_equal(&replay->training, &run->training) &&
           reports_equal(&replay->development, &run->development) &&
           reports_equal(&replay->confirmation, &run->confirmation) &&
           cgai_life_context_choice_get_stats(run->memory, &current) == CGAI_LIFE_OK &&
           stats_equal(&replay->choices, &current);
}

static int ledger_valid(const evolve_run *run, const char *root) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    checkpoint_replay replay = {0};
    replay.training = run->baseline_training;
    replay.development = run->baseline_development;
    replay.confirmation = run->baseline_confirmation;
    replay.choices = run->initial_choices;
    replay.chain = ledger_start(run);
    if (!evolve_path_join(path, sizeof(path), root, "src/gameplay/gameplay_model.c") ||
        !evolve_read_source(path, &replay.parent, &replay.storage))
        return 0;
    int okay = replay.parent.checksum == run->initial_parent_checksum;
    for (size_t i = 0U; okay && i < run->ledger_count; ++i)
        okay = replay_receipt(run, &replay, i);
    okay = okay && replay_matches(run, &replay);
    free(replay.storage);
    return okay;
}

static void fill_receipt(evolve_checkpoint_receipt *receipt, const evolve_trial *trial,
                         uint32_t accepted) {
    memset(receipt, 0, sizeof(*receipt));
    receipt->token = trial->choice.token;
    receipt->input_hash = trial->choice.input_hash;
    receipt->evidence_hash = trial->feedback.evidence_hash;
    receipt->parent_checksum = trial->candidate.source_checksum;
    receipt->candidate_checksum = trial->candidate.checksum;
    receipt->version_before = trial->before.version;
    receipt->version_after = trial->after.version;
    receipt->generation = trial->choice.generation;
    receipt->action = trial->choice.action;
    receipt->participant_mask = trial->choice.participant_mask;
    receipt->accepted = accepted;
    receipt->verified = trial->feedback.verified;
    receipt->deferred = trial->feedback.deferred;
    receipt->utility = trial->feedback.utility;
}

static void fill_evidence(evolve_checkpoint_receipt *receipt, const evolve_trial *trial) {
    memcpy(receipt->input, trial->input, sizeof(receipt->input));
    memcpy(receipt->proof, trial->feedback_proof, sizeof(receipt->proof));
    receipt->proof_bytes = (uint32_t)trial->feedback_proof_bytes;
    receipt->event = *trial->event;
    receipt->choice = trial->choice;
    receipt->prediction_after = trial->prediction_after;
    receipt->before = trial->before;
    receipt->after = trial->after;
    memcpy(receipt->actions, trial->actions, sizeof(receipt->actions));
    receipt->action_count = (uint32_t)trial->action_count;
    receipt->fallback = trial->fallback;
    receipt->audit_status = trial->audit_status;
    receipt->domain_frontier_bits = trial->candidate.domain_frontier_bits;
    receipt->edit_count = trial->candidate.edit_count;
    memcpy(receipt->edits, trial->candidate.edits, sizeof(receipt->edits));
    receipt->training = trial->training;
    receipt->development = trial->development;
    receipt->confirmation = trial->confirmation;
    receipt->parent_training_loss = trial->parent_training_loss;
}

static int record_valid(const evolve_run *run, const evolve_checkpoint_receipt *receipt) {
    evolve_mutation_candidate candidate = {0};
    cgai_life_context_choice_stats current;
    const cgai_life_context_choice_stats *before = run->ledger_count == 0U
                                                       ? &run->initial_choices
                                                       : &run->ledger[run->ledger_count - 1U].after;
    const int okay =
        receipt_identity_valid(receipt) && prediction_valid(receipt) && proof_valid(receipt) &&
        stats_equal(before, &receipt->before) &&
        cgai_life_context_choice_get_stats(run->memory, &current) == CGAI_LIFE_OK &&
        stats_equal(&current, &receipt->after) &&
        counters_valid(receipt, run->memory->run.config.training_epochs) &&
        measurements_valid(receipt, &run->training, &run->development, &run->confirmation) &&
        render_receipt(receipt, &run->parent, &candidate) &&
        receipt_unique(run, receipt, run->ledger_count);
    evolve_mutation_destroy(&candidate);
    return okay;
}

int evolve_checkpoint_record(evolve_run *run, const evolve_trial *trial, uint32_t accepted) {
    evolve_checkpoint_receipt receipt;
    if (run == NULL || trial == NULL || run->memory == NULL || trial->event == NULL ||
        accepted > 1U || run->ledger_count >= 8U || run->proposals != run->ledger_count + 1U ||
        trial->feedback_proof_bytes >= EVOLVE_CHECKPOINT_PROOF_BYTES ||
        memchr(trial->input, '\0', sizeof(trial->input)) == NULL ||
        memchr(trial->feedback_proof, '\0', sizeof(trial->feedback_proof)) == NULL)
        return 0;
    fill_receipt(&receipt, trial, accepted);
    fill_evidence(&receipt, trial);
    if (!record_valid(run, &receipt))
        return 0;
    receipt.chain_hash = evolve_checkpoint_receipt_hash(
        &receipt, run->ledger_count == 0U ? ledger_start(run)
                                          : run->ledger[run->ledger_count - 1U].chain_hash);
    if (receipt.chain_hash == 0U)
        return 0;
    run->ledger[run->ledger_count++] = receipt;
    return 1;
}

static int regular_file(const char *path) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0U;
#else
    struct stat info;
    return lstat(path, &info) == 0 && S_ISREG(info.st_mode);
#endif
}

static int stream_identity(FILE *file, uint64_t *size, uint64_t *hash) {
    unsigned char buffer[8192];
    uint64_t count = 0U, value = CHECKPOINT_HASH_START;
    while (count <= CHECKPOINT_MEMBER_LIMIT && !ferror(file) && !feof(file)) {
        const size_t used = fread(buffer, 1U, sizeof(buffer), file);
        count += used;
        for (size_t i = 0U; i < used; ++i)
            value = (value ^ buffer[i]) * UINT64_C(1099511628211);
    }
    *size = count;
    *hash = value;
    return count <= CHECKPOINT_MEMBER_LIMIT && !ferror(file) && feof(file);
}

static int file_identity(const char *path, uint64_t *size, uint64_t *hash) {
    if (!regular_file(path))
        return 0;
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    int okay = stream_identity(file, size, hash);
    if (fclose(file) != 0)
        okay = 0;
    return okay;
}

static int copy_manifest(const char *from, const char *to) {
    unsigned char *bytes = malloc(CHECKPOINT_MANIFEST_LIMIT + 1U);
    FILE *file = from != NULL && from[0] != '\0' ? fopen(from, "rb") : NULL;
    size_t count = 0U;
    int okay = bytes != NULL;
    if (okay && from != NULL && from[0] != '\0') {
        okay = file != NULL;
        if (okay) {
            count = fread(bytes, 1U, CHECKPOINT_MANIFEST_LIMIT + 1U, file);
            okay = count <= CHECKPOINT_MANIFEST_LIMIT && !ferror(file) && feof(file);
        }
    }
    if (file != NULL && fclose(file) != 0)
        okay = 0;
    okay = okay && evolve_write_exclusive(to, bytes, count);
    free(bytes);
    return okay;
}

static int live_identities(const evolve_run *run, const char *root, const char *inputs) {
    uint64_t hash;
    context_repository_batch *batch = NULL;
    context_repository_report report;
    if (!evolve_manifest_hash(inputs, &hash) || hash != run->inputs_checksum ||
        (run->external_checksum != 0U &&
         (!evolve_manifest_hash(run->external_manifest, &hash) || hash != run->external_checksum)))
        return 0;
    const int okay = context_repository_scan(root, &batch, &report) &&
                     report.source_hash == run->source_context_checksum;
    context_repository_destroy(batch);
    return okay;
}

static int manifest_members(const char *directory, checkpoint_manifest *manifest) {
    uint64_t hash = hash_word(CHECKPOINT_HASH_START, 1U);
    for (size_t i = 0U; i < 5U; ++i) {
        char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
        if (!evolve_path_join(path, sizeof(path), directory, members[i]) ||
            !file_identity(path, &manifest->sizes[i], &manifest->hashes[i]))
            return 0;
        hash = hash_word(hash_word(hash, manifest->sizes[i]), manifest->hashes[i]);
    }
    manifest->bundle_hash = hash;
    return 1;
}

static int binary_word(FILE *file, uint64_t *value, int reading) {
    unsigned char bytes[8];
    for (size_t i = 0U; i < 8U; ++i)
        bytes[i] = (unsigned char)(*value >> (8U * i));
    if ((reading ? fread(bytes, 1U, 8U, file) : fwrite(bytes, 1U, 8U, file)) != 8U)
        return 0;
    if (reading) {
        *value = 0U;
        for (size_t i = 0U; i < 8U; ++i)
            *value |= (uint64_t)bytes[i] << (8U * i);
    }
    return 1;
}

static int manifest_stream(FILE *file, checkpoint_manifest *manifest, int reading) {
    uint64_t magic = UINT64_C(0x314b48434c5645), version = 1U;
    int okay = binary_word(file, &magic, reading) && magic == UINT64_C(0x314b48434c5645) &&
               binary_word(file, &version, reading) && version == 1U;
    for (size_t i = 0U; okay && i < 5U; ++i)
        okay = binary_word(file, &manifest->sizes[i], reading) &&
               binary_word(file, &manifest->hashes[i], reading);
    okay = okay && binary_word(file, &manifest->bundle_hash, reading);
    if (reading && okay)
        okay = fgetc(file) == EOF && !ferror(file);
    return okay;
}

static int manifest_file(const char *directory, checkpoint_manifest *manifest, int reading) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    if (!evolve_path_join(path, sizeof(path), directory, "manifest.bin") ||
        (reading && !regular_file(path)))
        return 0;
    FILE *file = fopen(path, reading ? "rb" : "wbx");
    if (file == NULL)
        return 0;
    int okay = manifest_stream(file, manifest, reading);
    if (fclose(file) != 0)
        okay = 0;
    return okay;
}

static int write_members(const char *directory, const evolve_run *run, const char *inputs) {
    char paths[5][EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    for (size_t i = 0U; i < 5U; ++i)
        if (!evolve_path_join(paths[i], sizeof(paths[i]), directory, members[i]))
            return 0;
    return evolve_checkpoint_state_write(paths[0], run) &&
           evolve_write_exclusive(paths[1], run->parent.bytes, run->parent.size) &&
           cgai_life_context_save(run->memory, paths[2]) == CGAI_LIFE_OK &&
           copy_manifest(inputs, paths[3]) &&
           copy_manifest(run->external_checksum != 0U ? run->external_manifest : NULL, paths[4]);
}

static void remove_stage(const char *directory) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    for (size_t i = 0U; i < 5U; ++i)
        if (evolve_path_join(path, sizeof(path), directory, members[i]))
            (void)remove(path);
    if (evolve_path_join(path, sizeof(path), directory, "manifest.bin"))
        (void)remove(path);
#ifdef _WIN32
    (void)RemoveDirectoryA(directory);
#else
    (void)rmdir(directory);
#endif
}

static int path_exists(const char *path) {
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat info;
    return lstat(path, &info) == 0 || errno != ENOENT;
#endif
}

static int stage_directory(const char *destination, char *stage, size_t capacity) {
    struct timespec stamp;
    if (timespec_get(&stamp, TIME_UTC) != TIME_UTC)
        return 0;
    for (uint32_t attempt = 0U; attempt < 32U; ++attempt) {
        const int size = snprintf(stage, capacity, "%s.stage.%" PRIu64 ".%lu.%u", destination,
                                  (uint64_t)stamp.tv_sec, (unsigned long)stamp.tv_nsec, attempt);
        if (size < 0 || (size_t)size >= capacity)
            return 0;
        if (evolve_create_directory(stage))
            return 1;
    }
    return 0;
}

static int publish_directory(const char *stage, const char *destination) {
    char lock[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    const int size = snprintf(lock, sizeof(lock), "%s.publish-lock", destination);
    if (size < 0 || (size_t)size >= sizeof(lock))
        return 0;
    FILE *file = fopen(lock, "wbx");
    if (file == NULL)
        return 0;
    int okay = fclose(file) == 0 && !path_exists(destination);
#ifdef _WIN32
    okay = okay && MoveFileExA(stage, destination, MOVEFILE_WRITE_THROUGH) != 0;
#else
    okay = okay && rename(stage, destination) == 0;
#endif
    (void)remove(lock);
    return okay;
}

static int stage_valid(const char *stage, const char *root, const char *inputs) {
    evolve_run *candidate = calloc(1U, sizeof(*candidate));
    if (candidate == NULL)
        return 0;
    const int okay = restore_candidate(stage, root, inputs, candidate);
    discard_candidate(candidate);
    return okay;
}

int evolve_checkpoint_publish(evolve_run *run, const char *root, const char *inputs) {
    char name[64], destination[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U],
        stage[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    checkpoint_manifest manifest = {0};
    if (run == NULL || root == NULL || inputs == NULL || !boundary_valid(run) ||
        !ledger_valid(run, root) || !live_identities(run, root, inputs))
        return 0;
    const int size = snprintf(name, sizeof(name), "checkpoint-%04u%s", run->completed_generations,
                              run->finalized != 0U ? "-final" : "");
    if (size < 0 || (size_t)size >= sizeof(name) ||
        !evolve_path_join(destination, sizeof(destination), run->output, name) ||
        path_exists(destination) || !stage_directory(destination, stage, sizeof(stage)))
        return 0;
    const uint64_t previous = run->predecessor_hash;
    run->predecessor_hash = run->checkpoint_hash;
    int okay = write_members(stage, run, inputs) && manifest_members(stage, &manifest) &&
               manifest_file(stage, &manifest, 0) && live_identities(run, root, inputs) &&
               stage_valid(stage, root, inputs) && publish_directory(stage, destination);
    if (!okay) {
        run->predecessor_hash = previous;
        remove_stage(stage);
        return 0;
    }
    run->checkpoint_hash = manifest.bundle_hash;
    memcpy(run->latest_checkpoint, destination, strlen(destination) + 1U);
    return 1;
}

static int validate_manifest(const char *path, checkpoint_manifest *expected) {
    checkpoint_manifest actual = {0};
    if (!manifest_file(path, expected, 1) || !manifest_members(path, &actual) ||
        actual.bundle_hash != expected->bundle_hash)
        return 0;
    for (size_t i = 0U; i < 5U; ++i)
        if (expected->sizes[i] != actual.sizes[i] || expected->hashes[i] != actual.hashes[i])
            return 0;
    return 1;
}

static int read_members(const char *directory, evolve_run *run) {
    char paths[5][EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    cgai_life_config config = cgai_life_config_default();
    config.enable_merges = 0U;
    for (size_t i = 0U; i < 5U; ++i)
        if (!evolve_path_join(paths[i], sizeof(paths[i]), directory, members[i]))
            return 0;
    if (!evolve_checkpoint_state_read(paths[0], run) ||
        !evolve_read_source(paths[1], &run->parent, &run->parent_storage) ||
        cgai_life_context_create(&config, &run->memory) != CGAI_LIFE_OK ||
        cgai_life_context_load(run->memory, paths[2]) != CGAI_LIFE_OK)
        return 0;
    memcpy(run->external_manifest, paths[4], strlen(paths[4]) + 1U);
    return 1;
}

static int restore_candidate(const char *path, const char *root, const char *inputs,
                             evolve_run *run) {
    checkpoint_manifest manifest = {0};
    checkpoint_manifest after = {0};
    char stored_inputs[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    uint64_t hash;
    if (!validate_manifest(path, &manifest) || !read_members(path, run) || !boundary_valid(run) ||
        !evolve_path_join(stored_inputs, sizeof(stored_inputs), path, "inputs.txt") ||
        !evolve_manifest_hash(stored_inputs, &hash) || hash != run->inputs_checksum ||
        !live_identities(run, root, inputs) || !ledger_valid(run, root) ||
        !validate_manifest(path, &after) || after.bundle_hash != manifest.bundle_hash)
        return 0;
    run->checkpoint_hash = manifest.bundle_hash;
    const size_t length = strlen(path);
    if (length > EVOLVE_PROCESS_MAX_PATH_BYTES)
        return 0;
    memcpy(run->latest_checkpoint, path, length + 1U);
    return 1;
}

static void transfer_candidate(evolve_run *run, evolve_run *candidate) {
    candidate->options.output = run->options.output;
    candidate->options.resume = run->options.resume;
    candidate->options.pause_after = run->options.pause_after;
    candidate->report = run->report;
    memcpy(candidate->output, run->output, sizeof(candidate->output));
    free(run->parent_storage);
    cgai_life_context_destroy(run->memory);
    *run = *candidate;
}

int evolve_checkpoint_restore(const char *path, const char *root, const char *inputs,
                              evolve_run *run) {
    if (path == NULL || root == NULL || inputs == NULL || run == NULL)
        return 0;
    evolve_run *candidate = calloc(1U, sizeof(*candidate));
    if (candidate == NULL)
        return 0;
    const int okay = restore_candidate(path, root, inputs, candidate);
    if (okay)
        transfer_candidate(run, candidate);
    if (okay)
        free(candidate);
    else
        discard_candidate(candidate);
    return okay;
}
