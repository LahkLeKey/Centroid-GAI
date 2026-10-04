/** @file test_npc.c @brief Real native host actions, complete restart and immutable evaluation. */
#include "life_npc.h"
#include "test_utils.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, "Native own-history NPC check failed")

static cgai_life_npc *new_owner(uint32_t groups, uint32_t epochs) {
    cgai_life_config config = cgai_life_config_default();
    cgai_life_npc *owner = NULL;
    config.group_count = groups;
    config.training_epochs = epochs;
    config.enable_merges = 0U;
    CHECK(cgai_life_npc_create(&config, &owner) == CGAI_LIFE_OK);
    return owner;
}

static void unguarded_host(cgai_life_npc *owner) {
    uint32_t completed = 0U;
    CHECK(cgai_life_npc_train_step(owner, 1U, &completed) == CGAI_LIFE_OK && completed == 1U);
    CHECK(owner->world.last_action == LIFE_NPC_FALLBACK && owner->world.ticks == 1U);
    CHECK(owner->world.last_outcome == LIFE_NPC_IDLE);
    CHECK(owner->domain->round.count == 1U);
    CHECK(owner->domain->round.records[0].target == LIFE_NPC_WAIT);
    CHECK(owner->host_parent_version == 0U && owner->domain->version == 1U);
    CHECK(life_npc_owner_valid(owner));
}

static void exact_restart(cgai_life_npc *whole, const char *path) {
    cgai_life_npc *resumed = new_owner(2U, 1U);
    CHECK(cgai_life_npc_save(whole, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_npc_load(resumed, path) == CGAI_LIFE_OK);
    CHECK(cgai_life_npc_hash(whole) == cgai_life_npc_hash(resumed));
    for (uint32_t i = 0U; i < 40U; ++i) {
        const cgai_life_status status = cgai_life_npc_train_step(whole, 1U, NULL);
        if (status != CGAI_LIFE_OK)
            fprintf(stderr, "continuation i=%u decisions=%llu tick=%u status=%u\n", i,
                    (unsigned long long)whole->decisions, whole->world.ticks, (unsigned int)status);
        CHECK(status == CGAI_LIFE_OK);
        CHECK(cgai_life_npc_train_step(resumed, 1U, NULL) == CGAI_LIFE_OK);
        CHECK(cgai_life_npc_hash(whole) == cgai_life_npc_hash(resumed));
    }
    CHECK(whole->domain->encounter_renewals != 0U);
    CHECK(cgai_life_npc_save(whole, path) == CGAI_LIFE_OK);
    cgai_life_npc_destroy(resumed);
}

static void frozen_evaluation(cgai_life_npc *owner) {
    cgai_life_npc_evaluation report = {0};
    const uint64_t before = cgai_life_npc_hash(owner);
    CHECK(cgai_life_npc_evaluate(owner, CGAI_LIFE_CONTEXT_DEVELOPMENT, 3U, 1U, &report) ==
          CGAI_LIFE_OK);
    CHECK(report.episodes == 3U && report.successes + report.deaths + report.timeouts == 3U);
    CHECK(report.attempted_illegal == 0U && report.executed_illegal == 0U);
    CHECK(report.source_model_hash == before && cgai_life_npc_hash(owner) == before);
    CHECK(cgai_life_npc_evaluate(owner, CGAI_LIFE_CONTEXT_AUDIT, 1U, 1U, &report) ==
          CGAI_LIFE_INVALID_ARGUMENT);
    CHECK(cgai_life_npc_hash(owner) == before);
}

static void history_guards(cgai_life_npc *owner) {
    const life_npc_memory memory = owner->memory;
    ++owner->memory.cue;
    CHECK(!life_npc_owner_valid(owner));
    owner->memory = memory;
    ++owner->host_context_hash;
    CHECK(!life_npc_owner_valid(owner));
    --owner->host_context_hash;
    if (owner->domain->version > 1U) {
        const uint64_t parent = owner->host_parent_version;
        owner->host_parent_version = 0U;
        CHECK(!life_npc_owner_valid(owner));
        owner->host_parent_version = parent;
    }
    CHECK(life_npc_owner_valid(owner));
}

static void queued_source_guard(cgai_life_npc *owner) {
    CHECK(owner->domain->queue_count != 0U);
    cgai_life_domain_task *task = &owner->domain->queue[owner->domain->queue_count - 1U].task;
    const uint64_t source = task->source_hash;
    CHECK(task->episode_id == owner->episode_cursor + 1U);
    task->source_hash ^= UINT64_C(1);
    CHECK(life_domain_valid(owner->domain) && !life_npc_owner_valid(owner));
    task->source_hash = source;
    CHECK(life_npc_owner_valid(owner));
}

static void terminal_counters(void) {
    cgai_life_npc *owner = new_owner(8U, 0U);
    CHECK(cgai_life_npc_train_step(owner, 64U, NULL) == CGAI_LIFE_OK);
    CHECK(owner->world.terminal == LIFE_NPC_TIMEOUT && owner->timeouts == 1U);
    owner->timeouts = 0U;
    owner->successes = 1U;
    CHECK(!life_npc_owner_valid(owner));
    owner->successes = 0U;
    owner->timeouts = 1U;
    CHECK(cgai_life_npc_train_step(owner, 1U, NULL) == CGAI_LIFE_OK);
    CHECK(owner->episode_cursor == 1U && owner->world.ticks == 1U);
    cgai_life_npc_destroy(owner);
}

static void trailing_rejection(cgai_life_npc *owner, const char *path) {
    const uint64_t before = cgai_life_npc_hash(owner);
    FILE *file = fopen(path, "ab");
    CHECK(file != NULL && fputc(0, file) != EOF && fclose(file) == 0);
    CHECK(cgai_life_npc_load(owner, path) == CGAI_LIFE_IO_ERROR);
    CHECK(cgai_life_npc_hash(owner) == before);
}

int main(int argc, char **argv) {
    char path[4097];
    CHECK(argc == 2);
    CHECK(snprintf(path, sizeof(path), "%s/native-npc-test.bundle", argv[1]) > 0);
    cgai_life_npc *owner = new_owner(8U, 1U);
    unguarded_host(owner);
    exact_restart(owner, path);
    frozen_evaluation(owner);
    history_guards(owner);
    queued_source_guard(owner);
    trailing_rejection(owner, path);
    cgai_life_npc_destroy(owner);
    terminal_counters();
    CHECK(remove(path) == 0);
    return 0;
}
