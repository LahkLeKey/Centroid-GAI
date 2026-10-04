/** @file life_domain_main.c @brief Local native probe training and complete restart. */
#include "centroid_life_domain.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct domain_options {
    const char *command;
    const char *checkpoint;
    uint32_t groups;
    uint32_t steps;
    unsigned int seen;
} domain_options;

static int number(const char *text, uint32_t limit, uint32_t *result) {
    char *end = NULL;
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > limit)
        return 0;
    *result = (uint32_t)parsed;
    return 1;
}

static unsigned int option(domain_options *options, const char *name, const char *value) {
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

static int arguments(domain_options *options, int argc, char **argv) {
    if (argc < 3 || argv[2][0] == '\0' ||
        (strcmp(argv[1], "run") != 0 && strcmp(argv[1], "resume") != 0 &&
         strcmp(argv[1], "inspect") != 0))
        return 0;
    options->command = argv[1];
    options->checkpoint = argv[2];
    if (strcmp(options->command, "inspect") == 0)
        return argc == 3;
    for (int i = 3; i < argc; i += 2) {
        const unsigned int flag = i + 1 < argc ? option(options, argv[i], argv[i + 1]) : 0U;
        if (flag == 0U || (options->seen & flag) != 0U)
            return 0;
        options->seen |= flag;
    }
    return 1;
}

static cgai_life_domain_task task(const cgai_life_domain_stats *stats, uint32_t second) {
    const char *source =
        second == 0U ? "native authored probe x=1 y=0" : "native authored probe x=0 y=2";
    cgai_life_domain_task value = {0};
    value.version = CGAI_LIFE_DOMAIN_VERSION;
    value.family = 41U + second;
    value.split = CGAI_LIFE_CONTEXT_TRAIN;
    value.reviewed = 1U;
    value.source_hash = UINT64_C(14695981039346656037);
    for (size_t i = 0U; source[i] != '\0'; ++i)
        value.source_hash =
            (value.source_hash ^ (unsigned char)source[i]) * UINT64_C(1099511628211);
    value.x = second == 0U ? 1U : 0U;
    value.y = second == 0U ? 0U : 2U;
    value.observed_fields = 3U;
    value.legal_actions = 7U;
    value.eligible_count = stats->group_count;
    memcpy(value.eligible_uids, stats->group_uids, sizeof(value.eligible_uids));
    return value;
}

static int probe_prediction(cgai_life_domain *owner, const char *label) {
    cgai_life_domain_stats stats;
    if (cgai_life_domain_get_stats(owner, &stats) != CGAI_LIFE_OK)
        return 0;
    for (uint32_t i = 0U; i < 2U; ++i) {
        const cgai_life_domain_task input = task(&stats, i);
        cgai_life_domain_prediction prediction;
        if (cgai_life_domain_predict(owner, &input, input.eligible_uids, input.eligible_count,
                                     &prediction) != CGAI_LIFE_OK)
            return 0;
        printf("%s observation=%u x=%u y=%u action=%u probability=%.17g version=%" PRIu64 "\n",
               label, i, input.x, input.y, prediction.action, prediction.probability,
               prediction.model_version);
    }
    return 1;
}

static int enqueue_tasks(cgai_life_domain *owner) {
    cgai_life_domain_stats stats;
    if (cgai_life_domain_get_stats(owner, &stats) != CGAI_LIFE_OK)
        return 0;
    for (uint32_t i = 0U; i < 2U; ++i) {
        const cgai_life_domain_task input = task(&stats, i);
        uint64_t task_id;
        if (cgai_life_domain_enqueue(owner, &input, &task_id) != CGAI_LIFE_OK)
            return 0;
    }
    return 1;
}

static int train(cgai_life_domain *owner, uint32_t steps) {
    while (steps != 0U) {
        const uint32_t quantum =
            steps > CGAI_LIFE_MAX_STEP_GENERATIONS ? CGAI_LIFE_MAX_STEP_GENERATIONS : steps;
        if (cgai_life_domain_train_step(owner, quantum, NULL) != CGAI_LIFE_OK)
            return 0;
        steps -= quantum;
    }
    return 1;
}

static void report_groups(const cgai_life_domain_stats *stats) {
    for (uint32_t group = 0U; group < stats->group_count; ++group)
        printf("uid=%u steps=%" PRIu64 " hash=%016" PRIx64 " visits=%" PRIu64 " deferred=%" PRIu64
               "\n",
               stats->group_uids[group], stats->group_steps[group], stats->group_hashes[group],
               stats->group_visits[group], stats->group_deferred[group]);
    for (uint32_t slot = 0U; slot < stats->group_count; ++slot)
        printf("cell-slot=%u steps=%" PRIu64 " hash=%016" PRIx64 "\n", slot,
               stats->cell_group_steps[slot], stats->cell_group_hashes[slot]);
}

static int report(const cgai_life_domain *owner) {
    cgai_life_domain_stats stats;
    cgai_life_domain_round round;
    if (cgai_life_domain_get_stats(owner, &stats) != CGAI_LIFE_OK ||
        cgai_life_domain_get_round(owner, &round) != CGAI_LIFE_OK)
        return 0;
    printf("generation=%u groups=%u experts=%u queued=%u replay=%u observations=%" PRIu64
           " deferred=%" PRIu64 " updates=%" PRIu64 " version=%" PRIu64 " renewals=%" PRIu64
           " hash=%016" PRIx64 "\n",
           stats.generation, stats.group_count, stats.expert_count, stats.queued,
           stats.replay_records, stats.observations, stats.deferred, stats.training_updates,
           stats.version, stats.encounter_renewals, cgai_life_domain_hash(owner));
    report_groups(&stats);
    for (uint32_t i = 0U; i < round.count; ++i)
        printf("round-task=%" PRIu64 " parent=%" PRIu64
               " participants=%u executed=%u target=%u verified=%u admission=%u\n",
               round.records[i].task_id, round.records[i].parent_version,
               round.records[i].participant_mask, round.records[i].executed_action,
               round.records[i].target, round.records[i].verified,
               (unsigned int)round.records[i].admission);
    return 1;
}

static int execute(cgai_life_domain *owner, const domain_options *options) {
    const int fresh = strcmp(options->command, "run") == 0;
    if (!fresh && cgai_life_domain_load(owner, options->checkpoint) != CGAI_LIFE_OK)
        return 0;
    if (strcmp(options->command, "inspect") == 0)
        return report(owner) && probe_prediction(owner, "frozen");
    return probe_prediction(owner, "before") && (!fresh || enqueue_tasks(owner)) &&
           train(owner, options->steps) && probe_prediction(owner, "after") && report(owner) &&
           cgai_life_domain_save(owner, options->checkpoint) == CGAI_LIFE_OK;
}

int main(int argc, char **argv) {
    domain_options options = {NULL, NULL, 8U, 8U, 0U};
    if (!arguments(&options, argc, argv)) {
        fputs("usage: cgai_life_probe run CHECKPOINT [--groups 2..8] [--steps N]\n"
              "       cgai_life_probe resume CHECKPOINT [--steps N]\n"
              "       cgai_life_probe inspect CHECKPOINT\n",
              stderr);
        return 2;
    }
    cgai_life_config config = cgai_life_config_default();
    cgai_life_domain *owner = NULL;
    config.group_count = options.groups;
    config.scenario = 3U;
    config.training_epochs = 1U;
    config.enable_merges = 0U;
    if (cgai_life_domain_create(&config, CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE, &owner) !=
        CGAI_LIFE_OK)
        return 1;
    const int okay = execute(owner, &options);
    cgai_life_domain_destroy(owner);
    if (!okay)
        fputs("native domain operation failed\n", stderr);
    return okay ? 0 : 1;
}
