/** @file life_training.c @brief Train and freeze a Life owner using only the public API. */
#include "centroid_life.h"
#include <inttypes.h>
#include <stdio.h>

static cgai_life_status demonstrate(cgai_life *owner) {
    cgai_life_stats trained;
    cgai_life_stats evaluated;
    cgai_life_status status;
    if ((status = cgai_life_train_step(owner, 8U, NULL)) != CGAI_LIFE_OK ||
        (status = cgai_life_get_stats(owner, &trained)) != CGAI_LIFE_OK ||
        (status = cgai_life_evaluate_step(owner, 4U, NULL)) != CGAI_LIFE_OK ||
        (status = cgai_life_get_stats(owner, &evaluated)) != CGAI_LIFE_OK)
        return status;
    if (trained.policy_hash != evaluated.policy_hash ||
        trained.training_updates != evaluated.training_updates ||
        trained.replay_records != evaluated.replay_records)
        return CGAI_LIFE_ENGINE_ERROR;
    printf("generation=%u population=%u updates=%" PRIu64 " frozen=yes\n", evaluated.generation,
           evaluated.population, evaluated.training_updates);
    return CGAI_LIFE_OK;
}

int main(void) {
    const cgai_life_config config = cgai_life_config_default();
    cgai_life *owner = NULL;
    cgai_life_status status = cgai_life_create(&config, &owner);
    if (status == CGAI_LIFE_OK)
        status = demonstrate(owner);
    cgai_life_destroy(owner);
    if (status != CGAI_LIFE_OK)
        fprintf(stderr, "Life operation failed: %d\n", (int)status);
    return status == CGAI_LIFE_OK ? 0 : 1;
}
