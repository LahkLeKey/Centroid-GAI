/** @file life_npc_main.c @brief Local own-history NPC training and frozen evaluation. */
#include "centroid_life_npc.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct npc_options {
    const char *command, *path;
    uint32_t steps, groups, families, variants;
    unsigned int seen;
    cgai_life_context_split split;
} npc_options;

static int number(const char *text, uint32_t limit, uint32_t *value) {
    char *end = NULL;
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > limit)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static unsigned int training_option(npc_options *options, const char *name, const char *value) {
    uint32_t parsed;
    if (strcmp(name, "--steps") == 0 && number(value, 100000U, &parsed)) {
        options->steps = parsed;
        return 1U;
    }
    if (strcmp(name, "--groups") == 0 && strcmp(options->command, "run") == 0 &&
        number(value, CGAI_LIFE_GROUPS, &parsed) && parsed >= CGAI_LIFE_MIN_GROUPS) {
        options->groups = parsed;
        return 2U;
    }
    return 0U;
}

static unsigned int evaluation_option(npc_options *options, const char *name, const char *value) {
    uint32_t parsed;
    if (strcmp(name, "--families") == 0 && number(value, 24U, &parsed) && parsed != 0U) {
        options->families = parsed;
        return 4U;
    }
    if (strcmp(name, "--variants") == 0 && number(value, 48U, &parsed) && parsed != 0U) {
        options->variants = parsed;
        return 8U;
    }
    if (strcmp(name, "--split") == 0 &&
        (strcmp(value, "train") == 0 || strcmp(value, "dev") == 0)) {
        options->split =
            strcmp(value, "train") == 0 ? CGAI_LIFE_CONTEXT_TRAIN : CGAI_LIFE_CONTEXT_DEVELOPMENT;
        return 16U;
    }
    return 0U;
}

static int option_arguments(npc_options *options, int argc, char **argv, int evaluation) {
    for (int i = 3; i < argc; i += 2) {
        if (i + 1 >= argc)
            return 0;
        const unsigned int flag = evaluation ? evaluation_option(options, argv[i], argv[i + 1])
                                             : training_option(options, argv[i], argv[i + 1]);
        if (flag == 0U || (options->seen & flag) != 0U)
            return 0;
        options->seen |= flag;
    }
    return 1;
}

static int arguments(npc_options *options, int argc, char **argv) {
    if (argc < 3 || argv[2][0] == '\0')
        return 0;
    options->command = argv[1];
    options->path = argv[2];
    if (strcmp(options->command, "inspect") == 0)
        return argc == 3;
    const int evaluation = strcmp(options->command, "evaluate") == 0;
    if (!evaluation && strcmp(options->command, "run") != 0 &&
        strcmp(options->command, "resume") != 0)
        return 0;
    return option_arguments(options, argc, argv, evaluation);
}

static void report_groups(const cgai_life_domain_stats *stats) {
    for (uint32_t i = 0U; i < stats->group_count; ++i)
        printf("uid=%u steps=%" PRIu64 " visits=%" PRIu64 " deferred=%" PRIu64 " hash=%016" PRIx64
               "\n",
               stats->group_uids[i], stats->group_steps[i], stats->group_visits[i],
               stats->group_deferred[i], stats->group_hashes[i]);
}

static int report(const cgai_life_npc *owner) {
    cgai_life_npc_stats stats;
    if (cgai_life_npc_get_stats(owner, &stats) != CGAI_LIFE_OK)
        return 0;
    printf("decisions=%" PRIu64 " episode=%" PRIu64 " family=%u variant=%u tick=%u terminal=%u "
           "successes=%" PRIu64 " deaths=%" PRIu64 " timeouts=%" PRIu64 "\n",
           stats.decisions, stats.episode_id, stats.family, stats.variant, stats.tick,
           stats.terminal, stats.successes, stats.deaths, stats.timeouts);
    printf("generation=%u groups=%u queued=%u observations=%" PRIu64 " updates=%" PRIu64
           " version=%" PRIu64 " recipe=%016" PRIx64 " hash=%016" PRIx64 "\n",
           stats.domain.generation, stats.domain.group_count, stats.queued,
           stats.domain_observations, stats.domain.training_updates, stats.model_version,
           stats.recipe_hash, stats.state_hash);
    printf("host-action=%u outcome=%u parent=%" PRIu64 " context=%016" PRIx64
           " probability=%.17g\n",
           stats.action, stats.outcome, stats.host_parent_version, stats.host_context_hash,
           stats.host_probability);
    report_groups(&stats.domain);
    return 1;
}

static int train(cgai_life_npc *owner, uint32_t steps) {
    while (steps != 0U) {
        const uint32_t quantum = steps > 64U ? 64U : steps;
        if (cgai_life_npc_train_step(owner, quantum, NULL) != CGAI_LIFE_OK)
            return 0;
        steps -= quantum;
    }
    return 1;
}

static int evaluate(const cgai_life_npc *owner, const npc_options *options) {
    cgai_life_npc_evaluation results;
    if (cgai_life_npc_evaluate(owner, options->split, options->families, options->variants,
                               &results) != CGAI_LIFE_OK)
        return 0;
    printf("split=%s episodes=%" PRIu64 " successes=%" PRIu64 " deaths=%" PRIu64
           " timeouts=%" PRIu64 " decisions=%" PRIu64 " illegal-attempts=%" PRIu64
           " executed-illegal=%" PRIu64 " blocked=%" PRIu64 " recovered=%" PRIu64
           " fallbacks=%" PRIu64 " hash=%016" PRIx64 "\n",
           options->split == CGAI_LIFE_CONTEXT_TRAIN ? "train" : "dev", results.episodes,
           results.successes, results.deaths, results.timeouts, results.decisions,
           results.attempted_illegal, results.executed_illegal, results.blocked, results.recovered,
           results.fallbacks, results.source_model_hash);
    for (uint32_t mechanic = 0U; mechanic < 3U; ++mechanic)
        printf("mechanic=%u episodes=%" PRIu64 " successes=%" PRIu64 "\n", mechanic,
               results.mechanic_episodes[mechanic], results.mechanic_successes[mechanic]);
    return 1;
}

static int execute(cgai_life_npc *owner, const npc_options *options) {
    if (strcmp(options->command, "run") != 0 &&
        cgai_life_npc_load(owner, options->path) != CGAI_LIFE_OK)
        return 0;
    if (strcmp(options->command, "inspect") == 0)
        return report(owner);
    if (strcmp(options->command, "evaluate") == 0)
        return evaluate(owner, options);
    return train(owner, options->steps) && report(owner) &&
           cgai_life_npc_save(owner, options->path) == CGAI_LIFE_OK;
}

int main(int argc, char **argv) {
    npc_options options = {NULL, NULL, 64U, 8U, 24U, 48U, 0U, CGAI_LIFE_CONTEXT_DEVELOPMENT};
    if (!arguments(&options, argc, argv)) {
        fputs("usage: cgai_life_npc run CHECKPOINT [--groups 2..8] [--steps N]\n"
              "       cgai_life_npc resume CHECKPOINT [--steps N]\n"
              "       cgai_life_npc inspect CHECKPOINT\n"
              "       cgai_life_npc evaluate CHECKPOINT [--split train|dev] "
              "[--families 1..24] [--variants 1..48]\n",
              stderr);
        return 2;
    }
    cgai_life_config config = cgai_life_config_default();
    config.group_count = options.groups;
    config.enable_merges = 0U;
    cgai_life_npc *owner = NULL;
    const int okay =
        cgai_life_npc_create(&config, &owner) == CGAI_LIFE_OK && execute(owner, &options);
    cgai_life_npc_destroy(owner);
    if (!okay)
        fputs("Native NPC operation failed; any prior checkpoint remains published.\n", stderr);
    return okay ? EXIT_SUCCESS : EXIT_FAILURE;
}
