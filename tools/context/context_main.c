/** @file context_main.c @brief Native local codebase and attributed context CLI. */
#include "context_repository.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum context_command {
    CONTEXT_INGEST = 0,
    CONTEXT_INJECT = 1,
    CONTEXT_TRAIN = 2,
    CONTEXT_ASK = 3,
    CONTEXT_INSPECT = 4
} context_command;

enum context_option {
    OPTION_ROOT = 1U,
    OPTION_OUT = 2U,
    OPTION_CONTEXT = 4U,
    OPTION_STEPS = 8U,
    OPTION_SEED = 16U,
    OPTION_INPUT = 32U,
    OPTION_QUERY = 64U,
    OPTION_LIMIT = 128U,
    OPTION_KIND = 256U,
    OPTION_GROUPS = 512U
};

typedef struct context_options {
    context_command command;
    const char *root;
    const char *out;
    const char *context;
    const char *input;
    const char *query;
    uint64_t steps;
    uint64_t seed;
    size_t limit;
    cgai_life_context_kind kind;
    unsigned int seen;
    uint32_t group_count;
} context_options;

static void usage(FILE *file) {
    fputs("usage: cgai_context ingest --root PATH --out CHECKPOINT [--input CHECKPOINT] "
          "[--context FILE] "
          "[--steps N] [--seed N] [--groups 2..8]\n"
          "       cgai_context inject --input CHECKPOINT --context FILE --out CHECKPOINT "
          "[--steps N] [--kind llm|activity]\n"
          "       cgai_context train --input CHECKPOINT --out CHECKPOINT [--steps N]\n"
          "       cgai_context ask --input CHECKPOINT --query TEXT [--limit 1..16]\n"
          "       cgai_context inspect --input CHECKPOINT\n",
          file);
}

static int parse_unsigned(const char *text, uint64_t limit, uint64_t *result) {
    char *end = NULL;
    unsigned long long number;
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    number = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || number > limit)
        return 0;
    *result = (uint64_t)number;
    return 1;
}

static unsigned int path_option(context_options *options, const char *name, const char *value) {
    static const char *const names[] = {"--root", "--out", "--context", "--input", "--query"};
    const char **destinations[] = {&options->root, &options->out, &options->context,
                                   &options->input, &options->query};
    static const unsigned int flags[] = {OPTION_ROOT, OPTION_OUT, OPTION_CONTEXT, OPTION_INPUT,
                                         OPTION_QUERY};
    for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(name, names[i]) == 0 && value[0] != '\0') {
            *destinations[i] = value;
            return flags[i];
        }
    return 0U;
}

static unsigned int group_option(context_options *options, const char *name, const char *value) {
    uint64_t number;
    if (strcmp(name, "--groups") != 0 || !parse_unsigned(value, CGAI_LIFE_GROUPS, &number) ||
        number < CGAI_LIFE_MIN_GROUPS)
        return 0U;
    options->group_count = (uint32_t)number;
    return OPTION_GROUPS;
}

static unsigned int numeric_option(context_options *options, const char *name, const char *value) {
    uint64_t number;
    if (strcmp(name, "--steps") == 0 && parse_unsigned(value, 100000U, &number)) {
        options->steps = number;
        return OPTION_STEPS;
    }
    if (strcmp(name, "--seed") == 0 && parse_unsigned(value, UINT32_MAX, &number)) {
        options->seed = number;
        return OPTION_SEED;
    }
    if (strcmp(name, "--limit") == 0 && parse_unsigned(value, 16U, &number) && number != 0U) {
        options->limit = (size_t)number;
        return OPTION_LIMIT;
    }
    return group_option(options, name, value);
}

static unsigned int parse_option(context_options *options, const char *name, const char *value) {
    unsigned int flag = path_option(options, name, value);
    if (flag == 0U)
        flag = numeric_option(options, name, value);
    if (flag == 0U && strcmp(name, "--kind") == 0 &&
        (strcmp(value, "llm") == 0 || strcmp(value, "activity") == 0)) {
        options->kind =
            strcmp(value, "llm") == 0 ? CGAI_LIFE_CONTEXT_LLM : CGAI_LIFE_CONTEXT_ACTIVITY;
        flag = OPTION_KIND;
    }
    return flag;
}

static int parse_command(context_options *options, const char *text) {
    static const char *const commands[] = {"ingest", "inject", "train", "ask", "inspect"};
    for (size_t i = 0U; i < sizeof(commands) / sizeof(commands[0]); ++i)
        if (strcmp(text, commands[i]) == 0) {
            options->command = (context_command)i;
            return 1;
        }
    return 0;
}

static int valid_options(const context_options *options) {
    static const unsigned int allowed[] = {
        OPTION_ROOT | OPTION_OUT | OPTION_CONTEXT | OPTION_STEPS | OPTION_SEED | OPTION_GROUPS |
            OPTION_INPUT,
        OPTION_INPUT | OPTION_CONTEXT | OPTION_OUT | OPTION_STEPS | OPTION_KIND,
        OPTION_INPUT | OPTION_OUT | OPTION_STEPS, OPTION_INPUT | OPTION_QUERY | OPTION_LIMIT,
        OPTION_INPUT};
    static const unsigned int required[] = {
        OPTION_ROOT | OPTION_OUT, OPTION_INPUT | OPTION_CONTEXT | OPTION_OUT,
        OPTION_INPUT | OPTION_OUT, OPTION_INPUT | OPTION_QUERY, OPTION_INPUT};
    const size_t command = (size_t)options->command;
    if (options->command == CONTEXT_INGEST && options->input != NULL &&
        (options->seen & (OPTION_SEED | OPTION_GROUPS)) != 0U)
        return 0;
    return (options->seen & ~allowed[command]) == 0U &&
           (options->seen & required[command]) == required[command];
}

static int parse_arguments(context_options *options, int argc, char **argv) {
    if (argc < 2 || !parse_command(options, argv[1]))
        return 0;
    for (int i = 2; i < argc; i += 2) {
        unsigned int flag;
        if (i + 1 >= argc)
            return 0;
        flag = parse_option(options, argv[i], argv[i + 1]);
        if (flag == 0U || (options->seen & flag) != 0U)
            return 0;
        options->seen |= flag;
    }
    return valid_options(options);
}

static int operation(cgai_life_status status, const char *name) {
    if (status == CGAI_LIFE_OK)
        return 1;
    (void)fprintf(stderr, "%s failed (native status %d)\n", name, (int)status);
    return 0;
}

static void print_preparation(const char *name, const context_repository_report *report) {
    (void)printf("%s files=%" PRIu32 " chunks=%" PRIu32 " skipped=%" PRIu32 " bytes=%" PRIu64
                 " source-hash=%016" PRIx64 "\n",
                 name, report->files, report->records, report->skipped, report->bytes,
                 report->source_hash);
}

static int admit_batch(cgai_life_context *owner, context_repository_batch *batch,
                       const context_repository_report *report, const char *name, int prepared) {
    int okay;
    if (!prepared) {
        (void)fprintf(stderr, "%s failed: %s\n", name, report->error);
        context_repository_destroy(batch);
        return 0;
    }
    okay = operation(context_repository_apply(batch, owner), "context admission");
    context_repository_destroy(batch);
    if (okay)
        print_preparation(name, report);
    return okay;
}

static int ingest_repository(cgai_life_context *owner, const char *root) {
    context_repository_batch *batch = NULL;
    context_repository_report report;
    const int prepared = context_repository_scan(root, &batch, &report);
    return admit_batch(owner, batch, &report, "repository", prepared);
}

static int inject_context(cgai_life_context *owner, const context_options *options) {
    context_repository_batch *batch = NULL;
    context_repository_report report;
    const int prepared =
        context_repository_read_context(options->context, options->kind, &batch, &report);
    return admit_batch(owner, batch, &report, "attributed-context", prepared);
}

static int prepare_owner(cgai_life_context *owner, const context_options *options) {
    if (options->input != NULL &&
        !operation(cgai_life_context_load(owner, options->input), "checkpoint load"))
        return 0;
    if (options->root != NULL && !ingest_repository(owner, options->root))
        return 0;
    return options->context == NULL || inject_context(owner, options);
}

static int train_owner(cgai_life_context *owner, uint64_t steps) {
    while (steps != 0U) {
        const uint32_t quantum = steps > CGAI_LIFE_MAX_STEP_GENERATIONS
                                     ? CGAI_LIFE_MAX_STEP_GENERATIONS
                                     : (uint32_t)steps;
        if (!operation(cgai_life_context_train_step(owner, quantum, NULL), "Life context training"))
            return 0;
        steps -= quantum;
    }
    return 1;
}

static int inspect_choices(const cgai_life_context *owner) {
    cgai_life_context_choice_stats choices;
    if (!operation(cgai_life_context_choice_get_stats(owner, &choices), "code-choice inspection"))
        return 0;
    (void)printf("choice-version=%" PRIu64 " decisions=%" PRIu64 " observations=%" PRIu64
                 " deferred=%" PRIu64 " pending=%" PRIu32 "\n",
                 choices.version, choices.decisions, choices.observations, choices.deferred,
                 choices.pending);
    for (size_t i = 0U; i < choices.group_count; ++i)
        (void)printf("choice-group=%zu steps=%" PRIu64 " hash=%016" PRIx64 "\n", i,
                     choices.group_steps[i], choices.group_hashes[i]);
    return 1;
}

static int inspect_owner(const cgai_life_context *owner) {
    cgai_life_context_stats stats;
    if (!operation(cgai_life_context_get_stats(owner, &stats), "context inspection"))
        return 0;
    (void)printf("generation=%" PRIu32 " records=%" PRIu32 " fitted=%" PRIu32 " deferred=%" PRIu32
                 " contacts=%" PRIu64 " domain-updates=%" PRIu64 " source-hash=%016" PRIx64
                 " state-hash=%016" PRIx64 "\n",
                 stats.generation, stats.records, stats.trained_records, stats.deferred_records,
                 stats.contact_events, stats.domain_updates, stats.source_hash,
                 cgai_life_context_hash(owner));
    (void)printf("groups=%" PRIu32 " capacity=%u\n", stats.group_count, CGAI_LIFE_GROUPS);
    for (size_t i = 0U; i < stats.group_count; ++i)
        (void)printf("group=%zu updates=%" PRIu64 " hash=%016" PRIx64 "\n", i,
                     stats.group_updates[i], stats.group_hashes[i]);
    return inspect_choices(owner);
}

static void print_result(const cgai_life_context_result *result) {
    static const char *const labels[] = {"source excerpt", "LLM proposal (attributed context)",
                                         "activity record (attributed context)"};
    const size_t length = strlen(result->text);
    (void)printf("[%s] %s:%" PRIu32 "-%" PRIu32 " record=%" PRIu32 " learned-mask=%" PRIu32
                 " score=%.9g content-hash=%016" PRIx64 "\n",
                 labels[(size_t)result->kind], result->source, result->first_line,
                 result->last_line, result->record_id, result->learned_mask, result->score,
                 result->content_hash);
    (void)fputs(result->text, stdout);
    if (length == 0U || result->text[length - 1U] != '\n')
        (void)putchar('\n');
}

static int ask_owner(const cgai_life_context *owner, const context_options *options) {
    cgai_life_context_result results[16];
    size_t count = 0U;
    if (!operation(cgai_life_context_query(owner, options->query, results, options->limit, &count),
                   "frozen context query"))
        return 0;
    (void)printf("matches=%zu; returned text is stored evidence or attributed context\n", count);
    for (size_t i = 0U; i < count; ++i)
        print_result(&results[i]);
    return 1;
}

static int execute(cgai_life_context *owner, const context_options *options) {
    if (!prepare_owner(owner, options))
        return 0;
    if (options->command == CONTEXT_ASK)
        return ask_owner(owner, options);
    if (options->command == CONTEXT_INSPECT)
        return inspect_owner(owner);
    return train_owner(owner, options->steps) &&
           operation(cgai_life_context_save(owner, options->out), "checkpoint publication") &&
           inspect_owner(owner);
}

int main(int argc, char **argv) {
    context_options options = {CONTEXT_INGEST,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               64U,
                               42U,
                               4U,
                               CGAI_LIFE_CONTEXT_LLM,
                               0U,
                               CGAI_LIFE_DEFAULT_GROUPS};
    cgai_life_config config = cgai_life_config_default();
    cgai_life_context *owner = NULL;
    int okay;
    if (!parse_arguments(&options, argc, argv)) {
        usage(stderr);
        return 2;
    }
    config.seed = options.seed;
    config.group_count = options.group_count;
    config.scenario = 3U;
    config.training_epochs = 1U;
    config.mode = CGAI_LIFE_LEARNED;
    config.enable_merges = 0U;
    if (!operation(cgai_life_context_create(&config, &owner), "context creation"))
        return 1;
    okay = execute(owner, &options);
    cgai_life_context_destroy(owner);
    return okay ? 0 : 1;
}
