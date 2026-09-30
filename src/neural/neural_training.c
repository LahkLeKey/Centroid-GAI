/** @file neural_training.c @brief Deterministic shuffled Adam optimization of all neural
 * parameters. */
#include "internal/chat_internal.h"
#include "internal/error.h"
#include "internal/model_random.h"
#include "internal/neural_internal.h"
#include "internal/neural_math.h"
#include <math.h>
#include <stdlib.h>

/** Owned per-call optimizer state; no moments survive a training call. */
typedef struct neural_optimizer {
    cgai_neural_model *model;         /**< Borrowed mutable model. */
    cgai_neural_training training;    /**< Copied settings. */
    cgai_token_id *sequence;          /**< Owned teacher-forced targets ending in EOS. */
    size_t count;                     /**< Number of targets. */
    size_t *order;                    /**< Owned shuffled indices, never shuffled tokens. */
    double *storage;                  /**< Owned contiguous gradient and two Adam moments. */
    double *gradient;                 /**< Borrowed first storage slice. */
    double *first;                    /**< Borrowed first-moment slice. */
    double *second;                   /**< Borrowed second-moment slice. */
    cgai_neural_workspace *workspace; /**< Owned forward/backward buffers. */
    uint64_t step;                    /**< Adam step count, wide enough for 32-bit hosts. */
    const cgai_chat_model *chat;      /**< Optional borrowed conversation shape. */
    cgai_chat_dataset dataset;        /**< Owned independent chat records. */
    size_t *record_ids;               /**< Owned mapping from flattened target to dialogue. */
    size_t *positions;                /**< Owned mapping from flattened target to answer offset. */
} neural_optimizer;

static void fill_target_map(neural_optimizer *work);

/** @brief Release all temporary optimizer ownership, accepting partial initialization.
 * @param work Borrowed zero-initialized or prepared optimizer; model is not released. */
static void destroy_optimizer(neural_optimizer *work) {
    /* Step 1: Release each independent allocation, leaving model parameters in place. */
    cgai_neural_workspace_destroy(work->workspace);
    free(work->storage);
    free(work->order);
    free(work->sequence);
    free(work->record_ids);
    free(work->positions);
    cgai_chat_destroy_dataset(&work->dataset);
}

/** @brief Reject unsupported training settings before any model mutation.
 * @param training Borrowed settings.
 * @return OK for bounded finite values, ERROR otherwise. */
static cgai_status validate_training(const cgai_neural_training *training) {
    /* Step 1: Require a positive number of bounded passes and finite Adam settings. */
    if (training->epochs == 0U || training->epochs > 10000U || !isfinite(training->learning_rate) ||
        training->learning_rate <= 0.0 || training->learning_rate > 1.0 ||
        !isfinite(training->gradient_clip) || training->gradient_clip <= 0.0 ||
        training->gradient_clip > 1000.0)
        return cgai_fail("invalid neural training settings");
    return CGAI_STATUS_OK;
}

/** @brief Allocate fresh gradients, moments and inference buffers.
 * @param work Borrowed optimizer with model and sequence already initialized.
 * @return OK on allocation, ERROR with partial ownership retained for cleanup. */
static cgai_status allocate_optimizer(neural_optimizer *work) {
    /* Step 1: Allocate all scratch state before the first parameter update. */
    work->order = malloc(work->count * sizeof(*work->order));
    work->storage = calloc(work->model->parameter_count * 3U, sizeof(double));
    work->workspace = cgai_neural_workspace_create(work->model);
    if (work->order == NULL || work->storage == NULL || work->workspace == NULL)
        return cgai_fail("could not allocate neural optimizer");
    work->gradient = work->storage;
    work->first = work->gradient + work->model->parameter_count;
    work->second = work->first + work->model->parameter_count;
    for (size_t i = 0U; i < work->count; ++i)
        work->order[i] = i;
    return CGAI_STATUS_OK;
}

/** @brief Map a sequence without extending vocabulary, then allocate optimizer buffers.
 * @param work Borrowed optimizer with model and settings populated.
 * @param text Borrowed training sequence.
 * @return OK on preparation, ERROR with partial ownership retained for cleanup. */
static cgai_status prepare_optimizer(neural_optimizer *work, const char *text) {
    /* Step 1: Map text and reject a sequence containing only the appended EOS. */
    size_t unknown = 0U;
    work->sequence = cgai_neural_sequence(work->model, text, &work->count, &unknown);
    if (work->sequence == NULL)
        return CGAI_STATUS_ERROR;
    if (work->count < 2U)
        return cgai_fail("neural training text is empty");
    /* Step 2: Keep allocation separate from sequence validation. */
    return allocate_optimizer(work);
}

/** @brief Shuffle training examples while retaining their original preceding tokens.
 * @param work Borrowed mutable optimizer.
 * @param state Borrowed deterministic RNG state. */
static void shuffle_examples(neural_optimizer *work, uint64_t *state) {
    /* Step 1: Apply Fisher-Yates to target indices; causal sequence order is unchanged. */
    for (size_t i = work->count; i > 1U; --i) {
        const size_t other = (size_t)(cgai_random_next(state) % i);
        const size_t previous = work->order[i - 1U];
        work->order[i - 1U] = work->order[other];
        work->order[other] = previous;
    }
}

/** @brief Compute a finite global norm and clip the entire gradient consistently.
 * @param work Borrowed optimizer containing a complete gradient.
 * @return OK for finite gradients, ERROR before any parameter update otherwise. */
static cgai_status clip_gradient(neural_optimizer *work) {
    /* Step 1: Use hypot so summing squares cannot overflow prematurely. */
    double norm = 0.0;
    for (size_t i = 0U; i < work->model->parameter_count; ++i) {
        if (!isfinite(work->gradient[i]))
            return cgai_fail("nonfinite neural gradient");
        norm = hypot(norm, work->gradient[i]);
    }
    if (!isfinite(norm))
        return cgai_fail("nonfinite neural gradient norm");
    /* Step 2: Scale all parameter groups by the same factor. */
    const double scale =
        norm > work->training.gradient_clip ? work->training.gradient_clip / norm : 1.0;
    for (size_t i = 0U; i < work->model->parameter_count; ++i)
        work->gradient[i] *= scale;
    return CGAI_STATUS_OK;
}

/** @brief Update one scalar with bias-corrected Adam moments.
 * @param work Borrowed mutable optimizer and model.
 * @param index Scalar parameter offset.
 * @param first_correction Positive first-moment bias correction.
 * @param second_correction Positive second-moment bias correction.
 * @return OK on finite update, ERROR otherwise; previous updates remain. */
static cgai_status update_parameter(neural_optimizer *work, size_t index, double first_correction,
                                    double second_correction) {
    /* Step 1: Form running first and second moments from the clipped gradient. */
    const double gradient = work->gradient[index];
    work->first[index] = 0.9 * work->first[index] + 0.1 * gradient;
    work->second[index] = 0.999 * work->second[index] + 0.001 * gradient * gradient;
    /* Step 2: Apply the corrected step, checking it before publishing the scalar. */
    const double delta = work->training.learning_rate * (work->first[index] / first_correction) /
                         (sqrt(work->second[index] / second_correction) + 1e-8);
    const double updated = work->model->parameters[index] - delta;
    if (!isfinite(updated))
        return cgai_fail("nonfinite neural parameter update");
    work->model->parameters[index] = updated;
    return CGAI_STATUS_OK;
}

/** @brief Compute a complete gradient while retaining fixed chat padding embeddings.
 * @param work Borrowed mutable optimizer.
 * @param context Borrowed causal context.
 * @param target Non-BOS next-token target.
 * @return OK for a finite clipped gradient, ERROR before any parameter update. */
static cgai_status prepare_gradient(neural_optimizer *work, const cgai_token_id *context,
                                    cgai_token_id target) {
    if (!cgai_neural_gradient(work->model, context, target, work->workspace, work->gradient))
        return CGAI_STATUS_ERROR;
    /* Chat BOS is constant padding; stored values retain their inference meaning on reload. */
    if (work->chat != NULL)
        for (size_t i = 0U; i < work->model->config.embedding_dimensions; ++i)
            work->gradient[i] = 0.0;
    return clip_gradient(work);
}

/** @brief Differentiate one causal example before updating any parameters.
 * @param work Borrowed mutable optimizer.
 * @param position Target position in the original sequence.
 * @return OK on update, ERROR otherwise with prior steps retained. */
static cgai_status train_example(neural_optimizer *work, size_t position) {
    cgai_token_id context[CGAI_NEURAL_MAX_CONTEXT];
    /* Step 1: Build context from preceding tokens and compute every gradient at old weights. */
    if (work->chat != NULL) {
        const cgai_chat_record *record = &work->dataset.records[work->record_ids[position]];
        cgai_chat_context(work->chat, &record->prompt, record->answer, work->positions[position],
                          context);
    } else
        cgai_neural_context(work->model, work->sequence, position, context);
    if (!prepare_gradient(work, context, work->sequence[position]))
        return CGAI_STATUS_ERROR;
    /* Step 2: Correct startup bias and update all groups using the same optimizer step. */
    ++work->step;
    const double first_correction = 1.0 - pow(0.9, (double)work->step);
    const double second_correction = 1.0 - pow(0.999, (double)work->step);
    for (size_t i = 0U; i < work->model->parameter_count; ++i) {
        if (!update_parameter(work, i, first_correction, second_correction))
            return CGAI_STATUS_ERROR;
    }
    return CGAI_STATUS_OK;
}

/** @brief Execute complete shuffled passes with repeatable example ordering.
 * @param work Borrowed prepared optimizer.
 * @return OK on completion, ERROR on first failed example. */
static cgai_status run_epochs(neural_optimizer *work) {
    /* Step 1: Reset the local shuffle stream for reproducible independent training calls. */
    uint64_t state = work->model->config.seed;
    for (size_t epoch = 0U; epoch < work->training.epochs; ++epoch) {
        shuffle_examples(work, &state);
        /* Step 2: Learn each original context once in this pass, including EOS. */
        for (size_t i = 0U; i < work->count; ++i) {
            if (!train_example(work, work->order[i]))
                return CGAI_STATUS_ERROR;
        }
    }
    return CGAI_STATUS_OK;
}

/** @brief Train all parameter groups with clipped Adam and shuffled causal windows.
 * @param model Mutable handle; prior updates remain on failure.
 * @param text Borrowed nonempty training sequence; unknown words map to UNK.
 * @param requested Borrowed settings, or NULL for defaults.
 * @return OK after all passes, ERROR with a diagnostic otherwise. Each call resets
 * Adam moments. Text is one sequence with BOS padding and a final EOS target. */
cgai_status cgai_neural_train(cgai_neural_model *model, const char *text,
                              const cgai_neural_training *requested) {
    /* Step 1: Validate inputs before acquiring temporary ownership. */
    cgai_error_clear();
    if (model == NULL || text == NULL)
        return cgai_fail("neural model and training text required");
    neural_optimizer work = {0};
    work.model = model;
    work.training = requested ? *requested : cgai_neural_default_training();
    if (!validate_training(&work.training))
        return CGAI_STATUS_ERROR;
    /* Step 2: Prepare and execute, then release scratch state on every path. */
    cgai_status status = prepare_optimizer(&work, text);
    if (status)
        status = run_epochs(&work);
    destroy_optimizer(&work);
    return status;
}

/** @brief Flatten target identities while keeping dialogue boundaries separate.
 * @param work Borrowed optimizer owning a prepared dataset.
 * @return OK or ERROR with partial allocation retained for cleanup. */
static cgai_status flatten_dialogues(neural_optimizer *work) {
    for (size_t i = 0U; i < work->dataset.count; ++i)
        work->count += work->dataset.records[i].count;
    if (work->count == 0U || work->dataset.count == 0U)
        return cgai_fail("empty chat targets");
    work->sequence = calloc(work->count, sizeof(*work->sequence));
    work->record_ids = calloc(work->count, sizeof(*work->record_ids));
    work->positions = calloc(work->count, sizeof(*work->positions));
    if (work->sequence == NULL || work->record_ids == NULL || work->positions == NULL)
        return cgai_fail("could not allocate chat target map");
    fill_target_map(work);
    return allocate_optimizer(work);
}

/** @brief Fill target coordinates after all mapping storage exists.
 * @param work Borrowed optimizer with bounded allocated maps. */
static void fill_target_map(neural_optimizer *work) {
    size_t offset = 0U;
    for (size_t i = 0U; i < work->dataset.count; ++i) {
        for (size_t j = 0U; j < work->dataset.records[i].count; ++j) {
            work->sequence[offset] = work->dataset.records[i].answer[j];
            work->record_ids[offset] = i;
            work->positions[offset++] = j;
        }
    }
}

cgai_status cgai_chat_train(cgai_chat_model *model, const cgai_chat_example *examples, size_t count,
                            const cgai_neural_training *training) {
    cgai_error_clear();
    if (model == NULL)
        return cgai_fail("chat model required");
    neural_optimizer work = {0};
    work.model = model->network;
    work.chat = model;
    work.training = training ? *training : cgai_neural_default_training();
    if (!validate_training(&work.training))
        return CGAI_STATUS_ERROR;
    /* Step 1: One optimizer spans every target, record and epoch. */
    cgai_status status = cgai_chat_prepare_dataset(model, examples, count, &work.dataset);
    if (status)
        status = flatten_dialogues(&work);
    if (status)
        status = run_epochs(&work);
    /* Step 2: Release temporary moments and records on every path. */
    destroy_optimizer(&work);
    return status;
}
