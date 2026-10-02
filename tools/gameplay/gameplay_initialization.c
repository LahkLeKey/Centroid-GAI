/** @file gameplay_initialization.c @brief Deterministic training-only class prototypes for centroid
 * specialists. */
#include "gameplay/gameplay_internal.h"
#include "gameplay_tool.h"
#include "internal/error.h"
#include "internal/model_random.h"
#include <stdlib.h>

/** Complete bounded means; no frozen development or test record is consulted. */
typedef struct gameplay_prototypes {
    cgai_gameplay_model *model; /**< Borrowed aligned initially seeded model. */
    cgai_gameplay_session
        *session;        /**< Owned numerical scratch, released before returning model. */
    size_t count;        /**< Number of lexicographic task-local class prototypes. */
    size_t observations; /**< Complete balanced training-only observation count. */
    size_t offsets[CGAI_GAMEPLAY_MAX_TASKS]; /**< Lexicographic class offsets per task. */
    size_t counts[32];    /**< Training-only sample count for every represented class. */
    uint32_t tasks[32];   /**< Task represented by each inner expert slot. */
    uint32_t targets[32]; /**< Task-local label represented by each inner expert slot. */
    double means[32][64]; /**< Shared initial hidden category-class means. */
    double shared[64];    /**< Mean shared hidden representation across all training records. */
} gameplay_prototypes;

/** @brief Fill complete already-bounded lexicographic prototype slots.
 * @param work Borrowed zeroed prototype owner with initialized model.
 */
static void fill_slots(gameplay_prototypes *work) {
    /* Step1: Stable task/label order fixes every expert's semantic prior and replay identity. */
    const cgai_gameplay_config *config = &work->model->config;
    size_t slot = 0U;
    for (uint32_t task = 0U; task < config->task_count; ++task) {
        work->offsets[task] = slot;
        for (uint32_t target = 0U; target < config->output_counts[task]; ++target) {
            work->tasks[slot] = task;
            work->targets[slot] = target;
            ++slot;
        }
    }
}

/** @brief Declare complete lexicographic prototype capacity before any parameter initialization.
 * @param work Borrowed zeroed prototype owner with initialized model.
 * @return OK for complete bank capacity, ERROR otherwise. */
static cgai_status prepare_slots(gameplay_prototypes *work) {
    /* Step1: Each task-local label, including fallback, requires its own aligned prototype. */
    const cgai_gameplay_config *config = &work->model->config;
    for (size_t task = 0U; task < config->task_count; ++task)
        work->count += config->output_counts[task];
    if (work->count > config->centroids_per_module)
        return cgai_fail("gameplay prototype bank lacks complete task-local class capacity");
    /* Step2: Capacity validation makes every subsequent fixed scratch index safe. */
    fill_slots(work);
    return CGAI_STATUS_OK;
}

/** @brief Add one independent training-only initial hidden representation to its class mean.
 * @param work Borrowed prototype means and aligned numerical scratch.
 * @param example Borrowed complete training-only target.
 * @return OK after finite observation scoring, ERROR otherwise. */
static cgai_status observe_class(gameplay_prototypes *work, const cgai_gameplay_example *example) {
    /* Step1: Public scoring validates the full typed record without updating any parameters. */
    double loss = 0.0;
    if (!cgai_gameplay_evaluate(work->session, example, &loss))
        return CGAI_STATUS_ERROR;
    const size_t slot = work->offsets[example->task] + example->target;
    ++work->counts[slot];
    ++work->observations;
    /* Step2: Only the initial shared hidden representation establishes the RBF class prior. */
    for (size_t hidden = 0U; hidden < work->model->config.hidden_dimensions; ++hidden) {
        work->means[slot][hidden] += work->session->hidden[hidden];
        work->shared[hidden] += work->session->hidden[hidden];
    }
    return CGAI_STATUS_OK;
}

/** @brief Collect only canonical balanced training records before publishing any prototype weights.
 * @param work Borrowed zeroed class means with prepared prototype slots.
 * @return OK after complete training-only coverage, ERROR otherwise. */
static cgai_status collect_prototypes(gameplay_prototypes *work) {
    /* Step1: The registry's training-only constructor never requests development/test rows. */
    cgai_gameplay_example examples[GAMEPLAY_TRAINING_COUNT] = {0};
    size_t count = 0U;
    if (!gameplay_fixture_training(examples, GAMEPLAY_TRAINING_COUNT, &count))
        return CGAI_STATUS_ERROR;
    for (size_t i = 0U; i < count; ++i)
        if (!observe_class(work, &examples[i]))
            return CGAI_STATUS_ERROR;
    /* Step2: Every class must have supervision; missing labels cannot receive invented centroids.
     */
    for (size_t slot = 0U; slot < work->count; ++slot)
        if (work->counts[slot] == 0U)
            return cgai_fail("gameplay training records lack complete prototype class coverage");
    return CGAI_STATUS_OK;
}

/** @brief Draw a small seeded module-specific displacement without touching training RNG state.
 * @param random Borrowed prototype-only deterministic random stream.
 * @return Bounded symmetric displacement in minus0.02 through plus0.02. */
static double prototype_noise(uint64_t *random) {
    /* Step1: A separate caller-owned stream preserves absent continuation state. */
    const double fraction = (double)(cgai_random_next(random) >> 11U) * 0x1p-53;
    return (2.0 * fraction - 1.0) * 0.02;
}

/** @brief Position one module and its complete internal class prototype bank.
 * @param work Borrowed complete initial training-only class means.
 * @param module Specialist index.
 * @param random Borrowed deterministic prototype-only random stream. */
static void position_module(const gameplay_prototypes *work, size_t module, uint64_t *random) {
    /* Step1: Both modules share aligned training means and distinct small learned-center seeds. */
    cgai_gameplay_model *model = work->model;
    const size_t hidden = model->config.hidden_dimensions;
    for (size_t coordinate = 0U; coordinate < hidden; ++coordinate)
        model->outer[module * hidden + coordinate] =
            work->shared[coordinate] / (double)work->observations + prototype_noise(random);
    /* Step2: Every represented task/label gets one independently perturbed internal centroid. */
    for (size_t slot = 0U; slot < work->count; ++slot)
        for (size_t coordinate = 0U; coordinate < hidden; ++coordinate)
            model->inner[(module * model->config.centroids_per_module + slot) * hidden +
                         coordinate] = work->means[slot][coordinate] / (double)work->counts[slot] +
                                       prototype_noise(random);
}

/** @brief Give each represented class an explicit trainable task-local expert bias prior.
 * @param work Borrowed complete prototype ownership.
 * @param module Specialist module index. */
static void label_module(const gameplay_prototypes *work, size_t module) {
    /* Step1: Experts for another task retain a uniform task-local categorical distribution. */
    cgai_gameplay_model *model = work->model;
    const size_t bank = model->config.centroids_per_module;
    for (size_t task = 0U; task < model->config.task_count; ++task)
        for (size_t slot = 0U; slot < bank; ++slot)
            for (size_t output = 0U; output < model->config.output_counts[task]; ++output) {
                const size_t index =
                    (module * bank + slot) * model->config.output_counts[task] + output;
                model->heads[task][index] = slot < work->count && work->tasks[slot] == task
                                                ? (work->targets[slot] == output ? 2.0 : -2.0)
                                                : 0.0;
            }
}

/** @brief Seed all module centroids and expert bias priors from validated training-only means.
 * @param work Borrowed complete prototype owner. */
static void initialize_prototypes(const gameplay_prototypes *work) {
    /* Step1: Mean-based initialization uses no teacher examples in the optimizer itself. */
    uint64_t random = work->model->config.seed ^ UINT64_C(0x50524f544f545950);
    for (size_t module = 0U; module < work->model->config.module_count; ++module) {
        position_module(work, module, &random);
        label_module(work, module);
    }
}

/** @brief Create an aligned model with deterministic training-only RBF class-prototype priors.
 * @return Owned initialized model with absent optimizer/progress, or NULL with a diagnostic. */
cgai_gameplay_model *gameplay_tool_create(void) {
    /* Step1: Keep the generic seeded encoder and zero module-local conditional readouts. */
    const cgai_gameplay_config config = gameplay_fixture_config();
    cgai_gameplay_model *model = cgai_gameplay_create(&config);
    if (model == NULL)
        return NULL;
    gameplay_prototypes work = {0};
    work.model = model;
    work.session = cgai_gameplay_session_create(model, 0U);
    /* Step2: Complete capacity/coverage validation precedes all prototype parameter writes. */
    const cgai_status ready =
        work.session != NULL && prepare_slots(&work) && collect_prototypes(&work);
    if (ready)
        initialize_prototypes(&work);
    cgai_gameplay_session_destroy(work.session);
    if (!ready) {
        cgai_gameplay_destroy(model);
        return NULL;
    }
    return model;
}
