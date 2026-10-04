/** @file life_domain_io.c @brief Canonical complete domain continuation and atomic publication. */
#include "life_domain.h"
#include "life_io.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct domain_wire {
    FILE *file;
    int reading;
    uint64_t hash;
    cgai_life_domain_kind kind;
} domain_wire;

static int word(domain_wire *wire, uint64_t *value) {
    unsigned char bytes[8];
    for (size_t i = 0U; i < 8U; ++i)
        bytes[i] = (unsigned char)(*value >> (8U * i));
    if (wire->file != NULL && (wire->reading ? fread(bytes, 1U, 8U, wire->file)
                                             : fwrite(bytes, 1U, 8U, wire->file)) != 8U)
        return 0;
    if (wire->reading) {
        *value = 0U;
        for (size_t i = 0U; i < 8U; ++i)
            *value |= (uint64_t)bytes[i] << (8U * i);
    }
    wire->hash = life_domain_hash_word(wire->hash, *value);
    return 1;
}

static int small(domain_wire *wire, uint32_t *value) {
    uint64_t encoded = *value;
    if (!word(wire, &encoded) || encoded > UINT32_MAX)
        return 0;
    if (wire->reading)
        *value = (uint32_t)encoded;
    return 1;
}

static int real(domain_wire *wire, double *value) {
    uint64_t encoded;
    memcpy(&encoded, value, sizeof(encoded));
    if (!word(wire, &encoded))
        return 0;
    if (wire->reading)
        memcpy(value, &encoded, sizeof(encoded));
    return isfinite(*value);
}

static int words(domain_wire *wire, uint64_t *const values[], size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (!word(wire, values[i]))
            return 0;
    return 1;
}

static int smalls(domain_wire *wire, uint32_t *const values[], size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (!small(wire, values[i]))
            return 0;
    return 1;
}

static int small_array(domain_wire *wire, uint32_t *values, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (!small(wire, &values[i]))
            return 0;
    return 1;
}

static int real_array(domain_wire *wire, double *values, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (!real(wire, &values[i]))
            return 0;
    return 1;
}

static int task_extension(domain_wire *wire, cgai_life_domain_task *value) {
    return wire->kind == CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE ||
           (wire->kind == CGAI_LIFE_DOMAIN_NATIVE_NPC && small(wire, &value->feature_count) &&
            small_array(wire, value->features, CGAI_LIFE_DOMAIN_FEATURES) &&
            word(wire, &value->episode_id) && small(wire, &value->tick));
}

static int task(domain_wire *wire, cgai_life_domain_task *value) {
    uint32_t split = (uint32_t)value->split;
    uint32_t *fields[] = {&value->version,
                          &value->family,
                          &split,
                          &value->reviewed,
                          &value->x,
                          &value->y,
                          &value->observed_fields,
                          &value->legal_actions,
                          &value->fallback,
                          &value->eligible_count};
    if (!smalls(wire, fields, sizeof(fields) / sizeof(fields[0])) ||
        !word(wire, &value->source_hash) || split > CGAI_LIFE_CONTEXT_AUDIT ||
        !small_array(wire, value->eligible_uids, CGAI_LIFE_GROUPS))
        return 0;
    if (wire->reading)
        value->split = (cgai_life_context_split)split;
    return task_extension(wire, value);
}

static int contact(domain_wire *wire, cgai_life_collision_event *value) {
    uint32_t outcome = (uint32_t)value->outcome;
    uint32_t *fields[] = {
        &value->generation,           &value->conflict_id,       &value->conflict_age,
        &value->participant_mask,     &value->frontier_count,    &value->selected_output,
        &value->toggle_bits,          &value->legal_output_mask, &outcome,
        &value->teacher_target_valid, &value->teacher_target};
    uint64_t *identities[] = {&value->source_world_hash, &value->result_world_hash,
                              &value->source_policy_version, &value->result_policy_version};
    if (!smalls(wire, fields, sizeof(fields) / sizeof(fields[0])) ||
        outcome > CGAI_LIFE_COLLISION_UNKNOWN ||
        !small_array(wire, value->group_uids, CGAI_LIFE_GROUPS) ||
        !small_array(wire, value->group_ancestry, CGAI_LIFE_GROUPS) ||
        !small_array(wire, value->frontier_cells, CGAI_LIFE_MAX_FRONTIER_CELLS) ||
        !words(wire, identities, sizeof(identities) / sizeof(identities[0])))
        return 0;
    if (wire->reading)
        value->outcome = (cgai_life_collision_outcome)outcome;
    return 1;
}

static int record(domain_wire *wire, cgai_life_domain_record *value) {
    uint32_t admission = (uint32_t)value->admission;
    uint64_t *identities[] = {&value->task_id, &value->context_hash, &value->parent_version,
                              &value->teacher_identity, &value->evidence_hash};
    uint32_t *fields[] = {&value->participant_mask, &value->executed_action, &value->target,
                          &value->verified, &admission};
    if (!words(wire, identities, sizeof(identities) / sizeof(identities[0])) ||
        !task(wire, &value->task) || !contact(wire, &value->contact) ||
        !smalls(wire, fields, sizeof(fields) / sizeof(fields[0])) ||
        admission > CGAI_LIFE_DOMAIN_UNSUPPORTED_TARGET ||
        !small_array(wire, value->participant_uids, CGAI_LIFE_GROUPS) ||
        !small_array(wire, value->proposals, CGAI_LIFE_GROUPS) ||
        !real_array(wire, value->probabilities, CGAI_LIFE_GROUPS))
        return 0;
    if (wire->reading)
        value->admission = (cgai_life_domain_admission)admission;
    return 1;
}

static int profile_ready(domain_wire *wire, cgai_life_domain *owner, uint32_t kind) {
    if (!life_domain_kind_valid((cgai_life_domain_kind)kind) || kind != (uint32_t)wire->kind ||
        kind != (uint32_t)owner->kind)
        return 0;
    if (wire->reading && owner->probe.model == NULL &&
        !life_probe_init_kind(&owner->probe, owner->run.config.seed, owner->run.world.group_count,
                              (cgai_life_domain_kind)kind))
        return 0;
    return owner->probe.model != NULL && owner->probe.kind == owner->kind;
}

static int controls(domain_wire *wire, cgai_life_domain *owner) {
    uint32_t kind = (uint32_t)owner->kind;
    uint32_t *counts[] = {&kind, &owner->queue_count, &owner->replay_count, &owner->replay_cursor};
    if (!smalls(wire, counts, sizeof(counts) / sizeof(counts[0])) ||
        !profile_ready(wire, owner, kind) || owner->queue_count > CGAI_LIFE_DOMAIN_QUEUE ||
        owner->replay_count > CGAI_LIFE_DOMAIN_REPLAY)
        return 0;
    uint64_t *identities[] = {&owner->version,
                              &owner->rounds,
                              &owner->observations,
                              &owner->deferred,
                              &owner->encounter_renewals,
                              &owner->next_task_id,
                              &owner->probe.model->training_step,
                              &owner->probe.model->training_epochs,
                              &owner->probe.model->training_shuffle};
    return words(wire, identities, sizeof(identities) / sizeof(identities[0]));
}

static int probe(domain_wire *wire, life_probe *value) {
    uint64_t parameters = (uint64_t)value->model->parameter_count;
    if (!word(wire, &parameters) || parameters != value->model->parameter_count ||
        !small_array(wire, value->uids, CGAI_LIFE_GROUPS))
        return 0;
    for (size_t i = 0U; i < CGAI_LIFE_GROUPS; ++i)
        if (!word(wire, &value->steps[i]) || !real(wire, &value->mass[i]))
            return 0;
    return real_array(wire, value->model->parameters, value->model->parameter_count) &&
           real_array(wire, value->model->adam_first, value->model->parameter_count) &&
           real_array(wire, value->model->adam_second, value->model->parameter_count);
}

static int word_array(domain_wire *wire, uint64_t *values, size_t count) {
    for (size_t i = 0U; i < count; ++i)
        if (!word(wire, &values[i]))
            return 0;
    return 1;
}

static int boundary(domain_wire *wire, cgai_life_domain_boundary *value) {
    return word(wire, &value->shared_hash) && word(wire, &value->cell_shared_hash) &&
           word_array(wire, value->group_steps, CGAI_LIFE_GROUPS) &&
           word_array(wire, value->group_hashes, CGAI_LIFE_GROUPS) &&
           word_array(wire, value->cell_group_steps, CGAI_LIFE_GROUPS) &&
           word_array(wire, value->cell_group_hashes, CGAI_LIFE_GROUPS);
}

static int retained_inputs(domain_wire *wire, cgai_life_domain *owner) {
    for (size_t i = 0U; i < owner->queue_count; ++i)
        if (!word(wire, &owner->queue[i].id) || !task(wire, &owner->queue[i].task))
            return 0;
    for (size_t i = 0U; i < owner->replay_count; ++i)
        if (!record(wire, &owner->replay[i]))
            return 0;
    return word_array(wire, owner->group_visits, CGAI_LIFE_GROUPS) &&
           word_array(wire, owner->group_deferred, CGAI_LIFE_GROUPS);
}

static int history(domain_wire *wire, cgai_life_domain *owner) {
    if (!retained_inputs(wire, owner))
        return 0;
    if (!small(wire, &owner->round.generation) || !small(wire, &owner->round.count) ||
        owner->round.count > CGAI_LIFE_DOMAIN_ROUND_RECORDS ||
        !word(wire, &owner->round.parent_version) || !word(wire, &owner->round.result_version) ||
        !boundary(wire, &owner->round.before) || !boundary(wire, &owner->round.after))
        return 0;
    for (size_t i = 0U; i < owner->round.count; ++i)
        if (!record(wire, &owner->round.records[i]))
            return 0;
    return 1;
}

static int payload(domain_wire *wire, cgai_life_domain *owner) {
    return controls(wire, owner) && probe(wire, &owner->probe) && history(wire, owner);
}

uint64_t life_domain_payload_hash(const cgai_life_domain *owner) {
    domain_wire wire = {NULL, 0, UINT64_C(14695981039346656037), owner->kind};
    const int okay = payload(&wire, (cgai_life_domain *)owner);
    return okay ? wire.hash : 0U;
}

int life_domain_payload_write(FILE *file, const cgai_life_domain *owner) {
    domain_wire wire = {file, 0, UINT64_C(14695981039346656037), owner->kind};
    return payload(&wire, (cgai_life_domain *)owner);
}

int life_domain_payload_read(FILE *file, cgai_life_domain *owner) {
    domain_wire wire = {file, 1, UINT64_C(14695981039346656037), owner->kind};
    return payload(&wire, owner);
}

static int envelope(FILE *file, int reading, uint64_t *version) {
    uint64_t magic = UINT64_C(0x4c444f4d41494e31);
    domain_wire wire = {file, reading, UINT64_C(14695981039346656037),
                        CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE};
    return word(&wire, &magic) && magic == UINT64_C(0x4c444f4d41494e31) && word(&wire, version) &&
           (*version == 1U || *version == 2U);
}

int life_domain_checkpoint_write(FILE *file, const cgai_life_domain *owner) {
    if (file == NULL || !life_domain_valid(owner))
        return 0;
    uint64_t version = owner->kind == CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE ? 1U : 2U;
    uint64_t hash = cgai_life_domain_hash(owner), end = UINT64_C(0x454e44444f4d4149);
    domain_wire wire = {file, 0, UINT64_C(14695981039346656037), owner->kind};
    return hash != 0U && envelope(file, 0, &version) && life_snapshot_write(file, &owner->run) &&
           life_domain_payload_write(file, owner) && word(&wire, &hash) && word(&wire, &end);
}

static int write_owner(FILE *file, const void *pointer) {
    return life_domain_checkpoint_write(file, (const cgai_life_domain *)pointer);
}

cgai_life_status cgai_life_domain_save(const cgai_life_domain *owner, const char *path) {
    if (owner == NULL || path == NULL || path[0] == '\0')
        return CGAI_LIFE_INVALID_ARGUMENT;
    if (!life_domain_valid(owner))
        return CGAI_LIFE_ENGINE_ERROR;
    return life_checkpoint_publish(path, owner, write_owner) ? CGAI_LIFE_OK : CGAI_LIFE_IO_ERROR;
}

static int read_owner(FILE *file, cgai_life_domain *owner) {
    uint64_t hash = 0U, end = 0U, version = 0U;
    domain_wire wire = {file, 1, UINT64_C(14695981039346656037),
                        CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE};
    if (!envelope(file, 1, &version))
        return 0;
    owner->kind = version == 1U ? CGAI_LIFE_DOMAIN_CATEGORICAL_PROBE : CGAI_LIFE_DOMAIN_NATIVE_NPC;
    if (!life_snapshot_read(file, &owner->run) || !life_domain_payload_read(file, owner) ||
        !word(&wire, &hash) || !word(&wire, &end) || end != UINT64_C(0x454e44444f4d4149) ||
        ferror(file) || !life_domain_valid(owner))
        return 0;
    return hash != 0U && hash == cgai_life_domain_hash(owner);
}

static void replace_owner(cgai_life_domain *owner, cgai_life_domain *candidate) {
    life_run_destroy(&owner->run);
    life_probe_destroy(&owner->probe);
    *owner = *candidate;
    free(candidate);
}

int life_domain_checkpoint_read(FILE *file, cgai_life_domain *owner) {
    if (file == NULL || owner == NULL)
        return 0;
    cgai_life_domain *candidate = calloc(1U, sizeof(*candidate));
    if (candidate == NULL || !read_owner(file, candidate) ||
        (owner->kind != 0 && owner->kind != candidate->kind)) {
        cgai_life_domain_destroy(candidate);
        return 0;
    }
    replace_owner(owner, candidate);
    return 1;
}

cgai_life_status cgai_life_domain_load(cgai_life_domain *owner, const char *path) {
    if (owner == NULL || path == NULL || path[0] == '\0')
        return CGAI_LIFE_INVALID_ARGUMENT;
    FILE *file = fopen(path, "rb");
    cgai_life_domain *candidate = calloc(1U, sizeof(*candidate));
    int okay = file != NULL && candidate != NULL && read_owner(file, candidate) &&
               candidate->kind == owner->kind && fgetc(file) == EOF && !ferror(file);
    if (file != NULL && fclose(file) != 0)
        okay = 0;
    if (!okay) {
        cgai_life_domain_destroy(candidate);
        return CGAI_LIFE_IO_ERROR;
    }
    replace_owner(owner, candidate);
    return CGAI_LIFE_OK;
}
