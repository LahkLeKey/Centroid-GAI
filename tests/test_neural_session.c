/** @file test_neural_session.c @brief Bounded incremental inference and resource contracts. */
#include "test_neural.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** One caller-owned output and reference pair for an independently scheduled request. */
typedef struct session_job {
    cgai_neural_session *session;         /**< Owned session borrowing the shared model. */
    char actual[512];                     /**< Destination retained until session destruction. */
    char expected[512];                   /**< Independently generated blocking reference. */
    cgai_neural_generation_result result; /**< Most recent cumulative scheduler accounting. */
} session_job;

/** @brief Create a fixture whose unlikely EOS permits meaningful multi-step comparisons.
 * @return Owned compact model, released with cgai_neural_destroy(). */
static cgai_neural_model *session_model(void) {
    /* Step 1: Retain context-dependent experts while suppressing premature stopping. */
    cgai_neural_model *model = cgai_test_neural_fixture();
    for (size_t row = 0U; row < model->config.centroid_count; ++row)
        model->logits[row * model->vocabulary_size + CGAI_TOKEN_EOS] = -100.0;
    return model;
}

/** @brief Make one output the unique greedy choice for deterministic stopping/error checks.
 * @param model Borrowed mutable fixture with no active sessions.
 * @param selected Valid output ID, including EOS when a stopping event is required. */
static void session_force_output(cgai_neural_model *model, cgai_token_id selected) {
    /* Step 1: Give every expert the same finite, strongly separated distribution. */
    for (size_t row = 0U; row < model->config.centroid_count; ++row) {
        for (size_t token = 0U; token < model->vocabulary_size; ++token)
            model->logits[row * model->vocabulary_size + token] =
                token == selected.value ? 30.0 : -30.0;
    }
}

/** @brief Compare public progress fields without relying on structure padding.
 * @param first Borrowed first progress value.
 * @param second Borrowed second progress value. */
static void session_same_result(const cgai_neural_generation_result *first,
                                const cgai_neural_generation_result *second) {
    /* Step 1: Include preparation accounting as well as generated work and termination. */
    TEST_CHECK(first->finish == second->finish &&
                   first->generated_tokens == second->generated_tokens &&
                   first->forward_passes == second->forward_passes &&
                   first->prompt_tokens == second->prompt_tokens &&
                   first->unknown_prompt_tokens == second->unknown_prompt_tokens,
               "session progress changed without work");
}

/** @brief Finish a successful request while enforcing every individual work budget.
 * @param session Borrowed active session; completion remains available afterward.
 * @param budget Positive maximum attempted forward passes per call.
 * @param result Writable final cumulative progress. */
static void session_drain(cgai_neural_session *session, size_t budget,
                          cgai_neural_generation_result *result) {
    /* Step 1: Observe initial progress without calculating a prediction. */
    size_t calls = 0U;
    TEST_CHECK(cgai_neural_session_step(session, 0U, result) == CGAI_STATUS_OK, cgai_last_error());
    /* Step 2: Bound both forward work and the number of scheduler calls needed to finish. */
    while (result->finish == CGAI_NEURAL_FINISH_RUNNING) {
        const size_t previous = result->forward_passes;
        TEST_CHECK(cgai_neural_session_step(session, budget, result) == CGAI_STATUS_OK,
                   cgai_last_error());
        TEST_CHECK(result->forward_passes >= previous &&
                       result->forward_passes - previous <= budget,
                   "session exceeded its forward-pass budget");
        TEST_CHECK(++calls <= 1024U, "session did not make bounded progress");
    }
}

/** @brief Match one-shot output exactly despite incremental scheduling boundaries.
 * @param model Borrowed immutable fixture.
 * @param session Borrowed reusable session associated with model.
 * @param temperature Sampling temperature, including zero for greedy mode.
 * @param seed Requested local sampling seed; zero uses the model seed.
 * @param budget Positive forward-pass budget for each scheduler call. */
static void session_parity(const cgai_neural_model *model, cgai_neural_session *session,
                           double temperature, uint64_t seed, size_t budget) {
    /* Step 1: Generate identical requests through the blocking and incremental interfaces. */
    char expected[512];
    char actual[512];
    cgai_neural_generation_result result = {0};
    TEST_CHECK(cgai_neural_generate(model, "a unseen b", 18U, temperature, seed, expected,
                                    sizeof(expected)) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_session_begin(session, "a unseen b", 18U, temperature, seed, actual,
                                         sizeof(actual)) == CGAI_STATUS_OK,
               cgai_last_error());
    session_drain(session, budget, &result);
    /* Step 2: Preparation and completion accounting must remain independent of chunk size. */
    TEST_CHECK(strcmp(expected, actual) == 0, "incremental scheduling changed generated text");
    TEST_CHECK(result.finish == CGAI_NEURAL_FINISH_LIMIT && result.generated_tokens == 18U &&
                   result.forward_passes == 18U,
               "token-limit progress omitted or repeated predictions");
    TEST_CHECK(result.prompt_tokens == 3U && result.unknown_prompt_tokens == 1U,
               "session lost prompt or unknown-token accounting");
    const cgai_neural_generation_result completed = result;
    TEST_CHECK(cgai_neural_session_step(session, SIZE_MAX, &result) == CGAI_STATUS_OK,
               cgai_last_error());
    session_same_result(&completed, &result);
    TEST_CHECK(strcmp(expected, actual) == 0, "completed session changed its output");
}

/** @brief Exercise greedy and sampled parity while proving inference leaves weights immutable.
 * @param model Borrowed immutable fixture with EOS suppressed. */
static void session_scheduling(const cgai_neural_model *model) {
    /* Step 1: Snapshot weights and allocate one session reused by every scheduling pattern. */
    const size_t bytes = model->parameter_count * sizeof(double);
    double *before = malloc(bytes);
    TEST_CHECK(before != NULL, "could not allocate session immutability snapshot");
    memcpy(before, model->parameters, bytes);
    cgai_neural_session *session = cgai_neural_session_create(model, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    /* Step 2: Scheduling boundaries must preserve greedy and seeded stochastic continuations. */
    const size_t budgets[] = {1U, 2U, 7U, 32U};
    for (size_t index = 0U; index < sizeof(budgets) / sizeof(budgets[0]); ++index) {
        session_parity(model, session, 0.0, 0U, budgets[index]);
        session_parity(model, session, 0.8, 91U, budgets[index]);
        session_parity(model, session, 0.8, 0U, budgets[index]);
        session_parity(model, session, 1e-12, 17U, budgets[index]);
    }
    TEST_CHECK(memcmp(before, model->parameters, bytes) == 0, "session inference changed weights");
    cgai_neural_session_destroy(session);
    free(before);
}

/** @brief Check exact owned-byte and arithmetic reports against the private allocation layout.
 * @param model Borrowed initialized fixture with no optimizer moments. */
static void session_resource_shape(const cgai_neural_model *model) {
    /* Step 1: Count independent allocations, including unused vocabulary pointer capacity. */
    cgai_neural_resources resources = {0};
    size_t spellings = 0U;
    const size_t input = model->config.context_window * model->config.embedding_dimensions;
    const size_t parameters = model->vocabulary_size * model->config.embedding_dimensions +
                              model->config.hidden_dimensions * input +
                              model->config.hidden_dimensions +
                              model->config.centroid_count * model->config.hidden_dimensions +
                              model->config.centroid_count * model->vocabulary_size;
    const size_t scratch =
        2U * input + 2U * model->config.hidden_dimensions + 3U * model->config.centroid_count +
        model->config.centroid_count * model->vocabulary_size + model->vocabulary_size;
    TEST_CHECK(cgai_neural_get_resources(model, &resources) == CGAI_STATUS_OK, cgai_last_error());
    for (size_t token = 0U; token < model->vocabulary_size; ++token)
        spellings += strlen(model->vocabulary[token]) + 1U;
    /* Step 2: Size and work fields describe owned payloads rather than process memory. */
    TEST_CHECK(resources.vocabulary_size == model->vocabulary_size &&
                   resources.output_size == model->output_size &&
                   resources.parameter_count == parameters &&
                   resources.parameter_bytes == parameters * sizeof(double) &&
                   resources.optimizer_bytes == 0U,
               "resource report changed model dimensions or parameter storage");
    TEST_CHECK(resources.model_bytes == sizeof(*model) +
                                            CGAI_NEURAL_MAX_VOCABULARY * sizeof(char *) +
                                            spellings + resources.parameter_bytes,
               "resource report omitted model-owned storage");
    TEST_CHECK(resources.workspace_bytes ==
                       sizeof(cgai_neural_workspace) + scratch * sizeof(double) &&
                   resources.session_bytes > resources.workspace_bytes,
               "resource report omitted session or workspace storage");
    TEST_CHECK(resources.encoder_multiply_adds == model->config.hidden_dimensions * input &&
                   resources.routing_coordinates ==
                       model->config.centroid_count * model->config.hidden_dimensions &&
                   resources.expert_logits ==
                       model->config.centroid_count * (model->output_size - 1U),
               "resource report changed dense forward work dimensions");
    TEST_CHECK(resources.config.embedding_dimensions == model->config.embedding_dimensions &&
                   resources.config.hidden_dimensions == model->config.hidden_dimensions &&
                   resources.config.centroid_count == model->config.centroid_count &&
                   resources.config.context_window == model->config.context_window &&
                   resources.config.seed == model->config.seed &&
                   resources.config.routing_temperature == model->config.routing_temperature,
               "resource report changed the network configuration");
}

/** @brief Confirm weights-only artifacts remove optimizer storage from runtime accounting.
 * @param model Borrowed fixture with resident continuation moments.
 * @param untrained Borrowed pre-training resource report for the same shape. */
static void session_resource_reload(const cgai_neural_model *model,
                                    const cgai_neural_resources *untrained) {
    /* Step 1: Load an independently owned runtime artifact without retained Adam state. */
    const char *path = "centroid_gai_neural_session.cgnn";
    TEST_CHECK(cgai_neural_save(model, path) == CGAI_STATUS_OK, cgai_last_error());
    cgai_neural_model *loaded = cgai_neural_load(path);
    TEST_CHECK(loaded != NULL, cgai_last_error());
    cgai_neural_resources resources = {0};
    TEST_CHECK(cgai_neural_get_resources(loaded, &resources) == CGAI_STATUS_OK, cgai_last_error());
    /* Step 2: Weights and vocabulary stay resident while both optimizer arrays disappear. */
    TEST_CHECK(resources.optimizer_bytes == 0U && resources.model_bytes == untrained->model_bytes &&
                   resources.parameter_bytes == untrained->parameter_bytes &&
                   resources.workspace_bytes == untrained->workspace_bytes &&
                   resources.session_bytes == untrained->session_bytes,
               "weights-only loading retained optimizer storage or changed inference requirements");
    cgai_neural_destroy(loaded);
    TEST_CHECK(remove(path) == 0, "could not remove session resource artifact");
}

/** @brief Include both resident Adam arrays without changing inference scratch requirements.
 * @param model Borrowed mutable fixture with no active sessions. */
static void session_resource_optimizer(cgai_neural_model *model) {
    /* Step 1: Capture the weights-only shape before creating continuation state. */
    cgai_neural_resources before = {0};
    cgai_neural_resources after = {0};
    TEST_CHECK(cgai_neural_get_resources(model, &before) == CGAI_STATUS_OK, cgai_last_error());
    model->adam_first = calloc(model->parameter_count, sizeof(*model->adam_first));
    model->adam_second = calloc(model->parameter_count, sizeof(*model->adam_second));
    TEST_CHECK(model->adam_first != NULL && model->adam_second != NULL,
               "could not allocate owned resource-accounting fixture moments");
    TEST_CHECK(cgai_neural_get_resources(model, &after) == CGAI_STATUS_OK, cgai_last_error());
    /* Step 2: Optimizer accounting describes the resident handle rather than saved weights. */
    TEST_CHECK(after.optimizer_bytes == 2U * before.parameter_bytes &&
                   after.model_bytes == before.model_bytes + after.optimizer_bytes &&
                   after.workspace_bytes == before.workspace_bytes &&
                   after.session_bytes == before.session_bytes,
               "resource report omitted Adam moments or changed inference capacity");
    session_resource_reload(model, &before);
}

/** @brief Enforce the reported session allocation cap and observable idle state.
 * @param model Borrowed immutable fixture. */
static void session_caps(const cgai_neural_model *model) {
    /* Step 1: Reject caps before acquiring storage and accept the exact reported requirement. */
    cgai_neural_resources resources = {0};
    TEST_CHECK(cgai_neural_get_resources(model, &resources) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(cgai_neural_session_create(model, 1U) == NULL, "tiny session cap was accepted");
    TEST_CHECK(cgai_neural_session_create(model, resources.session_bytes - 1U) == NULL,
               "session cap omitted an owned allocation");
    cgai_neural_session *session = cgai_neural_session_create(model, resources.session_bytes);
    TEST_CHECK(session != NULL, cgai_last_error());
    /* Step 2: Both zero and positive work budgets preserve an idle session. */
    cgai_neural_generation_result first = {0};
    cgai_neural_generation_result second = {0};
    TEST_CHECK(cgai_neural_session_step(session, 0U, &first) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(first.finish == CGAI_NEURAL_FINISH_IDLE && first.forward_passes == 0U &&
                   first.generated_tokens == 0U && first.prompt_tokens == 0U &&
                   first.unknown_prompt_tokens == 0U,
               "new session was not idle");
    TEST_CHECK(cgai_neural_session_step(session, SIZE_MAX, &second) == CGAI_STATUS_OK,
               cgai_last_error());
    session_same_result(&first, &second);
    cgai_neural_session_destroy(session);
}

/** @brief Reject malformed job controls without changing an already running request.
 * @param session Borrowed active session.
 * @param output Borrowed current request destination, preserved by every rejection.
 * @param capacity Capacity of output including its terminator. */
static void session_bad_begin(cgai_neural_session *session, char *output, size_t capacity) {
    /* Step 1: Invalid pointers, capacities and token counts fail before replacing the job. */
    TEST_CHECK(cgai_neural_session_begin(session, NULL, 2U, 0.0, 1U, output, capacity) ==
                   CGAI_STATUS_ERROR,
               "null session prompt was accepted");
    TEST_CHECK(cgai_neural_session_begin(session, "a", 2U, 0.0, 1U, NULL, capacity) ==
                   CGAI_STATUS_ERROR,
               "null session output was accepted");
    TEST_CHECK(cgai_neural_session_begin(session, "a", 2U, 0.0, 1U, output, 0U) ==
                   CGAI_STATUS_ERROR,
               "zero session output capacity was accepted");
    TEST_CHECK(cgai_neural_session_begin(session, "a", 1000001U, 0.0, 1U, output, capacity) ==
                   CGAI_STATUS_ERROR,
               "oversized session token limit was accepted");
    /* Step 2: Nonfinite and out-of-range temperatures also leave the old request intact. */
    TEST_CHECK(cgai_neural_session_begin(session, "a", 2U, NAN, 1U, output, capacity) ==
                   CGAI_STATUS_ERROR,
               "NaN session temperature was accepted");
    TEST_CHECK(cgai_neural_session_begin(session, "a", 2U, INFINITY, 1U, output, capacity) ==
                   CGAI_STATUS_ERROR,
               "infinite session temperature was accepted");
    TEST_CHECK(cgai_neural_session_begin(session, "a", 2U, -1.0, 1U, output, capacity) ==
                   CGAI_STATUS_ERROR,
               "negative session temperature was accepted");
    TEST_CHECK(cgai_neural_session_begin(session, "a", 2U, 101.0, 1U, output, capacity) ==
                   CGAI_STATUS_ERROR,
               "oversized session temperature was accepted");
}

/** @brief Show invalid controls cannot advance a prior job or overwrite its valid prefix.
 * @param model Borrowed immutable fixture with EOS suppressed. */
static void session_rejections(const cgai_neural_model *model) {
    /* Step 1: Start useful work before injecting invalid requests. */
    cgai_neural_session *session = cgai_neural_session_create(model, 0U);
    char output[512];
    char saved[512];
    cgai_neural_generation_result before = {0};
    cgai_neural_generation_result after = {0};
    TEST_CHECK(session != NULL, cgai_last_error());
    TEST_CHECK(cgai_neural_session_begin(session, "a b", 18U, 0.8, 91U, output, sizeof(output)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_session_step(session, 1U, &before) == CGAI_STATUS_OK, cgai_last_error());
    memcpy(saved, output, strlen(output) + 1U);
    /* Step 2: Rejected begin and step arguments preserve both progress and text. */
    session_bad_begin(session, output, sizeof(output));
    TEST_CHECK(cgai_neural_session_step(session, 3U, NULL) == CGAI_STATUS_ERROR,
               "null session progress was accepted");
    TEST_CHECK(cgai_neural_session_step(session, 0U, &after) == CGAI_STATUS_OK, cgai_last_error());
    session_same_result(&before, &after);
    TEST_CHECK(strcmp(saved, output) == 0, "invalid session request overwrote output");
    /* Step 3: A valid replacement discards running work and resets its random stream. */
    session_parity(model, session, 0.8, 17U, 3U);
    cgai_neural_session_destroy(session);
}

/** @brief Prepare one independently seeded job and its corresponding blocking reference.
 * @param model Borrowed immutable fixture.
 * @param seed Local sampling seed for this job.
 * @param job Borrowed zero-initialized storage retaining session and output ownership. */
static void session_job_prepare(const cgai_neural_model *model, uint64_t seed, session_job *job) {
    /* Step 1: Acquire one private workspace for this request. */
    job->session = cgai_neural_session_create(model, 0U);
    TEST_CHECK(job->session != NULL, cgai_last_error());
    /* Step 2: Retain destinations while setting up blocking and incremental comparisons. */
    TEST_CHECK(cgai_neural_generate(model, "a b", 18U, 0.8, seed, job->expected,
                                    sizeof(job->expected)) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_session_begin(job->session, "a b", 18U, 0.8, seed, job->actual,
                                         sizeof(job->actual)) == CGAI_STATUS_OK,
               cgai_last_error());
}

/** @brief Keep simultaneous sessions' context and random streams independent.
 * @param model Borrowed immutable fixture with EOS suppressed. */
static void session_interleaved(const cgai_neural_model *model) {
    /* Step 1: Establish independent jobs and corresponding blocking references. */
    session_job jobs[2] = {0};
    session_job_prepare(model, 17U, &jobs[0]);
    session_job_prepare(model, 91U, &jobs[1]);
    /* Step 2: Alternate unequal work chunks, including calls after one job finishes. */
    for (size_t turn = 0U; turn < 18U; ++turn) {
        TEST_CHECK(cgai_neural_session_step(jobs[0].session, 1U, &jobs[0].result) == CGAI_STATUS_OK,
                   cgai_last_error());
        TEST_CHECK(cgai_neural_session_step(jobs[1].session, 3U, &jobs[1].result) == CGAI_STATUS_OK,
                   cgai_last_error());
    }
    for (size_t job = 0U; job < 2U; ++job) {
        TEST_CHECK(strcmp(jobs[job].actual, jobs[job].expected) == 0 &&
                       jobs[job].result.finish == CGAI_NEURAL_FINISH_LIMIT &&
                       jobs[job].result.forward_passes == 18U,
                   "interleaving changed a session's private generation state");
        cgai_neural_session_destroy(jobs[job].session);
    }
}

/** @brief EOS consumes forward work without being emitted into the text buffer. */
static void session_eos(void) {
    /* Step 1: Force the stopping event before creating immutable borrowed sessions. */
    cgai_neural_model *model = session_model();
    session_force_output(model, cgai_token_id_from_size(CGAI_TOKEN_EOS));
    cgai_neural_session *session = cgai_neural_session_create(model, 0U);
    char output[8] = "stale";
    cgai_neural_generation_result result = {0};
    TEST_CHECK(session != NULL, cgai_last_error());
    TEST_CHECK(cgai_neural_session_begin(session, "", 4U, 0.0, 1U, output, sizeof(output)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_session_step(session, 0U, &result) == CGAI_STATUS_OK &&
                   result.finish == CGAI_NEURAL_FINISH_RUNNING && result.forward_passes == 0U,
               "zero work budget calculated an EOS prediction");
    /* Step 2: One attempted prediction finishes the job without an emitted token. */
    TEST_CHECK(cgai_neural_session_step(session, 8U, &result) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(result.finish == CGAI_NEURAL_FINISH_EOS && result.forward_passes == 1U &&
                   result.generated_tokens == 0U && result.prompt_tokens == 0U && output[0] == '\0',
               "EOS accounting confused forward passes with emitted tokens");
    const cgai_neural_generation_result completed = result;
    TEST_CHECK(cgai_neural_session_step(session, SIZE_MAX, &result) == CGAI_STATUS_OK,
               cgai_last_error());
    session_same_result(&completed, &result);
    cgai_neural_session_destroy(session);
    cgai_neural_destroy(model);
}

/** @brief Capacity failures retain complete tokens and become terminal without extra work. */
static void session_capacity_failure(void) {
    /* Step 1: Fit exactly one ordinary token, then force the next append to fail. */
    cgai_neural_model *model = session_model();
    session_force_output(model, cgai_neural_lookup(model, "left"));
    cgai_neural_session *session = cgai_neural_session_create(model, 0U);
    char output[6];
    cgai_neural_generation_result result = {0};
    TEST_CHECK(session != NULL, cgai_last_error());
    TEST_CHECK(cgai_neural_session_begin(session, "a", 2U, 0.8, 91U, output, sizeof(output)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_session_step(session, 2U, &result) == CGAI_STATUS_ERROR,
               "small output buffer was accepted");
    TEST_CHECK(result.finish == CGAI_NEURAL_FINISH_ERROR && result.forward_passes == 2U &&
                   result.generated_tokens == 1U && strcmp(output, "left") == 0,
               "failed append changed the valid prefix or work accounting");
    /* Step 2: Failed sessions cannot consume further samples, even with a zero work budget. */
    const cgai_neural_generation_result failed = result;
    TEST_CHECK(cgai_neural_session_step(session, SIZE_MAX, &result) == CGAI_STATUS_ERROR,
               "failed session resumed generation");
    session_same_result(&failed, &result);
    TEST_CHECK(cgai_neural_session_step(session, 0U, &result) == CGAI_STATUS_ERROR,
               "zero work budget concealed a terminal failure");
    session_same_result(&failed, &result);
    cgai_neural_session_destroy(session);
    cgai_neural_destroy(model);
}

/** @brief Numerical failure counts its attempted pass and prevents stale-output reuse. */
static void session_numeric_failure(void) {
    /* Step 1: Inject a nonfinite private parameter before any session borrows the fixture. */
    cgai_neural_model *model = session_model();
    model->encoder[0] = NAN;
    cgai_neural_session *session = cgai_neural_session_create(model, 0U);
    char output[32];
    cgai_neural_generation_result result = {0};
    TEST_CHECK(session != NULL, cgai_last_error());
    TEST_CHECK(cgai_neural_session_begin(session, "a", 2U, 0.0, 1U, output, sizeof(output)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    /* Step 2: The failing calculation remains observable despite producing no token. */
    TEST_CHECK(cgai_neural_session_step(session, 1U, &result) == CGAI_STATUS_ERROR,
               "nonfinite encoder parameter was accepted");
    TEST_CHECK(result.finish == CGAI_NEURAL_FINISH_ERROR && result.forward_passes == 1U &&
                   result.generated_tokens == 0U && output[0] == '\0',
               "numerical failure lost attempted work or emitted stale text");
    const cgai_neural_generation_result failed = result;
    TEST_CHECK(cgai_neural_session_step(session, SIZE_MAX, &result) == CGAI_STATUS_ERROR,
               "numerically failed session resumed work");
    session_same_result(&failed, &result);
    cgai_neural_session_destroy(session);
    cgai_neural_destroy(model);
}

/** @brief A zero-token request skips prompt processing; a preparation error remains terminal.
 * @param model Borrowed immutable fixture with EOS suppressed. */
static void session_preparation(const cgai_neural_model *model) {
    /* Step 1: Use an over-limit spelling to distinguish validation from prompt processing. */
    char *prompt = malloc(CGAI_NEURAL_MAX_TOKEN_BYTES + 2U);
    cgai_neural_session *session = cgai_neural_session_create(model, 0U);
    char output[512] = "stale";
    cgai_neural_generation_result result = {0};
    TEST_CHECK(prompt != NULL && session != NULL, "could not allocate preparation fixture");
    memset(prompt, 'x', CGAI_NEURAL_MAX_TOKEN_BYTES + 1U);
    prompt[CGAI_NEURAL_MAX_TOKEN_BYTES + 1U] = '\0';
    TEST_CHECK(cgai_neural_session_begin(session, prompt, 0U, 0.0, 1U, output, sizeof(output)) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_neural_session_step(session, SIZE_MAX, &result) == CGAI_STATUS_OK &&
                   result.finish == CGAI_NEURAL_FINISH_LIMIT && result.forward_passes == 0U &&
                   result.generated_tokens == 0U && result.prompt_tokens == 0U && output[0] == '\0',
               "zero-token request processed the prompt");
    /* Step 2: A useful token request rejects that same spelling and exposes terminal failure. */
    TEST_CHECK(cgai_neural_session_begin(session, prompt, 1U, 0.0, 1U, output, sizeof(output)) ==
                   CGAI_STATUS_ERROR,
               "over-limit prompt spelling was accepted");
    TEST_CHECK(cgai_neural_session_step(session, 1U, &result) == CGAI_STATUS_ERROR &&
                   result.finish == CGAI_NEURAL_FINISH_ERROR && result.forward_passes == 0U,
               "failed prompt preparation performed prediction work");
    /* Step 3: A fresh valid job recovers the same session without stale prompt state. */
    session_parity(model, session, 0.8, 91U, 1U);
    cgai_neural_session_destroy(session);
    free(prompt);
}

/** @brief Reject missing resource/session arguments without reading invalid handles.
 * @param model Borrowed initialized fixture. */
static void session_nulls(const cgai_neural_model *model) {
    /* Step 1: Failure must not publish a partial resource report. */
    cgai_neural_resources resources = {0};
    resources.parameter_count = 137U;
    TEST_CHECK(cgai_neural_get_resources(NULL, &resources) == CGAI_STATUS_ERROR,
               "null resource model was accepted");
    TEST_CHECK(resources.parameter_count == 137U, "failed resource inspection published output");
    TEST_CHECK(cgai_neural_get_resources(model, NULL) == CGAI_STATUS_ERROR,
               "null resource destination was accepted");
    /* Step 2: Invalid session handles produce diagnostics rather than dereferences. */
    char output[8] = "stale";
    cgai_neural_generation_result result = {0};
    TEST_CHECK(cgai_neural_session_create(NULL, 0U) == NULL, "null session model was accepted");
    TEST_CHECK(cgai_neural_session_begin(NULL, "a", 1U, 0.0, 1U, output, sizeof(output)) ==
                   CGAI_STATUS_ERROR,
               "null begin session was accepted");
    TEST_CHECK(strcmp(output, "stale") == 0, "invalid begin session changed output");
    TEST_CHECK(cgai_neural_session_step(NULL, 1U, &result) == CGAI_STATUS_ERROR,
               "null step session was accepted");
    cgai_neural_session_destroy(NULL);
}

/** @brief Run session scheduling, lifetime, resource, and failure regression checks. */
void cgai_test_neural_session(void) {
    /* Step 1: Share one immutable shape across allocation, scheduling and input checks. */
    cgai_neural_model *model = session_model();
    session_resource_shape(model);
    session_nulls(model);
    session_caps(model);
    session_scheduling(model);
    session_rejections(model);
    session_interleaved(model);
    session_preparation(model);
    /* Step 2: Mutate only after all borrowed sessions have been released. */
    session_resource_optimizer(model);
    cgai_neural_destroy(model);
    session_eos();
    session_capacity_failure();
    session_numeric_failure();
}
