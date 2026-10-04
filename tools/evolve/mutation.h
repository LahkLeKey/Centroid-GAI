/** @file mutation.h @brief Finite C11 initialization choices scheduled at Life contacts. */
#ifndef CGAI_EVOLVE_MUTATION_H
#define CGAI_EVOLVE_MUTATION_H

#include "centroid_life.h"
#include <stddef.h>
#include <stdint.h>

#define EVOLVE_MUTATION_MAX_SOURCE_BYTES (1024U * 1024U)
#define EVOLVE_MUTATION_SITES 4U
#define EVOLVE_MUTATION_CHOICES 3U
#define EVOLVE_MUTATION_MAX_EDITS 2U
#define EVOLVE_MUTATION_LITERAL_BYTES 5U
#define EVOLVE_MUTATION_PROFILE_ACTIONS 81U
#define EVOLVE_MUTATION_MAX_ALTERNATIVES 4U

typedef enum evolve_mutation_status {
    EVOLVE_MUTATION_OK = 0,
    EVOLVE_MUTATION_NO_EDIT = 1,
    EVOLVE_MUTATION_INVALID = -1,
    EVOLVE_MUTATION_OUT_OF_MEMORY = -2
} evolve_mutation_status;

/** Stable site IDs; each catalog is ordered low, baseline, high. */
typedef enum evolve_mutation_site {
    EVOLVE_MUTATION_EMBEDDINGS = 0,
    EVOLVE_MUTATION_ENCODER = 1,
    EVOLVE_MUTATION_OUTER = 2,
    EVOLVE_MUTATION_INNER = 3
} evolve_mutation_site;

typedef struct evolve_mutation_profile {
    uint32_t choices[EVOLVE_MUTATION_SITES];
} evolve_mutation_profile;

/** Validated source borrows immutable bytes until candidate generation finishes.
 * Checksums are deterministic FNV-1a continuation checks, not cryptographic identity. */
typedef struct evolve_mutation_source {
    const char *bytes;
    size_t size;
    uint64_t checksum;
    evolve_mutation_profile profile;
    size_t literal_offsets[EVOLVE_MUTATION_SITES];
    uint32_t literal_lengths[EVOLVE_MUTATION_SITES];
} evolve_mutation_source;

typedef struct evolve_mutation_edit {
    uint32_t site;
    uint32_t before_choice;
    uint32_t after_choice;
    size_t source_offset;
    size_t candidate_offset;
    char before_literal[EVOLVE_MUTATION_LITERAL_BYTES];
    char after_literal[EVOLVE_MUTATION_LITERAL_BYTES];
} evolve_mutation_edit;

/** Candidate owns bytes, with a trailing NUL excluded from size and checksum.
 * Edits are ordered by source offset and change exactly one catalog literal each. */
typedef struct evolve_mutation_candidate {
    char *bytes;
    size_t size;
    uint64_t source_checksum;
    uint64_t checksum;
    evolve_mutation_profile profile;
    uint32_t edit_count;
    uint32_t domain_frontier_bits; /**< Code site scheduling bits, distinct from world toggles. */
    evolve_mutation_edit edits[EVOLVE_MUTATION_MAX_EDITS];
    uint32_t event_generation;
    uint32_t event_conflict_id;
    uint64_t event_source_world_hash;
} evolve_mutation_candidate;

/** Validate the four uniquely registered statements and finite literal catalogs.
 * Quoted strings, characters, comments and preprocessing directives do not contain
 * mutation sites. Embedded NUL, unfinished quoted/comment text, preprocessing line
 * splices and trigraph escapes are rejected.
 * Registered statement spelling is exact; full C syntax is left to the compiler.
 * Failure leaves output untouched. No allocation or file access occurs. */
evolve_mutation_status evolve_mutation_validate(const char *bytes, size_t size,
                                                evolve_mutation_source *output);

/** Map one committed frontier's one/two toggle cells to distinct registered sites.
 * Mapping uses source checksum, event source world hash, cell indices and toggle order;
 * each chosen replacement differs from its parent's catalog value. The same source
 * and event produce identical bytes and metadata. A stale source or malformed event
 * is rejected. Fallback/no-change events return NO_EDIT without publishing output.
 * Output must be zero-initialized with bytes==NULL; failure and NO_EDIT leave it intact.
 * Parent bytes are never written. Release a successful candidate with destroy. */
evolve_mutation_status evolve_mutation_generate(const evolve_mutation_source *source,
                                                const cgai_life_collision_event *event,
                                                evolve_mutation_candidate *output);

/** Encode all four catalog choices in base three, or UINT32_MAX when invalid. */
uint32_t evolve_mutation_profile_action(const evolve_mutation_profile *profile);
/** Enumerate the two choices at each contact's independently scheduled one/two sites. Every
 * complete action changes precisely those sites, with 2 or 4 distinct alternatives.
 * A fixed hash of authentic source world, generation and conflict chooses one or two
 * distinct canonical frontier cells and then distinct catalog sites. Selection ignores
 * cell-edit outputs, teacher targets and measured domain outcomes; real no-op contacts
 * remain eligible. The deterministic substitution is fallback. No candidate bytes
 * are allocated. An empty frontier returns NO_EDIT; failures preserve caller outputs. */
evolve_mutation_status evolve_mutation_alternatives(
    const evolve_mutation_source *source, const cgai_life_collision_event *event,
    uint32_t actions[EVOLVE_MUTATION_MAX_ALTERNATIVES], size_t *count, uint32_t *fallback);
/** Render a complete profile action admitted by alternatives, preserving all other
 * source bytes and precise edit provenance. Arbitrary catalog/site changes reject.
 * Ownership and failure semantics match generate. */
evolve_mutation_status evolve_mutation_select(const evolve_mutation_source *source,
                                              const cgai_life_collision_event *event,
                                              uint32_t action, evolve_mutation_candidate *output);

/** Release candidate storage and clear its complete metadata; NULL is allowed. */
void evolve_mutation_destroy(evolve_mutation_candidate *candidate);

/** Borrow a catalog spelling, or NULL for an invalid site or choice. */
const char *evolve_mutation_literal(uint32_t site, uint32_t choice);

#endif
