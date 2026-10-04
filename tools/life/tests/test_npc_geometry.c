/** @file test_npc_geometry.c @brief Neutral initial NPC family geometry, without outcomes. */
#include "life_npc_world.h"
#include "npc_world.h"
#include "test_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) TEST_CHECK(condition, "Native NPC neutral geometry check failed")
#define GEOMETRY_KEYS (2U * 72U * 48U)
#define GEOMETRY_BYTES 94U

/* These exports come from an independently namespaced archived v3 object.
 * This test never invokes any observation, teacher, step or actor evaluation. */
int npc_v3_family_get(npc_split split, uint32_t index, npc_family *family);
int npc_v3_world_init(npc_world *world, const npc_family *family, uint32_t variant);

typedef struct geometry_point {
    uint32_t x, y;
} geometry_point;
typedef struct geometry_endpoint {
    geometry_point item, exit;
} geometry_endpoint;
typedef struct geometry_map {
    uint32_t cells[81];
    geometry_point actor, junction;
    geometry_endpoint endpoints[2];
    uint32_t endpoint_count;
} geometry_map;
typedef struct geometry_key {
    unsigned char bytes[GEOMETRY_BYTES];
    uint32_t family, variant, archived;
} geometry_key;

static geometry_point transform_point(geometry_point point, uint32_t symmetry) {
    if (symmetry >= 4U)
        point.x = 8U - point.x;
    for (uint32_t rotation = 0U; rotation < symmetry % 4U; ++rotation) {
        const uint32_t next_x = 8U - point.y;
        point.y = point.x;
        point.x = next_x;
    }
    return point;
}

static geometry_point translation_origin(const geometry_map *map, uint32_t symmetry) {
    geometry_point origin = {8U, 8U};
    for (uint32_t cell = 0U; cell < 81U; ++cell) {
        if (map->cells[cell] == LIFE_NPC_WALL)
            continue;
        const geometry_point point =
            transform_point((geometry_point){cell % 9U, cell / 9U}, symmetry);
        if (point.x < origin.x)
            origin.x = point.x;
        if (point.y < origin.y)
            origin.y = point.y;
    }
    return origin;
}

static void encode_terrain(const geometry_map *map, uint32_t symmetry, geometry_point origin,
                           unsigned char *key) {
    for (uint32_t cell = 0U; cell < 81U; ++cell) {
        uint32_t tile = map->cells[cell];
        if (tile == LIFE_NPC_WALL)
            continue;
        const geometry_point point =
            transform_point((geometry_point){cell % 9U, cell / 9U}, symmetry);
        if (tile == LIFE_NPC_ITEM_TILE || tile == LIFE_NPC_EXIT_TILE)
            tile = LIFE_NPC_FLOOR;
        CHECK(point.x >= origin.x && point.y >= origin.y);
        key[(point.y - origin.y) * 9U + point.x - origin.x] = (unsigned char)('0' + tile);
    }
}

static void encode_point(unsigned char *key, geometry_point point, uint32_t symmetry,
                         geometry_point origin) {
    point = transform_point(point, symmetry);
    CHECK(point.x >= origin.x && point.y >= origin.y);
    key[0] = (unsigned char)(point.x - origin.x);
    key[1] = (unsigned char)(point.y - origin.y);
}

static void encode_endpoints(const geometry_map *map, uint32_t symmetry, geometry_point origin,
                             unsigned char *key) {
    for (uint32_t endpoint = 0U; endpoint < map->endpoint_count; ++endpoint) {
        encode_point(key + endpoint * 4U, map->endpoints[endpoint].item, symmetry, origin);
        encode_point(key + endpoint * 4U + 2U, map->endpoints[endpoint].exit, symmetry, origin);
    }
    if (map->endpoint_count == 2U && memcmp(key, key + 4U, 4U) > 0) {
        unsigned char temporary[4];
        memcpy(temporary, key, sizeof(temporary));
        memcpy(key, key + 4U, sizeof(temporary));
        memcpy(key + 4U, temporary, sizeof(temporary));
    }
}

static void geometry_trial(const geometry_map *map, uint32_t symmetry,
                           unsigned char key[GEOMETRY_BYTES]) {
    const geometry_point origin = translation_origin(map, symmetry);
    memset(key, 0, GEOMETRY_BYTES);
    memset(key, '1', 81U);
    encode_terrain(map, symmetry, origin, key);
    encode_point(key + 81U, map->actor, symmetry, origin);
    if (map->endpoint_count == 2U)
        encode_point(key + 83U, map->junction, symmetry, origin);
    encode_endpoints(map, symmetry, origin, key + 85U);
    key[93] = (unsigned char)map->endpoint_count;
}

static void canonical_geometry(const geometry_map *map, unsigned char key[GEOMETRY_BYTES]) {
    geometry_trial(map, 0U, key);
    for (uint32_t symmetry = 1U; symmetry < 8U; ++symmetry) {
        unsigned char trial[GEOMETRY_BYTES];
        geometry_trial(map, symmetry, trial);
        if (memcmp(trial, key, GEOMETRY_BYTES) < 0)
            memcpy(key, trial, GEOMETRY_BYTES);
    }
}

static geometry_endpoint native_endpoint(const life_npc_world *world) {
    return (geometry_endpoint){{world->item_x, world->item_y}, {world->exit_x, world->exit_y}};
}

static geometry_endpoint archived_endpoint(const npc_world *world) {
    return (geometry_endpoint){{world->item_x, world->item_y}, {world->exit_x, world->exit_y}};
}

static void native_geometry(uint32_t family_id, uint32_t variant, geometry_map *map) {
    life_npc_family family;
    life_npc_world world;
    CHECK(life_npc_family_get((life_npc_split)(family_id / 24U), family_id % 24U, &family));
    CHECK(life_npc_world_init(&world, &family, variant));
    memset(map, 0, sizeof(*map));
    memcpy(map->cells, world.cells, sizeof(map->cells));
    map->actor = (geometry_point){world.x, world.y};
    map->junction = (geometry_point){world.junction_x, world.junction_y};
    map->endpoints[0] = native_endpoint(&world);
    map->endpoint_count = 1U;
    if (family.mechanic == LIFE_NPC_CUE) {
        CHECK(life_npc_world_init(&world, &family, variant ^ 4U));
        map->endpoints[1] = native_endpoint(&world);
        map->endpoint_count = 2U;
    }
}

static void archived_geometry(uint32_t family_id, uint32_t variant, geometry_map *map) {
    npc_family family;
    npc_world world;
    CHECK(npc_v3_family_get((npc_split)(family_id / 24U), family_id % 24U, &family));
    CHECK(npc_v3_world_init(&world, &family, variant));
    memset(map, 0, sizeof(*map));
    memcpy(map->cells, world.cells, sizeof(map->cells));
    map->actor = (geometry_point){world.x, world.y};
    map->junction = (geometry_point){world.junction_x, world.junction_y};
    map->endpoints[0] = archived_endpoint(&world);
    map->endpoint_count = 1U;
    if (family.mechanic == NPC_CUE) {
        CHECK(npc_v3_world_init(&world, &family, variant ^ 4U));
        map->endpoints[1] = archived_endpoint(&world);
        map->endpoint_count = 2U;
    }
}

static void collect_family(geometry_key *keys, uint32_t archived, uint32_t family) {
    for (uint32_t variant = 0U; variant < 48U; ++variant) {
        geometry_map map;
        geometry_key *key = keys + family * 48U + variant;
        if (archived != 0U)
            archived_geometry(family, variant, &map);
        else
            native_geometry(family, variant, &map);
        canonical_geometry(&map, key->bytes);
        key->family = family;
        key->variant = variant;
        key->archived = archived;
    }
}

static int compare_keys(const void *left, const void *right) {
    return memcmp(((const geometry_key *)left)->bytes, ((const geometry_key *)right)->bytes,
                  GEOMETRY_BYTES);
}

static void verify_pair(const geometry_key *first, const geometry_key *second) {
    if (first->archived != 0U && second->archived != 0U)
        return;
    if (first->archived == second->archived && first->family == second->family)
        return;
    fprintf(stderr, "Neutral alias: %s family%u variant%u / %s family%u variant%u\n",
            first->archived != 0U ? "archived-v3" : "native", first->family, first->variant,
            second->archived != 0U ? "archived-v3" : "native", second->family, second->variant);
    CHECK(0);
}

static void verify_keys(geometry_key *keys) {
    qsort(keys, GEOMETRY_KEYS, sizeof(*keys), compare_keys);
    for (size_t index = 1U; index < GEOMETRY_KEYS; ++index)
        if (compare_keys(keys + index - 1U, keys + index) == 0)
            verify_pair(keys + index - 1U, keys + index);
}

int main(void) {
    geometry_key *keys = calloc(GEOMETRY_KEYS, sizeof(*keys));
    CHECK(keys != NULL);
    for (uint32_t family = 0U; family < 72U; ++family) {
        collect_family(keys, 0U, family);
        collect_family(keys + 72U * 48U, 1U, family);
    }
    verify_keys(keys);
    free(keys);
    puts("NPC neutral geometry: 6912 initial maps, all symmetries/translations, fresh family "
         "isolation and archived-v3 disjointness pass; no outcomes generated");
    return 0;
}
