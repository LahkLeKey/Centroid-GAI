/** @file life_domain_state.c @brief Semantic UID, event and complete owner validation. */
#include "life_domain.h"
#include <math.h>

static unsigned int population(uint32_t mask) {
    unsigned int count = 0U;
    while (mask != 0U) {
        count += mask & 1U;
        mask >>= 1U;
    }
    return count;
}

static int contact_valid(const cgai_life_domain *owner, const cgai_life_collision_event *event) {
    const uint32_t groups = (uint32_t)owner->probe.model->config.module_count;
    const uint32_t allowed = (1U << groups) - 1U;
    life_patch patch = {0};
    if (event->generation == 0U || event->generation > owner->run.world.tick ||
        event->conflict_id == 0U || event->conflict_age > event->generation ||
        population(event->participant_mask) < 2U || (event->participant_mask & ~allowed) != 0U ||
        event->frontier_count > CGAI_LIFE_MAX_FRONTIER_CELLS ||
        event->selected_output >= LIFE_OUTPUTS || event->teacher_target_valid != 1U ||
        event->teacher_target >= LIFE_OUTPUTS ||
        (unsigned int)event->outcome > CGAI_LIFE_COLLISION_UNKNOWN ||
        event->source_world_hash == 0U || event->result_world_hash == 0U ||
        event->source_policy_version > event->result_policy_version)
        return 0;
    patch.cell_count = (uint8_t)event->frontier_count;
    return event->legal_output_mask == life_allowed_outputs(&patch) &&
           event->toggle_bits == life_output_bits(event->selected_output) &&
           (event->legal_output_mask & (1U << event->selected_output)) != 0U &&
           (event->legal_output_mask & (1U << event->teacher_target)) != 0U;
}

static int contact_slot_valid(const cgai_life_domain *owner, const cgai_life_collision_event *event,
                              size_t slot) {
    if ((event->participant_mask & (1U << slot)) == 0U)
        return event->group_uids[slot] == 0U && event->group_ancestry[slot] == 0U;
    if (life_probe_uid_mask(&owner->probe, &event->group_uids[slot], 1U) == 0U ||
        event->group_ancestry[slot] == 0U ||
        event->group_ancestry[slot] >= (1U << owner->probe.model->config.module_count))
        return 0;
    for (size_t previous = 0U; previous < slot; ++previous)
        if (event->group_uids[slot] == event->group_uids[previous])
            return 0;
    return 1;
}

static int contact_slots_valid(const cgai_life_domain *owner,
                               const cgai_life_collision_event *event) {
    for (size_t slot = 0U; slot < CGAI_LIFE_GROUPS; ++slot)
        if (!contact_slot_valid(owner, event, slot))
            return 0;
    for (size_t i = 0U; i < CGAI_LIFE_MAX_FRONTIER_CELLS; ++i)
        if ((i >= event->frontier_count && event->frontier_cells[i] != 0U) ||
            event->frontier_cells[i] >= LIFE_CELLS ||
            (i != 0U && i < event->frontier_count &&
             event->frontier_cells[i] <= event->frontier_cells[i - 1U]))
            return 0;
    return 1;
}

static uint32_t expected_participants(const cgai_life_domain *owner,
                                      const cgai_life_domain_record *record) {
    uint32_t expected = 0U;
    const uint32_t eligible =
        life_probe_uid_mask(&owner->probe, record->task.eligible_uids, record->task.eligible_count);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group) {
        for (size_t slot = 0U; slot < CGAI_LIFE_GROUPS; ++slot)
            if (owner->probe.uids[group] != 0U &&
                owner->probe.uids[group] == record->contact.group_uids[slot] &&
                (eligible & (1U << group)) != 0U)
                expected |= 1U << group;
    }
    return expected;
}

static int proposal_group_valid(const cgai_life_domain *owner,
                                const cgai_life_domain_record *record, size_t group) {
    if ((record->participant_mask & (1U << group)) == 0U)
        return record->participant_uids[group] == 0U && record->proposals[group] == 0U &&
               record->probabilities[group] == 0.0;
    return record->participant_uids[group] == owner->probe.uids[group] &&
           record->proposals[group] < life_probe_output_count(&owner->probe) &&
           (record->task.legal_actions & (1U << record->proposals[group])) != 0U &&
           isfinite(record->probabilities[group]) && record->probabilities[group] >= 0.0 &&
           record->probabilities[group] <= 1.0;
}

static int proposals_valid(const cgai_life_domain *owner, const cgai_life_domain_record *record) {
    const uint32_t expected = expected_participants(owner, record);
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        if (!proposal_group_valid(owner, record, group))
            return 0;
    return population(expected) >= 2U && expected == record->participant_mask;
}

int life_domain_record_valid(const cgai_life_domain *owner, const cgai_life_domain_record *record) {
    uint32_t target = 0U;
    uint64_t teacher = 0U;
    if (!life_probe_task_valid(&owner->probe, &record->task, 1) || record->task_id == 0U ||
        record->task_id >= owner->next_task_id ||
        record->context_hash != life_probe_task_hash(&record->task) ||
        record->parent_version > owner->version ||
        !life_probe_verify(&owner->probe, &record->task, &target, &teacher) ||
        record->teacher_identity != teacher || record->target != target || record->verified != 1U ||
        record->executed_action >= life_probe_output_count(&owner->probe) ||
        (record->task.legal_actions & (1U << record->executed_action)) == 0U ||
        record->evidence_hash != life_domain_evidence(record) ||
        !contact_valid(owner, &record->contact) || !contact_slots_valid(owner, &record->contact) ||
        !proposals_valid(owner, record))
        return 0;
    const int supported = (record->task.legal_actions & (1U << record->target)) != 0U;
    return record->admission ==
           (supported ? CGAI_LIFE_DOMAIN_ADMITTED : CGAI_LIFE_DOMAIN_UNSUPPORTED_TARGET);
}

static int queue_item_valid(const cgai_life_domain *owner, size_t index) {
    if (owner->queue[index].id == 0U || owner->queue[index].id >= owner->next_task_id ||
        !life_probe_task_valid(&owner->probe, &owner->queue[index].task, 1) ||
        (index != 0U && owner->queue[index].id <= owner->queue[index - 1U].id))
        return 0;
    for (size_t previous = 0U; previous < index; ++previous)
        if (life_probe_task_hash(&owner->queue[index].task) ==
                life_probe_task_hash(&owner->queue[previous].task) ||
            (owner->kind == CGAI_LIFE_DOMAIN_NATIVE_NPC &&
             owner->queue[index].task.episode_id == owner->queue[previous].task.episode_id &&
             owner->queue[index].task.tick == owner->queue[previous].task.tick))
            return 0;
    return 1;
}

static int queue_valid(const cgai_life_domain *owner) {
    for (size_t i = 0U; i < owner->queue_count; ++i) {
        if (!queue_item_valid(owner, i))
            return 0;
    }
    for (size_t i = owner->queue_count; i < CGAI_LIFE_DOMAIN_QUEUE; ++i)
        if (owner->queue[i].id != 0U)
            return 0;
    return 1;
}

static uint32_t admitted_participants(const cgai_life_domain *owner) {
    uint32_t mask = 0U;
    for (size_t i = 0U; i < owner->round.count; ++i)
        if (owner->round.records[i].admission == CGAI_LIFE_DOMAIN_ADMITTED)
            mask |= owner->round.records[i].participant_mask;
    return mask;
}

static int boundary_valid(const cgai_life_domain *owner) {
    if (owner->rounds == 0U)
        return owner->round.generation == 0U && owner->round.count == 0U;
    cgai_life_domain_boundary current;
    const cgai_life_domain_boundary *before = &owner->round.before, *after = &owner->round.after;
    const uint32_t admitted = admitted_participants(owner);
    if (!life_domain_boundary_capture(owner, &current) ||
        before->shared_hash != after->shared_hash ||
        before->cell_shared_hash != after->cell_shared_hash ||
        current.shared_hash != after->shared_hash ||
        current.cell_shared_hash != after->cell_shared_hash)
        return 0;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i)
        if (before->group_steps[i] > after->group_steps[i] ||
            before->cell_group_steps[i] > after->cell_group_steps[i] ||
            current.group_steps[i] != after->group_steps[i] ||
            current.group_hashes[i] != after->group_hashes[i] ||
            current.cell_group_steps[i] != after->cell_group_steps[i] ||
            current.cell_group_hashes[i] != after->cell_group_hashes[i] ||
            ((admitted & (1U << i)) == 0U && before->group_steps[i] != after->group_steps[i]) ||
            (before->group_steps[i] == after->group_steps[i] &&
             before->group_hashes[i] != after->group_hashes[i]) ||
            (before->cell_group_steps[i] == after->cell_group_steps[i] &&
             before->cell_group_hashes[i] != after->cell_group_hashes[i]))
            return 0;
    return 1;
}

static int round_record_valid(const cgai_life_domain *owner, size_t index) {
    const cgai_life_domain_record *record = &owner->round.records[index];
    if (record->contact.generation != owner->round.generation ||
        record->parent_version != owner->round.parent_version ||
        !life_domain_record_valid(owner, record))
        return 0;
    for (size_t previous = 0U; previous < index; ++previous)
        if ((record->task_id == owner->round.records[previous].task_id &&
             (record->participant_mask & owner->round.records[previous].participant_mask) != 0U) ||
            (owner->kind == CGAI_LIFE_DOMAIN_NATIVE_NPC &&
             record->task.episode_id == owner->round.records[previous].task.episode_id &&
             record->task.tick == owner->round.records[previous].task.tick))
            return 0;
    return 1;
}

static int rounds_valid(const cgai_life_domain *owner) {
    if (owner->round.count > CGAI_LIFE_DOMAIN_ROUND_RECORDS ||
        owner->round.generation > owner->run.world.tick ||
        owner->round.parent_version > owner->round.result_version ||
        owner->round.result_version != owner->version)
        return 0;
    for (size_t i = 0U; i < owner->round.count; ++i)
        if (!round_record_valid(owner, i))
            return 0;
    for (size_t i = 0U; i < owner->replay_count; ++i)
        if (!life_domain_record_valid(owner, &owner->replay[i]) ||
            owner->replay[i].admission != CGAI_LIFE_DOMAIN_ADMITTED)
            return 0;
    return boundary_valid(owner);
}

static int bindings_valid(const cgai_life_domain *owner) {
    const uint32_t groups = (uint32_t)owner->probe.model->config.module_count;
    if (groups != owner->run.world.group_count ||
        groups != life_policy_group_count(owner->run.policy) ||
        owner->run.config.group_count != groups || groups < CGAI_LIFE_MIN_GROUPS ||
        groups > CGAI_LIFE_GROUPS)
        return 0;
    uint32_t found = 0U;
    for (size_t i = 0U; i < groups; ++i) {
        const uint32_t bit =
            life_probe_uid_mask(&owner->probe, &owner->run.world.entities[i].uid, 1U);
        if (bit == 0U || (found & bit) != 0U)
            return 0;
        found |= bit;
    }
    return 1;
}

static int retained_visits_valid(const cgai_life_domain *owner, size_t group) {
    uint64_t admitted = 0U, deferred = 0U;
    for (size_t i = 0U; i < owner->replay_count; ++i)
        if ((owner->replay[i].participant_mask & (1U << group)) != 0U)
            ++admitted;
    for (size_t i = 0U; i < owner->round.count; ++i)
        if (owner->round.records[i].admission == CGAI_LIFE_DOMAIN_UNSUPPORTED_TARGET &&
            (owner->round.records[i].participant_mask & (1U << group)) != 0U)
            ++deferred;
    return owner->group_deferred[group] >= deferred &&
           owner->group_visits[group] >= admitted + deferred;
}

static int visits_valid(const cgai_life_domain *owner) {
    if (owner->encounter_renewals > owner->rounds ||
        owner->encounter_renewals > owner->run.world.tick / 32U)
        return 0;
    for (size_t group = 0U; group < CGAI_LIFE_GROUPS; ++group)
        if (!retained_visits_valid(owner, group) ||
            owner->group_deferred[group] > owner->deferred ||
            owner->group_deferred[group] > owner->group_visits[group] ||
            owner->group_visits[group] > owner->observations + owner->deferred ||
            (group >= owner->run.world.group_count && owner->group_visits[group] != 0U))
            return 0;
    return 1;
}

int life_domain_valid(const cgai_life_domain *owner) {
    if (owner == NULL || !life_domain_kind_valid(owner->kind) || owner->kind != owner->probe.kind ||
        owner->run.policy == NULL || owner->run.config.mode != LIFE_MODE_LEARNED ||
        owner->run.config.enable_merges != 0 || owner->run.config.training_epochs > 64U ||
        owner->run.config.scenario > 3U || owner->run.config.seed > UINT32_MAX ||
        owner->queue_count > CGAI_LIFE_DOMAIN_QUEUE ||
        owner->replay_count > CGAI_LIFE_DOMAIN_REPLAY ||
        owner->replay_cursor >= CGAI_LIFE_DOMAIN_REPLAY || owner->next_task_id == 0U ||
        owner->rounds > owner->run.world.tick || owner->version > owner->rounds ||
        owner->observations > owner->rounds * CGAI_LIFE_DOMAIN_ROUND_RECORDS ||
        owner->deferred > owner->rounds * CGAI_LIFE_DOMAIN_ROUND_RECORDS - owner->observations ||
        owner->replay_count != (owner->observations < CGAI_LIFE_DOMAIN_REPLAY
                                    ? owner->observations
                                    : CGAI_LIFE_DOMAIN_REPLAY) ||
        owner->replay_cursor != owner->observations % CGAI_LIFE_DOMAIN_REPLAY ||
        !life_probe_valid(&owner->probe) || !bindings_valid(owner))
        return 0;
    if (owner->version > owner->probe.model->training_step ||
        (owner->version == 0U) != (owner->probe.model->training_step == 0U) ||
        owner->probe.model->training_step >
            owner->rounds * CGAI_LIFE_DOMAIN_REPLAY * owner->run.config.training_epochs ||
        owner->probe.model->training_epochs > owner->rounds * owner->run.config.training_epochs ||
        (owner->run.config.training_epochs == 0U &&
         (owner->version != 0U || owner->probe.model->training_step != 0U)))
        return 0;
    return queue_valid(owner) && rounds_valid(owner) && visits_valid(owner);
}
