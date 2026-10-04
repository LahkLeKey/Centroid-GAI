/** @file life_main.c @brief Offline Centroid Life run and exact-continuation CLI. */
#include "life_io.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct life_options {
    life_run_config config;
    const char *prefix;
    const char *input;
    uint64_t steps;
    unsigned int seen;
    int continuation;
    int evaluation;
    int inspection;
    int start;
} life_options;

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

static void usage(FILE *file) {
    fputs("usage: cgai_life run [--mode conway|teacher|learned] "
          "[--scenario gliders|block|blinkers|crowd] [--seed N] [--steps N] "
          "[--epochs N] [--groups 2..8] [--no-merge] [--out PREFIX]\n"
          "       cgai_life resume|replay|evaluate SNAPSHOT [--steps N] [--out PREFIX]\n"
          "       cgai_life inspect SNAPSHOT\n",
          file);
}

static int parse_choice(const char *text, const char *const *choices, uint32_t count,
                        uint32_t *result) {
    for (uint32_t i = 0U; i < count; ++i)
        if (strcmp(text, choices[i]) == 0) {
            *result = i;
            return 1;
        }
    return 0;
}

static unsigned int group_option(life_options *options, const char *name, const char *value) {
    uint64_t parsed = 0U;
    if (strcmp(name, "--groups") != 0 || !parse_unsigned(value, LIFE_MODULES, &parsed) ||
        parsed < LIFE_MIN_MODULES)
        return 0U;
    options->config.group_count = (uint32_t)parsed;
    return 256U;
}

static unsigned int numeric_option(life_options *options, const char *name, const char *value) {
    uint64_t parsed = 0U;
    if (strcmp(name, "--seed") == 0) {
        if (!parse_unsigned(value, UINT32_MAX, &parsed))
            return 0U;
        options->config.seed = parsed;
        return 16U;
    }
    if (strcmp(name, "--epochs") == 0) {
        if (!parse_unsigned(value, 64U, &parsed) || parsed == 0U)
            return 0U;
        options->config.training_epochs = (uint32_t)parsed;
        return 32U;
    }
    return group_option(options, name, value);
}

static unsigned int named_option(life_options *options, const char *name, const char *value) {
    static const char *const scenarios[] = {"gliders", "block", "blinkers", "crowd"};
    static const char *const modes[] = {"conway", "teacher", "learned"};
    uint32_t parsed = 0U;
    if (strcmp(name, "--mode") == 0 && parse_choice(value, modes, 3U, &parsed)) {
        options->config.mode = (life_mode)parsed;
        return 64U;
    }
    if (strcmp(name, "--scenario") == 0 && parse_choice(value, scenarios, 4U, &parsed)) {
        options->config.scenario = parsed;
        return 128U;
    }
    return 0U;
}

static unsigned int common_option(life_options *options, const char *name, const char *value) {
    uint64_t parsed = 0U;
    if (strcmp(name, "--steps") == 0 && parse_unsigned(value, 100000U, &parsed)) {
        options->steps = parsed;
        return 2U;
    }
    if (strcmp(name, "--out") == 0 && value[0] != '\0') {
        options->prefix = value;
        return 4U;
    }
    return 0U;
}

static unsigned int valued_option(life_options *options, const char *name, const char *value) {
    unsigned int flag = common_option(options, name, value);
    if (flag != 0U || options->continuation)
        return flag;
    flag = numeric_option(options, name, value);
    return flag != 0U ? flag : named_option(options, name, value);
}

static int parse_command(life_options *options, int argc, char **argv) {
    if (argc < 2)
        return 0;
    if (strcmp(argv[1], "resume") == 0 || strcmp(argv[1], "replay") == 0 ||
        strcmp(argv[1], "evaluate") == 0 || strcmp(argv[1], "inspect") == 0) {
        if (argc < 3)
            return 0;
        options->continuation = 1;
        options->evaluation = strcmp(argv[1], "evaluate") == 0;
        options->inspection = strcmp(argv[1], "inspect") == 0;
        options->input = argv[2];
        options->start = 3;
        return 1;
    }
    return strcmp(argv[1], "run") == 0;
}

static unsigned int parse_option(life_options *options, int argc, char **argv, int *index) {
    if (strcmp(argv[*index], "--no-merge") == 0 && !options->continuation) {
        options->config.enable_merges = 0;
        return 1U;
    }
    const char *name = argv[*index];
    ++*index;
    if (*index >= argc)
        return 0U;
    return valued_option(options, name, argv[*index]);
}

static int parse_arguments(life_options *options, int argc, char **argv) {
    if (!parse_command(options, argc, argv))
        return 0;
    if (options->inspection)
        return argc == 3;
    for (int i = options->start; i < argc; ++i) {
        const unsigned int flag = parse_option(options, argc, argv, &i);
        if (flag == 0U || (options->seen & flag) != 0U)
            return 0;
        options->seen |= flag;
    }
    return 1;
}

static int save_snapshot(const char *prefix, const life_run *run) {
    const size_t length = strlen(prefix);
    if (length > SIZE_MAX - sizeof(".snapshot"))
        return 0;
    char *snapshot = malloc(length + sizeof(".snapshot"));
    if (snapshot == NULL)
        return 0;
    memcpy(snapshot, prefix, length);
    memcpy(snapshot + length, ".snapshot", sizeof(".snapshot"));
    const int okay = life_snapshot_save(snapshot, run);
    free(snapshot);
    return okay;
}

static int run_generations(const life_options *options, life_run *run) {
    life_trace *trace = life_trace_open(options->prefix, run);
    if (trace == NULL) {
        fputs("could not open native trace files\n", stderr);
        return 0;
    }
    int okay = life_trace_append(trace, run);
    for (uint64_t i = 0U; i < options->steps && okay; ++i) {
        okay = options->evaluation ? life_run_evaluate_tick(run) : life_run_tick(run);
        if (okay)
            okay = life_trace_append(trace, run);
    }
    if (!life_trace_close(trace))
        okay = 0;
    return okay && save_snapshot(options->prefix, run);
}

static void report_run(const life_run *run, const char *prefix) {
    printf("generation=%" PRIu32 " population=%" PRIu32 " collisions=%" PRIu32
           " training_updates=%" PRIu64 " merges=%" PRIu64 " hash=%016" PRIx64
           "\ntrace=%s.trace\nmetrics=%s.tsv\nsnapshot=%s.snapshot\n",
           run->world.tick, life_population(&run->world, life_world_group_mask(&run->world)),
           run->world.stats.collisions, run->stats.training_updates, run->stats.accepted_merges,
           life_run_hash(run), prefix, prefix, prefix);
}

static int generation_budget_valid(const life_options *options, const life_run *run) {
    return options->steps <= (uint64_t)UINT32_MAX - run->world.tick &&
           (options->evaluation || options->steps == 0U ||
            (uint64_t)run->world.tick + options->steps <= (uint64_t)UINT32_MAX - 8U);
}

static int execute_ready(const life_options *options, life_run *run) {
    if (options->inspection)
        return life_inspect(stdout, run) ? 0 : 1;
    if (!generation_budget_valid(options, run)) {
        fputs("generation counter or teaching lookahead would overflow\n", stderr);
        return 2;
    }
    const int okay = run_generations(options, run);
    if (okay)
        report_run(run, options->prefix);
    else
        fputs("Life execution or artifact write failed\n", stderr);
    return okay ? 0 : 1;
}

static int execute(const life_options *options) {
    life_run run;
    memset(&run, 0, sizeof(run));
    if (!(options->continuation ? life_snapshot_load(options->input, &run)
                                : life_run_init(&run, &options->config))) {
        fputs("could not initialize Life run or load a valid snapshot bundle\n", stderr);
        return 1;
    }
    const int status = execute_ready(options, &run);
    life_run_destroy(&run);
    return status;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    life_options options = {{42U, 0U, 8U, LIFE_MODE_LEARNED, 1, LIFE_DEFAULT_MODULES},
                            "centroid-life",
                            NULL,
                            64U,
                            0U,
                            0,
                            0,
                            0,
                            2};
    if (!parse_arguments(&options, argc, argv)) {
        usage(stderr);
        return 2;
    }
    return execute(&options);
}
