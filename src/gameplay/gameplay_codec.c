/** @file gameplay_codec.c @brief Bounded portable composed weights and exact Adam checkpoints. */
#include "gameplay_internal.h"
#include "internal/error.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Maximum inference artifact bytes. */
#define GAMEPLAY_MODEL_FILE_LIMIT (64UL * 1024UL * 1024UL)
/** Maximum exact checkpoint bytes. */
#define GAMEPLAY_CHECKPOINT_FILE_LIMIT (256UL * 1024UL * 1024UL)

/** @brief Read one bounded complete line and remove only its line-ending bytes.
 * @param stream Borrowed readable stream.
 * @param line Writable512-byte line buffer.
 * @return Nonzero for a complete LF or CRLF line. */
static int read_line(FILE *stream, char *line) {
    /* Step1: Reject truncated or unterminated metadata before splitting it. */
    if (fgets(line, 512, stream) == NULL)
        return 0;
    const size_t length = strlen(line);
    if (length == 0U || line[length - 1U] != '\n')
        return 0;
    line[length - 1U] = '\0';
    if (length > 1U && line[length - 2U] == '\r')
        line[length - 2U] = '\0';
    return 1;
}

/** @brief Read one bounded unambiguous scalar line in declared schema order.
 * @param stream Borrowed readable stream.
 * @param expected Required key spelling.
 * @param value Writable256-byte scalar token.
 * @return Nonzero for one exact key/value line, zero otherwise. */
static int read_value(FILE *stream, const char *expected, char *value) {
    /* Step1: Require a complete bounded line and admit only LF or CRLF endings. */
    char line[512] = {0};
    if (!read_line(stream, line))
        return 0;
    /* Step2: One literal space separates a fixed key from a nonempty scalar token. */
    char *separator = strchr(line, ' ');
    if (separator == NULL)
        return 0;
    *separator = '\0';
    const char *token = separator + 1;
    if (strcmp(line, expected) != 0 || token[0] == '\0' || strlen(token) >= 256U ||
        strpbrk(token, " \t\r\n") != NULL)
        return 0;
    memcpy(value, token, strlen(token) + 1U);
    return 1;
}

/** @brief Parse one unsigned decimal scalar without signs, overflow or trailing data.
 * @param stream Borrowed input stream.
 * @param key Required schema key.
 * @param value Writable decoded scalar.
 * @return Nonzero on complete checked parsing. */
static int read_u64(FILE *stream, const char *key, uint64_t *value) {
    /* Step1: Reject syntax before using the standard overflow-aware conversion. */
    char token[256] = {0};
    if (!read_value(stream, key, token) || token[0] < '0' || token[0] > '9')
        return 0;
    errno = 0;
    char *end = NULL;
    const unsigned long long parsed = strtoull(token, &end, 10);
    /* Step2: Publish only a representable uint64 and complete token. */
    if (errno != 0 || *end != '\0' || parsed > UINT64_MAX)
        return 0;
    *value = (uint64_t)parsed;
    return 1;
}

/** @brief Parse one bounded platform-size scalar.
 * @param stream Borrowed input stream.
 * @param key Required schema key.
 * @param value Writable decoded size.
 * @return Nonzero on complete checked parsing. */
static int read_size(FILE *stream, const char *key, size_t *value) {
    /* Step1: Read at the wire width before checking the host allocation width. */
    uint64_t parsed = 0U;
    if (!read_u64(stream, key, &parsed) || parsed > SIZE_MAX)
        return 0;
    *value = (size_t)parsed;
    return 1;
}

/** @brief Parse one bounded categorical cardinality scalar.
 * @param stream Borrowed input stream.
 * @param key Required schema key.
 * @param value Writable decoded cardinality.
 * @return Nonzero on complete checked parsing. */
static int read_u32(FILE *stream, const char *key, uint32_t *value) {
    /* Step1: Narrow only after the full wire integer has passed its bound. */
    uint64_t parsed = 0U;
    if (!read_u64(stream, key, &parsed) || parsed > UINT32_MAX)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

/** @brief Parse an exact finite hexadecimal floating-point token.
 * @param stream Borrowed input stream.
 * @param key Required schema key.
 * @param value Writable finite decoded real.
 * @return Nonzero on complete checked parsing. */
static int read_real(FILE *stream, const char *key, double *value) {
    /* Step1: Hexadecimal storage retains doubles across portable C11 implementations. */
    char token[256] = {0};
    if (!read_value(stream, key, token))
        return 0;
    const char *magnitude = token + (token[0] == '-' ? 1U : 0U);
    if (strncmp(magnitude, "0x", 2U) != 0 || strchr(magnitude, 'p') == NULL)
        return 0;
    errno = 0;
    char *end = NULL;
    const double parsed = strtod(token, &end);
    /* Step2: Representable subnormals are allowed; nonfinite or incomplete values are rejected. */
    if (*end != '\0' || !isfinite(parsed) || (errno == ERANGE && parsed == 0.0))
        return 0;
    *value = parsed;
    return 1;
}

/** @brief Write the fixed scalar architecture fields.
 * @param stream Borrowed output stream.
 * @param config Borrowed validated shape.
 * @return Nonzero when every formatted write succeeds. */
static int write_shape(FILE *stream, const cgai_gameplay_config *config) {
    /* Step1: Use explicit widths and lossless temperature spelling. */
    return fprintf(stream,
                   "seed %llu\nrouting_temperature %a\nfeature_count %zu\n"
                   "embedding_dimensions %zu\nhidden_dimensions %zu\nmodule_count %zu\n"
                   "centroids_per_module %zu\ntask_count %zu\n",
                   (unsigned long long)config->seed, config->routing_temperature,
                   config->feature_count, config->embedding_dimensions, config->hidden_dimensions,
                   config->module_count, config->centroids_per_module, config->task_count) >= 0;
}

/** @brief Read the fixed scalar architecture fields before any allocation.
 * @param stream Borrowed input stream.
 * @param config Writable zeroed shape.
 * @return Nonzero on supported scalar syntax. */
static int read_shape(FILE *stream, cgai_gameplay_config *config) {
    /* Step1: Array lengths are checked before their loops can read additional input. */
    return read_u64(stream, "seed", &config->seed) &&
           read_real(stream, "routing_temperature", &config->routing_temperature) &&
           read_size(stream, "feature_count", &config->feature_count) &&
           read_size(stream, "embedding_dimensions", &config->embedding_dimensions) &&
           read_size(stream, "hidden_dimensions", &config->hidden_dimensions) &&
           read_size(stream, "module_count", &config->module_count) &&
           read_size(stream, "centroids_per_module", &config->centroids_per_module) &&
           read_size(stream, "task_count", &config->task_count) &&
           config->feature_count <= CGAI_GAMEPLAY_MAX_FEATURES &&
           config->task_count <= CGAI_GAMEPLAY_MAX_TASKS;
}

/** @brief Write ordered feature categories and task domains/eligibility.
 * @param stream Borrowed output stream.
 * @param config Borrowed validated shape.
 * @return Nonzero when all declared fields are written. */
static int write_schema(FILE *stream, const cgai_gameplay_config *config) {
    /* Step1: Feature order fixes category embedding offsets. */
    for (size_t index = 0U; index < config->feature_count; ++index)
        if (fprintf(stream, "cardinality %u\n", (unsigned)config->cardinalities[index]) < 0)
            return 0;
    /* Step2: Each head has its own output domain and explicit module/feature admission. */
    for (size_t index = 0U; index < config->task_count; ++index)
        if (fprintf(stream, "output_count %u\ntask_modules %llu\ntask_features %llu\n",
                    (unsigned)config->output_counts[index],
                    (unsigned long long)config->task_modules[index],
                    (unsigned long long)config->task_features[index]) < 0)
            return 0;
    return 1;
}

/** @brief Read ordered category and task metadata before allocation.
 * @param stream Borrowed input stream.
 * @param config Writable bounded scalar shape.
 * @return Nonzero on complete ordered schema. */
static int read_schema(FILE *stream, cgai_gameplay_config *config) {
    /* Step1: Read only the lengths already bounded by read_shape. */
    for (size_t index = 0U; index < config->feature_count; ++index)
        if (!read_u32(stream, "cardinality", &config->cardinalities[index]))
            return 0;
    /* Step2: No task may reuse a different head's output numbering implicitly. */
    for (size_t index = 0U; index < config->task_count; ++index)
        if (!read_u32(stream, "output_count", &config->output_counts[index]) ||
            !read_u64(stream, "task_modules", &config->task_modules[index]) ||
            !read_u64(stream, "task_features", &config->task_features[index]))
            return 0;
    return 1;
}

/** @brief Write one complete parameter or optimizer array.
 * @param stream Borrowed output stream.
 * @param key Stable row key.
 * @param values Borrowed finite scalar array.
 * @param count Array length.
 * @return Nonzero on every formatted write. */
static int write_array(FILE *stream, const char *key, const double *values, size_t count) {
    /* Step1: Keep one exact scalar per line without platform representation dependence. */
    for (size_t index = 0U; index < count; ++index)
        if (!isfinite(values[index]) || fprintf(stream, "%s %a\n", key, values[index]) < 0)
            return 0;
    return 1;
}

/** @brief Read and validate one finite scalar array.
 * @param stream Borrowed input stream.
 * @param key Required row key.
 * @param values Writable bounded array.
 * @param count Exact array length.
 * @param nonnegative Nonzero for Adam second moments.
 * @return Nonzero on complete finite array. */
static int read_array(FILE *stream, const char *key, double *values, size_t count,
                      int nonnegative) {
    /* Step1: No partial array is ever published by the enclosing owned constructor. */
    for (size_t index = 0U; index < count; ++index)
        if (!read_real(stream, key, &values[index]) || (nonnegative && values[index] < 0.0))
            return 0;
    return 1;
}

/** @brief Serialize continuation counters and optimizer-presence metadata.
 * @param stream Borrowed output stream.
 * @param model Borrowed initialized model.
 * @return Nonzero on complete write. */
static int write_progress(FILE *stream, const cgai_gameplay_model *model) {
    /* Step1: Missing moments require fresh counters; incomplete ownership is rejected. */
    if ((model->adam_first == NULL) != (model->adam_second == NULL) ||
        (model->adam_first == NULL && (model->training_step != 0U || model->training_epochs != 0U)))
        return 0;
    return fprintf(stream,
                   "training_step %llu\ntraining_epochs %llu\ntraining_shuffle %llu\n"
                   "optimizer_present %u\n",
                   (unsigned long long)model->training_step,
                   (unsigned long long)model->training_epochs,
                   (unsigned long long)model->training_shuffle,
                   model->adam_first != NULL ? 1U : 0U) >= 0;
}

/** @brief Read complete bounded continuation counters and optional optimizer ownership.
 * @param stream Borrowed input stream.
 * @param model Mutable private zeroed-parameter owner.
 * @return Nonzero after valid progress and optional moment allocations. */
static int read_progress(FILE *stream, cgai_gameplay_model *model) {
    /* Step1: Preserve exact RNG/counters, permitting partial-epoch checkpoint state. */
    uint64_t present = 0U;
    if (!read_u64(stream, "training_step", &model->training_step) ||
        !read_u64(stream, "training_epochs", &model->training_epochs) ||
        !read_u64(stream, "training_shuffle", &model->training_shuffle) ||
        !read_u64(stream, "optimizer_present", &present) || present > 1U ||
        model->training_epochs > model->training_step)
        return 0;
    if (present == 0U)
        return model->training_step == 0U && model->training_epochs == 0U;
    /* Step2: Parameters have already passed the allocator's checked scalar bound. */
    model->adam_first = calloc(model->parameter_count, sizeof(double));
    model->adam_second = calloc(model->parameter_count, sizeof(double));
    return model->adam_first != NULL && model->adam_second != NULL;
}

/** @brief Serialize one distinct portable artifact or complete checkpoint body.
 * @param stream Borrowed output stream.
 * @param model Borrowed immutable initialized network.
 * @param checkpoint Nonzero to include optimizer and progress.
 * @return Nonzero on complete schema and scalar writes. */
static int write_model(FILE *stream, const cgai_gameplay_model *model, int checkpoint) {
    /* Step1: A distinct magic prevents standalone neural decoders from accepting this layout. */
    const char *magic = checkpoint ? "CGAI-GAMEPLAY-CHECKPOINT 1\n" : "CGAI-GAMEPLAY-MODEL 1\n";
    if (fputs(magic, stream) == EOF || !write_shape(stream, &model->config) ||
        !write_schema(stream, &model->config) ||
        fprintf(stream, "parameter_count %zu\n", model->parameter_count) < 0)
        return 0;
    if (checkpoint && !write_progress(stream, model))
        return 0;
    /* Step2: Inference contains no Adam arrays; checkpoints retain both moments exactly. */
    if (!write_array(stream, "weight", model->parameters, model->parameter_count))
        return 0;
    if (checkpoint && model->adam_first != NULL &&
        (!write_array(stream, "first", model->adam_first, model->parameter_count) ||
         !write_array(stream, "second", model->adam_second, model->parameter_count)))
        return 0;
    return fputs("end 1\n", stream) != EOF;
}

/** @brief Open a bounded artifact stream without following an unchecked dimension allocation.
 * @param path Borrowed trusted path.
 * @param maximum Maximum admitted file bytes.
 * @return Owned readable stream at its beginning, or NULL. */
static FILE *open_bounded(const char *path, unsigned long maximum) {
    /* Step1: Measure byte length before scanning any attacker-controlled numeric fields. */
    if (path == NULL || path[0] == '\0')
        return NULL;
    FILE *stream = fopen(path, "rb");
    if (stream == NULL)
        return NULL;
    const int seeked = fseek(stream, 0L, SEEK_END) == 0;
    const long length = seeked ? ftell(stream) : -1L;
    /* Step2: Both file caps fit signed long even on Windows LLP64. */
    if (length < 1L || (unsigned long)length > maximum || fseek(stream, 0L, SEEK_SET) != 0) {
        (void)fclose(stream);
        return NULL;
    }
    return stream;
}

/** @brief Read bounded metadata and allocate private weights with checked shape products.
 * @param stream Borrowed input stream.
 * @param checkpoint Required artifact kind.
 * @return Owned private zeroed model, or NULL. */
static cgai_gameplay_model *read_owner(FILE *stream, int checkpoint) {
    /* Step1: Exact header and schema order reject ambiguous or duplicate fields. */
    char line[128] = {0};
    const char *expected = checkpoint ? "CGAI-GAMEPLAY-CHECKPOINT 1\n" : "CGAI-GAMEPLAY-MODEL 1\n";
    cgai_gameplay_config config = {0};
    size_t parameters = 0U;
    if (fgets(line, (int)sizeof(line), stream) == NULL || strcmp(line, expected) != 0 ||
        !read_shape(stream, &config) || !read_schema(stream, &config) ||
        !read_size(stream, "parameter_count", &parameters))
        return NULL;
    /* Step2: Decode into unpublished ownership and reject mismatched parameter counts. */
    cgai_gameplay_model *model = cgai_gameplay_allocate(&config);
    if (model != NULL &&
        (parameters != model->parameter_count || (checkpoint && !read_progress(stream, model)))) {
        cgai_gameplay_destroy(model);
        return NULL;
    }
    return model;
}

/** @brief Read all arrays and require exact final marker and end of file.
 * @param stream Borrowed input stream.
 * @param model Mutable private complete owner.
 * @param checkpoint Required artifact kind.
 * @return Nonzero on complete finite data and exact consumption. */
static int read_arrays(FILE *stream, cgai_gameplay_model *model, int checkpoint) {
    /* Step1: Bound all array loops by validated allocated parameter ownership. */
    if (!read_array(stream, "weight", model->parameters, model->parameter_count, 0))
        return 0;
    if (checkpoint && model->adam_first != NULL &&
        (!read_array(stream, "first", model->adam_first, model->parameter_count, 0) ||
         !read_array(stream, "second", model->adam_second, model->parameter_count, 1)))
        return 0;
    /* Step2: Trailing whitespace or extra rows cannot masquerade as a valid release. */
    uint64_t marker = 0U;
    return read_u64(stream, "end", &marker) && marker == 1U && fgetc(stream) == EOF &&
           ferror(stream) == 0;
}

/** @brief Save one trusted portable file and close on every path.
 * @param model Borrowed immutable complete network.
 * @param path Trusted destination.
 * @param checkpoint Nonzero for complete optimizer persistence.
 * @return OK on complete close, ERROR otherwise. */
static cgai_status save_file(const cgai_gameplay_model *model, const char *path, int checkpoint) {
    /* Step1: Reject missing arguments before opening any destination. */
    cgai_error_clear();
    if (model == NULL || path == NULL || path[0] == '\0')
        return cgai_fail("gameplay model and destination required");
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
        return cgai_fail("cannot open gameplay artifact destination");
    /* Step2: Always close, including failed array or schema writes. */
    const int written = write_model(stream, model, checkpoint);
    const int closed = fclose(stream) == 0;
    return written && closed ? CGAI_STATUS_OK : cgai_fail("cannot save complete gameplay artifact");
}

/** @brief Load one bounded file into unpublished complete ownership.
 * @param path Trusted input path.
 * @param checkpoint Required artifact kind.
 * @return Owned model, or NULL with a diagnostic. */
static cgai_gameplay_model *load_file(const char *path, int checkpoint) {
    /* Step1: Cap bytes before reading any declared dimensions. */
    cgai_error_clear();
    FILE *stream =
        open_bounded(path, checkpoint ? GAMEPLAY_CHECKPOINT_FILE_LIMIT : GAMEPLAY_MODEL_FILE_LIMIT);
    if (stream == NULL) {
        cgai_fail("cannot open bounded gameplay artifact");
        return NULL;
    }
    cgai_gameplay_model *model = read_owner(stream, checkpoint);
    const int parsed = model != NULL && read_arrays(stream, model, checkpoint);
    const int closed = fclose(stream) == 0;
    /* Step2: No malformed or incomplete state escapes the constructor. */
    if (!parsed || !closed) {
        cgai_gameplay_destroy(model);
        cgai_fail("invalid composed gameplay artifact or checkpoint");
        return NULL;
    }
    return model;
}

/** @brief Save portable composed weights without training state.
 * @param model Borrowed immutable model.
 * @param path Trusted destination.
 * @return OK after complete close, ERROR otherwise. */
cgai_status cgai_gameplay_save(const cgai_gameplay_model *model, const char *path) {
    /* Step1: Inference deliberately omits counters and Adam allocations. */
    return save_file(model, path, 0);
}
/** @brief Load portable composed inference weights.
 * @param path Trusted input path.
 * @return Owned inference network, or NULL. */
cgai_gameplay_model *cgai_gameplay_load(const char *path) {
    /* Step1: Distinct magic rejects checkpoints and legacy neural files. */
    return load_file(path, 0);
}
/** @brief Save complete composed continuation state as exact hexadecimal scalars.
 * @param model Borrowed immutable model.
 * @param path Trusted destination.
 * @return OK after complete close, ERROR otherwise. */
cgai_status cgai_gameplay_checkpoint_save(const cgai_gameplay_model *model, const char *path) {
    /* Step1: Preserve every parameter, moment and shuffle/counter scalar. */
    return save_file(model, path, 1);
}
/** @brief Load complete composed continuation state.
 * @param path Trusted input path.
 * @return Owned exact continuation network, or NULL. */
cgai_gameplay_model *cgai_gameplay_checkpoint_load(const char *path) {
    /* Step1: Bounded schema parsing publishes only complete valid ownership. */
    return load_file(path, 1);
}
