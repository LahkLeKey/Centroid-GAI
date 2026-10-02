/** @file gameplay_training.c @brief Deterministic independent-task joint AdamW continuation. */
#include "gameplay_internal.h"
#include "internal/error.h"
#include "internal/model_random.h"
#include <math.h>
#include <stdlib.h>

/** Owned per-call scratch; parameters and persistent moments remain model-owned. */
typedef struct gameplay_optimizer {
    cgai_gameplay_model *model;            /**< Borrowed exclusive mutable network. */
    const cgai_gameplay_example *examples; /**< Borrowed canonical independent records. */
    cgai_gameplay_training training;       /**< Copied full-pass settings. */
    size_t count;                          /**< Independent record count. */
    size_t parameter_count; /**< Immutable complete scratch width captured before updates. */
    size_t *order;          /**< Owned shuffled indices, rebuilt canonically each epoch. */
    double *gradient;       /**< Owned complete parameter-gradient scratch. */
    cgai_gameplay_session *session; /**< Owned reusable forward and derivative scratch. */
} gameplay_optimizer;
/** Precomputed global clipping and bias correction for one prospective successful update. */
typedef struct gameplay_adam {
    double scale;             /**< Gradient norm clipping multiplier. */
    double first_correction;  /**< One minus the first decay raised to next step. */
    double second_correction; /**< One minus the second decay raised to next step. */
} gameplay_adam;
/** One proposed scalar update, fully validated before publication. */
typedef struct gameplay_update {
    double first;  /**< Proposed first moment. */
    double second; /**< Proposed second moment. */
    double value;  /**< Proposed trainable weight. */
} gameplay_update;

/** @brief Validate complete training settings without changing model state.
 * @param training Borrowed additional-pass settings.
 * @return OK for finite supported settings, ERROR otherwise. */
static cgai_status validate_training(const cgai_gameplay_training *training) {
    /* Step 1: Bound passes, step amplitude and global derivative clipping. */
    if (training == NULL || training->epochs == 0U || training->epochs > 10000U ||
        !isfinite(training->learning_rate) || training->learning_rate <= 0.0 ||
        training->learning_rate > 1.0 || !isfinite(training->gradient_clip) ||
        training->gradient_clip <= 0.0 || training->gradient_clip > 1000.0 ||
        !isfinite(training->weight_decay) || training->weight_decay < 0.0 ||
        training->weight_decay > 1.0)
        return cgai_fail("invalid gameplay training settings");
    return CGAI_STATUS_OK;
}

/** @brief Validate all independent records before acquiring continuation ownership.
 * @param model Borrowed initialized network.
 * @param examples Borrowed ordered task records.
 * @param count Record count.
 * @return OK on complete valid records, ERROR otherwise. */
static cgai_status validate_examples(const cgai_gameplay_model *model,
                                     const cgai_gameplay_example *examples, size_t count) {
    /* Step 1: Require a bounded nonempty complete independent dataset. */
    if (model == NULL || examples == NULL || count == 0U || count > 10000U)
        return cgai_fail("invalid gameplay training records");
    for (size_t i = 0U; i < count; ++i)
        if (!cgai_gameplay_validate_example(model, &examples[i]))
            return CGAI_STATUS_ERROR;
    return CGAI_STATUS_OK;
}

/** @brief Validate persistent optimizer ownership and counter capacity before mutation.
 * @param work Borrowed prepared settings and record count.
 * @return OK for coherent bounded continuation state, ERROR otherwise. */
static cgai_status validate_continuation(const gameplay_optimizer *work) {
    /* Step 1: Successful requested full passes must fit both persisted progress counters. */
    const cgai_gameplay_model *model = work->model;
    if ((uint64_t)work->training.epochs > UINT64_MAX - model->training_epochs ||
        (uint64_t)work->training.epochs > (UINT64_MAX - model->training_step) / work->count)
        return cgai_fail("gameplay continuation progress would overflow");
    /* Step 2: Moments are acquired and retained together for exact Adam continuation. */
    if ((model->adam_first == NULL) != (model->adam_second == NULL) ||
        (model->adam_first == NULL && (model->training_step != 0U || model->training_epochs != 0U)))
        return cgai_fail("invalid gameplay continuation ownership");
    return CGAI_STATUS_OK;
}

/** @brief Allocate both model-owned moment arrays before publishing either.
 * @param model Borrowed mutable network with absent moments.
 * @return OK on complete publication, ERROR without state mutation. */
static cgai_status initialize_moments(cgai_gameplay_model *model) {
    /* Step 1: Acquire complete Adam ownership transactionally. */
    double *first = calloc(model->parameter_count, sizeof(*first));
    double *second = calloc(model->parameter_count, sizeof(*second));
    if (first == NULL || second == NULL) {
        free(first);
        free(second);
        return cgai_fail("could not allocate gameplay Adam moments");
    }
    /* Step 2: Begin the model-local shuffle stream only after complete allocation. */
    model->adam_first = first;
    model->adam_second = second;
    model->training_shuffle = model->config.seed;
    return CGAI_STATUS_OK;
}

/** @brief Release all per-call scratch, accepting partially prepared state.
 * @param work Borrowed optimizer owner; model remains owned by its caller. */
static void destroy_optimizer(gameplay_optimizer *work) {
    /* Step 1: Named model-owned moment arrays and parameters are retained. */
    free(work->order);
    free(work->gradient);
    cgai_gameplay_session_destroy(work->session);
}

/** @brief Acquire scratch before initializing model-owned Adam state.
 * @param work Borrowed complete validated optimizer settings.
 * @return OK on readiness, ERROR with partial scratch retained for cleanup. */
static cgai_status prepare_optimizer(gameplay_optimizer *work) {
    /* Step 1: Validate counters and allocate all per-call numerical workspace first. */
    if (!validate_continuation(work))
        return CGAI_STATUS_ERROR;
    work->order = malloc(work->count * sizeof(*work->order));
    work->parameter_count = work->model->parameter_count;
    work->gradient = calloc(work->parameter_count, sizeof(*work->gradient));
    work->session = cgai_gameplay_session_create(work->model, 0U);
    if (work->order == NULL || work->gradient == NULL || work->session == NULL)
        return cgai_fail("could not allocate gameplay optimizer scratch");
    /* Step 2: Acquire persistent moments only after all validation and scratch acquisition. */
    if (work->model->adam_first == NULL && !initialize_moments(work->model))
        return CGAI_STATUS_ERROR;
    return CGAI_STATUS_OK;
}

/** @brief Produce a complete prospective scalar Adam update without mutation.
 * @param work Borrowed optimizer settings and derivatives.
 * @param adam Borrowed global clipping and bias correction.
 * @param index Parameter scalar index.
 * @return Complete prospective update. */
static gameplay_update propose_update(const gameplay_optimizer *work, const gameplay_adam *adam,
                                      size_t index) {
    /* Step 1: Joint optimization updates all moment streams, including currently zero derivatives.
     */
    const double gradient = work->gradient[index] * adam->scale;
    gameplay_update update;
    update.first = 0.9 * work->model->adam_first[index] + 0.1 * gradient;
    update.second = 0.999 * work->model->adam_second[index] + 0.001 * gradient * gradient;
    update.value = work->model->parameters[index] -
                   work->training.learning_rate * (update.first / adam->first_correction) /
                       (sqrt(update.second / adam->second_correction) + 1e-8);
    /* Step2: Decoupled shrinkage does not enter Adam moments or derivative clipping. */
    if (work->training.weight_decay != 0.0)
        update.value -= work->training.learning_rate * work->training.weight_decay *
                        work->model->parameters[index];
    return update;
}

/** @brief Compute one globally clipped and bias-corrected optimizer step.
 * @param work Borrowed exact gradient and live continuation state.
 * @param adam Writable complete prospective global step values.
 * @return OK for finite norm/corrections, ERROR otherwise. */
static cgai_status prepare_adam(const gameplay_optimizer *work, gameplay_adam *adam) {
    /* Step 1: Reject invalid derivative magnitudes before applying clipping. */
    double squared = 0.0;
    for (size_t i = 0U; i < work->parameter_count; ++i)
        squared += work->gradient[i] * work->gradient[i];
    if (!isfinite(squared))
        return cgai_fail("nonfinite gameplay gradient norm");
    const double norm = sqrt(squared);
    adam->scale = norm > work->training.gradient_clip ? work->training.gradient_clip / norm : 1.0;
    /* Step 2: Persisted successful update count supplies exact continuation bias corrections. */
    const double step = (double)(work->model->training_step + 1U);
    adam->first_correction = 1.0 - pow(0.9, step);
    adam->second_correction = 1.0 - pow(0.999, step);
    return CGAI_STATUS_OK;
}

/** @brief Validate every proposed scalar before any weights or moments are published.
 * @param work Borrowed optimizer with complete finite derivatives.
 * @param adam Borrowed prospective global step.
 * @return OK for complete finite updates, ERROR otherwise. */
static cgai_status validate_updates(const gameplay_optimizer *work, const gameplay_adam *adam) {
    /* Step 1: A single bad scalar rejects the entire current update. */
    for (size_t i = 0U; i < work->parameter_count; ++i) {
        const gameplay_update update = propose_update(work, adam, i);
        if (!isfinite(update.first) || !isfinite(update.second) || update.second < 0.0 ||
            !isfinite(update.value))
            return cgai_fail("nonfinite gameplay Adam update");
    }
    return CGAI_STATUS_OK;
}

/** @brief Validate and then publish one entire parameter/moment update atomically.
 * @param work Borrowed exact gradients and mutable continuation model.
 * @return OK after one complete successful update, ERROR before this update mutates state. */
static cgai_status apply_update(gameplay_optimizer *work) {
    /* Step 1: Preflight every prospective scalar before publishing any weight or moment. */
    gameplay_adam adam;
    if (!prepare_adam(work, &adam) || !validate_updates(work, &adam))
        return CGAI_STATUS_ERROR;
    /* Step 2: Reproduce validated scalars and advance the successful update count last. */
    for (size_t i = 0U; i < work->parameter_count; ++i) {
        const gameplay_update update = propose_update(work, &adam, i);
        work->model->adam_first[i] = update.first;
        work->model->adam_second[i] = update.second;
        work->model->parameters[i] = update.value;
    }
    ++work->model->training_step;
    return CGAI_STATUS_OK;
}

/** @brief Shuffle canonical record indices with the model-owned deterministic stream.
 * @param work Borrowed prepared optimizer and live shuffle stream. */
static void shuffle_records(gameplay_optimizer *work) {
    /* Step 1: Every complete epoch starts from the caller's stable canonical record ordering. */
    for (size_t i = 0U; i < work->count; ++i)
        work->order[i] = i;
    /* Step 2: Fisher-Yates consumes exactly one local random word per swap. */
    for (size_t count = work->count; count > 1U; --count) {
        const size_t selected = (size_t)(cgai_random_next(&work->model->training_shuffle) % count);
        const size_t temporary = work->order[count - 1U];
        work->order[count - 1U] = work->order[selected];
        work->order[selected] = temporary;
    }
}

/** @brief Optimize one complete independently shuffled task epoch.
 * @param work Borrowed prepared optimizer.
 * @return OK on full completion, ERROR preserving earlier successful record updates. */
static cgai_status train_epoch(gameplay_optimizer *work) {
    /* Step 1: Each target gets only its own complete observation and task head. */
    shuffle_records(work);
    for (size_t index = 0U; index < work->count; ++index) {
        const cgai_gameplay_example *example = &work->examples[work->order[index]];
        if (!cgai_gameplay_forward(work->session, &example->state, example->task,
                                   work->model->config.task_modules[example->task]) ||
            !cgai_gameplay_backward(work->session, example, work->gradient) || !apply_update(work))
            return CGAI_STATUS_ERROR;
    }
    /* Step 2: Only a successful complete pass advances the persisted epoch count. */
    ++work->model->training_epochs;
    return CGAI_STATUS_OK;
}

/** @brief Continue deterministic joint independent-task clipped AdamW for complete passes.
 * @param model Borrowed exclusive mutable network.
 * @param examples Borrowed canonical independent targets.
 * @param count Record count, 1..10000.
 * @param training Borrowed bounded additional-pass settings.
 * @return OK after complete passes, ERROR otherwise. Invalid input preserves full model state. */
cgai_status cgai_gameplay_train_continue(cgai_gameplay_model *model,
                                         const cgai_gameplay_example *examples, size_t count,
                                         const cgai_gameplay_training *training) {
    /* Step 1: Validate every record and setting before any continuation-state mutation. */
    if (!validate_training(training) || !validate_examples(model, examples, count))
        return CGAI_STATUS_ERROR;
    gameplay_optimizer work = {0};
    work.model = model;
    work.examples = examples;
    work.training = *training;
    work.count = count;
    /* Step 2: Run successful whole passes and clean up all per-call scratch on every path. */
    cgai_status status = prepare_optimizer(&work);
    for (size_t epoch = 0U; status && epoch < training->epochs; ++epoch)
        status = train_epoch(&work);
    destroy_optimizer(&work);
    return status;
}
