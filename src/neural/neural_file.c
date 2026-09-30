/** @file neural_file.c @brief Bounded, versioned native neural model artifacts. */
#include "internal/error.h"
#include "internal/neural_internal.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Maximum accepted artifact bytes, checked before vocabulary or parameter allocation. */
#define CGAI_NEURAL_FILE_LIMIT (64U * 1024U * 1024U)
/** Version-one fixed header size; fields are written individually without structure padding. */
#define CGAI_NEURAL_HEADER_BYTES (8U + sizeof(uint32_t) + 7U * sizeof(uint64_t) + sizeof(double))
/** Neural-only magic, including its terminating zero byte. */
static const char cgai_neural_magic[8] = {'C', 'G', 'A', 'I', 'N', 'N', '1', '\0'};
/** Borrowed stream plus the number of bytes that remain inside the measured artifact. */
typedef struct cgai_neural_reader {
    FILE *stream;     /**< Borrowed open binary input stream. */
    size_t remaining; /**< Unconsumed measured bytes. */
} cgai_neural_reader;

/**
 * @brief Consume an exact span within the measured file boundary.
 *
 * Successful reads reduce the remaining count; a short read leaves a failure diagnostic to
 * the public loader and never permits a subsequent allocation based on missing bytes.
 * @param reader Mutable borrowed stream boundary.
 * @param output Borrowed writable span of at least size bytes.
 * @param size Requested byte count.
 * @return Nonzero on a complete bounded read, otherwise zero.
 */
static int cgai_neural_read(cgai_neural_reader *reader, void *output, size_t size) {
    /* Step 1: Reject requests outside the original file size or incomplete reads. */
    if (size > reader->remaining || fread(output, 1U, size, reader->stream) != size)
        return 0;
    /* Step 2: Publish consumption only after all bytes have arrived. */
    reader->remaining -= size;
    return 1;
}

/**
 * @brief Measure and rewind an input before allocating model-owned storage.
 *
 * Artifacts larger than 64 MiB are rejected even if their declared dimensions are small.
 * The file must be seekable; changes while loading cause incomplete or trailing-data failure.
 * @param reader Borrowed descriptor with an open stream.
 * @return Nonzero for an accepted bounded length, otherwise zero.
 */
static int cgai_neural_measure(cgai_neural_reader *reader) {
    /* Step 1: Ask the stream for its physical length without allocating a payload buffer. */
    if (fseek(reader->stream, 0L, SEEK_END) != 0)
        return 0;
    const long length = ftell(reader->stream);
    /* Step 2: Require a complete header and the aggregate artifact bound before rewinding. */
    if (length < (long)CGAI_NEURAL_HEADER_BYTES || length > (long)CGAI_NEURAL_FILE_LIMIT ||
        fseek(reader->stream, 0L, SEEK_SET) != 0)
        return 0;
    reader->remaining = (size_t)length;
    return 1;
}

/**
 * @brief Read the neural-only signature and supported version.
 *
 * Existing count-based .cgai artifacts have a different signature and cannot be accepted here.
 * @param reader Mutable borrowed input boundary.
 * @return Nonzero only for the exact version-one signature.
 */
static int cgai_neural_read_signature(cgai_neural_reader *reader) {
    /* Step 1: Read into local values before comparing the complete signature. */
    char magic[sizeof(cgai_neural_magic)] = {0};
    uint32_t version = 0U;
    return cgai_neural_read(reader, magic, sizeof(magic)) &&
           cgai_neural_read(reader, &version, sizeof(version)) && version == 1U &&
           memcmp(magic, cgai_neural_magic, sizeof(magic)) == 0;
}

/**
 * @brief Decode bounded architecture values without narrowing unchecked integers.
 *
 * The five integer fields encode embedding width, hidden width, centroid count, window, and
 * initialization seed. Doubles use the producing machine's native representation.
 * @param reader Mutable borrowed input boundary.
 * @param config Borrowed writable configuration, changed only after dimensions are bounded.
 * @return Nonzero on a complete valid configuration, otherwise zero.
 */
static int cgai_neural_read_config(cgai_neural_reader *reader, cgai_neural_config *config) {
    /* Step 1: Read native scalars and reject out-of-range dimensions before size_t casts. */
    uint64_t fields[5] = {0};
    double temperature = 0.0;
    if (!cgai_neural_read(reader, fields, sizeof(fields)) ||
        !cgai_neural_read(reader, &temperature, sizeof(temperature)) || fields[0] > 64U ||
        fields[1] > 128U || fields[2] > 128U || fields[3] > CGAI_NEURAL_MAX_CONTEXT)
        return 0;
    /* Step 2: Publish the bounded shape and apply the shared positive/finite constraints. */
    config->embedding_dimensions = (size_t)fields[0];
    config->hidden_dimensions = (size_t)fields[1];
    config->centroid_count = (size_t)fields[2];
    config->context_window = (size_t)fields[3];
    config->seed = fields[4];
    config->routing_temperature = temperature;
    return cgai_neural_validate_config(config) == CGAI_STATUS_OK;
}

/**
 * @brief Calculate the contiguous parameter count for a previously bounded shape.
 *
 * The sum covers embeddings, the ordered-window encoder, hidden biases, centroids, and token
 * logits in exactly their allocation order. Bounded dimensions keep these products representable.
 * @param model Borrowed shell with validated configuration and bounded vocabulary size.
 * @return Number of scalar doubles required by that shell.
 */
static size_t cgai_neural_file_parameters(const cgai_neural_model *model) {
    /* Step 1: Sum the five matrix/vector lengths using their validated dimensions. */
    const cgai_neural_config *config = &model->config;
    return model->vocabulary_size * config->embedding_dimensions +
           config->hidden_dimensions * config->context_window * config->embedding_dimensions +
           config->hidden_dimensions + config->centroid_count * config->hidden_dimensions +
           config->centroid_count * model->vocabulary_size;
}

/**
 * @brief Validate declared counts against dimensions and the remaining file before allocation.
 *
 * Each token needs an eight-byte length and at least one spelling byte. Parameter bytes must
 * also fit; this rejects impossible count declarations before allocating either large table.
 * @param reader Mutable borrowed input boundary.
 * @param model Mutable owned shell receiving its pointer table and parameter allocation.
 * @return Nonzero after both tables are allocated, otherwise zero; destroy the partial shell.
 */
static int cgai_neural_read_counts(cgai_neural_reader *reader, cgai_neural_model *model) {
    /* Step 1: Validate counts without narrowing oversized fields. */
    uint64_t counts[2] = {0};
    if (!cgai_neural_read(reader, counts, sizeof(counts)) || counts[0] < 3U ||
        counts[0] > CGAI_NEURAL_MAX_VOCABULARY || counts[1] > CGAI_NEURAL_MAX_PARAMETERS)
        return 0;
    model->vocabulary_size = (size_t)counts[0];
    /* Step 2: Prove parameter agreement and minimum remaining bytes before allocating. */
    if (counts[1] != cgai_neural_file_parameters(model) ||
        counts[0] * (sizeof(uint64_t) + 1U) + counts[1] * sizeof(double) > reader->remaining)
        return 0;
    /* Step 3: Use a fixed 8192-slot pointer budget; serialized counts never size this allocation.
     */
    model->vocabulary = (char **)calloc(CGAI_NEURAL_MAX_VOCABULARY, sizeof(char *));
    return model->vocabulary != NULL && cgai_neural_allocate_parameters(model) == CGAI_STATUS_OK;
}

/**
 * @brief Recognize a single normalized non-control tokenizer spelling.
 *
 * Words group alphanumeric bytes, UTF-8 high bytes, and apostrophes; punctuation occupies one
 * byte. Embedded control characters, whitespace, and uppercase forms are rejected.
 * @param spelling Borrowed terminated nonempty spelling.
 * @return Nonzero when the spelling can be one ordinary normalized token.
 */
static int cgai_neural_regular_spelling(const char *spelling) {
    /* Step 1: Check every byte against the tokenizer's word class and normalization. */
    const size_t length = strlen(spelling);
    for (size_t i = 0U; i < length; ++i) {
        const unsigned char byte = (unsigned char)spelling[i];
        if (iscntrl(byte) || isspace(byte) || tolower(byte) != byte)
            return 0;
        if (!(isalnum(byte) || byte >= 128U || byte == '\'') && length != 1U)
            return 0;
    }
    /* Step 2: Empty spellings never describe a token. */
    return length != 0U;
}

/**
 * @brief Check fixed control identities and uniqueness against an initialized prefix.
 *
 * Controls occupy IDs zero through two only. Ordinary tokens must match normalized tokenizer
 * output; comparing prior entries prevents ambiguous lookup after loading.
 * @param model Borrowed model with entries through index initialized.
 * @param index Token position being validated.
 * @return Nonzero for a valid unique spelling, otherwise zero.
 */
static int cgai_neural_valid_spelling(const cgai_neural_model *model, size_t index) {
    /* Step 1: Require exact control spellings or a valid ordinary token. */
    static const char *const controls[3] = {"<bos>", "<eos>", "<unk>"};
    const char *spelling = model->vocabulary[index];
    if (index < 3U)
        return strcmp(spelling, controls[index]) == 0;
    if (!cgai_neural_regular_spelling(spelling))
        return 0;
    /* Step 2: Search only earlier initialized entries for duplicate spellings. */
    for (size_t i = 0U; i < index; ++i) {
        if (strcmp(spelling, model->vocabulary[i]) == 0)
            return 0;
    }
    return 1;
}

/**
 * @brief Read and validate one owned vocabulary spelling.
 *
 * The length is checked before allocation; the serialized bytes exclude a terminator, which is
 * supplied locally. On failure, the model retains any allocation for its normal destructor.
 * @param reader Mutable borrowed input boundary.
 * @param model Mutable shell with a zero-initialized vocabulary pointer table.
 * @param index Writable vocabulary slot, below vocabulary_size.
 * @return Nonzero after a complete valid spelling, otherwise zero.
 */
static int cgai_neural_read_spelling(cgai_neural_reader *reader, cgai_neural_model *model,
                                     size_t index) {
    /* Step 1: Bound the byte count before creating the terminated string allocation. */
    uint64_t length = 0U;
    if (!cgai_neural_read(reader, &length, sizeof(length)) || length == 0U ||
        length > CGAI_NEURAL_MAX_TOKEN_BYTES || length > reader->remaining)
        return 0;
    char *spelling = (char *)calloc((size_t)length + 1U, 1U);
    model->vocabulary[index] = spelling;
    /* Step 2: Reject incomplete or embedded-NUL strings before normalization checks. */
    return spelling != NULL && cgai_neural_read(reader, spelling, (size_t)length) &&
           memchr(spelling, '\0', (size_t)length) == NULL &&
           cgai_neural_valid_spelling(model, index);
}

/**
 * @brief Check that every saved parameter is finite.
 *
 * NaN and infinities are not accepted optimizer or inference state. This traversal neither
 * normalizes nor changes weights, preserving exact save/load predictions.
 * @param model Borrowed initialized parameter block.
 * @return Nonzero only if every scalar is finite.
 */
static int cgai_neural_finite_parameters(const cgai_neural_model *model) {
    /* Step 1: Inspect each scalar without modifying its native representation. */
    for (size_t i = 0U; i < model->parameter_count; ++i) {
        if (!isfinite(model->parameters[i]))
            return 0;
    }
    return 1;
}

/**
 * @brief Decode all fields and require physical end-of-file after the final weight.
 *
 * Any partially initialized allocations belong to the caller's shell. The extra EOF probe
 * rejects trailing data even when the underlying file grew after its initial measurement.
 * @param reader Mutable borrowed input boundary.
 * @param model Mutable zeroed shell receiving owned tables.
 * @return Nonzero only for a complete validated artifact.
 */
static int cgai_neural_read_model(cgai_neural_reader *reader, cgai_neural_model *model) {
    /* Step 1: Validate signature, shape, and allocation sizes in dependency order. */
    if (!cgai_neural_measure(reader) || !cgai_neural_read_signature(reader) ||
        !cgai_neural_read_config(reader, &model->config) || !cgai_neural_read_counts(reader, model))
        return 0;
    /* Step 2: Build the vocabulary before reading the exact contiguous parameter span. */
    for (size_t i = 0U; i < model->vocabulary_size; ++i) {
        if (!cgai_neural_read_spelling(reader, model, i))
            return 0;
    }
    /* Step 3: Require finite weights and no trailing measured or newly appended bytes. */
    return cgai_neural_read(reader, model->parameters, model->parameter_count * sizeof(double)) &&
           cgai_neural_finite_parameters(model) && reader->remaining == 0U &&
           fgetc(reader->stream) == EOF && !ferror(reader->stream);
}

/** @brief Load a bounded versioned neural artifact with validated vocabulary and finite weights.
 * @param path Borrowed path to an artifact produced on the same architecture.
 * @return Owned model, or NULL with a diagnostic; release with cgai_neural_destroy(). */
cgai_neural_model *cgai_neural_load(const char *path) {
    /* Step 1: Validate the path and acquire independent shell and stream ownership. */
    cgai_error_clear();
    if (path == NULL || *path == '\0') {
        (void)cgai_fail("neural load requires a path");
        return NULL;
    }
    cgai_neural_reader reader = {fopen(path, "rb"), 0U};
    cgai_neural_model *model = (cgai_neural_model *)calloc(1U, sizeof(*model));
    int ok = reader.stream != NULL && model != NULL && cgai_neural_read_model(&reader, model);
    /* Step 2: Close the stream before publishing a model, including every failure path. */
    if (reader.stream != NULL && fclose(reader.stream) != 0)
        ok = 0;
    if (!ok) {
        cgai_neural_destroy(model);
        (void)cgai_fail(
            "could not load neural artifact: invalid, oversized, truncated, or unreadable");
        return NULL;
    }
    return model;
}

/**
 * @brief Verify model fields and the aggregate artifact byte bound before replacement.
 *
 * This preflight ensures a successful save can be accepted by the bounded loader. It runs
 * before opening the destination so invalid state does not truncate an existing artifact.
 * @param model Borrowed immutable handle, possibly NULL.
 * @return Nonzero for a serializable finite model, otherwise zero.
 */
static int cgai_neural_saveable(const cgai_neural_model *model) {
    /* Step 1: Validate pointers, dimensions, vocabulary bounds, and parameter layout. */
    if (model == NULL || model->vocabulary == NULL || model->parameters == NULL ||
        cgai_neural_validate_config(&model->config) != CGAI_STATUS_OK ||
        model->vocabulary_size < 3U || model->vocabulary_size > CGAI_NEURAL_MAX_VOCABULARY ||
        model->parameter_count != cgai_neural_file_parameters(model) ||
        model->parameter_count > CGAI_NEURAL_MAX_PARAMETERS)
        return 0;
    size_t bytes = CGAI_NEURAL_HEADER_BYTES + model->parameter_count * sizeof(double);
    /* Step 2: Bound every spelling and its contribution to the total before opening a file. */
    for (size_t i = 0U; i < model->vocabulary_size; ++i) {
        if (model->vocabulary[i] == NULL)
            return 0;
        const size_t length = strlen(model->vocabulary[i]);
        if (length > CGAI_NEURAL_MAX_TOKEN_BYTES || !cgai_neural_valid_spelling(model, i))
            return 0;
        bytes += sizeof(uint64_t) + length;
        if (bytes > CGAI_NEURAL_FILE_LIMIT)
            return 0;
    }
    return cgai_neural_finite_parameters(model);
}

/**
 * @brief Write the fixed header in documented scalar order.
 *
 * Arrays contain equal-width uint64_t fields, while version and temperature are written
 * separately. No compiler-inserted structure padding reaches the artifact.
 * @param stream Borrowed writable binary stream.
 * @param model Borrowed validated model.
 * @return Nonzero only after every header byte is accepted by the stream.
 */
static int cgai_neural_write_header(FILE *stream, const cgai_neural_model *model) {
    /* Step 1: Construct field arrays in the same order used by the bounded decoder. */
    const uint32_t version = 1U;
    const uint64_t fields[5] = {model->config.embedding_dimensions, model->config.hidden_dimensions,
                                model->config.centroid_count, model->config.context_window,
                                model->config.seed};
    const uint64_t counts[2] = {model->vocabulary_size, model->parameter_count};
    /* Step 2: Stop at the first short write; the caller also verifies close. */
    return fwrite(cgai_neural_magic, sizeof(cgai_neural_magic), 1U, stream) == 1U &&
           fwrite(&version, sizeof(version), 1U, stream) == 1U &&
           fwrite(fields, sizeof(fields), 1U, stream) == 1U &&
           fwrite(&model->config.routing_temperature, sizeof(double), 1U, stream) == 1U &&
           fwrite(counts, sizeof(counts), 1U, stream) == 1U;
}

/**
 * @brief Write length-prefixed spellings followed by contiguous native weights.
 *
 * Strings exclude their NUL terminator on disk. The preflight established all lengths and
 * finite parameters before this non-atomic write began.
 * @param stream Borrowed writable binary stream.
 * @param model Borrowed validated immutable model.
 * @return Nonzero after all payload writes, otherwise zero.
 */
static int cgai_neural_write_payload(FILE *stream, const cgai_neural_model *model) {
    /* Step 1: Serialize spellings in their fixed vocabulary-ID order. */
    for (size_t i = 0U; i < model->vocabulary_size; ++i) {
        const uint64_t length = strlen(model->vocabulary[i]);
        if (fwrite(&length, sizeof(length), 1U, stream) != 1U ||
            fwrite(model->vocabulary[i], 1U, (size_t)length, stream) != (size_t)length)
            return 0;
    }
    /* Step 2: Preserve the exact double representations for deterministic round trips. */
    return fwrite(model->parameters, sizeof(double), model->parameter_count, stream) ==
           model->parameter_count;
}

/** @brief Save a standalone versioned neural artifact, distinct from .cgai files.
 * @param model Borrowed immutable handle.
 * @param path Borrowed destination path, normally ending in .cgnn; existing contents replaced.
 * @return OK on complete write/close, ERROR otherwise; replacement is not atomic.
 * @note Artifacts are capped at 64 MiB. Experimental native numeric representation;
 * use trusted files on the same architecture. */
cgai_status cgai_neural_save(const cgai_neural_model *model, const char *path) {
    /* Step 1: Validate state before acquiring a stream that replaces destination contents. */
    cgai_error_clear();
    if (path == NULL || *path == '\0' || !cgai_neural_saveable(model))
        return cgai_fail("neural save requires a valid model within the 64 MiB artifact limit");
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
        return cgai_fail("could not open neural artifact for writing");
    /* Step 2: Write in dependency order, and always flush/close before returning status. */
    int ok = cgai_neural_write_header(stream, model) && cgai_neural_write_payload(stream, model);
    if (fclose(stream) != 0)
        ok = 0;
    return ok ? CGAI_STATUS_OK : cgai_fail("could not completely write neural artifact");
}
