/** @file test_feedback.c @brief Authentic contact feedback, isolation and exact restart checks. */
#include "centroid_life.h"
#include "life_context.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, "Native measured Life feedback check failed")
#define TEST_PATH_BYTES 4096U
#define TEST_ACTION 80U
#define TEST_FALLBACK 1U

static const char choice_input[] = "source centroid collision compiler verified optimization";
static const uint32_t alternatives[] = {TEST_FALLBACK, TEST_ACTION};

static cgai_life_context *new_owner(uint32_t epochs) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life_context *owner = NULL;
    config.enable_merges = 0U;
    config.training_epochs = epochs;
    CHECK(cgai_life_context_create(&config, &owner) == CGAI_LIFE_OK);
    CHECK(owner != NULL);
    return owner;
}

static cgai_life_context_choice_stats stats(const cgai_life_context *owner) {
    cgai_life_context_choice_stats result;
    CHECK(cgai_life_context_choice_get_stats(owner, &result) == CGAI_LIFE_OK);
    return result;
}

static life_context_choice_state *model_copy(const cgai_life_context *owner) {
    life_context_choice_state *copy = malloc(sizeof(*copy));
    CHECK(copy != NULL);
    *copy = owner->choice;
    return copy;
}

static cgai_life_context_feedback feedback(double utility) {
    return (cgai_life_context_feedback){CGAI_LIFE_CONTEXT_TRAIN, 1U, 0U, UINT64_C(781940583),
                                        utility};
}

static void same_choice(const cgai_life_context_choice *first,
                        const cgai_life_context_choice *second) {
    CHECK(first->token == second->token && first->input_hash == second->input_hash);
    CHECK(first->model_version == second->model_version && first->world_hash == second->world_hash);
    CHECK(first->generation == second->generation && first->conflict_id == second->conflict_id);
    CHECK(first->participant_mask == second->participant_mask && first->action == second->action);
    CHECK(first->supported_groups == second->supported_groups);
    CHECK(first->predicted_utility == second->predicted_utility);
}

static void stable_contact(cgai_life_context *owner, uint8_t mask) {
    const uint32_t generation = owner->run.world.tick;
    life_world_clear(&owner->run.world, 42U);
    owner->run.world.tick = generation;
    for (int y = 10; y < 12; ++y)
        for (int x = 10; x < 12; ++x)
            CHECK(life_world_set(&owner->run.world, x, y, mask) == LIFE_OK);
}

static cgai_life_events contact_mask(cgai_life_context *owner, uint8_t mask) {
    cgai_life_events events;
    stable_contact(owner, mask);
    const uint64_t source_hash = life_world_hash(&owner->run.world);
    CHECK(cgai_life_context_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_get_events(owner, &events) == CGAI_LIFE_OK);
    CHECK(events.generation_valid == 1U && events.event_count == 1U);
    CHECK(events.events[0].participant_mask == mask);
    CHECK(events.events[0].source_world_hash == source_hash);
    CHECK(events.events[0].result_world_hash == life_world_hash(&owner->run.world));
    return events;
}

static cgai_life_events contact(cgai_life_context *owner) { return contact_mask(owner, 3U); }

static cgai_life_context_choice begin(cgai_life_context *owner, uint32_t fallback) {
    cgai_life_context_choice decision;
    CHECK(cgai_life_context_choice_begin(owner, 0U, choice_input, alternatives, 2U, fallback,
                                         &decision) == CGAI_LIFE_OK);
    CHECK(decision.token != 0U && decision.input_hash != 0U);
    same_choice(&decision, &owner->choice.decision);
    return decision;
}

static cgai_life_context_choice predict(const cgai_life_context *owner, uint32_t mask,
                                        uint32_t fallback) {
    cgai_life_context_choice result;
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_choice_predict(owner, choice_input, mask, alternatives, 2U, fallback,
                                           &result) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_hash(owner) == before);
    CHECK(result.token == 0U && result.generation == 0U && result.conflict_id == 0U);
    CHECK(result.world_hash == 0U);
    return result;
}

static void before_contact(cgai_life_context *owner) {
    cgai_life_context_choice output;
    unsigned char expected[sizeof(output)];
    memset(&output, 0x5a, sizeof(output));
    memcpy(expected, &output, sizeof(expected));
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_choice_begin(owner, 0U, choice_input, alternatives, 2U, TEST_ACTION,
                                         &output) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(memcmp(&output, expected, sizeof(output)) == 0);
    CHECK(cgai_life_context_hash(owner) == before);
    const cgai_life_context_choice frozen = predict(owner, 3U, TEST_FALLBACK);
    CHECK(frozen.action == TEST_FALLBACK && frozen.supported_groups == 0U);
    CHECK(frozen.predicted_utility == 0.0 && stats(owner).decisions == 0U);
}

static void stable_record_ids(cgai_life_context *owner) {
    cgai_life_context_input input = {"src/feedback.c",
                                     "centroid source compiler feedback",
                                     2U,
                                     9U,
                                     813U,
                                     CGAI_LIFE_CONTEXT_SOURCE,
                                     CGAI_LIFE_CONTEXT_TRAIN,
                                     1U,
                                     15U};
    uint32_t first = 77U, repeated = 99U;
    CHECK(cgai_life_context_add_record(owner, &input, &first) == CGAI_LIFE_OK);
    CHECK(first == 1U);
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_add_record(owner, &input, &repeated) == CGAI_LIFE_OK);
    CHECK(repeated == first && cgai_life_context_hash(owner) == before);
    input.text = "changed centroid source compiler feedback";
    CHECK(cgai_life_context_add_record(owner, &input, &repeated) == CGAI_LIFE_OK);
    CHECK(repeated == 2U);
    input.text = NULL;
    CHECK(cgai_life_context_add_record(owner, &input, &repeated) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(repeated == 2U);
}

static void authentic_decision(const cgai_life_context_choice *decision,
                               const cgai_life_events *events) {
    const cgai_life_collision_event *event = &events->events[0];
    CHECK(decision->generation == event->generation);
    CHECK(decision->conflict_id == event->conflict_id && event->conflict_id != 0U);
    CHECK(decision->world_hash == event->source_world_hash);
    CHECK(decision->participant_mask == event->participant_mask);
    CHECK(decision->action == TEST_ACTION && decision->model_version == 0U);
    CHECK(decision->supported_groups == 0U && decision->predicted_utility == 0.0);
}

static void rejected_observe(cgai_life_context *owner, const cgai_life_context_choice *decision,
                             const cgai_life_context_feedback *measurement) {
    const uint64_t before = cgai_life_context_hash(owner);
    const cgai_life_context_choice pending = owner->choice.decision;
    CHECK(cgai_life_context_choice_observe(owner, decision, measurement) ==
          CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_context_hash(owner) == before);
    same_choice(&pending, &owner->choice.decision);
}

static void heldout_feedback(cgai_life_context *owner, const cgai_life_context_choice *decision) {
    cgai_life_context_feedback measurement = feedback(1.0);
    measurement.split = CGAI_LIFE_CONTEXT_DEVELOPMENT;
    rejected_observe(owner, decision, &measurement);
    measurement.split = CGAI_LIFE_CONTEXT_AUDIT;
    rejected_observe(owner, decision, &measurement);
    measurement = feedback(1.0);
    measurement.verified = 0U;
    rejected_observe(owner, decision, &measurement);
    measurement.verified = 2U;
    rejected_observe(owner, decision, &measurement);
    measurement = feedback(1.0);
    measurement.evidence_hash = 0U;
    rejected_observe(owner, decision, &measurement);
}

static void nonfinite_feedback(cgai_life_context *owner, const cgai_life_context_choice *decision) {
    const double utilities[] = {NAN, INFINITY, -INFINITY, 1.000001, -1.000001};
    for (size_t i = 0U; i < sizeof(utilities) / sizeof(utilities[0]); ++i) {
        const cgai_life_context_feedback measurement = feedback(utilities[i]);
        rejected_observe(owner, decision, &measurement);
    }
    rejected_observe(owner, NULL, &(cgai_life_context_feedback){0});
    rejected_observe(owner, decision, NULL);
}

static void stale_decisions(cgai_life_context *owner, const cgai_life_context_choice *decision) {
    const cgai_life_context_feedback measurement = feedback(1.0);
    cgai_life_context_choice altered = *decision;
    ++altered.token;
    rejected_observe(owner, &altered, &measurement);
    altered = *decision;
    ++altered.generation;
    rejected_observe(owner, &altered, &measurement);
    altered = *decision;
    ++altered.model_version;
    rejected_observe(owner, &altered, &measurement);
    altered = *decision;
    altered.action = TEST_FALLBACK;
    rejected_observe(owner, &altered, &measurement);
}

static void altered_provenance(cgai_life_context *owner, const cgai_life_context_choice *decision) {
    const cgai_life_context_feedback measurement = feedback(1.0);
    cgai_life_context_choice altered = *decision;
    altered.world_hash ^= 1U;
    rejected_observe(owner, &altered, &measurement);
    altered = *decision;
    altered.input_hash ^= 1U;
    rejected_observe(owner, &altered, &measurement);
    altered = *decision;
    altered.participant_mask = 15U;
    rejected_observe(owner, &altered, &measurement);
    altered = *decision;
    altered.predicted_utility = NAN;
    rejected_observe(owner, &altered, &measurement);
}

static void pending_blocks(cgai_life_context *owner) {
    const uint64_t before = cgai_life_context_hash(owner);
    uint32_t completed = 77U;
    cgai_life_context_choice output = {0};
    CHECK(cgai_life_context_train_step(owner, 1U, &completed) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(completed == 77U && cgai_life_context_hash(owner) == before);
    CHECK(cgai_life_context_choice_begin(owner, 0U, choice_input, alternatives, 2U, TEST_ACTION,
                                         &output) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(output.token == 0U && cgai_life_context_hash(owner) == before);
    CHECK(stats(owner).pending == 1U && stats(owner).decisions == 1U);
    (void)predict(owner, 3U, TEST_FALLBACK);
    CHECK(cgai_life_context_hash(owner) == before);
}

static void unchanged_action(const life_context_choice_state *before,
                             const cgai_life_context *owner, size_t group, size_t action) {
    const life_context_action_model *expected = &before->models[group][action];
    const life_context_action_model *actual = &owner->choice.models[group][action];
    CHECK(memcmp(expected, actual, sizeof(*actual)) == 0);
}

static void isolated_models(const life_context_choice_state *before, const cgai_life_context *owner,
                            const cgai_life_context_choice *decision) {
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group) {
        const int participating = (decision->participant_mask & (1U << group)) != 0U;
        CHECK(owner->choice.group_steps[group] ==
              before->group_steps[group] + (participating ? 1U : 0U));
        for (size_t action = 0U; action < CGAI_LIFE_CONTEXT_ACTIONS; ++action) {
            if (!participating || action != decision->action)
                unchanged_action(before, owner, group, action);
            else {
                CHECK(owner->choice.models[group][action].steps == 1U);
                CHECK(owner->choice.models[group][action].observations == 1U);
                CHECK(memcmp(&before->models[group][action], &owner->choice.models[group][action],
                             sizeof(life_context_action_model)) != 0);
            }
        }
    }
}

static void learning_counters(const cgai_life_context *owner) {
    const cgai_life_context_choice_stats after = stats(owner);
    CHECK(after.pending == 0U && after.version == 1U && after.observations == 1U);
    CHECK(after.decisions == 1U && after.deferred == 0U);
    CHECK(after.group_steps[0] == 1U && after.group_steps[1] == 1U);
    CHECK(after.group_steps[2] == 0U && after.group_steps[3] == 0U);
    CHECK(owner->choice.decision.token == 0U && owner->choice.input[0] == '\0');
}

static void consumed_rejects(cgai_life_context *owner, const cgai_life_context_choice *decision) {
    const cgai_life_context_feedback measurement = feedback(1.0);
    const uint64_t before = cgai_life_context_hash(owner);
    cgai_life_context_choice output = {0};
    rejected_observe(owner, decision, &measurement);
    CHECK(cgai_life_context_choice_begin(owner, 0U, choice_input, alternatives, 2U, TEST_ACTION,
                                         &output) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(output.token == 0U && cgai_life_context_hash(owner) == before);
}

static void unchanged_policy(const cgai_life_context *owner, const life_policy_info *expected) {
    life_policy_info actual;
    CHECK(life_policy_inspect(owner->run.policy, &actual) == CGAI_STATUS_OK);
    CHECK(actual.shared_hash == expected->shared_hash && actual.version == expected->version);
    CHECK(memcmp(actual.module_hash, expected->module_hash, sizeof(actual.module_hash)) == 0);
    CHECK(memcmp(actual.module_steps, expected->module_steps, sizeof(actual.module_steps)) == 0);
}

static void measured_learning(cgai_life_context *owner, const cgai_life_context_choice *decision) {
    const uint64_t run_hash = life_run_hash(&owner->run);
    life_policy_info policy;
    CHECK(life_policy_inspect(owner->run.policy, &policy) == CGAI_STATUS_OK);
    const life_context_record record = owner->records[0];
    life_context_choice_state *before = model_copy(owner);
    const cgai_life_context_feedback measurement = feedback(1.0);
    CHECK(cgai_life_context_choice_observe(owner, decision, &measurement) == CGAI_LIFE_OK);
    isolated_models(before, owner, decision);
    learning_counters(owner);
    CHECK(life_run_hash(&owner->run) == run_hash);
    unchanged_policy(owner, &policy);
    CHECK(memcmp(&record, &owner->records[0], sizeof(record)) == 0);
    const cgai_life_context_choice result = predict(owner, 3U, TEST_FALLBACK);
    CHECK(result.action == TEST_ACTION && result.supported_groups == 3U);
    CHECK(fabs(result.predicted_utility - 0.1) < 1e-12);
    consumed_rejects(owner, decision);
    free(before);
}

static void later_event_rejects(cgai_life_context *owner,
                                const cgai_life_context_choice *previous) {
    (void)contact(owner);
    const cgai_life_context_choice next = begin(owner, TEST_FALLBACK);
    CHECK(next.token != previous->token && next.generation > previous->generation);
    CHECK(next.action == TEST_ACTION && next.model_version == 1U);
    const cgai_life_context_feedback measurement = feedback(-1.0);
    rejected_observe(owner, previous, &measurement);
    CHECK(cgai_life_context_choice_observe(owner, &next, &measurement) == CGAI_LIFE_OK);
}

static void contact_gates(void) {
    cgai_life_context *owner = new_owner(1U);
    before_contact(owner);
    stable_record_ids(owner);
    const cgai_life_events events = contact(owner);
    const cgai_life_context_choice decision = begin(owner, TEST_ACTION);
    authentic_decision(&decision, &events);
    heldout_feedback(owner, &decision);
    nonfinite_feedback(owner, &decision);
    stale_decisions(owner, &decision);
    altered_provenance(owner, &decision);
    pending_blocks(owner);
    measured_learning(owner, &decision);
    later_event_rejects(owner, &decision);
    cgai_life_context_destroy(owner);
}

static void negative_utility(void) {
    cgai_life_context *owner = new_owner(1U);
    (void)contact(owner);
    const cgai_life_context_choice decision = begin(owner, TEST_ACTION);
    const cgai_life_context_feedback measurement = feedback(-1.0);
    CHECK(cgai_life_context_choice_observe(owner, &decision, &measurement) == CGAI_LIFE_OK);
    const uint32_t only_action[] = {TEST_ACTION};
    cgai_life_context_choice learned;
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_choice_predict(owner, choice_input, 3U, only_action, 1U, TEST_ACTION,
                                           &learned) == CGAI_LIFE_OK);
    CHECK(learned.action == TEST_ACTION && fabs(learned.predicted_utility + 0.1) < 1e-12);
    CHECK(learned.supported_groups == 3U && cgai_life_context_hash(owner) == before);
    const cgai_life_context_choice result = predict(owner, 3U, TEST_FALLBACK);
    CHECK(result.action == TEST_FALLBACK && result.predicted_utility == 0.0);
    const cgai_life_context_choice unrelated = predict(owner, 12U, TEST_FALLBACK);
    CHECK(unrelated.action == TEST_FALLBACK && unrelated.supported_groups == 0U);
    cgai_life_context_destroy(owner);
}

static void no_gradient(uint32_t epochs, uint32_t deferred) {
    cgai_life_context *owner = new_owner(epochs);
    (void)contact(owner);
    const cgai_life_context_choice decision = begin(owner, TEST_ACTION);
    life_context_choice_state *before = model_copy(owner);
    const uint64_t run_hash = life_run_hash(&owner->run);
    cgai_life_context_feedback measurement = feedback(1.0);
    measurement.deferred = deferred;
    CHECK(cgai_life_context_choice_observe(owner, &decision, &measurement) == CGAI_LIFE_OK);
    CHECK(memcmp(before->models, owner->choice.models, sizeof(before->models)) == 0);
    CHECK(memcmp(before->group_steps, owner->choice.group_steps, sizeof(before->group_steps)) == 0);
    const cgai_life_context_choice_stats after = stats(owner);
    CHECK(after.pending == 0U && after.version == 0U && after.deferred == deferred);
    CHECK(after.observations == 1U - deferred && after.decisions == 1U);
    CHECK(life_run_hash(&owner->run) == run_hash);
    consumed_rejects(owner, &decision);
    free(before);
    cgai_life_context_destroy(owner);
}

static void rejected_prediction(cgai_life_context *owner, uint32_t mask, const uint32_t *actions,
                                size_t count, uint32_t fallback) {
    cgai_life_context_choice output;
    unsigned char expected[sizeof(output)];
    memset(&output, 0x5a, sizeof(output));
    memcpy(expected, &output, sizeof(expected));
    const uint64_t before = cgai_life_context_hash(owner);
    CHECK(cgai_life_context_choice_predict(owner, choice_input, mask, actions, count, fallback,
                                           &output) == CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(memcmp(expected, &output, sizeof(output)) == 0);
    CHECK(cgai_life_context_hash(owner) == before);
}

static void invalid_choices(void) {
    cgai_life_context *owner = new_owner(1U);
    const uint32_t duplicates[] = {1U, 1U};
    const uint32_t too_large[] = {1U, CGAI_LIFE_CONTEXT_ACTIONS};
    rejected_prediction(owner, 0U, alternatives, 2U, TEST_FALLBACK);
    rejected_prediction(owner, 16U, alternatives, 2U, TEST_FALLBACK);
    rejected_prediction(owner, 3U, NULL, 2U, TEST_FALLBACK);
    rejected_prediction(owner, 3U, alternatives, 0U, TEST_FALLBACK);
    rejected_prediction(owner, 3U, alternatives, CGAI_LIFE_CONTEXT_MAX_CHOICES + 1U, TEST_FALLBACK);
    rejected_prediction(owner, 3U, duplicates, 2U, TEST_FALLBACK);
    rejected_prediction(owner, 3U, too_large, 2U, TEST_FALLBACK);
    rejected_prediction(owner, 3U, alternatives, 2U, 0U);
    cgai_life_context_destroy(owner);
}

static void path_name(char *output, const char *directory, const char *name) {
    const int written = snprintf(output, TEST_PATH_BYTES, "%s/%s", directory, name);
    CHECK(written > 0 && (size_t)written < TEST_PATH_BYTES);
}

static void empty_feed(const cgai_life_context *owner) {
    cgai_life_events actual;
    const cgai_life_events empty = {0};
    CHECK(cgai_life_context_get_events(owner, &actual) == CGAI_LIFE_OK);
    CHECK(memcmp(&actual, &empty, sizeof(actual)) == 0);
}

static cgai_life_context_choice pending_at_seam(cgai_life_context *owner) {
    (void)contact(owner);
    const cgai_life_context_choice first = begin(owner, TEST_ACTION);
    const cgai_life_context_feedback measurement = feedback(-1.0);
    CHECK(cgai_life_context_choice_observe(owner, &first, &measurement) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_train_step(owner, 30U, NULL) == CGAI_LIFE_OK);
    const cgai_life_events events = contact(owner);
    CHECK(events.generation == CGAI_LIFE_CONTEXT_ENCOUNTER_GENERATIONS);
    const cgai_life_context_choice pending = begin(owner, TEST_FALLBACK);
    CHECK(pending.action == TEST_FALLBACK && pending.token != first.token);
    return pending;
}

static void exact_owners(const cgai_life_context *first, const cgai_life_context *second) {
    CHECK(cgai_life_context_hash(first) == cgai_life_context_hash(second));
    CHECK(life_run_hash(&first->run) == life_run_hash(&second->run));
    CHECK(memcmp(first->choice.models, second->choice.models, sizeof(first->choice.models)) == 0);
    const cgai_life_context_choice first_result = predict(first, 3U, TEST_FALLBACK);
    const cgai_life_context_choice second_result = predict(second, 3U, TEST_FALLBACK);
    same_choice(&first_result, &second_result);
}

static void save_and_reload(const cgai_life_context *whole, cgai_life_context *restored,
                            const char *path) {
    CHECK(cgai_life_context_save(whole, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_load(restored, path) == CGAI_LIFE_OK);
    empty_feed(restored);
    exact_owners(whole, restored);
}

static void checkpoint_continuation(const char *path) {
    cgai_life_context *whole = new_owner(1U);
    cgai_life_context *restored = new_owner(1U);
    const cgai_life_context_choice pending = pending_at_seam(whole);
    save_and_reload(whole, restored, path);
    CHECK(stats(restored).pending == 1U);
    const cgai_life_context_feedback measurement = feedback(1.0);
    CHECK(cgai_life_context_choice_observe(whole, &pending, &measurement) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_choice_observe(restored, &pending, &measurement) == CGAI_LIFE_OK);
    exact_owners(whole, restored);
    save_and_reload(whole, restored, path);
    CHECK(cgai_life_context_train_step(whole, 3U, NULL) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_train_step(restored, 3U, NULL) == CGAI_LIFE_OK);
    exact_owners(whole, restored);
    cgai_life_context_destroy(whole);
    cgai_life_context_destroy(restored);
}

static void train_all_groups(cgai_life_context *owner) {
    (void)contact_mask(owner, 15U);
    const cgai_life_context_choice decision = begin(owner, TEST_ACTION);
    CHECK(decision.participant_mask == 15U);
    const cgai_life_context_feedback measurement = feedback(1.0);
    CHECK(cgai_life_context_choice_observe(owner, &decision, &measurement) == CGAI_LIFE_OK);
    const cgai_life_context_choice_stats after = stats(owner);
    CHECK(after.version == 1U && after.observations == 1U);
    for (size_t group = 0U; group < after.group_count; ++group)
        CHECK(after.group_steps[group] == 1U);
    const cgai_life_context_choice result = predict(owner, 15U, TEST_FALLBACK);
    CHECK(result.action == TEST_ACTION && result.supported_groups == 15U);
    CHECK(fabs(result.predicted_utility - 0.1) < 1e-12);
}

static void checkpoint_all_groups(const char *path) {
    cgai_life_context *whole = new_owner(1U);
    cgai_life_context *restored = new_owner(1U);
    train_all_groups(whole);
    const cgai_life_events events = contact_mask(whole, 15U);
    const cgai_life_context_choice pending = begin(whole, TEST_FALLBACK);
    CHECK(pending.participant_mask == events.events[0].participant_mask);
    CHECK(pending.supported_groups == 15U && pending.action == TEST_ACTION);
    save_and_reload(whole, restored, path);
    CHECK(stats(restored).pending == 1U && restored->choice.decision.supported_groups == 15U);
    same_choice(&pending, &restored->choice.decision);
    const cgai_life_context_feedback measurement = feedback(-1.0);
    CHECK(cgai_life_context_choice_observe(whole, &pending, &measurement) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_choice_observe(restored, &pending, &measurement) == CGAI_LIFE_OK);
    exact_owners(whole, restored);
    const cgai_life_context_choice result = predict(restored, 15U, TEST_FALLBACK);
    CHECK(result.action == TEST_FALLBACK && result.supported_groups == 0U);
    cgai_life_context_destroy(whole);
    cgai_life_context_destroy(restored);
}

static void copy_remainder(FILE *input, FILE *output) {
    unsigned char bytes[512];
    while (!feof(input)) {
        const size_t count = fread(bytes, 1U, sizeof(bytes), input);
        CHECK(!ferror(input));
        CHECK(fwrite(bytes, 1U, count, output) == count);
    }
}

static void copy_through(FILE *input, FILE *output, const char *marker) {
    char line[512];
    while (fgets(line, sizeof(line), input) != NULL) {
        CHECK(fputs(line, output) >= 0);
        if (strncmp(line, marker, strlen(marker)) == 0)
            return;
    }
    CHECK(0);
}

static void malformed_model(const char *source, const char *target) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(target, "wb");
    char previous[128];
    CHECK(input != NULL && output != NULL);
    copy_through(input, output, "MODEL ");
    CHECK(fscanf(input, "%127s", previous) == 1);
    CHECK(fputs("nan", output) >= 0);
    copy_remainder(input, output);
    CHECK(fclose(input) == 0);
    CHECK(fclose(output) == 0);
}

static void truncated_choice(const char *source, const char *target) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(target, "wb");
    CHECK(input != NULL && output != NULL);
    copy_through(input, output, "CHOICE_STATE ");
    CHECK(fclose(input) == 0);
    CHECK(fclose(output) == 0);
}

static void rejected_checkpoint(cgai_life_context *owner, const char *path) {
    const uint64_t before = cgai_life_context_hash(owner);
    cgai_life_events events, after;
    CHECK(cgai_life_context_get_events(owner, &events) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_load(owner, path) == CGAI_LIFE_IO_ERROR);
    CHECK(cgai_life_context_hash(owner) == before);
    CHECK(cgai_life_context_get_events(owner, &after) == CGAI_LIFE_OK);
    CHECK(memcmp(&events, &after, sizeof(events)) == 0);
}

static void checkpoint_rejections(const char *path, const char *bad) {
    cgai_life_context *owner = new_owner(1U);
    const cgai_life_context_choice pending = pending_at_seam(owner);
    CHECK(cgai_life_context_save(owner, path) == CGAI_LIFE_OK);
    malformed_model(path, bad);
    rejected_checkpoint(owner, bad);
    truncated_choice(path, bad);
    rejected_checkpoint(owner, bad);
    const cgai_life_context_feedback measurement = feedback(1.0);
    CHECK(cgai_life_context_choice_observe(owner, &pending, &measurement) == CGAI_LIFE_OK);
    CHECK(cgai_life_context_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    cgai_life_context_destroy(owner);
}

int main(int argc, char **argv) {
    char path[TEST_PATH_BYTES], bad[TEST_PATH_BYTES];
    CHECK(argc == 2);
    path_name(path, argv[1], "test-life-feedback.snapshot");
    path_name(bad, argv[1], "test-life-feedback.bad.snapshot");
    contact_gates();
    negative_utility();
    no_gradient(1U, 1U);
    no_gradient(0U, 0U);
    invalid_choices();
    checkpoint_continuation(path);
    checkpoint_all_groups(path);
    checkpoint_rejections(path, bad);
    CHECK(remove(path) == 0);
    CHECK(remove(bad) == 0);
    puts("Native verified outcome learning, participant isolation and continuation checks passed");
    return 0;
}
