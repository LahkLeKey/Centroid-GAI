/** @file life_domain_round.c @brief Freeze complete authentic rounds before native verification. */
#include "life_domain.h"
#include <math.h>
#include <string.h>

static unsigned int population(uint32_t mask) {
    unsigned int count = 0U;
    while (mask != 0U) {
        count += mask & 1U;
        mask >>= 1U;
    }
    return count;
}

static uint32_t record_participants(const life_probe *probe, const cgai_life_domain_task *task,
                                    const cgai_life_collision_event *contact,
                                    uint32_t uids[CGAI_LIFE_GROUPS]) {
    const uint32_t eligible = life_probe_uid_mask(probe, task->eligible_uids, task->eligible_count);
    uint32_t mask = 0U;
    for (size_t slot = 0U; slot < CGAI_LIFE_GROUPS; ++slot)
        if ((contact->participant_mask & (1U << slot)) != 0U)
            for (size_t group = 0U; group < probe->model->config.module_count; ++group)
                if (contact->group_uids[slot] == probe->uids[group] &&
                    (eligible & (1U << group)) != 0U) {
                    mask |= 1U << group;
                    uids[group] = probe->uids[group];
                }
    return mask;
}

static int freeze_proposals(const life_probe *probe, cgai_life_domain_record *record) {
    cgai_life_domain_prediction prediction = {0};
    for (size_t group = 0U; group < probe->model->config.module_count; ++group)
        if ((record->participant_mask & (1U << group)) != 0U) {
            if (!life_probe_predict(probe, &record->task, 1U << group, &prediction))
                return 0;
            record->proposals[group] = prediction.action;
            record->probabilities[group] = prediction.probability;
        }
    if (!life_probe_predict(probe, &record->task, record->participant_mask, &prediction))
        return 0;
    record->executed_action = prediction.action;
    return 1;
}

static uint32_t reserved_uids(const cgai_life_domain *owner, uint64_t task_id) {
    uint32_t reserved = 0U;
    for (size_t i = 0U; i < owner->round.count; ++i)
        if (owner->round.records[i].task_id == task_id)
            reserved |= owner->round.records[i].participant_mask;
    return reserved;
}

static int npc_context_reserved(const cgai_life_domain *owner, const cgai_life_domain_task *task) {
    if (owner->kind != CGAI_LIFE_DOMAIN_NATIVE_NPC)
        return 0;
    for (size_t i = 0U; i < owner->round.count; ++i)
        if (owner->round.records[i].task.episode_id == task->episode_id &&
            owner->round.records[i].task.tick == task->tick)
            return 1;
    return 0;
}

static int prepare_record(const cgai_life_domain *owner, uint32_t task,
                          const cgai_life_collision_event *contact,
                          cgai_life_domain_record *record) {
    record->task_id = owner->queue[task].id;
    record->task = owner->queue[task].task;
    record->context_hash = life_probe_task_hash(&record->task);
    record->parent_version = owner->version;
    record->contact = *contact;
    return freeze_proposals(&owner->probe, record);
}

static int freeze_contact(cgai_life_domain *owner, const cgai_life_collision_event *contact) {
    uint32_t used = 0U;
    for (uint32_t task = 0U; task < owner->queue_count && used < CGAI_LIFE_DOMAIN_TASKS_PER_CONTACT;
         ++task) {
        cgai_life_domain_record record = {0};
        record.participant_mask = record_participants(&owner->probe, &owner->queue[task].task,
                                                      contact, record.participant_uids);
        if (population(record.participant_mask) < 2U ||
            (record.participant_mask & reserved_uids(owner, owner->queue[task].id)) != 0U ||
            npc_context_reserved(owner, &owner->queue[task].task))
            continue;
        if (!prepare_record(owner, task, contact, &record))
            return 0;
        owner->round.records[owner->round.count++] = record;
        ++used;
    }
    return 1;
}

static uint64_t contact_evidence(uint64_t hash, const cgai_life_collision_event *contact) {
    const uint64_t fields[] = {
        contact->conflict_age,      contact->participant_mask,     contact->frontier_count,
        contact->selected_output,   contact->toggle_bits,          contact->legal_output_mask,
        (uint64_t)contact->outcome, contact->teacher_target_valid, contact->teacher_target};
    for (size_t i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i)
        hash = life_domain_hash_word(hash, fields[i]);
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i) {
        hash = life_domain_hash_word(hash, contact->group_uids[i]);
        hash = life_domain_hash_word(hash, contact->group_ancestry[i]);
    }
    for (size_t i = 0U; i < CGAI_LIFE_MAX_FRONTIER_CELLS; ++i)
        hash = life_domain_hash_word(hash, contact->frontier_cells[i]);
    return hash;
}

uint64_t life_domain_evidence(const cgai_life_domain_record *record) {
    const uint64_t words[] = {record->task_id,
                              record->context_hash,
                              record->parent_version,
                              record->teacher_identity,
                              record->contact.generation,
                              record->contact.conflict_id,
                              record->contact.source_world_hash,
                              record->contact.result_world_hash,
                              record->contact.source_policy_version,
                              record->contact.result_policy_version,
                              record->participant_mask,
                              record->executed_action,
                              record->target,
                              record->verified,
                              (uint64_t)record->admission};
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0U; i < sizeof(words) / sizeof(words[0]); ++i)
        hash = life_domain_hash_word(hash, words[i]);
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i) {
        uint64_t bits;
        memcpy(&bits, &record->probabilities[i], sizeof(bits));
        hash = life_domain_hash_word(hash, record->participant_uids[i]);
        hash = life_domain_hash_word(hash, record->proposals[i]);
        hash = life_domain_hash_word(hash, bits);
    }
    hash = contact_evidence(hash, &record->contact);
    return hash == 0U ? 1U : hash;
}

static int verify_record(const life_probe *probe, cgai_life_domain_record *record) {
    /* Authority is this versioned native categorical rule, never cellular viability
     * or a caller's verified flag. It runs only after ALL proposals are frozen. */
    if (!life_probe_verify(probe, &record->task, &record->target, &record->teacher_identity))
        return 0;
    record->verified = 1U;
    record->admission = (record->task.legal_actions & (1U << record->target)) != 0U
                            ? CGAI_LIFE_DOMAIN_ADMITTED
                            : CGAI_LIFE_DOMAIN_UNSUPPORTED_TARGET;
    record->evidence_hash = life_domain_evidence(record);
    return 1;
}

static int verify_round(cgai_life_domain *owner) {
    for (size_t record = 0U; record < owner->round.count; ++record)
        if (!verify_record(&owner->probe, &owner->round.records[record]))
            return 0;
    return 1;
}

int life_domain_collect(cgai_life_domain *owner, const cgai_life_events *events) {
    memset(&owner->round, 0, sizeof(owner->round));
    owner->round.generation = events->generation;
    owner->round.parent_version = owner->version;
    if (events->generation_valid != 1U || events->event_count > CGAI_LIFE_MAX_COLLISION_EVENTS)
        return 0;
    for (size_t event = 0U; event < events->event_count; ++event)
        if (!freeze_contact(owner, &events->events[event]))
            return 0;
    return verify_round(owner);
}

static void retain_record(cgai_life_domain *owner, const cgai_life_domain_record *record) {
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        if ((record->participant_mask & (1U << group)) != 0U) {
            ++owner->group_visits[group];
            if (record->admission == CGAI_LIFE_DOMAIN_UNSUPPORTED_TARGET)
                ++owner->group_deferred[group];
        }
    if (record->admission == CGAI_LIFE_DOMAIN_UNSUPPORTED_TARGET) {
        ++owner->deferred;
        return;
    }
    owner->replay[owner->replay_cursor] = *record;
    owner->replay_cursor = (owner->replay_cursor + 1U) % CGAI_LIFE_DOMAIN_REPLAY;
    if (owner->replay_count < CGAI_LIFE_DOMAIN_REPLAY)
        ++owner->replay_count;
    ++owner->observations;
}

static int task_consumed(const cgai_life_domain *owner, uint64_t task_id) {
    for (size_t i = 0U; i < owner->round.count; ++i)
        if (owner->round.records[i].task_id == task_id)
            return 1;
    return 0;
}

static void consume_queue(cgai_life_domain *owner) {
    uint32_t remaining = 0U;
    for (uint32_t i = 0U; i < owner->queue_count; ++i)
        if (!task_consumed(owner, owner->queue[i].id))
            owner->queue[remaining++] = owner->queue[i];
    memset(owner->queue + remaining, 0,
           (CGAI_LIFE_DOMAIN_QUEUE - remaining) * sizeof(owner->queue[0]));
    owner->queue_count = remaining;
}

static int update_replay(cgai_life_domain *owner, uint32_t current) {
    for (uint32_t epoch = 0U; epoch < owner->run.config.training_epochs; ++epoch) {
        for (uint32_t i = 0U; i < owner->replay_count; ++i) {
            const cgai_life_domain_record *record = &owner->replay[i];
            const uint32_t admitted = record->participant_mask & current;
            if (admitted != 0U &&
                !life_probe_update(&owner->probe, &record->task, record->target, admitted))
                return 0;
        }
        if (current != 0U && owner->replay_count != 0U)
            ++owner->probe.model->training_epochs;
    }
    return 1;
}

int life_domain_learn(cgai_life_domain *owner) {
    uint32_t current = 0U;
    const uint64_t before = owner->probe.model->training_step;
    for (size_t i = 0U; i < owner->round.count; ++i) {
        if (owner->round.records[i].admission == CGAI_LIFE_DOMAIN_ADMITTED)
            current |= owner->round.records[i].participant_mask;
        retain_record(owner, &owner->round.records[i]);
    }
    if (!update_replay(owner, current))
        return 0;
    if (owner->probe.model->training_step != before)
        ++owner->version;
    owner->round.result_version = owner->version;
    ++owner->rounds;
    consume_queue(owner);
    return 1;
}
