/** @file npc_evaluation.c @brief Complete independent NPC episodes and learned-module ablation. */
#include "npc_evaluation.h"
#include "internal/error.h"
#include "npc_teacher.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Frozen controller ordering in every comparative report. */
static const char *const actor_names[NPC_ACTORS] = {"planner", "reactive",   "initialized",
                                                    "trained", "memoryless", "module0",
                                                    "module1", "previous"};
/** Frozen principal mechanic ordering. */
static const char *const mechanic_names[3] = {"item", "hazard", "cue"};

/** @brief Add one complete metric value without excluding failure terminals.
 * @param destination Writable running accumulator.
 * @param source Borrowed episode or family totals. */
static void metrics_add(npc_episode_metrics *destination, const npc_episode_metrics *source) {
    destination->count += source->count;
    destination->success += source->success;
    destination->survived += source->survived;
    destination->deaths += source->deaths;
    destination->timeouts += source->timeouts;
    destination->cost += source->cost;
    destination->blocked += source->blocked;
    destination->recoveries += source->recoveries;
    destination->illegal_attempts += source->illegal_attempts;
    destination->illegal_executed += source->illegal_executed;
    destination->fallbacks += source->fallbacks;
}

/** @brief Choose an actor action using only its own observation and bounded memory.
 * @param actor Frozen controller index.
 * @param session Neural actor's independent adapter, or NULL for authored actors.
 * @param memory Authored actor's independent observed memory.
 * @param observation Current visible information.
 * @param action Writable selected proposal.
 * @return OK on a complete proposal, ERROR otherwise. */
static cgai_status episode_action(size_t actor, npc_policy_session *session, npc_memory *memory,
                                  const npc_observation *observation, uint32_t *action) {
    if (actor < 2U) {
        npc_memory_observe(memory, observation);
        *action = actor == 0U ? npc_teacher_action(observation, memory)
                              : npc_reactive_action(observation);
        return CGAI_STATUS_OK;
    }
    cgai_gameplay_result selected;
    const uint64_t modules = actor == 5U ? UINT64_C(1) : actor == 6U ? UINT64_C(2) : UINT64_C(3);
    if (!npc_policy_decide(session, observation, modules, &selected))
        return CGAI_STATUS_ERROR;
    *action = selected.output;
    return CGAI_STATUS_OK;
}

/** @brief Execute and score one actor-caused authoritative transition.
 * @param world Mutable authoritative episode.
 * @param actor Frozen controller index.
 * @param session Neural actor's independent adapter.
 * @param memory Authored actor's independent memory.
 * @param metrics Mutable complete-episode accumulator.
 * @param pending Mutable observed-obstruction recovery marker.
 * @return OK after a valid selection and host step, ERROR otherwise. */
static cgai_status episode_step(npc_world *world, size_t actor, npc_policy_session *session,
                                npc_memory *memory, npc_episode_metrics *metrics, int *pending) {
    npc_observation observation;
    npc_world_observe(world, &observation);
    uint32_t action = NPC_FALLBACK;
    if (!episode_action(actor, session, memory, &observation, &action))
        return CGAI_STATUS_ERROR;
    metrics->fallbacks += action == NPC_FALLBACK ? 1U : 0U;
    const npc_outcome outcome = npc_world_step(world, action);
    if (outcome == NPC_BLOCKED) {
        ++metrics->blocked;
        *pending = 1;
    } else if (outcome == NPC_MOVED && *pending) {
        ++metrics->recoveries;
        *pending = 0;
    }
    return CGAI_STATUS_OK;
}

/** @brief Include every terminal outcome in the complete-episode metric denominator.
 * @param world Borrowed authoritative terminal state.
 * @param metrics Writable complete-episode accumulator. */
static void episode_terminal(const npc_world *world, npc_episode_metrics *metrics) {
    metrics->count = 1U;
    metrics->success = world->terminal == NPC_SUCCESS ? 1U : 0U;
    metrics->survived = world->terminal != NPC_DEATH ? 1U : 0U;
    metrics->deaths = world->terminal == NPC_DEATH ? 1U : 0U;
    metrics->timeouts = world->terminal == NPC_TIMEOUT ? 1U : 0U;
    metrics->cost = world->ticks;
    metrics->illegal_attempts = world->attempted_illegal;
    metrics->illegal_executed = world->executed_illegal;
}

/** @brief Run one actor from initial conditions using only its own observed history.
 * @param family Pinned provenance, never passed to the controller.
 * @param variant Pinned family-local variant.
 * @param actor Frozen controller index.
 * @param session Independently reset numerical/history adapter for neural actors.
 * @param metrics Writable complete episode result.
 * @return OK on a complete terminal rollout, ERROR otherwise. */
static cgai_status run_episode(const npc_family *family, uint32_t variant, size_t actor,
                               npc_policy_session *session, npc_episode_metrics *metrics) {
    npc_world world;
    npc_memory memory;
    npc_memory_reset(&memory);
    if (!npc_world_init(&world, family, variant))
        return cgai_fail("could not initialize a pinned NPC evaluation episode");
    npc_policy_session_reset(session);
    npc_episode_metrics completed = {0};
    int pending_obstruction = 0;
    while (world.terminal == NPC_RUNNING && world.ticks < NPC_MAX_TICKS)
        if (!episode_step(&world, actor, session, &memory, &completed, &pending_obstruction))
            return CGAI_STATUS_ERROR;
    if (world.terminal == NPC_RUNNING)
        return cgai_fail("NPC episode runner failed to reach a declared terminal");
    episode_terminal(&world, &completed);
    *metrics = completed;
    return CGAI_STATUS_OK;
}

/** @brief Accumulate both module restrictions on the same fixed encoded observation.
 * @param session Exclusive neural adapter scratch.
 * @param example Borrowed complete target and encoded observation.
 * @param selected Previously measured full-mixture routing diagnostics.
 * @param full_loss Full-mixture target negative log likelihood.
 * @param evaluation Writable accumulated diagnostics.
 * @return OK after both controlled restrictions, ERROR otherwise. */
static cgai_status composition_modules(npc_policy_session *session,
                                       const cgai_gameplay_example *example,
                                       const cgai_gameplay_result *selected, double full_loss,
                                       npc_evaluation *evaluation) {
    for (size_t module = 0U; module < 2U; ++module) {
        double loss = 0.0;
        if (!cgai_gameplay_evaluate_modules(session->neural, example, UINT64_C(1) << module,
                                            &loss) ||
            !isfinite(loss))
            return cgai_fail("invalid NPC module-restricted loss");
        evaluation->module_weights[module] += selected->module_weights[module];
        evaluation->module_contributions[module] += selected->module_contributions[module];
        evaluation->module_loss[module] += loss;
        evaluation->module_loss_change[module] += fabs(loss - full_loss);
    }
    return CGAI_STATUS_OK;
}

/** @brief Score one pinned visible teacher state with both modules eligible.
 * @param session Exclusive trained-model adapter.
 * @param observation Current public observation on the teacher trajectory.
 * @param evaluation Mutable fixed-sample diagnostic report.
 * @param target Writable teacher action used for the next authoritative transition.
 * @return OK after complete scoring or declared fallback exclusion, ERROR otherwise. */
static cgai_status composition_observation(npc_policy_session *session,
                                           const npc_observation *observation,
                                           npc_evaluation *evaluation, uint32_t *target) {
    npc_memory_observe(&session->memory, observation);
    *target = npc_teacher_action(observation, &session->memory);
    if (*target == NPC_FALLBACK)
        return CGAI_STATUS_OK;
    cgai_gameplay_result selected;
    cgai_gameplay_example example = {0};
    example.target = *target;
    npc_encode(observation, &session->memory, 1, &example.state);
    double full_loss = 0.0;
    if (!npc_policy_decide(session, observation, 3U, &selected) || selected.forward_passes != 1U ||
        selected.active_modules != 2U ||
        !cgai_gameplay_evaluate(session->neural, &example, &full_loss) || !isfinite(full_loss))
        return cgai_fail("invalid NPC full-mixture composition sample");
    ++evaluation->composition_count;
    evaluation->mean_loss += full_loss;
    return composition_modules(session, &example, &selected, full_loss, evaluation);
}

/** @brief Collect at most eight teacher decisions from one pinned composition episode.
 * @param session Trained-model adapter independently reset for this trajectory.
 * @param family Development-family provenance.
 * @param variant One of the pinned first-eight sibling variants.
 * @param evaluation Mutable fixed-sample diagnostic report.
 * @return OK on complete selected trajectory prefix, ERROR otherwise. */
static cgai_status composition_episode(npc_policy_session *session, const npc_family *family,
                                       uint32_t variant, npc_evaluation *evaluation) {
    npc_world world;
    if (!npc_world_init(&world, family, variant))
        return cgai_fail("invalid pinned composition variant");
    npc_policy_session_reset(session);
    for (size_t step = 0U; step < 8U && world.terminal == NPC_RUNNING; ++step) {
        npc_observation observation;
        npc_world_observe(&world, &observation);
        uint32_t target = NPC_FALLBACK;
        if (!composition_observation(session, &observation, evaluation, &target))
            return CGAI_STATUS_ERROR;
        (void)npc_world_step(&world, target);
    }
    return CGAI_STATUS_OK;
}

/** @brief Collect one pinned development-family composition block.
 * @param session Trained-model adapter.
 * @param index Development-family local index.
 * @param evaluation Mutable fixed-sample diagnostic report.
 * @return OK on complete block, ERROR otherwise. */
static cgai_status composition_family(npc_policy_session *session, uint32_t index,
                                      npc_evaluation *evaluation) {
    npc_family family;
    if (!npc_family_get(NPC_DEV, index, &family))
        return cgai_fail("invalid pinned composition family");
    for (uint32_t variant = 0U; variant < 8U; ++variant)
        if (!composition_episode(session, &family, variant, evaluation))
            return CGAI_STATUS_ERROR;
    return CGAI_STATUS_OK;
}

/** @brief Convert complete composition accumulators to their pinned observation means.
 * @param evaluation Mutable complete nonempty sample. */
static void composition_normalize(npc_evaluation *evaluation) {
    const double divisor = (double)evaluation->composition_count;
    evaluation->mean_loss /= divisor;
    for (size_t module = 0U; module < 2U; ++module) {
        evaluation->module_weights[module] /= divisor;
        evaluation->module_contributions[module] /= divisor;
        evaluation->module_loss[module] /= divisor;
        evaluation->module_loss_change[module] /= divisor;
    }
}

/** @brief Measure learned module contribution on a pinned teacher-observation sample.
 * @param model Borrowed trained immutable model.
 * @param evaluation Writable diagnostic accumulator.
 * @return OK on complete deterministic sample, ERROR otherwise. */
static cgai_status composition_sample(const cgai_gameplay_model *model,
                                      npc_evaluation *evaluation) {
    npc_policy_session *session = npc_policy_session_create(model, 1);
    if (session == NULL)
        return CGAI_STATUS_ERROR;
    cgai_status status = CGAI_STATUS_OK;
    for (uint32_t index = 0U; index < NPC_FAMILIES_PER_SPLIT && status; ++index)
        status = composition_family(session, index, evaluation);
    npc_policy_session_destroy(session);
    if (!status)
        return status;
    if (evaluation->composition_count == 0U)
        return cgai_fail("empty NPC composition observation sample");
    composition_normalize(evaluation);
    return CGAI_STATUS_OK;
}

/** Evaluation-only ownership of seeded comparison weights and independent actor scratch. */
typedef struct npc_evaluation_owner {
    cgai_gameplay_model *initialized; /**< Untouched seed42 comparison model. */
    npc_policy_session
        *sessions[6]; /**< Seed, trained, memoryless, restrictions, and prior weights. */
} npc_evaluation_owner;

/** @brief Prepare every independently resettable neural actor before rollouts.
 * @param model Borrowed trained immutable model.
 * @param memoryless Borrowed separately trained history-disabled immutable model.
 * @param previous Borrowed pinned previous accepted immutable model.
 * @param owner Mutable zero-initialized evaluation owner.
 * @return OK on complete preparation, ERROR otherwise. */
static cgai_status evaluation_prepare(const cgai_gameplay_model *model,
                                      const cgai_gameplay_model *memoryless,
                                      const cgai_gameplay_model *previous,
                                      npc_evaluation_owner *owner) {
    const cgai_gameplay_config config = npc_policy_config();
    owner->initialized = cgai_gameplay_create(&config);
    if (owner->initialized == NULL)
        return CGAI_STATUS_ERROR;
    const cgai_gameplay_model *models[6] = {
        owner->initialized, model, memoryless, model, model, previous};
    for (size_t index = 0U; index < 6U; ++index) {
        owner->sessions[index] = npc_policy_session_create(models[index], index != 2U);
        if (owner->sessions[index] == NULL)
            return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

/** @brief Release complete or partially prepared evaluation-only ownership.
 * @param owner Borrowed private evaluation owner. */
static void evaluation_destroy(npc_evaluation_owner *owner) {
    for (size_t index = 0U; index < 6U; ++index)
        npc_policy_session_destroy(owner->sessions[index]);
    cgai_gameplay_destroy(owner->initialized);
}

/** @brief Run every actor independently on one paired family-local initial condition.
 * @param family Provenance passed only to world initialization.
 * @param variant Pinned sibling variant.
 * @param owner Borrowed independently reset actor sessions.
 * @param evaluation Mutable complete outcome report.
 * @param index Split-local family accumulator index.
 * @return OK after all actor episodes, ERROR otherwise. */
static cgai_status evaluation_variant(const npc_family *family, uint32_t variant,
                                      const npc_evaluation_owner *owner, npc_evaluation *evaluation,
                                      uint32_t index) {
    for (size_t actor = 0U; actor < NPC_ACTORS; ++actor) {
        npc_episode_metrics episode;
        if (!run_episode(family, variant, actor, actor < 2U ? NULL : owner->sessions[actor - 2U],
                         &episode))
            return CGAI_STATUS_ERROR;
        metrics_add(&evaluation->family[index][actor], &episode);
        metrics_add(&evaluation->total[actor], &episode);
        metrics_add(&evaluation->mechanic[family->mechanic][actor], &episode);
    }
    return CGAI_STATUS_OK;
}

/** @brief Include every sibling of one whole reserved family in evaluation.
 * @param split Development or final audit partition.
 * @param index Split-local family accumulator index.
 * @param owner Borrowed independently reset actor sessions.
 * @param evaluation Mutable complete outcome report.
 * @return OK after complete paired family block, ERROR otherwise. */
static cgai_status evaluation_family(npc_split split, uint32_t index,
                                     const npc_evaluation_owner *owner,
                                     npc_evaluation *evaluation) {
    npc_family family;
    if (!npc_family_get(split, index, &family))
        return cgai_fail("could not resolve an isolated NPC evaluation family");
    for (uint32_t variant = 0U; variant < NPC_VARIANTS_PER_FAMILY; ++variant)
        if (!evaluation_variant(&family, variant, owner, evaluation, index))
            return CGAI_STATUS_ERROR;
    return CGAI_STATUS_OK;
}

/** @brief Run all fixed families before measuring the pinned composition sample.
 * @param model Borrowed trained immutable model.
 * @param split Development or final audit partition.
 * @param owner Borrowed independently reset actor sessions.
 * @param evaluation Mutable complete outcome report.
 * @return OK after complete isolated evaluation, ERROR otherwise. */
static cgai_status evaluation_all(const cgai_gameplay_model *model, npc_split split,
                                  const npc_evaluation_owner *owner, npc_evaluation *evaluation) {
    for (uint32_t index = 0U; index < NPC_FAMILIES_PER_SPLIT; ++index)
        if (!evaluation_family(split, index, owner, evaluation))
            return CGAI_STATUS_ERROR;
    return composition_sample(model, evaluation);
}

/** @brief Evaluate paired initial conditions with independently generated actor histories.
 * @param model Borrowed trained compatible model.
 * @param memoryless Borrowed equivalent separately trained history-disabled model.
 * @param previous Borrowed pinned previous accepted compatible weights.
 * @param split Isolated development or audit partition.
 * @param evaluation Writable complete report.
 * @return OK after complete measurement, ERROR without publishing a partial report. */
cgai_status npc_evaluate(const cgai_gameplay_model *model, const cgai_gameplay_model *memoryless,
                         const cgai_gameplay_model *previous, npc_split split,
                         npc_evaluation *evaluation) {
    if (evaluation == NULL || (split != NPC_DEV && split != NPC_AUDIT) ||
        !npc_policy_compatible(model) || !npc_policy_compatible(memoryless) ||
        !npc_policy_compatible(previous))
        return cgai_fail("NPC evaluation requires compatible models and an isolated split");
    npc_evaluation_owner owner = {0};
    cgai_status status = evaluation_prepare(model, memoryless, previous, &owner);
    npc_evaluation measured = {0};
    if (status)
        status = evaluation_all(model, split, &owner, &measured);
    evaluation_destroy(&owner);
    if (status)
        *evaluation = measured;
    return status;
}

/** @brief Write one complete metric row following an already written key prefix.
 * @param stream Borrowed writable report stream.
 * @param metrics Borrowed complete metrics.
 * @return Nonzero for a successful formatted write. */
static int write_metrics(FILE *stream, const npc_episode_metrics *metrics) {
    return fprintf(stream, "\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\n",
                   metrics->count, metrics->success, metrics->survived, metrics->deaths,
                   metrics->timeouts, metrics->cost, metrics->blocked, metrics->recoveries,
                   metrics->illegal_attempts, metrics->illegal_executed, metrics->fallbacks) >= 0;
}

/** @brief Write every paired family block without omitting unsuccessful actors.
 * @param stream Borrowed writable report stream.
 * @param split Development or final audit partition.
 * @param evaluation Borrowed complete evaluation.
 * @return Nonzero after all family writes succeed. */
static int write_families(FILE *stream, npc_split split, const npc_evaluation *evaluation) {
    for (uint32_t index = 0U; index < NPC_FAMILIES_PER_SPLIT; ++index) {
        npc_family family;
        if (!npc_family_get(split, index, &family))
            return 0;
        for (size_t actor = 0U; actor < NPC_ACTORS; ++actor)
            if (fprintf(stream, "family\t%u\t%s", family.id, actor_names[actor]) < 0 ||
                !write_metrics(stream, &evaluation->family[index][actor]))
                return 0;
    }
    return 1;
}

/** @brief Write complete overall and principal-mechanic outcome strata.
 * @param stream Borrowed writable report stream.
 * @param evaluation Borrowed complete evaluation.
 * @return Nonzero after all outcome writes succeed. */
static int write_outcomes(FILE *stream, const npc_evaluation *evaluation) {
    for (size_t actor = 0U; actor < NPC_ACTORS; ++actor)
        if (fprintf(stream, "summary\t%s", actor_names[actor]) < 0 ||
            !write_metrics(stream, &evaluation->total[actor]))
            return 0;
    for (size_t mechanic = 0U; mechanic < 3U; ++mechanic)
        for (size_t actor = 0U; actor < NPC_ACTORS; ++actor)
            if (fprintf(stream, "mechanic\t%s\t%s", mechanic_names[mechanic], actor_names[actor]) <
                    0 ||
                !write_metrics(stream, &evaluation->mechanic[mechanic][actor]))
                return 0;
    return 1;
}

/** @brief Write both true mean-loss differences and per-observation absolute diagnostics.
 * @param stream Borrowed writable report stream.
 * @param evaluation Borrowed complete pinned-sample diagnostics.
 * @return Nonzero after every diagnostic write succeeds. */
static int write_composition(FILE *stream, const npc_evaluation *evaluation) {
    if (fprintf(stream, "composition_count\t%zu\nmean_loss\t%.17g\n", evaluation->composition_count,
                evaluation->mean_loss) < 0)
        return 0;
    for (size_t module = 0U; module < 2U; ++module)
        if (fprintf(stream,
                    "module_weight\t%zu\t%.17g\nmodule_contribution\t%zu\t%.17g\n"
                    "module_loss\t%zu\t%.17g\nmodule_loss_change\t%zu\t%.17g\n"
                    "module_mean_loss_difference\t%zu\t%.17g\n",
                    module, evaluation->module_weights[module], module,
                    evaluation->module_contributions[module], module,
                    evaluation->module_loss[module], module, evaluation->module_loss_change[module],
                    module, fabs(evaluation->module_loss[module] - evaluation->mean_loss)) < 0)
            return 0;
    return 1;
}

/** @brief Write all compact episode rows, including every unsuccessful actor rollout.
 * @param path Trusted report destination.
 * @param split Isolated reported partition.
 * @param evaluation Borrowed complete measured results.
 * @return OK after complete write and close, ERROR otherwise. */
cgai_status npc_evaluation_write(const char *path, npc_split split,
                                 const npc_evaluation *evaluation) {
    if (path == NULL || evaluation == NULL || (split != NPC_DEV && split != NPC_AUDIT))
        return cgai_fail("invalid NPC evaluation report arguments");
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
        return cgai_fail("could not open NPC evaluation report");
    int valid = fprintf(stream,
                        "schema\tnpc-evaluation-v3\nsplit\t%s\nfamily_count\t%u\n"
                        "variants_per_family\t%u\nweighting\tequal-family\n"
                        "initialized_seed\t42\nhistory_disabled_fields\t8,9,10,11\n"
                        "composition_sample_identity\t"
                        "dev-first8variants-first8decisions-nonfallback-v3\n",
                        split == NPC_DEV ? "development" : "audit", NPC_FAMILIES_PER_SPLIT,
                        NPC_VARIANTS_PER_FAMILY) >= 0;
    valid = valid && write_families(stream, split, evaluation) &&
            write_outcomes(stream, evaluation) && write_composition(stream, evaluation);
    const int closed = fclose(stream) == 0;
    return valid && closed ? CGAI_STATUS_OK : cgai_fail("incomplete NPC evaluation report");
}

/** @brief Load inference-only artifacts and produce complete comparative episode evidence.
 * @param model_path Trained weights path.
 * @param memoryless_path Equivalent history-disabled weights path.
 * @param previous_path Pinned previous accepted compatible weights path.
 * @param report_path New report destination.
 * @param split Isolated evaluation split.
 * @return OK on complete measurement and write, ERROR otherwise. */
cgai_status npc_evaluation_files(const char *model_path, const char *memoryless_path,
                                 const char *previous_path, const char *report_path,
                                 npc_split split) {
    cgai_gameplay_model *model = cgai_gameplay_load(model_path);
    cgai_gameplay_model *memoryless = cgai_gameplay_load(memoryless_path);
    cgai_gameplay_model *previous = cgai_gameplay_load(previous_path);
    npc_evaluation evaluation;
    const cgai_status status = model != NULL && memoryless != NULL && previous != NULL &&
                                       npc_evaluate(model, memoryless, previous, split, &evaluation)
                                   ? npc_evaluation_write(report_path, split, &evaluation)
                                   : CGAI_STATUS_ERROR;
    cgai_gameplay_destroy(model);
    cgai_gameplay_destroy(memoryless);
    cgai_gameplay_destroy(previous);
    return status;
}

/** @brief Write a complete validated offline resource report.
 * @param path Trusted compact report destination.
 * @param resources Borrowed complete inference ownership report.
 * @param session_bytes Complete persistent adapter and scratch heap.
 * @return OK after complete write and close, ERROR otherwise. */
static cgai_status verification_write(const char *path, const cgai_gameplay_resources *resources,
                                      size_t session_bytes) {
    FILE *stream = path != NULL ? fopen(path, "wb") : NULL;
    if (stream == NULL)
        return cgai_fail("could not open NPC verification report");
    const int written =
        fprintf(stream,
                "schema\tnpc-verify-v3\ncontract_version\t%u\n"
                "shape\tD16-H48-M2-K8-O7\nseed\t42\nactions\t7\n"
                "parameters\t%zu\nmodel_bytes\t%zu\noptimizer_bytes\t%zu\n"
                "neural_session_bytes\t%zu\nadapter_bytes\t%zu\n"
                "session_bytes\t%zu\n",
                3U, resources->parameter_count, resources->model_bytes, resources->optimizer_bytes,
                resources->session_bytes, sizeof(npc_policy_session), session_bytes) >= 0;
    const int closed = fclose(stream) == 0;
    return written && closed ? CGAI_STATUS_OK : cgai_fail("incomplete NPC verification report");
}

/** @brief Verify portable weights and exact profile shape without raw training data.
 * @param model_path Inference-only deployment artifact.
 * @param report_path New compact report destination.
 * @return OK for valid bounded inference ownership and complete report, ERROR otherwise. */
cgai_status npc_verify_file(const char *model_path, const char *report_path) {
    cgai_gameplay_model *model = cgai_gameplay_load(model_path);
    if (model == NULL)
        return CGAI_STATUS_ERROR;
    cgai_gameplay_resources resources;
    size_t session_bytes = 0U;
    cgai_status status = npc_policy_resources(model, &resources, &session_bytes);
    if (status && (resources.optimizer_bytes != 0U || resources.model_bytes > NPC_MODEL_LIMIT))
        status = cgai_fail("NPC deployment weights exceed the inference-only resource contract");
    if (status)
        status = verification_write(report_path, &resources, session_bytes);
    cgai_gameplay_destroy(model);
    return status;
}
