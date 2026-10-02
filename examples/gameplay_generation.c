/** @file gameplay_generation.c @brief Engine-neutral bounded generation scheduling example. */
#include "centroid_gai_neural.h"
#include <inttypes.h>
#include <stdio.h>

/** Example admission budget in bytes; profile on the game's minimum target hardware. */
#define GAME_MODEL_BUDGET (4U * 1024U * 1024U)
/** Example per-job scratch budget, separate from the shared model allocation. */
#define GAME_SESSION_BUDGET (2U * 1024U * 1024U)
/** Short text is cheaper to evaluate and validate than unconstrained long replies. */
#define GAME_OUTPUT_TOKENS 24U
/** One quantum counts attempted forward passes, including EOS. */
#define GAME_FORWARD_QUANTUM 2U

/** @brief Inspect a loaded specialist before admitting it to the example runtime.
 * @param model Borrowed immutable weights-only model.
 * @return One when it fits this example's memory budgets, or zero with a diagnostic. */
static int game_admit_model(const cgai_neural_model *model) {
    /* Step 1: Obtain requested heap payloads and dense work counters. */
    cgai_neural_resources resources = {0};
    if (!cgai_neural_get_resources(model, &resources))
        return 0;
    printf("model=%zu bytes weights=%zu bytes session=%zu bytes vocabulary=%zu\n",
           resources.model_bytes, resources.parameter_bytes, resources.session_bytes,
           resources.vocabulary_size);
    printf("per forward: encoder=%" PRIu64 " routing=%" PRIu64 " expert logits=%" PRIu64 "\n",
           resources.encoder_multiply_adds, resources.routing_coordinates, resources.expert_logits);
    /* Step 2: Admit only models whose requested payloads fit the explicit profile. */
    if (resources.model_bytes > GAME_MODEL_BUDGET ||
        resources.session_bytes > GAME_SESSION_BUDGET) {
        fprintf(stderr, "specialist exceeds example memory profile\n");
        return 0;
    }
    return 1;
}

/** @brief Simulate successive host scheduling opportunities without a clock deadline.
 * @param session Borrowed exclusive prepared session.
 * @return One on EOS or output limit, zero on runtime failure. */
static int game_schedule_job(cgai_neural_session *session) {
    /* Step 1: Query the prepared state without performing inference. */
    cgai_neural_generation_result result = {0};
    if (!cgai_neural_session_step(session, 0U, &result))
        return 0;
    /* Step 2: An engine calls once per scheduling opportunity, preferably on a worker. */
    while (result.finish == CGAI_NEURAL_FINISH_RUNNING) {
        if (!cgai_neural_session_step(session, GAME_FORWARD_QUANTUM, &result))
            return 0;
        printf("scheduled: passes=%zu emitted=%zu\n", result.forward_passes,
               result.generated_tokens);
    }
    return 1;
}

/** @brief Prepare off the gameplay path, run bounded quanta, and release the session.
 * @param model Borrowed admitted immutable model.
 * @param prompt Borrowed event/context description.
 * @return One on completed generation, zero on failure. */
static int game_generate(const cgai_neural_model *model, const char *prompt) {
    /* Step 1: Keep the output alive for the full job and allocate scratch once. */
    char output[2048] = {0};
    cgai_neural_session *session = cgai_neural_session_create(model, GAME_SESSION_BUDGET);
    if (session == NULL)
        return 0;
    /* Step 2: Preparation tokenizes the prompt; scheduled steps allocate no storage. */
    int success = cgai_neural_session_begin(session, prompt, GAME_OUTPUT_TOKENS, 0.0, 42U, output,
                                            sizeof(output)) &&
                  game_schedule_job(session);
    if (success)
        printf("proposal (validate before applying): %s\n", output);
    cgai_neural_session_destroy(session);
    return success;
}

/** @brief Exercise the C11 integration contract using a trusted same-architecture artifact.
 * @param argc Argument count; exactly model path and prompt are required.
 * @param argv Borrowed command arguments.
 * @return Zero on completion; nonzero on invalid input, admission or generation failure. */
int main(int argc, char **argv) {
    /* Step 1: Load weights during initialization, before a frame-critical request. */
    if (argc != 3) {
        fprintf(stderr, "usage: cgai_gameplay_demo MODEL.cgnn PROMPT\n");
        return 2;
    }
    cgai_neural_model *model = cgai_neural_load(argv[1]);
    if (model == NULL) {
        fprintf(stderr, "%s\n", cgai_last_error());
        return 1;
    }
    /* Step 2: Apply the memory profile and schedule a bounded proposal job. */
    const int success = game_admit_model(model) && game_generate(model, argv[2]);
    if (!success)
        fprintf(stderr, "%s\n", cgai_last_error());
    cgai_neural_destroy(model);
    return success ? 0 : 1;
}
