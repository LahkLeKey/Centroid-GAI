/** @file test_evaluation.c @brief Complete own-history comparison with a pinned previous model. */
#include "internal/file_utils.h"
#include "npc_evaluation.h"
#include "test_utils.h"
#include <stdlib.h>
#include <string.h>

/** @brief Require every attempted episode to occur in the terminal denominator.
 * @param metrics Borrowed complete actor outcomes. */
static void complete_denominators(const npc_episode_metrics *metrics) {
    TEST_CHECK(metrics->count == NPC_FAMILIES_PER_SPLIT * NPC_VARIANTS_PER_FAMILY,
               "missing independently executed development episodes");
    TEST_CHECK(metrics->success + metrics->deaths + metrics->timeouts == metrics->count,
               "terminal denominator omitted failed episodes");
    TEST_CHECK(metrics->survived + metrics->deaths == metrics->count,
               "survival denominator omitted failed episodes");
    TEST_CHECK(metrics->illegal_executed == 0U, "host executed an illegal proposal");
}

/** @brief Compare identical seeded weights through separately caused actor histories.
 * @param model Borrowed untouched compatible model. */
static void independent_previous(const cgai_gameplay_model *model) {
    npc_evaluation evaluation;
    TEST_CHECK(npc_evaluate(model, model, model, NPC_DEV, &evaluation), cgai_last_error());
    for (size_t actor = 0U; actor < NPC_ACTORS; ++actor)
        complete_denominators(&evaluation.total[actor]);
    TEST_CHECK(evaluation.total[0].success == evaluation.total[0].count,
               "observation-only planner failed development geometry");
    TEST_CHECK(memcmp(&evaluation.total[3], &evaluation.total[7], sizeof(npc_episode_metrics)) == 0,
               "identical prior weights did not produce independently identical episodes");
    TEST_CHECK(evaluation.composition_count > 0U, "composition sample omitted all observations");
    TEST_CHECK(npc_evaluation_write("npc-v3-evaluation.tsv", NPC_DEV, &evaluation),
               cgai_last_error());
}

/** @brief Reject missing prior weights before publishing any measured results.
 * @param model Borrowed compatible seeded model. */
static void missing_previous(const cgai_gameplay_model *model) {
    npc_evaluation untouched = {0};
    untouched.composition_count = 17U;
    TEST_CHECK(!npc_evaluate(model, model, NULL, NPC_DEV, &untouched),
               "missing pinned baseline was silently accepted");
    TEST_CHECK(untouched.composition_count == 17U, "failed evaluation published a partial report");
    TEST_CHECK(!npc_evaluate(model, model, model, NPC_TRAIN, &untouched),
               "training split was exposed through reserved evaluation");
}

/** @brief Check explicit eighth actor and profile identity in the compact report. */
static void previous_report(void) {
    uint8_t *bytes = NULL;
    size_t size = 0U;
    TEST_CHECK(cgai_file_read_all("npc-v3-evaluation.tsv", &bytes, &size), cgai_last_error());
    TEST_CHECK(size > 0U && strstr((const char *)bytes, "schema\tnpc-evaluation-v3\n") != NULL,
               "report used an earlier profile schema");
    TEST_CHECK(strstr((const char *)bytes, "summary\tprevious\t1152\t") != NULL,
               "report omitted the complete pinned prior actor");
    free(bytes);
}

/** @brief Execute isolated development-only comparison checks.
 * @return Zero after all assertions. */
int main(void) {
    const cgai_gameplay_config config = npc_policy_config();
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    missing_previous(model);
    independent_previous(model);
    previous_report();
    cgai_gameplay_destroy(model);
    return 0;
}
