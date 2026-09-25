#ifndef CGAI_MODEL_VALIDATION_H
#define CGAI_MODEL_VALIDATION_H
#include "cgai_internal.h"
/* Check finite vectors, reserved vocabulary, and exact count conservation. */
cgai_status cgai_model_validate_statistics(const cgai_model *model);
#endif
