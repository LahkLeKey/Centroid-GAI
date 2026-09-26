#include "centroid_gai.h"
#include "internal/cgai_internal.h"
#include "internal/model_io.h"
#include "internal/model_validation.h"
#include "internal/vocabulary.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Owned source models and expected composition results. */
typedef struct composition_fixture {
    cgai_model *a;         /**< First trained source. */
    cgai_model *b;         /**< Second trained source. */
    cgai_model *preserved; /**< Owned preserved union. */
    cgai_model *compact;   /**< Owned compacted union. */
    uint8_t *before;       /**< Original first-source bytes. */
    size_t before_size;    /**< Length of the original artifact. */
} composition_fixture;

/**
 * @brief Sum observations of one spelling across initialized centroid rows.
 * @param model
 * Borrowed model, kept alive for the operation.
 * @param token Borrowed NUL-terminated token spelling.
 * @return Total observed targets for the spelling, or zero when absent.
 */
static uint64_t total_token(const cgai_model *model, const char *token) {
    const cgai_token_id id = cgai_vocabulary_find(model, token);
    if (!cgai_token_id_is_valid(id))
        return 0U;
    uint64_t total = 0U;
    for (size_t c = 0; c < model->initialized_centroids; ++c)
        total += model->token_counts[c * model->vocabulary_size + id.value];
    return total;
}

/**
 * @brief Train independent source models and capture original artifact bytes.
 * @param f Composition fixture owned by the parent test.
 * @return Zero on success; a failed assertion reports the violated invariant.
 */
static int setup_fixture(composition_fixture *f) {
    cgai_config config = {8U, 3U, 2U, 42U};
    f->a = cgai_model_create(&config);
    config.centroid_count = 5U;
    f->b = cgai_model_create(&config);
    TEST_CHECK(f->a && f->b, cgai_last_error());
    TEST_CHECK(cgai_model_train_text(f->a, "shared alpha alpha red") == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_model_train_text(f->b, "blue shared beta beta blue") == CGAI_STATUS_OK,
               cgai_last_error());
    TEST_CHECK(cgai_model_encode(f->a, NULL, 0U, &f->before_size) == CGAI_STATUS_OK,
               cgai_last_error());
    f->before = (uint8_t *)malloc(f->before_size);
    TEST_CHECK(f->before, "test allocation");
    TEST_CHECK(cgai_model_encode(f->a, f->before, f->before_size, &f->before_size) ==
                   CGAI_STATUS_OK,
               cgai_last_error());
    return 0;
}

/**
 * @brief Verify preserved rows, vocabulary remapping, and conserved token totals.
 * @param f Composition fixture owned by the parent test.
 * @return Zero on success; a failed assertion reports the violated invariant.
 */
static int check_preserved(composition_fixture *f) {
    const cgai_model *sources[] = {f->a, f->b};
    f->preserved = cgai_model_merge(sources, 2U, 0U);
    TEST_CHECK(f->preserved && f->preserved->initialized_centroids == 8U, cgai_last_error());
    TEST_CHECK(f->preserved->examples_seen == f->a->examples_seen + f->b->examples_seen,
               "examples conserved");
    TEST_CHECK(memcmp(f->preserved->centroids, f->a->centroids, 3U * 8U * sizeof(float)) == 0,
               "preserved vectors");
    for (size_t t = 0; t < f->preserved->vocabulary_size; ++t) {
        const char *token = f->preserved->vocabulary[t];
        TEST_CHECK(total_token(f->preserved, token) ==
                       total_token(f->a, token) + total_token(f->b, token),
                   "remapped token counts");
    }
    return 0;
}

/**
 * @brief Verify compacted means against observation-weighted source vectors.
 * @param f Composition fixture owned by the parent test.
 * @return Zero on success; a failed assertion reports the violated invariant.
 */
static int check_weighted_mean(const composition_fixture *f) {
    const cgai_model *sources[] = {f->a, f->b};
    for (size_t d = 0; d < f->compact->config.dimensions; ++d) {
        double weighted = 0.0;
        for (size_t s = 0; s < 2U; ++s)
            for (size_t c = 0; c < sources[s]->initialized_centroids; ++c)
                weighted += sources[s]->centroids[c * f->compact->config.dimensions + d] *
                            (double)sources[s]->cluster_sizes[c];
        TEST_CHECK(fabs(f->compact->centroids[d] - weighted / (double)f->compact->examples_seen) <
                       1e-6,
                   "weighted mean");
    }
    return 0;
}

/**
 * @brief Verify compaction and subsequent superset repacking conserve observations.
 * @param f Composition fixture owned by the parent test.
 * @return Zero on success; a failed assertion reports the violated invariant.
 */
static int check_compact(composition_fixture *f) {
    const cgai_model *sources[] = {f->a, f->b};
    f->compact = cgai_model_merge(sources, 2U, 1U);
    TEST_CHECK(f->compact && f->compact->initialized_centroids == 1U, cgai_last_error());
    TEST_CHECK(cgai_model_validate_statistics(f->compact) == CGAI_STATUS_OK, cgai_last_error());
    TEST_CHECK(check_weighted_mean(f) == 0, "weighted components");
    for (size_t t = 0; t < f->compact->vocabulary_size; ++t)
        TEST_CHECK(total_token(f->compact, f->compact->vocabulary[t]) ==
                       total_token(f->preserved, f->compact->vocabulary[t]),
                   "compacted counts conserved");
    const cgai_model *single[] = {f->preserved};
    cgai_model *repacked = cgai_model_merge(single, 1U, 2U);
    TEST_CHECK(repacked && repacked->examples_seen == f->preserved->examples_seen,
               "superset compaction");
    cgai_model_destroy(repacked);
    return 0;
}

/**
 * @brief Verify composed artifacts survive serialization and independent import.
 * @param model Borrowed model, kept alive for the operation.
 * @return Zero on success; a failed assertion reports the violated invariant.
 */
static int check_roundtrip(const cgai_model *model) {
    size_t size = 0U;
    TEST_CHECK(cgai_model_encode(model, NULL, 0U, &size) == CGAI_STATUS_OK, cgai_last_error());
    uint8_t *encoded = (uint8_t *)malloc(size);
    TEST_CHECK(encoded, "test allocation");
    TEST_CHECK(cgai_model_encode(model, encoded, size, &size) == CGAI_STATUS_OK, cgai_last_error());
    cgai_model *restored = cgai_model_decode(encoded, size);
    TEST_CHECK(restored && restored->examples_seen == model->examples_seen, cgai_last_error());
    free(encoded);
    cgai_model_destroy(restored);
    return 0;
}

/**
 * @brief Compare source bytes before and after model composition.
 * @param f Composition fixture owned by the parent test.
 * @return Zero on success; a failed assertion reports the violated invariant.
 */
static int check_source_unchanged(composition_fixture *f) {
    uint8_t *after = (uint8_t *)malloc(f->before_size);
    TEST_CHECK(after, "test allocation");
    TEST_CHECK(cgai_model_encode(f->a, after, f->before_size, &f->before_size) == CGAI_STATUS_OK &&
                   !memcmp(f->before, after, f->before_size),
               "source unchanged");
    free(after);
    return 0;
}

/**
 * @brief Verify incompatible spaces, duplicate sources, and oversized targets fail.
 * @param f Composition fixture owned by the parent test.
 * @return Zero on success; a failed assertion reports the violated invariant.
 */
static int check_incompatible(const composition_fixture *f) {
    const cgai_model *sources[] = {f->a, f->b};
    f->b->config.seed++;
    TEST_CHECK(!cgai_model_merge(sources, 2U, 0U), "seed mismatch rejected");
    f->b->config.seed--;
    f->b->config.context_window++;
    TEST_CHECK(!cgai_model_merge(sources, 2U, 0U), "context mismatch rejected");
    f->b->config.context_window--;
    TEST_CHECK(!cgai_model_merge(sources, 2U, 9U), "oversized target rejected");
    const cgai_model *duplicate[] = {f->a, f->a};
    TEST_CHECK(!cgai_model_merge(duplicate, 2U, 0U), "duplicate source rejected");
    return 0;
}

/**
 * @brief Verify inconsistent counts and nonfinite centroids are rejected.
 * @param f Composition fixture owned by the parent test.
 * @return Zero on success; a failed assertion reports the violated invariant.
 */
static int check_invalid_statistics(const composition_fixture *f) {
    const cgai_model *sources[] = {f->a, f->b};
    f->b->cluster_sizes[0]++;
    TEST_CHECK(!cgai_model_merge(sources, 2U, 1U), "invalid counts rejected");
    f->b->cluster_sizes[0]--;
    f->b->centroids[0] = NAN;
    TEST_CHECK(!cgai_model_merge(sources, 2U, 0U), "nonfinite rejected");
    return 0;
}

/**
 * @brief Release every model and byte buffer owned by the composition fixture.
 * @param f Composition fixture owned by the parent test.
 */
static void cleanup_fixture(composition_fixture *f) {
    cgai_model_destroy(f->compact);
    cgai_model_destroy(f->preserved);
    cgai_model_destroy(f->a);
    cgai_model_destroy(f->b);
    free(f->before);
}

/**
 * @brief Run model composition conservation, compatibility, and ownership checks.
 * @return Zero on success; a failed assertion reports the violated invariant.
 */
int test_composition(void) {
    composition_fixture f = {0};
    const int result = setup_fixture(&f) || check_preserved(&f) || check_compact(&f) ||
                       check_roundtrip(f.compact) || check_source_unchanged(&f) ||
                       check_incompatible(&f) || check_invalid_statistics(&f);
    cleanup_fixture(&f);
    return result;
}
