/** @file life_context.h @brief Private state of collision-gated lexical memory. */
#ifndef CGAI_LIFE_CONTEXT_INTERNAL_H
#define CGAI_LIFE_CONTEXT_INTERNAL_H
#include "centroid_life.h"
#include "life_training.h"

typedef struct life_context_record {
    char source[CGAI_LIFE_CONTEXT_SOURCE_BYTES + 1U];
    char text[CGAI_LIFE_CONTEXT_TEXT_BYTES + 1U];
    uint32_t first_line, last_line, family;
    uint32_t kind, split, reviewed, eligible_mask, learned_mask;
    uint64_t content_hash;
    double centroids[CGAI_LIFE_GROUPS][CGAI_LIFE_CONTEXT_FEATURES];
} life_context_record;

typedef struct life_context_action_model {
    double centroid[CGAI_LIFE_CONTEXT_FEATURES];
    double readout[CGAI_LIFE_CONTEXT_FEATURES];
    uint64_t observations;
    uint64_t steps;
} life_context_action_model;

typedef struct life_context_choice_state {
    life_context_action_model models[CGAI_LIFE_GROUPS][CGAI_LIFE_CONTEXT_ACTIONS];
    uint64_t version, decisions, observations, deferred;
    uint64_t group_steps[CGAI_LIFE_GROUPS];
    uint32_t consumed_generation, consumed_mask;
    uint32_t pending;
    cgai_life_context_choice decision;
    char input[CGAI_LIFE_CONTEXT_TEXT_BYTES + 1U];
    double features[CGAI_LIFE_CONTEXT_FEATURES];
} life_context_choice_state;

struct cgai_life_context {
    life_run run;
    life_context_record *records;
    uint32_t count;
    uint32_t cursors[CGAI_LIFE_GROUPS];
    uint64_t group_updates[CGAI_LIFE_GROUPS];
    uint64_t domain_updates, contact_events;
    cgai_life_events events;
    life_context_choice_state choice;
};

uint64_t life_context_bytes_hash(const void *bytes, size_t size);
uint32_t life_context_group_count(const cgai_life_context *owner);
uint32_t life_context_group_mask(const cgai_life_context *owner);
int life_context_record_valid(const life_context_record *record);
int life_context_state_valid(const cgai_life_context *owner);
/** Fixed shared lexical encoder; never mutated by outcome feedback. */
int life_context_encode(const char *text, double features[CGAI_LIFE_CONTEXT_FEATURES]);
int life_context_choice_valid(const cgai_life_context *owner);
uint64_t life_context_choice_hash(const cgai_life_context *owner);
uint64_t life_context_legacy_hash(const cgai_life_context *owner);
#endif
