/** @file test_gameplay_initialization.c @brief Training-only class priors and seeded prototype
 * replay. */
#include "gameplay/gameplay_internal.h"
#include "gameplay_tool.h"
#include "internal/file_utils.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Bounded expected initial hidden means, reconstructed independently from training records. */
typedef struct expected_prototypes {
    size_t count;                            /**< Complete lexicographic class count. */
    size_t observations;                     /**< Balanced training-only record count. */
    size_t offsets[CGAI_GAMEPLAY_MAX_TASKS]; /**< First prototype for each task. */
    size_t counts[32];                       /**< Training sample count per task-local class. */
    double means[32][64]; /**< Raw generic encoder means before prototype seeding. */
    double shared[64];    /**< Mean initial hidden across the balanced training-only stream. */
} expected_prototypes;
/** Test-owned exact fresh checkpoint destinations. */
static const char first_path[] = "gameplay-initialization-first.cgcheckpoint";
/** Independently initialized comparison checkpoint. */
static const char second_path[] = "gameplay-initialization-second.cgcheckpoint";

/** @brief Check a complete training record corresponds to the frozen training split only.
 * @param example Borrowed canonical training record. */
static void training_membership(const cgai_gameplay_example *example) {
    /* Step1: Test-only membership inspection checks generated records against the complete
     * registry. */
    size_t matches = 0U;
    for (size_t index = 0U; index < GAMEPLAY_FIXTURE_CASE_COUNT; ++index) {
        gameplay_fixture_case candidate;
        TEST_CHECK(gameplay_fixture_get(index, &candidate) == CGAI_STATUS_OK, cgai_last_error());
        if (candidate.example.task == example->task &&
            memcmp(&candidate.example.state, &example->state, sizeof(example->state)) == 0) {
            TEST_CHECK(candidate.split == GAMEPLAY_TRAINING &&
                           candidate.example.target == example->target,
                       "development/test observation entered prototype initialization records");
            ++matches;
        }
    }
    TEST_CHECK(matches == 1U, "prototype training record lacks unique frozen training membership");
}

/** @brief Add independently scored raw initial hidden values to a training-only class mean.
 * @param session Exclusive generic seeded-model scratch.
 * @param expected Borrowed expected aggregate means.
 * @param example Borrowed canonical training-only target. */
static void observe_expected(cgai_gameplay_session *session, expected_prototypes *expected,
                             const cgai_gameplay_example *example) {
    /* Step1: Prototype seeding must not change the generic shared encoder's coordinate system. */
    training_membership(example);
    double loss = 0.0;
    TEST_CHECK(cgai_gameplay_evaluate(session, example, &loss) == CGAI_STATUS_OK,
               cgai_last_error());
    const size_t slot = expected->offsets[example->task] + example->target;
    ++expected->counts[slot];
    ++expected->observations;
    for (size_t hidden = 0U; hidden < session->model->config.hidden_dimensions; ++hidden) {
        expected->means[slot][hidden] += session->hidden[hidden];
        expected->shared[hidden] += session->hidden[hidden];
    }
}

/** @brief Reconstruct initial class means using the canonical training-only constructor.
 * @param model Borrowed generic seeded aligned model.
 * @param expected Writable zeroed expected means. */
static void collect_expected(const cgai_gameplay_model *model, expected_prototypes *expected) {
    /* Step1: Lexicographic task-local output domains require one supervised prototype each. */
    for (size_t task = 0U; task < model->config.task_count; ++task) {
        expected->offsets[task] = expected->count;
        expected->count += model->config.output_counts[task];
    }
    TEST_CHECK(expected->count <= 32U, "prototype fixture exceeds bounded expected means");
    cgai_gameplay_example examples[GAMEPLAY_TRAINING_COUNT] = {0};
    size_t count = 0U;
    TEST_CHECK(gameplay_fixture_training(examples, GAMEPLAY_TRAINING_COUNT, &count) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(count == 384U, "prototype record stream changed its balanced training-only count");
    /* Step2: Score only these training observations before any initialization helper writes. */
    cgai_gameplay_session *session = cgai_gameplay_session_create(model, 0U);
    TEST_CHECK(session != NULL, cgai_last_error());
    for (size_t i = 0U; i < count; ++i)
        observe_expected(session, expected, &examples[i]);
    cgai_gameplay_session_destroy(session);
}

/** @brief Verify all class/outer centroids are their training-only initial hidden means plus small
 * noise.
 * @param model Borrowed prototype-initialized model.
 * @param expected Borrowed independently reconstructed means.
 * @param module Specialist index whose class and outer coordinates are checked. */
static void module_means(const cgai_gameplay_model *model, const expected_prototypes *expected,
                         size_t module) {
    /* Step1: Both specialist centers share the same initial training-only representation. */
    const size_t hidden = model->config.hidden_dimensions;
    for (size_t coordinate = 0U; coordinate < hidden; ++coordinate)
        TEST_CHECK(fabs(model->outer[module * hidden + coordinate] -
                        expected->shared[coordinate] / (double)expected->observations) <=
                       0.020000000000001,
                   "outer centroid is not the training-only mean plus bounded seed noise");
    /* Step2: Each module has one deterministic specialist centroid for every task-local label.
     */
    for (size_t slot = 0U; slot < expected->count; ++slot)
        for (size_t coordinate = 0U; coordinate < hidden; ++coordinate) {
            const size_t index =
                (module * model->config.centroids_per_module + slot) * hidden + coordinate;
            TEST_CHECK(fabs(model->inner[index] -
                            expected->means[slot][coordinate] / (double)expected->counts[slot]) <=
                           0.020000000000001,
                       "inner expert centroid is not its training-only class prototype");
        }
}

/** @brief Verify complete training-only class coverage and bounded centroid priors.
 * @param model Borrowed prototype-initialized model.
 * @param expected Borrowed independently reconstructed training means. */
static void centroid_means(const cgai_gameplay_model *model, const expected_prototypes *expected) {
    /* Step1: Every label must be supervised before any class prototype weight is published. */
    for (size_t slot = 0U; slot < expected->count; ++slot)
        TEST_CHECK(expected->counts[slot] != 0U, "prototype class lacks training-only supervision");
    for (size_t module = 0U; module < model->config.module_count; ++module)
        module_means(model, expected, module);
}

/** @brief Verify class expert biases have semantic task-local priors while other heads stay
 * uniform.
 * @param model Borrowed prototype-initialized model.
 * @param expected Borrowed lexicographic prototype offsets.
 * @param task Requested task-local output head.
 * @param module Specialist module containing this expert.
 * @param slot Lexicographic class-prototype expert index. */
static void slot_prior(const cgai_gameplay_model *model, const expected_prototypes *expected,
                       size_t task, size_t module, size_t slot) {
    /* Step1: One represented task/label has matching expert bias; every other head remains uniform.
     */
    const size_t bank = model->config.centroids_per_module;
    for (size_t output = 0U; output < model->config.output_counts[task]; ++output) {
        const int represented = slot >= expected->offsets[task] &&
                                slot < expected->offsets[task] + model->config.output_counts[task];
        const double bias =
            represented ? (slot - expected->offsets[task] == output ? 2.0 : -2.0) : 0.0;
        const size_t index = (module * bank + slot) * model->config.output_counts[task] + output;
        TEST_CHECK(model->heads[task][index] == bias,
                   "prototype expert bias does not represent its declared task-local label");
    }
}

/** @brief Verify every module expert's task-local label prior and uniform incompatible heads.
 * @param model Borrowed prototype-initialized model.
 * @param expected Borrowed lexicographic prototype offsets. */
static void expert_priors(const cgai_gameplay_model *model, const expected_prototypes *expected) {
    /* Step1: Prior numbering is local to each task and repeated consistently in every module bank.
     */
    for (size_t task = 0U; task < model->config.task_count; ++task)
        for (size_t module = 0U; module < model->config.module_count; ++module)
            for (size_t slot = 0U; slot < model->config.centroids_per_module; ++slot)
                slot_prior(model, expected, task, module, slot);
}

/** @brief Require shared encoder, zero readouts and absent optimizer state remain unchanged.
 * @param raw Borrowed generic seeded baseline model.
 * @param seeded Borrowed prototype-initialized aligned model. */
static void unchanged_shared(const cgai_gameplay_model *raw, const cgai_gameplay_model *seeded) {
    /* Step1: Prototype seeding aligns specialists inside the existing shared initial neural
     * representation. */
    const size_t shared = (size_t)(raw->outer - raw->parameters);
    TEST_CHECK(memcmp(raw->parameters, seeded->parameters, shared * sizeof(double)) == 0,
               "prototype initialization changed seeded embeddings or shared encoder");
    for (size_t task = 0U; task < seeded->config.task_count; ++task)
        for (size_t index = 0U;
             index < seeded->config.module_count * seeded->config.output_counts[task] *
                         seeded->config.hidden_dimensions;
             ++index)
            TEST_CHECK(seeded->decoders[task][index] == 0.0,
                       "prototype initialization changed neutral module readouts");
    /* Step2: Initializing a supervised prior does not consume Adam steps or continuation shuffling.
     */
    TEST_CHECK(seeded->adam_first == NULL && seeded->adam_second == NULL &&
                   seeded->training_step == 0U && seeded->training_epochs == 0U &&
                   seeded->training_shuffle == 0U,
               "prototype construction entered training continuation state");
    TEST_CHECK(memcmp(seeded->inner,
                      seeded->inner +
                          seeded->config.centroids_per_module * seeded->config.hidden_dimensions,
                      seeded->config.centroids_per_module * seeded->config.hidden_dimensions *
                          sizeof(double)) != 0,
               "module prototype perturbations failed to break specialist symmetry");
}

/** @brief Require two independently created prototype models serialize exact fresh state
 * identically.
 * @param first Borrowed prototype-initialized model.
 * @param second Borrowed independently prototype-initialized model. */
static void deterministic_checkpoint(const cgai_gameplay_model *first,
                                     const cgai_gameplay_model *second) {
    /* Step1: Compare complete fresh configuration, exact parameters and zero continuation state. */
    TEST_CHECK(cgai_gameplay_checkpoint_save(first, first_path) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_gameplay_checkpoint_save(second, second_path) == CGAI_STATUS_OK,
               cgai_last_error());
    uint8_t *left = NULL;
    uint8_t *right = NULL;
    size_t left_count = 0U;
    size_t right_count = 0U;
    TEST_CHECK(cgai_file_read_all(first_path, &left, &left_count) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_file_read_all(second_path, &right, &right_count) == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(left_count == right_count && memcmp(left, right, left_count) == 0,
               "prototype initialization is not exactly checkpoint replayable");
    free(left);
    free(right);
    TEST_CHECK(remove(first_path) == 0 && remove(second_path) == 0,
               "could not remove prototype initialization checkpoint fixtures");
}

/** @brief Verify conventional supervised class prototypes use only frozen training data and replay
 * exactly.
 * @return Zero after every prior, coverage and deterministic-state invariant passes. */
int main(void) {
    /* Step1: Reconstruct means independently from the unchanged generic shared encoder. */
    const cgai_gameplay_config config = gameplay_fixture_config();
    cgai_gameplay_model *raw = cgai_gameplay_create(&config);
    TEST_CHECK(raw != NULL, cgai_last_error());
    expected_prototypes expected = {0};
    collect_expected(raw, &expected);
    /* Step2: Verify every initialized centroid/expert prior and exact independent replay. */
    cgai_gameplay_model *first = gameplay_tool_create();
    cgai_gameplay_model *second = gameplay_tool_create();
    TEST_CHECK(first != NULL && second != NULL, cgai_last_error());
    unchanged_shared(raw, first);
    centroid_means(first, &expected);
    expert_priors(first, &expected);
    deterministic_checkpoint(first, second);
    cgai_gameplay_destroy(second);
    cgai_gameplay_destroy(first);
    cgai_gameplay_destroy(raw);
    return 0;
}
