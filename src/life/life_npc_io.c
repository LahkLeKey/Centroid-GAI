/** @file life_npc_io.c @brief Complete native episode and learner stream bundles. */
#include "life_io.h"
#include "life_npc.h"
#include <stdlib.h>
#include <string.h>

static int word(FILE *file, uint64_t *value, int reading) {
    unsigned char bytes[8];
    for (size_t i = 0U; i < 8U; ++i)
        bytes[i] = (unsigned char)(*value >> (8U * i));
    if (reading ? fread(bytes, 1U, 8U, file) != 8U : fwrite(bytes, 1U, 8U, file) != 8U)
        return 0;
    if (reading) {
        *value = 0U;
        for (size_t i = 0U; i < 8U; ++i)
            *value |= (uint64_t)bytes[i] << (8U * i);
    }
    return 1;
}

static int words(FILE *file, uint64_t *values, size_t count, int reading) {
    for (size_t i = 0U; i < count; ++i)
        if (!word(file, &values[i], reading))
            return 0;
    return 1;
}

static int envelope(FILE *file, int reading) {
    uint64_t header[] = {UINT64_C(0x4c4e50434f574e31), 1U, life_npc_recipe_hash()};
    return words(file, header, 3U, reading) && header[0] == UINT64_C(0x4c4e50434f574e31) &&
           header[1] == 1U && header[2] == life_npc_recipe_hash();
}

static void host_fields(const cgai_life_npc *owner, uint64_t fields[10]) {
    uint64_t probability;
    memcpy(&probability, &owner->host_probability, sizeof(probability));
    const uint64_t values[] = {owner->episode_cursor,     owner->decisions,
                               owner->successes,          owner->deaths,
                               owner->timeouts,           owner->world.ticks,
                               life_npc_host_hash(owner), owner->host_parent_version,
                               owner->host_context_hash,  probability};
    memcpy(fields, values, sizeof(values));
}

static int host_actions(FILE *file, cgai_life_npc *owner, int reading) {
    for (size_t i = 0U; i < LIFE_NPC_MAX_TICKS; ++i) {
        uint64_t action = owner->actions[i];
        if (!word(file, &action, reading) || action > 6U)
            return 0;
        if (reading)
            owner->actions[i] = (uint32_t)action;
    }
    return 1;
}

static int apply_host_fields(cgai_life_npc *owner, const uint64_t fields[10]) {
    owner->episode_cursor = fields[0];
    owner->decisions = fields[1];
    owner->successes = fields[2];
    owner->deaths = fields[3];
    owner->timeouts = fields[4];
    owner->host_parent_version = fields[7];
    owner->host_context_hash = fields[8];
    memcpy(&owner->host_probability, &fields[9], sizeof(owner->host_probability));
    return life_npc_owner_reconstruct(owner, (uint32_t)fields[5]) &&
           fields[6] == life_npc_host_hash(owner);
}

static int host(FILE *file, cgai_life_npc *owner, int reading) {
    uint64_t fields[10];
    host_fields(owner, fields);
    if (!words(file, fields, 10U, reading) || fields[5] > LIFE_NPC_MAX_TICKS ||
        !host_actions(file, owner, reading))
        return 0;
    return !reading || apply_host_fields(owner, fields);
}

static int write_owner(FILE *file, const void *pointer) {
    const cgai_life_npc *owner = pointer;
    uint64_t footer[] = {cgai_life_npc_hash(owner), UINT64_C(0x454e444e50434f57)};
    return footer[0] != 0U && envelope(file, 0) && host(file, (cgai_life_npc *)owner, 0) &&
           life_domain_checkpoint_write(file, owner->domain) && words(file, footer, 2U, 0);
}

cgai_life_status cgai_life_npc_save(const cgai_life_npc *owner, const char *path) {
    if (path == NULL || path[0] == '\0' || !life_npc_owner_valid(owner))
        return CGAI_LIFE_INVALID_ARGUMENT;
    return life_checkpoint_publish(path, owner, write_owner) ? CGAI_LIFE_OK : CGAI_LIFE_IO_ERROR;
}

static cgai_life_npc *read_owner(FILE *file) {
    cgai_life_npc *candidate = calloc(1U, sizeof(*candidate));
    uint64_t footer[2] = {0};
    if (candidate == NULL)
        return NULL;
    candidate->domain = calloc(1U, sizeof(*candidate->domain));
    if (candidate->domain != NULL && envelope(file, 1) && host(file, candidate, 1) &&
        life_domain_checkpoint_read(file, candidate->domain) && words(file, footer, 2U, 1) &&
        footer[1] == UINT64_C(0x454e444e50434f57) && fgetc(file) == EOF && !ferror(file) &&
        footer[0] != 0U && footer[0] == cgai_life_npc_hash(candidate))
        return candidate;
    cgai_life_npc_destroy(candidate);
    return NULL;
}

cgai_life_status cgai_life_npc_load(cgai_life_npc *owner, const char *path) {
    if (owner == NULL || path == NULL || path[0] == '\0')
        return CGAI_LIFE_INVALID_ARGUMENT;
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return CGAI_LIFE_IO_ERROR;
    cgai_life_npc *candidate = read_owner(file);
    const int closed = fclose(file) == 0;
    if (candidate == NULL || !closed) {
        cgai_life_npc_destroy(candidate);
        return CGAI_LIFE_IO_ERROR;
    }
    cgai_life_domain_destroy(owner->domain);
    *owner = *candidate;
    free(candidate);
    return CGAI_LIFE_OK;
}
