/** @file evolve_main.c @brief Contact-owned source choices learned from native training results. */
#include "build_config.h"
#include "context_repository.h"
#include "evolve_run.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *file) {
    fputs("usage: cgai_life_evolve --out NEW_DIRECTORY [--candidates 1..8] "
          "[--seed 0..4294967295] [--steps 1..256] [--context LLM_FILE] [--memory CHECKPOINT]\n"
          "                       [--pause-after 0..256]\n"
          "       cgai_life_evolve --resume BUNDLE_DIRECTORY --out NEW_DIRECTORY "
          "[--pause-after 0..256]\n"
          "       output parent must exist; candidates are built, tested, checked and scored\n"
          "       outputs inside the source repository must use its excluded build directory\n"
          "       resume restores total budgets and completed generations without warmup\n"
          "       TRAIN results teach choices; dev/confirm remain separate admission gates\n",
          file);
}

static int number(const char *text, uint32_t limit, uint32_t *output) {
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0;
    char *end = NULL;
    errno = 0;
    const unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value > limit)
        return 0;
    *output = (uint32_t)value;
    return 1;
}

static unsigned int path_option(evolve_options *options, const char *name, const char *value) {
    static const char *const names[] = {"--out", "--context", "--memory", "--resume"};
    const char **outputs[] = {&options->output, &options->context, &options->memory,
                              &options->resume};
    static const unsigned int flags[] = {1U, 16U, 32U, 64U};
    if (value[0] == '\0' || strpbrk(value, "\t\r\n") != NULL)
        return 0U;
    for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(name, names[i]) == 0) {
            *outputs[i] = value;
            return flags[i];
        }
    return 0U;
}

static unsigned int option(evolve_options *options, const char *name, const char *value) {
    const unsigned int path = path_option(options, name, value);
    if (path != 0U)
        return path;
    if (strcmp(name, "--candidates") == 0)
        return number(value, 8U, &options->candidates) && options->candidates != 0U ? 2U : 0U;
    if (strcmp(name, "--seed") == 0)
        return number(value, UINT32_MAX, &options->seed) ? 4U : 0U;
    if (strcmp(name, "--steps") == 0)
        return number(value, 256U, &options->generations) && options->generations != 0U ? 8U : 0U;
    if (strcmp(name, "--pause-after") == 0)
        return number(value, 256U, &options->pause_after) ? 128U : 0U;
    return 0U;
}

static int parse(int argc, char **argv, evolve_options *output) {
    evolve_options options = {
        .candidates = 2U, .seed = 42U, .generations = 64U, .pause_after = UINT32_MAX};
    unsigned int seen = 0U;
    if (argc < 3 || argc > 17 || argc % 2 == 0)
        return 0;
    for (int i = 1; i < argc; i += 2) {
        const unsigned int flag = option(&options, argv[i], argv[i + 1]);
        if (flag == 0U || (seen & flag) != 0U)
            return 0;
        seen |= flag;
    }
    if (options.output == NULL || (options.resume != NULL && (seen & 62U) != 0U))
        return 0;
    *output = options;
    return 1;
}

static int bind_external_input(FILE *file, const char *path) {
    char absolute[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    return path == NULL ||
           (evolve_absolute_path(path, absolute, sizeof(absolute)) &&
            strpbrk(absolute, "\t\r\n") == NULL && fprintf(file, "%s\n", absolute) >= 0);
}

static int external_manifest(evolve_run *run) {
    if (run->options.memory == NULL && run->options.context == NULL)
        return 1;
    if (!evolve_path_join(run->external_manifest, sizeof(run->external_manifest), run->output,
                          "external-inputs.txt"))
        return 0;
    FILE *file = fopen(run->external_manifest, "wbx");
    if (file == NULL)
        return 0;
    int okay = bind_external_input(file, run->options.memory) &&
               bind_external_input(file, run->options.context) && fflush(file) == 0;
    if (fclose(file) != 0)
        okay = 0;
    return okay && evolve_manifest_hash(run->external_manifest, &run->external_checksum);
}

static int output_parent(const char *path, char *parent, const char **leaf) {
    const char *separator = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    const size_t length = separator == NULL ? 1U : (size_t)(separator - path) + 1U;
    if (length > EVOLVE_PROCESS_MAX_PATH_BYTES)
        return 0;
    memcpy(parent, separator == NULL ? "." : path, length);
    parent[length] = '\0';
    *leaf = separator == NULL ? path : separator + 1;
    return (*leaf)[0] != '\0' && strcmp(*leaf, ".") != 0 && strcmp(*leaf, "..") != 0;
}

static int prepare_output(evolve_run *run) {
    char parent[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    char absolute[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    const char *leaf = NULL;
    if (!output_parent(run->options.output, parent, &leaf) ||
        !evolve_absolute_path(parent, absolute, sizeof(absolute)) ||
        !evolve_path_join(run->output, sizeof(run->output), absolute, leaf))
        return 0;
    if (!evolve_output_allowed(run->output)) {
        fputs("Evolution output must be outside the source repository or inside its build "
              "directory; generated candidates cannot enter source memory.\n",
              stderr);
        return 0;
    }
    return evolve_create_directory(run->output);
}

static int prepare(evolve_run *run) {
    char source[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    if (strpbrk(EVOLVE_SOURCE_DIRECTORY, "\t\r\n") != NULL ||
        !evolve_path_join(source, sizeof(source), EVOLVE_SOURCE_DIRECTORY,
                          "src/gameplay/gameplay_model.c") ||
        !evolve_manifest_hash(EVOLVE_INPUT_MANIFEST, &run->inputs_checksum) ||
        !evolve_read_source(source, &run->parent, &run->parent_storage) ||
        !evolve_inputs_unchanged(run))
        return 0;
    run->initial_parent_checksum = run->parent.checksum;
    run->build_recipe_checksum = evolve_checkpoint_build_recipe();
    return prepare_output(run) && external_manifest(run);
}

static int apply_context(evolve_run *run, context_repository_batch *batch,
                         const context_repository_report *report, const char *name, int prepared) {
    const int okay = prepared && context_repository_apply(batch, run->memory) == CGAI_LIFE_OK;
    context_repository_destroy(batch);
    if (!okay)
        fprintf(stderr, "%s admission failed: %s\n", name, report->error);
    else
        printf("%s files=%u chunks=%u bytes=%" PRIu64 " hash=%" PRIu64 "\n", name, report->files,
               report->records, report->bytes, report->source_hash);
    return okay;
}

static int source_context(evolve_run *run) {
    context_repository_batch *batch = NULL;
    context_repository_report report;
    const int prepared = context_repository_scan(EVOLVE_SOURCE_DIRECTORY, &batch, &report);
    if (!apply_context(run, batch, &report, "working-source", prepared))
        return 0;
    run->source_context_checksum = report.source_hash;
    return 1;
}

static int llm_context(evolve_run *run) {
    context_repository_batch *batch = NULL;
    context_repository_report report;
    if (run->options.context == NULL)
        return 1;
    const int prepared = context_repository_read_context(run->options.context,
                                                         CGAI_LIFE_CONTEXT_LLM, &batch, &report);
    if (!apply_context(run, batch, &report, "attributed-llm", prepared))
        return 0;
    run->llm_context_checksum = report.source_hash;
    return 1;
}

static int memory_create(evolve_run *run) {
    cgai_life_config config = cgai_life_config_default();
    config.seed = run->options.seed;
    config.training_epochs = 8U;
    config.enable_merges = 0U;
    if (cgai_life_context_create(&config, &run->memory) != CGAI_LIFE_OK ||
        (run->options.memory != NULL &&
         cgai_life_context_load(run->memory, run->options.memory) != CGAI_LIFE_OK))
        return 0;
    cgai_life_context_choice_stats stats;
    if (cgai_life_context_choice_get_stats(run->memory, &stats) != CGAI_LIFE_OK ||
        stats.pending != 0U)
        return 0;
    run->initial_memory_checksum = cgai_life_context_hash(run->memory);
    return source_context(run) && llm_context(run) && evolve_inputs_unchanged(run);
}

static int prepare_memory(evolve_run *run) {
    if (!memory_create(run) ||
        cgai_life_context_train_step(run->memory, EVOLVE_CONTEXT_PREPARATION_GENERATIONS, NULL) !=
            CGAI_LIFE_OK ||
        !evolve_inputs_unchanged(run))
        return 0;
    run->prepared_memory_checksum = cgai_life_context_hash(run->memory);
    cgai_life_context_stats context;
    if (cgai_life_context_get_stats(run->memory, &context) != CGAI_LIFE_OK ||
        cgai_life_context_choice_get_stats(run->memory, &run->initial_choices) != CGAI_LIFE_OK)
        return 0;
    run->prepared_generation = context.generation;
    return evolve_report_start(run);
}

static int baseline(evolve_run *run) {
    evolve_work work = {0};
    if (!evolve_work_create(run, "baseline", run->parent.bytes, run->parent.size, &work))
        return 0;
    const evolve_evaluation_status status =
        evolve_evaluate(run, &work, &run->training, &run->development, &run->confirmation);
    if (status != EVOLVE_EVALUATION_OK) {
        fprintf(stderr, "Baseline %s; see %s\n", evolve_evaluation_name(status), work.directory);
        return 0;
    }
    run->baseline_training = run->training;
    run->baseline_development = run->development;
    run->baseline_confirmation = run->confirmation;
    return evolve_report_baseline(run);
}

static int adopt(evolve_run *run, evolve_trial *trial) {
    evolve_mutation_source source;
    if (evolve_mutation_validate(trial->candidate.bytes, trial->candidate.size, &source) !=
        EVOLVE_MUTATION_OK)
        return 0;
    free(run->parent_storage);
    run->parent_storage = trial->candidate.bytes;
    trial->candidate.bytes = NULL;
    run->parent = source;
    run->training = trial->training;
    run->development = trial->development;
    run->confirmation = trial->confirmation;
    ++run->accepted;
    return 1;
}

static const char *admission(const evolve_run *run, evolve_evaluation_status status,
                             const evolve_trial *trial) {
    if (status != EVOLVE_EVALUATION_OK)
        return evolve_evaluation_name(status);
    if (!life_fitness_admits(&run->development, &trial->development))
        return "development_rejected";
    if (!life_fitness_admits(&run->confirmation, &trial->confirmation))
        return "confirmation_rejected";
    return "accepted";
}

static int input_prefix(const evolve_run *run, evolve_trial *trial) {
    const evolve_mutation_profile *profile = &run->parent.profile;
    static const char *const category[3] = {"low", "base", "high"};
    const int written =
        snprintf(trial->input, sizeof(trial->input),
                 "native C gameplay_model.c initialization candidate\nparent_checksum=%" PRIu64
                 "\nembeddings=%s encoder=%s outer=%s inner=%s\n"
                 "embeddings_%s encoder_%s outer_%s inner_%s\n",
                 run->parent.checksum, evolve_mutation_literal(0U, profile->choices[0]),
                 evolve_mutation_literal(1U, profile->choices[1]),
                 evolve_mutation_literal(2U, profile->choices[2]),
                 evolve_mutation_literal(3U, profile->choices[3]), category[profile->choices[0]],
                 category[profile->choices[1]], category[profile->choices[2]],
                 category[profile->choices[3]]);
    return written > 0 && (size_t)written < sizeof(trial->input);
}

static int append_excerpt(evolve_trial *trial, const cgai_life_context_result *result) {
    const size_t used = strlen(trial->input);
    const int written = snprintf(trial->input + used, sizeof(trial->input) - used,
                                 "context kind=%u hash=%" PRIu64 " source=%.96s lines=%u..%u\n"
                                 "%.640s\n",
                                 (unsigned int)result->kind, result->content_hash, result->source,
                                 result->first_line, result->last_line, result->text);
    return written >= 0 && (size_t)written < sizeof(trial->input) - used;
}

static size_t preferred_source(const cgai_life_context_result *results, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (strcmp(results[i].source, "src/gameplay/gameplay_model.c") == 0)
            return i;
    return 0U;
}

static int append_kind_excerpt(const evolve_run *run, evolve_trial *trial,
                               cgai_life_context_kind kind) {
    cgai_life_context_result results[16];
    size_t count = 0U;
    const size_t capacity = kind == CGAI_LIFE_CONTEXT_SOURCE ? 16U : 1U;
    if (cgai_life_context_query_kind(run->memory,
                                     "gameplay_model.c random_scalar embeddings encoder centroid "
                                     "initialization",
                                     kind, results, capacity, &count) != CGAI_LIFE_OK)
        return 0;
    const size_t index = kind == CGAI_LIFE_CONTEXT_SOURCE ? preferred_source(results, count) : 0U;
    return count == 0U || append_excerpt(trial, &results[index]);
}

static int decision_input(const evolve_run *run, evolve_trial *trial) {
    return input_prefix(run, trial) && append_kind_excerpt(run, trial, CGAI_LIFE_CONTEXT_SOURCE) &&
           append_kind_excerpt(run, trial, CGAI_LIFE_CONTEXT_LLM);
}

static int issue_choice(evolve_run *run, uint32_t event_index, evolve_trial *trial) {
    if (!decision_input(run, trial) || !evolve_inputs_unchanged(run) ||
        cgai_life_context_choice_get_stats(run->memory, &trial->before) != CGAI_LIFE_OK ||
        cgai_life_context_choice_begin(run->memory, event_index, trial->input, trial->actions,
                                       trial->action_count, trial->fallback,
                                       &trial->choice) != CGAI_LIFE_OK)
        return 0;
    return evolve_mutation_select(&run->parent, trial->event, trial->choice.action,
                                  &trial->candidate) == EVOLVE_MUTATION_OK;
}

static int training_comparable(const evolve_run *run, const evolve_trial *trial) {
    return life_fitness_report_valid(&run->training) &&
           life_fitness_report_valid(&trial->training) &&
           run->training.split == LIFE_FITNESS_TRAIN &&
           trial->training.split == LIFE_FITNESS_TRAIN &&
           run->training.pack_hash == trial->training.pack_hash &&
           run->training.records == trial->training.records;
}

static int training_proof(const evolve_run *run, const evolve_work *work, evolve_trial *trial) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    double utility = run->training.mean_loss - trial->training.mean_loss;
    utility = utility < -1.0 ? -1.0 : (utility > 1.0 ? 1.0 : utility);
    const int written = snprintf(
        trial->feedback_proof, sizeof(trial->feedback_proof),
        "LIFE_SOURCE_TRAINING_FEEDBACK\t1\nsplit\ttrain\n"
        "choice_token\t%" PRIu64 "\ninput_hash\t%" PRIu64
        "\naction\t%u\nparent_source_checksum\t%" PRIu64 "\ncandidate_source_checksum\t%" PRIu64
        "\npack_hash\t%" PRIu64 "\nrecords\t%" PRIu64 "\nparent_loss\t%.17g\ncandidate_loss\t%.17g"
        "\nutility\t%.17g\nEND\n",
        trial->choice.token, trial->choice.input_hash, trial->choice.action,
        trial->candidate.source_checksum, trial->candidate.checksum, trial->training.pack_hash,
        trial->training.records, run->training.mean_loss, trial->training.mean_loss, utility);
    if (written < 0 || (size_t)written >= sizeof(trial->feedback_proof) ||
        !evolve_path_join(path, sizeof(path), work->directory, "training-feedback.tsv") ||
        !evolve_write_exclusive(path, trial->feedback_proof, (size_t)written))
        return 0;
    trial->feedback_proof_bytes = (size_t)written;
    trial->feedback = (cgai_life_context_feedback){
        CGAI_LIFE_CONTEXT_TRAIN, 1U, 0U, evolve_bytes_hash(trial->feedback_proof, (size_t)written),
        utility};
    return 1;
}

static int deferred_proof(const evolve_work *work, evolve_trial *trial,
                          evolve_evaluation_status measured) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    const int written = snprintf(trial->feedback_proof, sizeof(trial->feedback_proof),
                                 "LIFE_SOURCE_DEFERRED_FEEDBACK\t1\nchoice_token\t%" PRIu64
                                 "\ninput_hash\t%" PRIu64 "\naction\t%u\nparent_checksum\t%" PRIu64
                                 "\ncandidate_checksum\t%" PRIu64 "\nstage\t%s"
                                 "\nutility_measured\t0\ndeferred\t1\nEND\n",
                                 trial->choice.token, trial->choice.input_hash,
                                 trial->choice.action, trial->candidate.source_checksum,
                                 trial->candidate.checksum, evolve_evaluation_name(measured));
    if (written <= 0 || (size_t)written >= sizeof(trial->feedback_proof) ||
        !evolve_path_join(path, sizeof(path), work->directory, "deferred-feedback.tsv") ||
        !evolve_write_exclusive(path, trial->feedback_proof, (size_t)written))
        return 0;
    trial->feedback_proof_bytes = (size_t)written;
    trial->feedback.evidence_hash = evolve_bytes_hash(trial->feedback_proof, (size_t)written);
    return 1;
}

static int observe_trial(evolve_run *run, const evolve_work *work, evolve_trial *trial,
                         evolve_evaluation_status measured) {
    trial->parent_training_loss = run->training.mean_loss;
    trial->feedback = (cgai_life_context_feedback){CGAI_LIFE_CONTEXT_TRAIN, 1U, 1U, 0U, 0.0};
    if (measured == EVOLVE_EVALUATION_OK &&
        (!training_comparable(run, trial) || !training_proof(run, work, trial)))
        return 0;
    if (measured != EVOLVE_EVALUATION_OK && !deferred_proof(work, trial, measured))
        return 0;
    return cgai_life_context_choice_observe(run->memory, &trial->choice, &trial->feedback) ==
               CGAI_LIFE_OK &&
           cgai_life_context_choice_get_stats(run->memory, &trial->after) == CGAI_LIFE_OK &&
           cgai_life_context_choice_predict(
               run->memory, trial->input, trial->choice.participant_mask, trial->actions,
               trial->action_count, trial->fallback, &trial->prediction_after) == CGAI_LIFE_OK;
}

static int admit_activity(evolve_run *run, const char *source, const char *text,
                          cgai_life_context_split split) {
    cgai_life_context_stats stats;
    if (cgai_life_context_get_stats(run->memory, &stats) != CGAI_LIFE_OK)
        return 0;
    const cgai_life_context_input input = {source, text, 0U,
                                           0U,     0U,   CGAI_LIFE_CONTEXT_ACTIVITY,
                                           split,  1U,   (UINT32_C(1) << stats.group_count) - 1U};
    return cgai_life_context_add(run->memory, &input) == CGAI_LIFE_OK;
}

static int training_activity(evolve_run *run, const evolve_trial *trial) {
    char source[96], text[1024];
    const int named =
        snprintf(source, sizeof(source), "evolution/candidate-%04u/training", run->proposals);
    const int written =
        snprintf(text, sizeof(text),
                 "Native source choice=%" PRIu64 " action=%u parent=%" PRIu64 " candidate=%" PRIu64
                 " training measurement verified=%u deferred=%u proof=%" PRIu64
                 " parent train loss=%.17g candidate train loss=%.17g utility=%.17g. "
                 "Outcome applies only to fixed training collision fixtures.",
                 trial->choice.token, trial->choice.action, trial->candidate.source_checksum,
                 trial->candidate.checksum, trial->feedback.verified, trial->feedback.deferred,
                 trial->feedback.evidence_hash, trial->parent_training_loss,
                 trial->training.mean_loss, trial->feedback.utility);
    return named > 0 && (size_t)named < sizeof(source) && written > 0 &&
           (size_t)written < sizeof(text) &&
           admit_activity(run, source, text, CGAI_LIFE_CONTEXT_TRAIN);
}

static int audit_activity(evolve_run *run, const evolve_trial *trial, const char *status) {
    char source[96], text[1024];
    const int named =
        snprintf(source, sizeof(source), "evolution/candidate-%04u/audit", run->proposals);
    const int written =
        snprintf(text, sizeof(text),
                 "Held-out regression gate choice=%" PRIu64 " status=%s candidate=%" PRIu64
                 " development loss=%.17g confirmation loss=%.17g. "
                 "These results cannot fit lexical memory or teach action choices.",
                 trial->choice.token, status, trial->candidate.checksum,
                 trial->development.mean_loss, trial->confirmation.mean_loss);
    return named > 0 && (size_t)named < sizeof(source) && written > 0 &&
           (size_t)written < sizeof(text) &&
           admit_activity(run, source, text, CGAI_LIFE_CONTEXT_AUDIT);
}

static uint32_t audit_status(evolve_evaluation_status measured, const char *status) {
    if (measured != EVOLVE_EVALUATION_OK)
        return (uint32_t)measured + (uint32_t)EVOLVE_CHECKPOINT_CONFIGURE_REJECTED - 1U;
    if (strcmp(status, "accepted") == 0)
        return EVOLVE_CHECKPOINT_ACCEPTED;
    return strcmp(status, "development_rejected") == 0 ? EVOLVE_CHECKPOINT_DEV_REJECTED
                                                       : EVOLVE_CHECKPOINT_CONFIRM_REJECTED;
}

static int execute_trial(evolve_run *run, evolve_work *work, evolve_trial *trial) {
    const evolve_evaluation_status measured =
        evolve_evaluate(run, work, &trial->training, &trial->development, &trial->confirmation);
    if (measured == EVOLVE_EVALUATION_INPUTS_CHANGED || !evolve_work_unchanged(run, work))
        return 0;
    const char *status = admission(run, measured, trial);
    trial->audit_status = audit_status(measured, status);
    if (!observe_trial(run, work, trial, measured) ||
        !evolve_report_candidate(run, work, trial, status) || !training_activity(run, trial) ||
        !audit_activity(run, trial, status) ||
        !evolve_checkpoint_record(run, trial, trial->audit_status == EVOLVE_CHECKPOINT_ACCEPTED))
        return 0;
    printf("candidate=%u action=%u edits=%u status=%s training=%s utility=%.9g\n", run->proposals,
           trial->choice.action, trial->candidate.edit_count, status,
           trial->feedback.deferred != 0U ? "deferred" : "observed", trial->feedback.utility);
    return strcmp(status, "accepted") != 0 || adopt(run, trial);
}

static int evaluate_candidate(evolve_run *run, evolve_trial *trial) {
    char name[64];
    evolve_work work = {0};
    const int written = snprintf(name, sizeof(name), "candidate-%04u", run->proposals);
    if (written < 0 || (size_t)written >= sizeof(name) ||
        !evolve_work_create(run, name, trial->candidate.bytes, trial->candidate.size, &work))
        return 0;
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    return evolve_path_join(path, sizeof(path), work.directory, "choice-input.txt") &&
           evolve_write_exclusive(path, trial->input, strlen(trial->input)) &&
           evolve_report_choice(&work, trial) && execute_trial(run, &work, trial);
}

static int propose(evolve_run *run, const cgai_life_collision_event *event, uint32_t event_index) {
    evolve_trial trial = {0};
    trial.event = event;
    const evolve_mutation_status status = evolve_mutation_alternatives(
        &run->parent, event, trial.actions, &trial.action_count, &trial.fallback);
    if (status == EVOLVE_MUTATION_NO_EDIT)
        return 1;
    if (status != EVOLVE_MUTATION_OK || !issue_choice(run, event_index, &trial))
        return 0;
    ++run->proposals;
    const int okay = evaluate_candidate(run, &trial);
    evolve_mutation_destroy(&trial.candidate);
    return okay;
}

static int generation(evolve_run *run) {
    cgai_life_events events;
    if (!evolve_inputs_unchanged(run) ||
        cgai_life_context_train_step(run->memory, 1U, NULL) != CGAI_LIFE_OK ||
        cgai_life_context_get_events(run->memory, &events) != CGAI_LIFE_OK)
        return 0;
    for (uint32_t i = 0U; i < events.event_count && run->proposals < run->options.candidates; ++i)
        if (!propose(run, &events.events[i], i))
            return 0;
    return 1;
}

static int finish(evolve_run *run) {
    char path[EVOLVE_PROCESS_MAX_PATH_BYTES + 1U];
    if (!evolve_inputs_unchanged(run) ||
        cgai_life_context_train_step(run->memory, EVOLVE_CONTEXT_ACTIVITY_GENERATIONS, NULL) !=
            CGAI_LIFE_OK ||
        !evolve_inputs_unchanged(run) ||
        !evolve_path_join(path, sizeof(path), run->output, "best.c") ||
        !evolve_write_exclusive(path, run->parent.bytes, run->parent.size))
        return 0;
    if (!evolve_path_join(path, sizeof(path), run->output, "memory.context") ||
        cgai_life_context_save(run->memory, path) != CGAI_LIFE_OK)
        return 0;
    run->finalized = 1U;
    return evolve_checkpoint_publish(run, EVOLVE_SOURCE_DIRECTORY, EVOLVE_INPUT_MANIFEST) &&
           evolve_report_finish(run, run->completed_generations);
}

static int prepare_resume(evolve_run *run) {
    if (!evolve_checkpoint_restore(run->options.resume, EVOLVE_SOURCE_DIRECTORY,
                                   EVOLVE_INPUT_MANIFEST, run))
        return 0;
    if (run->finalized != 0U) {
        fputs("Completed source-search bundles cannot resume final activity fitting.\n", stderr);
        return 0;
    }
    return evolve_inputs_unchanged(run) && prepare_output(run) && evolve_report_start(run) &&
           evolve_report_baseline(run);
}

static int search_available(const evolve_run *run) {
    return run->completed_generations < run->options.generations &&
           run->proposals < run->options.candidates;
}

static int search(evolve_run *run) {
    while (search_available(run) && run->invocation_generations < run->options.pause_after) {
        if (!generation(run))
            return 0;
        ++run->completed_generations;
        ++run->invocation_generations;
        if (!evolve_checkpoint_publish(run, EVOLVE_SOURCE_DIRECTORY, EVOLVE_INPUT_MANIFEST))
            return 0;
    }
    return 1;
}

static int pause_run(evolve_run *run) {
    run->paused = 1U;
    return evolve_report_finish(run, run->completed_generations);
}

static int execute(evolve_run *run) {
    if (run->options.resume != NULL) {
        if (!prepare_resume(run))
            return 0;
    } else if (!prepare(run) || !prepare_memory(run) || !baseline(run) ||
               !evolve_checkpoint_publish(run, EVOLVE_SOURCE_DIRECTORY, EVOLVE_INPUT_MANIFEST))
        return 0;
    if (!search(run) || !(search_available(run) ? pause_run(run) : finish(run)))
        return 0;
    printf("proposals=%u accepted=%u search-generations=%u invocation-generations=%u "
           "status=%s report=%s/run.tsv\n",
           run->proposals, run->accepted, run->completed_generations, run->invocation_generations,
           run->paused != 0U ? "paused" : "complete", run->output);
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }
    evolve_run run = {0};
    if (!parse(argc, argv, &run.options)) {
        usage(stderr);
        return 2;
    }
    int okay = execute(&run);
    if (run.report != NULL && fclose(run.report) != 0)
        okay = 0;
    cgai_life_context_destroy(run.memory);
    free(run.parent_storage);
    if (!okay)
        fputs("Source evolution failed; review preserved artifacts and stage logs.\n", stderr);
    return okay ? 0 : 1;
}
