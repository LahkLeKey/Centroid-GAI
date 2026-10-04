/** @file evolve_checkpoint_codec.c @brief Little-endian typed state without native padding. */
#include "evolve_checkpoint_codec.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct checkpoint_wire {
    FILE *file;
    int reading;
    uint64_t hash;
} checkpoint_wire;

static int bytes(checkpoint_wire *wire, void *data, size_t count) {
    unsigned char *values = data;
    const size_t used = wire->file == NULL ? count
                        : wire->reading    ? fread(data, 1U, count, wire->file)
                                           : fwrite(data, 1U, count, wire->file);
    if (used != count)
        return 0;
    for (size_t i = 0U; i < count; ++i)
        wire->hash = (wire->hash ^ values[i]) * UINT64_C(1099511628211);
    return 1;
}

static int word(checkpoint_wire *wire, uint64_t *value) {
    unsigned char encoded[8];
    for (size_t i = 0U; i < 8U; ++i)
        encoded[i] = (unsigned char)(*value >> (8U * i));
    if (!bytes(wire, encoded, sizeof(encoded)))
        return 0;
    if (wire->reading) {
        *value = 0U;
        for (size_t i = 0U; i < 8U; ++i)
            *value |= (uint64_t)encoded[i] << (8U * i);
    }
    return 1;
}

static int small(checkpoint_wire *wire, uint32_t *value) {
    uint64_t encoded = *value;
    if (!word(wire, &encoded) || encoded > UINT32_MAX)
        return 0;
    *value = (uint32_t)encoded;
    return 1;
}

static int scalar(checkpoint_wire *wire, double *value) {
    uint64_t encoded;
    _Static_assert(sizeof(double) == sizeof(encoded), "Checkpoint requires 64-bit doubles");
    memcpy(&encoded, value, sizeof(encoded));
    if (!word(wire, &encoded))
        return 0;
    memcpy(value, &encoded, sizeof(encoded));
    return isfinite(*value);
}

static int words(checkpoint_wire *wire, uint64_t **fields, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (!word(wire, fields[i]))
            return 0;
    return 1;
}

static int smalls(checkpoint_wire *wire, uint32_t **fields, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (!small(wire, fields[i]))
            return 0;
    return 1;
}

static int text(checkpoint_wire *wire, char *value, size_t capacity) {
    const char *end = wire->reading ? value : memchr(value, '\0', capacity);
    if (end == NULL)
        return 0;
    uint64_t count = wire->reading ? 0U : (uint64_t)(end - value);
    if (!word(wire, &count) || count >= capacity || !bytes(wire, value, (size_t)count) ||
        memchr(value, '\0', (size_t)count) != NULL)
        return 0;
    value[count] = '\0';
    return 1;
}

static int report(checkpoint_wire *wire, life_fitness_report *value) {
    uint32_t split = (uint32_t)value->split;
    uint32_t *controls[] = {&value->version,
                            &split,
                            &value->complete,
                            &value->pack_records,
                            &value->model_count,
                            &value->training_epochs,
                            &value->training_generations};
    uint64_t *counts[] = {&value->pack_hash,        &value->records,        &value->correct,
                          &value->training_updates, &value->forward_passes, &value->active_modules,
                          &value->active_centroids};
    const int okay = smalls(wire, controls, sizeof(controls) / sizeof(controls[0])) &&
                     words(wire, counts, sizeof(counts) / sizeof(counts[0])) &&
                     scalar(wire, &value->mean_loss);
    value->split = (life_fitness_split)split;
    return okay;
}

static int choice(checkpoint_wire *wire, cgai_life_context_choice *value) {
    uint64_t *identities[] = {&value->token, &value->input_hash, &value->model_version,
                              &value->world_hash};
    uint32_t *controls[] = {&value->generation, &value->conflict_id, &value->participant_mask,
                            &value->action, &value->supported_groups};
    return words(wire, identities, sizeof(identities) / sizeof(identities[0])) &&
           smalls(wire, controls, sizeof(controls) / sizeof(controls[0])) &&
           scalar(wire, &value->predicted_utility);
}

static int stats(checkpoint_wire *wire, cgai_life_context_choice_stats *value) {
    uint64_t *counts[] = {&value->version, &value->decisions, &value->observations,
                          &value->deferred};
    if (!small(wire, &value->group_count) || value->group_count < CGAI_LIFE_MIN_GROUPS ||
        value->group_count > CGAI_LIFE_GROUPS ||
        !words(wire, counts, sizeof(counts) / sizeof(counts[0])) || !small(wire, &value->pending))
        return 0;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i)
        if (!word(wire, &value->group_steps[i]) || !word(wire, &value->group_hashes[i]))
            return 0;
    return 1;
}

static int event_controls(checkpoint_wire *wire, cgai_life_collision_event *value) {
    uint32_t outcome = (uint32_t)value->outcome;
    uint32_t *fields[] = {
        &value->generation,           &value->conflict_id,       &value->conflict_age,
        &value->participant_mask,     &value->frontier_count,    &value->selected_output,
        &value->toggle_bits,          &value->legal_output_mask, &outcome,
        &value->teacher_target_valid, &value->teacher_target};
    const int okay = smalls(wire, fields, sizeof(fields) / sizeof(fields[0]));
    value->outcome = (cgai_life_collision_outcome)outcome;
    return okay;
}

static int event(checkpoint_wire *wire, cgai_life_collision_event *value) {
    uint64_t *identities[] = {&value->source_world_hash, &value->result_world_hash,
                              &value->source_policy_version, &value->result_policy_version};
    if (!event_controls(wire, value) ||
        !words(wire, identities, sizeof(identities) / sizeof(identities[0])))
        return 0;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i)
        if (!small(wire, &value->group_uids[i]) || !small(wire, &value->group_ancestry[i]))
            return 0;
    for (size_t i = 0U; i < CGAI_LIFE_MAX_FRONTIER_CELLS; ++i)
        if (!small(wire, &value->frontier_cells[i]))
            return 0;
    return 1;
}

static int edit(checkpoint_wire *wire, evolve_mutation_edit *value) {
    uint64_t source = value->source_offset, candidate = value->candidate_offset;
    uint32_t *controls[] = {&value->site, &value->before_choice, &value->after_choice};
    if (!smalls(wire, controls, sizeof(controls) / sizeof(controls[0])) || !word(wire, &source) ||
        !word(wire, &candidate) || source > SIZE_MAX || candidate > SIZE_MAX ||
        !text(wire, value->before_literal, sizeof(value->before_literal)) ||
        !text(wire, value->after_literal, sizeof(value->after_literal)))
        return 0;
    value->source_offset = (size_t)source;
    value->candidate_offset = (size_t)candidate;
    return 1;
}

static int receipt_controls(checkpoint_wire *wire, evolve_checkpoint_receipt *value) {
    uint64_t *identities[] = {
        &value->token,           &value->input_hash,         &value->evidence_hash,
        &value->parent_checksum, &value->candidate_checksum, &value->version_before,
        &value->version_after};
    uint32_t *controls[] = {&value->generation,
                            &value->action,
                            &value->participant_mask,
                            &value->accepted,
                            &value->verified,
                            &value->deferred,
                            &value->proof_bytes,
                            &value->action_count,
                            &value->fallback,
                            &value->audit_status,
                            &value->domain_frontier_bits,
                            &value->edit_count};
    return words(wire, identities, sizeof(identities) / sizeof(identities[0])) &&
           smalls(wire, controls, sizeof(controls) / sizeof(controls[0])) &&
           scalar(wire, &value->utility) && scalar(wire, &value->parent_training_loss);
}

static int receipt(checkpoint_wire *wire, evolve_checkpoint_receipt *value) {
    if (!receipt_controls(wire, value) || !text(wire, value->input, sizeof(value->input)) ||
        !text(wire, value->proof, sizeof(value->proof)) || !event(wire, &value->event) ||
        !choice(wire, &value->choice) || !choice(wire, &value->prediction_after) ||
        !stats(wire, &value->before) || !stats(wire, &value->after))
        return 0;
    for (size_t i = 0U; i < EVOLVE_MUTATION_MAX_ALTERNATIVES; ++i)
        if (!small(wire, &value->actions[i]))
            return 0;
    for (size_t i = 0U; i < EVOLVE_MUTATION_MAX_EDITS; ++i)
        if (!edit(wire, &value->edits[i]))
            return 0;
    return report(wire, &value->training) && report(wire, &value->development) &&
           report(wire, &value->confirmation);
}

static int state_controls(checkpoint_wire *wire, evolve_run *run) {
    uint64_t *identities[] = {&run->inputs_checksum,         &run->external_checksum,
                              &run->source_context_checksum, &run->llm_context_checksum,
                              &run->initial_memory_checksum, &run->prepared_memory_checksum,
                              &run->initial_parent_checksum, &run->predecessor_hash,
                              &run->build_recipe_checksum};
    uint32_t *counts[] = {
        &run->options.candidates,  &run->options.seed, &run->options.generations,
        &run->proposals,           &run->accepted,     &run->completed_generations,
        &run->prepared_generation, &run->ledger_count, &run->finalized};
    return words(wire, identities, sizeof(identities) / sizeof(identities[0])) &&
           smalls(wire, counts, sizeof(counts) / sizeof(counts[0]));
}

static int state(checkpoint_wire *wire, evolve_run *run) {
    uint64_t version = 2U;
    uint64_t capacity = CGAI_LIFE_GROUPS;
    if (!word(wire, &version) || version != 2U || !word(wire, &capacity) ||
        capacity != CGAI_LIFE_GROUPS || !state_controls(wire, run) ||
        run->ledger_count > EVOLVE_CHECKPOINT_MAX_RECEIPTS || !stats(wire, &run->initial_choices) ||
        !report(wire, &run->baseline_training) || !report(wire, &run->baseline_development) ||
        !report(wire, &run->baseline_confirmation) || !report(wire, &run->training) ||
        !report(wire, &run->development) || !report(wire, &run->confirmation))
        return 0;
    for (size_t i = 0U; i < run->ledger_count; ++i)
        if (!receipt(wire, &run->ledger[i]) || !word(wire, &run->ledger[i].chain_hash))
            return 0;
    return 1;
}

static int state_file(const char *path, evolve_run *run, int reading) {
    FILE *file = fopen(path, reading ? "rb" : "wbx");
    if (file == NULL)
        return 0;
    checkpoint_wire wire = {file, reading, UINT64_C(14695981039346656037)};
    int okay = state(&wire, run);
    if (reading && okay)
        okay = fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0)
        okay = 0;
    return okay;
}

int evolve_checkpoint_state_write(const char *path, const evolve_run *run) {
    evolve_run *copy = malloc(sizeof(*copy));
    if (copy == NULL)
        return 0;
    *copy = *run;
    const int okay = state_file(path, copy, 0);
    free(copy);
    return okay;
}

int evolve_checkpoint_state_read(const char *path, evolve_run *run) {
    return state_file(path, run, 1);
}

uint64_t evolve_checkpoint_receipt_hash(const evolve_checkpoint_receipt *value, uint64_t previous) {
    evolve_checkpoint_receipt copy = *value;
    checkpoint_wire wire = {NULL, 0, previous};
    const int okay = receipt(&wire, &copy);
    return okay ? wire.hash : 0U;
}
