/** @file test_collection.c @brief Verified closed-loop records and snapshot provenance. */
#include "gameplay/gameplay_internal.h"
#include "internal/file_utils.h"
#include "npc_collection.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Isolated generated test paths, always removed after successful validation. */
static const char *const paths[] = {"npc-v3-initial.cgcheckpoint",
                                    "npc-v3-first.cgcheckpoint",
                                    "npc-v3-second.cgcheckpoint",
                                    "npc-v3-replay.cgcheckpoint",
                                    "npc-v3-memory-first.cgcheckpoint",
                                    "npc-v3-memory-second.cgcheckpoint",
                                    "npc-v3-memory-replay.cgcheckpoint",
                                    "npc-v3-model.cggp",
                                    "npc-v3-corpus0.tsv",
                                    "npc-v3-corpus1.tsv",
                                    "npc-v3-corpus-repeat.tsv",
                                    "npc-v3-coverage0.tsv",
                                    "npc-v3-coverage1.tsv",
                                    "npc-v3-coverage-repeat.tsv",
                                    "npc-v3-corrupt.tsv",
                                    "npc-v3-inspection.tsv"};

/** @brief Require exact complete-file equality for checkpoints or canonical generated records.
 * @param first Expected complete file.
 * @param second Independently reconstructed complete file. */
static void exact_files(const char *first, const char *second) {
    uint8_t *left = NULL;
    uint8_t *right = NULL;
    size_t left_size = 0U;
    size_t right_size = 0U;
    TEST_CHECK(cgai_file_read_all(first, &left, &left_size), cgai_last_error());
    TEST_CHECK(cgai_file_read_all(second, &right, &right_size), cgai_last_error());
    TEST_CHECK(left_size == right_size && memcmp(left, right, left_size) == 0,
               "immutable NPC v3 replay changed complete canonical bytes");
    free(left);
    free(right);
}

/** @brief Align every expert to a deliberately hazardous legal proposal.
 * @param model Borrowed compatible seeded model without optimizer ownership. */
static void hazardous_policy(cgai_gameplay_model *model) {
    TEST_CHECK(npc_policy_compatible(model), "hazard fixture requires the frozen NPC shape");
    const size_t experts = model->config.module_count * model->config.centroids_per_module;
    const size_t outputs = model->config.output_counts[0];
    memset(model->heads[0], 0, experts * outputs * sizeof(*model->heads[0]));
    memset(model->decoders[0], 0,
           model->config.module_count * outputs * model->config.hidden_dimensions *
               sizeof(*model->decoders[0]));
    for (size_t expert = 0U; expert < experts; ++expert)
        model->heads[0][expert * outputs + NPC_NORTH] = 20.0;
}

/** @brief Prove the fixture proposes a permitted fatal move before collector correction.
 * @param model Borrowed shaped inference model without optimizer ownership. */
static void verify_hazardous_policy(const cgai_gameplay_model *model) {
    npc_family family;
    npc_world world;
    npc_observation observation;
    cgai_gameplay_result selected;
    TEST_CHECK(npc_family_get(NPC_TRAIN, 1U, &family) && npc_world_init(&world, &family, 8U),
               "could not initialize the pinned collection hazard fixture");
    TEST_CHECK(npc_world_step(&world, NPC_WAIT) == NPC_IDLE,
               "hazard fixture did not admit its initial idle decision");
    npc_world_observe(&world, &observation);
    TEST_CHECK(observation.neighbors[0] == NPC_DANGER &&
                   (observation.allowed_actions & (UINT64_C(1) << NPC_NORTH)) != 0U,
               "hazard fixture no longer exposes a legal fatal north move");
    npc_policy_session *session = npc_policy_session_create(model, 1);
    TEST_CHECK(session != NULL, cgai_last_error());
    TEST_CHECK(npc_policy_decide(session, &observation, UINT64_C(3), &selected), cgai_last_error());
    TEST_CHECK(selected.output == NPC_NORTH &&
                   npc_world_step(&world, selected.output) == NPC_DIED &&
                   world.terminal == NPC_DEATH && world.attempted_illegal == 0U,
               "hazard fixture did not select the legal fatal move without correction");
    npc_policy_session_destroy(session);
}

/** @brief Populate a private snapshot fixture without running a trainer.
 * @param model Borrowed seeded model with absent optimizer moments. */
static void snapshot_optimizer(cgai_gameplay_model *model) {
    const size_t count = model->parameter_count;
    TEST_CHECK(count > 0U && count <= CGAI_GAMEPLAY_MAX_PARAMETERS,
               "invalid snapshot fixture parameter capacity");
    model->adam_first = calloc(count, sizeof(*model->adam_first));
    model->adam_second = calloc(count, sizeof(*model->adam_second));
    TEST_CHECK(model->adam_first != NULL && model->adam_second != NULL,
               "could not allocate snapshot fixture moments");
    model->training_step = 6U;
    model->training_epochs = 1U;
    model->parameters[0] += 0.01;
}

/** @brief Write seeded or populated state for read-only collection checks.
 * @param path Borrowed test-owned snapshot destination.
 * @param populated Nonzero creates nonempty progress and optimizer ownership. */
static void write_snapshot(const char *path, int populated) {
    const cgai_gameplay_config config = npc_policy_config();
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    TEST_CHECK(model != NULL, cgai_last_error());
    if (populated) {
        hazardous_policy(model);
        verify_hazardous_policy(model);
        snapshot_optimizer(model);
    }
    TEST_CHECK(cgai_gameplay_checkpoint_save(model, path), cgai_last_error());
    cgai_gameplay_destroy(model);
}

/** @brief Exercise bounded snapshot collection and deterministic exact corpus regeneration. */
static void collect_rounds(void) {
    write_snapshot(paths[0], 0);
    TEST_CHECK(npc_collection_write(paths[0], paths[8], paths[11], 0U, 0U), cgai_last_error());
    write_snapshot(paths[1], 1);
    TEST_CHECK(npc_collection_write(paths[1], paths[9], paths[12], 1U, 4U), cgai_last_error());
    TEST_CHECK(npc_collection_write(paths[1], paths[10], paths[13], 1U, 4U), cgai_last_error());
    exact_files(paths[9], paths[10]);
    exact_files(paths[12], paths[13]);
    npc_collection *collection = npc_collection_load(paths[9]);
    TEST_CHECK(collection != NULL && collection->count <= NPC_TRAINING_LIMIT &&
                   collection->base_episodes == 192U && collection->policy_episodes != 0U &&
                   collection->guarded_episodes != 0U,
               "collection omitted complete teacher coverage or pre-fatal policy corrections");
    TEST_CHECK(collection->source_epochs == 1U && collection->source_steps != 0U,
               "collection omitted populated snapshot progress provenance");
    TEST_CHECK(collection->exploration_episodes != 0U &&
                   collection->exploration_episodes + collection->exploration_rejected_episodes ==
                       48U,
               "collection omitted the frozen forty-eight safe exploration attempts");
    free(collection);
}

/** @brief Require memoryless training masks only the four history fields on identical records. */
static void history_ablation(void) {
    npc_collection *history = npc_collection_load(paths[9]);
    npc_collection *memoryless = npc_collection_load(paths[9]);
    TEST_CHECK(history != NULL && memoryless != NULL, cgai_last_error());
    npc_collection_disable_history(memoryless);
    TEST_CHECK(history->count == memoryless->count &&
                   memcmp(history->roles, memoryless->roles,
                          history->count * sizeof(history->roles[0])) == 0 &&
                   memcmp(history->sources, memoryless->sources,
                          history->count * sizeof(history->sources[0])) == 0,
               "history ablation changed policy-round, family, variant or step provenance");
    for (size_t index = 0U; index < history->count; ++index) {
        TEST_CHECK(history->roles[index] ==
                       (uint32_t)(history->examples[index].state.values[15] == NPC_CUE ||
                                  history->examples[index].state.values[10] == NPC_BLOCKED),
                   "role was not derived from the original observable full-history record");
        for (size_t field = 8U; field < 12U; ++field)
            history->examples[index].state.values[field] = 0U;
        TEST_CHECK(memcmp(&history->examples[index], &memoryless->examples[index],
                          sizeof(history->examples[index])) == 0,
                   "history ablation changed targets, order or non-history inputs");
    }
    free(history);
    free(memoryless);
}

/** @brief Verify original observed blockage roles survive the memoryless input ablation.
 * @param collection Mutable private verified full-history corpus.
 * @param expected Expected full-record observed noncue blockage count. */
static void exploration_roles(npc_collection *collection, size_t expected) {
    npc_collection_disable_history(collection);
    size_t blocked_roles = 0U;
    for (size_t index = 0U; index < collection->count; ++index)
        blocked_roles += collection->roles[index] == 1U &&
                         collection->examples[index].state.values[15] != NPC_CUE;
    TEST_CHECK(blocked_roles == expected,
               "memoryless masking reassigned an observed recovery role");
}

/** @brief Check each explored complete trajectory contains one visible alternative and recovery. */
static void exploration_coverage(void) {
    npc_collection *collection = npc_collection_load(paths[8]);
    TEST_CHECK(collection != NULL, cgai_last_error());
    size_t alternatives = 0U;
    size_t recovery = 0U;
    size_t preserved_blocked = 0U;
    for (size_t index = 0U; index < collection->count; ++index) {
        const npc_collection_source *source = &collection->sources[index];
        const cgai_gameplay_example *example = &collection->examples[index];
        if (source->phase >= 3U) {
            TEST_CHECK(example->state.values[15] != NPC_CUE && source->variant % 8U < 3U,
                       "exploration crossed cue privacy or the frozen first-three sibling recipe");
            alternatives += source->executed != example->target;
            recovery += source->phase == 4U;
        }
        preserved_blocked +=
            example->state.values[15] != NPC_CUE && example->state.values[10] == NPC_BLOCKED;
    }
    TEST_CHECK(alternatives == collection->exploration_episodes && recovery != 0U &&
                   preserved_blocked != 0U,
               "verified exploration omitted exact one-action alternatives or observed recovery");
    exploration_roles(collection, preserved_blocked);
    free(collection);
}

/** @brief Change one exact text segment in a copied generated corpus for rejection tests.
 * @param needle Existing segment of the valid first-round corpus.
 * @param replacement Corrupt replacement segment. */
static void mutate_corpus(const char *needle, const char *replacement) {
    uint8_t *bytes = NULL;
    size_t size = 0U;
    TEST_CHECK(cgai_file_read_all(paths[8], &bytes, &size), cgai_last_error());
    const char *found = strstr((const char *)bytes, needle);
    TEST_CHECK(found != NULL, "missing canonical corpus mutation fixture");
    const size_t offset = (size_t)(found - (const char *)bytes);
    const size_t needle_size = strlen(needle);
    const size_t replacement_size = strlen(replacement);
    uint8_t *mutated = malloc(size + replacement_size + 1U);
    TEST_CHECK(mutated != NULL, "could not allocate bounded corruption fixture");
    memcpy(mutated, bytes, offset);
    memcpy(mutated + offset, replacement, replacement_size);
    memcpy(mutated + offset + replacement_size, bytes + offset + needle_size,
           size - offset - needle_size);
    TEST_CHECK(cgai_file_write_all(paths[14], mutated, size - needle_size + replacement_size),
               cgai_last_error());
    TEST_CHECK(npc_collection_load(paths[14]) == NULL,
               "malformed or unverified NPC v3 correction corpus was admitted");
    free(mutated);
    free(bytes);
}

/** @brief Reject a falsified exploration count without assuming all perturbations succeed. */
static void exploration_count_rejection(void) {
    npc_collection *collection = npc_collection_load(paths[8]);
    TEST_CHECK(collection != NULL, cgai_last_error());
    char needle[96];
    (void)snprintf(needle, sizeof(needle), "exploration_rejected_episodes\t%zu\n",
                   collection->exploration_rejected_episodes);
    mutate_corpus(needle, "exploration_rejected_episodes\t49\n");
    free(collection);
}

/** @brief Reject a changed actual exploration action while retaining the teacher target. */
static void exploration_action_rejection(void) {
    npc_collection *collection = npc_collection_load(paths[8]);
    TEST_CHECK(collection != NULL, cgai_last_error());
    size_t index = 0U;
    while (index < collection->count &&
           (collection->sources[index].phase != 3U ||
            collection->sources[index].executed == collection->examples[index].target))
        ++index;
    TEST_CHECK(index < collection->count, "missing actual exploration corruption fixture");
    const npc_collection_source *source = &collection->sources[index];
    const uint32_t target = collection->examples[index].target;
    char needle[160];
    char replacement[160];
    (void)snprintf(needle, sizeof(needle), "example\t%zu\t0\t%u\t%u\t%u\t3\t1\t0\t%u\t%u\t", index,
                   source->family, source->variant, source->step, target, source->executed);
    (void)snprintf(replacement, sizeof(replacement),
                   "example\t%zu\t0\t%u\t%u\t%u\t3\t1\t0\t%u\t%u\t", index, source->family,
                   source->variant, source->step, target, target);
    mutate_corpus(needle, replacement);
    free(collection);
}

/** @brief Reject raw-data corruption, family leakage and unverified successful labels. */
static void corpus_rejections(void) {
    mutate_corpus("schema\tnpc-corpus-v3\n", "schema\tnpc-corpus-v1\n");
    mutate_corpus("profile\thistory\n", "profile\tmemoryless\n");
    mutate_corpus("round\t0\n", "round\t1\n");
    mutate_corpus("base_episodes\t192\n", "base_episodes\t191\n");
    exploration_count_rejection();
    exploration_action_rejection();
    mutate_corpus("example\t0\t0\t0\t0\t0\t0\t1\t0\t", "example\t0\t0\t48\t0\t0\t0\t1\t0\t");
    mutate_corpus("example\t0\t0\t0\t0\t0\t0\t1\t0\t", "example\t0\t0\t0\t0\t0\t0\t0\t0\t");
    mutate_corpus("example\t0\t0\t0\t0\t0\t0\t1\t0\t", "example\t0\t0\t0\t0\t0\t0\t1\t1\t");
    TEST_CHECK(!npc_collection_write(NULL, paths[10], paths[13], 0U, 0U),
               "null immutable checkpoint was admitted");
    TEST_CHECK(!npc_collection_write(paths[0], paths[10], paths[13], 1U, 4U),
               "future closed-loop round reused an untrained seeded policy");
}

/** @brief Verify current-world collection, history masking and corpus validation.
 * @return Zero after every deterministic collection and corruption invariant passes. */
int main(void) {
    collect_rounds();
    history_ablation();
    exploration_coverage();
    corpus_rejections();
    for (size_t index = 0U; index < sizeof(paths) / sizeof(paths[0]); ++index)
        (void)remove(paths[index]);
    return 0;
}
