/** @file bark_selector.c @brief Resolve a validated one-pass bark ID to authored game content. */
#include "centroid_gai_bark.h"
#include <stdio.h>
#include <string.h>

/** Example model admission cap, matching the version-one gameplay profile. */
#define GAME_BARK_MODEL_LIMIT 262144U
/** Example requested heap cap for one reusable selection session. */
#define GAME_BARK_SESSION_LIMIT 65536U

/** @brief Find a compact categorical input in an explicit host-owned name table.
 * @param text Borrowed command argument.
 * @param names Borrowed static value names.
 * @param count Number of names.
 * @param value Writable zero-based enum value, unchanged on missing name.
 * @return Nonzero for a known exact name. */
static int game_value(const char *text, const char *const *names, size_t count,
                      unsigned int *value) {
    /* Step 1: Reject arbitrary strings before they can reach the typed runtime contract. */
    for (size_t index = 0U; index < count; ++index)
        if (strcmp(text, names[index]) == 0) {
            *value = (unsigned int)index;
            return 1;
        }
    return 0;
}

/** @brief Prepare four typed observations in the contract's stable input order.
 * @param arguments Borrowed four argument strings in event/danger/relationship/setting order.
 * @param request Writable host request.
 * @return Nonzero for a complete known state. */
static int game_request(char **arguments, cgai_bark_request *request) {
    /* Step 1: The host maps its own events to a small genre-independent observation contract. */
    const char *events[] = {"idle", "greet", "threat", "victory", "discovery", "retreat"};
    const char *dangers[] = {"low", "high"};
    const char *relationships[] = {"friendly", "neutral", "hostile"};
    const char *settings[] = {"indoor", "outdoor"};
    unsigned int values[4] = {0};
    if (!game_value(arguments[0], events, 6U, &values[0]) ||
        !game_value(arguments[1], dangers, 2U, &values[1]) ||
        !game_value(arguments[2], relationships, 3U, &values[2]) ||
        !game_value(arguments[3], settings, 2U, &values[3]))
        return 0;
    /* Step 2: Stable enums avoid prompt tokenization and allocation during a gameplay request. */
    *request =
        cgai_bark_default_request((cgai_bark_event)values[0], (cgai_bark_danger)values[1],
                                  (cgai_bark_relationship)values[2], (cgai_bark_setting)values[3]);
    return 1;
}

/** @brief Restrict proposal IDs to authored content appropriate for this event family.
 * @param event Valid host event.
 * @return Allowed nonzero line-ID bits; abstention is always permitted by the runtime. */
static uint64_t game_allowed(cgai_bark_event event) {
    /* Step 1: Host rules determine legality independently of the learned preference. */
    const uint64_t masks[] = {UINT64_C(0),  UINT64_C(14),  UINT64_C(48),
                              UINT64_C(64), UINT64_C(128), UINT64_C(256)};
    return masks[(size_t)event];
}

/** @brief Parse an optional most-recent authored line ID for immediate repetition suppression.
 * @param name Borrowed catalog name.
 * @param request Borrowed mutable request.
 * @return Nonzero when the catalog name exists. */
static int game_recent(const char *name, cgai_bark_request *request) {
    /* Step 1: Resolve only the fixed authored catalog; arbitrary output text is not accepted. */
    for (unsigned int id = 0U; id < (unsigned int)CGAI_BARK_ID_COUNT; ++id)
        if (strcmp(name, cgai_bark_id_name((cgai_bark_id)id)) == 0) {
            request->recent_id = id;
            return 1;
        }
    return 0;
}

/** @brief Run one validated proposal after model and scratch preparation.
 * @param model Borrowed immutable loaded specialist.
 * @param request Borrowed typed host request with legal-ID mask.
 * @return Nonzero on a published line or silence; zero on resource/runtime failure. */
static int game_select(const cgai_bark_model *model, const cgai_bark_request *request) {
    /* Step 1: Admit the resident model and allocate scratch outside the timed gameplay call. */
    cgai_bark_resources resources = {0};
    if (!cgai_bark_get_resources(model, &resources) ||
        resources.model_bytes > GAME_BARK_MODEL_LIMIT)
        return 0;
    cgai_bark_session *session = cgai_bark_session_create(model, GAME_BARK_SESSION_LIMIT);
    if (session == NULL)
        return 0;
    /* Step 2: Resolve only a validated authored ID, or deterministic silence. */
    cgai_bark_result result = {0};
    const int selected = cgai_bark_select(session, request, &result);
    if (selected)
        printf("id=%s passes=%zu line=%s\n", cgai_bark_id_name(result.id), result.forward_passes,
               result.text != NULL ? result.text : "(silence)");
    cgai_bark_session_destroy(session);
    return selected;
}

/** @brief Demonstrate an engine's typed event, legality mask and authored-text resolution.
 * @param argc Model, four observations and optional recent-line name.
 * @param argv Borrowed command arguments.
 * @return Zero on completion, two on invalid input, one on artifact/runtime failure. */
int main(int argc, char **argv) {
    /* Step 1: Validate host input before loading or applying any model proposal. */
    cgai_bark_request request = {0};
    if (argc < 6 || argc > 7 || !game_request(&argv[2], &request) ||
        (argc == 7 && !game_recent(argv[6], &request))) {
        fprintf(stderr,
                "usage: cgai_bark_demo MODEL EVENT DANGER RELATIONSHIP SETTING [RECENT_ID]\n");
        return 2;
    }
    request.allowed_ids = game_allowed(request.state.event);
    cgai_bark_model *model = cgai_bark_load(argv[1]);
    if (model == NULL) {
        fprintf(stderr, "%s\n", cgai_last_error());
        return 1;
    }
    /* Step 2: The completed ID resolves to game-owned content, with silence as the fallback. */
    const int selected = game_select(model, &request);
    if (!selected)
        fprintf(stderr, "%s\n", cgai_last_error());
    cgai_bark_destroy(model);
    return selected ? 0 : 1;
}
