/** @file neural_checkpoint.c @brief Bounded exact text checkpoints for standalone neural training.
 *
 * Hexadecimal doubles require the C numeric locale. No operation changes the process locale.
 * The codec preserves Adam state; whole-epoch replay also requires the same corpus and settings.
 */
#include "internal/error.h"
#include "internal/neural_internal.h"
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Maximum physical checkpoint bytes, checked before model allocations and replacement. */
#define CGAI_CHECKPOINT_LIMIT (256U * 1024U * 1024U)
/** Borrowed input stream and unconsumed bytes from its initial physical measurement. */
typedef struct checkpoint_reader {
    FILE *stream;     /**< Borrowed seekable binary input. */
    size_t remaining; /**< Remaining measured bytes. */
} checkpoint_reader;
/** Borrowed output stream, or NULL while measuring the exact serialized size. */
typedef struct checkpoint_writer {
    FILE *stream; /**< Borrowed binary destination; NULL for preflight. */
    size_t bytes; /**< Successfully measured or written bytes. */
} checkpoint_writer;

/** @brief Require a dot decimal separator without mutating the global locale.
 * @return Nonzero when C-locale hexadecimal numbers can be exchanged exactly. */
static int numeric_locale(void) {
    /* Step 1: Inspect the existing numeric locale rather than changing caller state. */
    return strcmp(localeconv()->decimal_point, ".") == 0;
}

/** @brief Measure a bounded seekable checkpoint before allocating model-owned arrays.
 * @param reader Borrowed input descriptor with an open stream.
 * @return Nonzero after successful measurement and rewind. */
static int measure_input(checkpoint_reader *reader) {
    /* Step 1: Inspect physical length without trusting any serialized count. */
    if (fseek(reader->stream, 0L, SEEK_END) != 0)
        return 0;
    const long length = ftell(reader->stream);
    /* Step 2: Bound the complete file and rewind before parsing its first field. */
    if (length <= 0L || length > (long)CGAI_CHECKPOINT_LIMIT ||
        fseek(reader->stream, 0L, SEEK_SET) != 0)
        return 0;
    reader->remaining = (size_t)length;
    return 1;
}

/** @brief Consume one byte strictly inside the initial file boundary.
 * @param reader Mutable borrowed input descriptor.
 * @return The unsigned byte, or EOF for a missing or failed read. */
static int read_byte(checkpoint_reader *reader) {
    /* Step 1: Prevent appended data from extending the measured parsing budget. */
    if (reader->remaining == 0U)
        return EOF;
    const int byte = fgetc(reader->stream);
    if (byte != EOF)
        --reader->remaining;
    return byte;
}

/** @brief Read one short LF or CRLF line, rejecting NUL and unterminated content.
 * @param reader Mutable borrowed input descriptor.
 * @param line Writable buffer of capacity bytes.
 * @param capacity Buffer capacity including its terminating NUL.
 * @return Nonzero for a complete bounded line without its line ending. */
static int read_line(checkpoint_reader *reader, char *line, size_t capacity) {
    /* Step 1: Consume only complete short lines inside the physical file boundary. */
    size_t count = 0U;
    int byte = read_byte(reader);
    while (byte != EOF && byte != '\n') {
        if (byte == 0 || count + 1U >= capacity)
            return 0;
        line[count++] = (char)byte;
        byte = read_byte(reader);
    }
    /* Step 2: Normalize only the CR immediately preceding a required LF. */
    if (byte != '\n')
        return 0;
    if (count != 0U && line[count - 1U] == '\r')
        --count;
    line[count] = '\0';
    return strchr(line, '\r') == NULL;
}

/** @brief Read an exact section marker or version signature.
 * @param reader Mutable borrowed input descriptor.
 * @param expected Borrowed short expected line without its ending.
 * @return Nonzero for an exact match. */
static int read_marker(checkpoint_reader *reader, const char *expected) {
    /* Step 1: Require the complete marker, including its line boundary. */
    char line[64];
    return read_line(reader, line, sizeof(line)) && strcmp(line, expected) == 0;
}

/** @brief Parse a digits-only decimal value without overflow or sign acceptance.
 * @param text Borrowed nonempty terminated decimal text.
 * @param value Writable result, published only on complete success.
 * @return Nonzero for a uint64_t value. */
static int parse_unsigned(const char *text, uint64_t *value) {
    /* Step 1: Accumulate digits with an explicit bound before multiplication. */
    uint64_t parsed = 0U;
    if (*text == '\0')
        return 0;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9')
            return 0;
        const uint64_t digit = (uint64_t)(*cursor - '0');
        if (parsed > (UINT64_MAX - digit) / 10U)
            return 0;
        parsed = parsed * 10U + digit;
    }
    /* Step 2: Publish only a completely consumed valid value. */
    *value = parsed;
    return 1;
}

/** @brief Parse a finite hexadecimal double with complete input consumption.
 * @param text Borrowed terminated hexadecimal floating-point text.
 * @param value Writable result, published only on complete success.
 * @return Nonzero for a finite exact-representation value. */
static int parse_real(const char *text, double *value) {
    /* Step 1: Require hexadecimal syntax instead of accepting decimal, NaN or infinity. */
    const char *start = *text == '-' || *text == '+' ? text + 1 : text;
    if (strncmp(start, "0x", 2U) != 0 || strchr(start, 'p') == NULL)
        return 0;
    char *end = NULL;
    errno = 0;
    const double parsed = strtod(text, &end);
    /* Step 2: Permit exact subnormals even when the C library reports underflow. */
    if (end == text || *end != '\0' || !isfinite(parsed) ||
        (errno == ERANGE && parsed == 0.0 && strcmp(start, "0x0p+0") != 0 &&
         strcmp(start, "0x0p0") != 0))
        return 0;
    *value = parsed;
    return 1;
}

/** @brief Read one named decimal header field.
 * @param reader Mutable borrowed input descriptor.
 * @param name Borrowed expected field name.
 * @param value Writable parsed result.
 * @return Nonzero for an exact field name and valid decimal value. */
static int read_unsigned(checkpoint_reader *reader, const char *name, uint64_t *value) {
    /* Step 1: Bound the complete field before parsing its value. */
    char line[128];
    const size_t length = strlen(name);
    return read_line(reader, line, sizeof(line)) && strncmp(line, name, length) == 0 &&
           line[length] == ' ' && parse_unsigned(line + length + 1U, value);
}

/** @brief Read one named hexadecimal floating-point header field.
 * @param reader Mutable borrowed input descriptor.
 * @param name Borrowed expected field name.
 * @param value Writable parsed result.
 * @return Nonzero for an exact field name and finite hexadecimal value. */
static int read_real(checkpoint_reader *reader, const char *name, double *value) {
    /* Step 1: Bound the complete field before parsing its exact numeric representation. */
    char line[128];
    const size_t length = strlen(name);
    return read_line(reader, line, sizeof(line)) && strncmp(line, name, length) == 0 &&
           line[length] == ' ' && parse_real(line + length + 1U, value);
}

/** @brief Read and validate dimensions before computing any shape products.
 * @param reader Mutable borrowed input descriptor.
 * @param model Mutable zeroed shell receiving its fixed configuration.
 * @return Nonzero for a supported version and bounded configuration. */
static int read_config(checkpoint_reader *reader, cgai_neural_model *model) {
    /* Step 1: Keep untrusted integer fields wide until every dimension is bounded. */
    uint64_t fields[5] = {0};
    if (!read_marker(reader, "CGAI-CHECKPOINT 1") ||
        !read_unsigned(reader, "embedding_dimensions", &fields[0]) ||
        !read_unsigned(reader, "hidden_dimensions", &fields[1]) ||
        !read_unsigned(reader, "centroid_count", &fields[2]) ||
        !read_unsigned(reader, "context_window", &fields[3]) ||
        !read_unsigned(reader, "seed", &fields[4]) || fields[0] > 64U || fields[1] > 128U ||
        fields[2] > 128U || fields[3] > CGAI_NEURAL_MAX_CONTEXT)
        return 0;
    /* Step 2: Narrow only bounded dimensions, then validate the routing scale. */
    model->config.embedding_dimensions = (size_t)fields[0];
    model->config.hidden_dimensions = (size_t)fields[1];
    model->config.centroid_count = (size_t)fields[2];
    model->config.context_window = (size_t)fields[3];
    model->config.seed = fields[4];
    return read_real(reader, "routing_temperature", &model->config.routing_temperature) &&
           cgai_neural_validate_config(&model->config) == CGAI_STATUS_OK;
}

/** @brief Compute the bounded contiguous parameter layout from a validated shape.
 * @param model Borrowed shell with bounded configuration and vocabulary count.
 * @return Required scalar parameter count, safely representable on 32-bit hosts. */
static size_t parameter_count(const cgai_neural_model *model) {
    /* Step 1: Match the allocation helper's embeddings, encoder, bias, centroids and logits. */
    const cgai_neural_config *config = &model->config;
    return model->vocabulary_size * config->embedding_dimensions +
           config->hidden_dimensions * config->context_window * config->embedding_dimensions +
           config->hidden_dimensions + config->centroid_count * config->hidden_dimensions +
           config->centroid_count * model->vocabulary_size;
}

/** @brief Read bounded table counts and optimizer metadata before allocations.
 * @param reader Mutable borrowed input descriptor.
 * @param model Mutable owned shell receiving counts and counters.
 * @param optimizer Writable presence flag, restricted to zero or one.
 * @return Nonzero for consistent counts and a sufficient remaining byte budget. */
static int read_counts(checkpoint_reader *reader, cgai_neural_model *model, uint64_t *optimizer) {
    /* Step 1: Validate wide serialized counts and the shape-derived parameter count. */
    uint64_t vocabulary = 0U;
    uint64_t parameters = 0U;
    if (!read_unsigned(reader, "vocabulary_size", &vocabulary) || vocabulary < 3U ||
        vocabulary > CGAI_NEURAL_MAX_VOCABULARY ||
        !read_unsigned(reader, "parameter_count", &parameters) ||
        parameters > CGAI_NEURAL_MAX_PARAMETERS || !read_unsigned(reader, "optimizer", optimizer) ||
        *optimizer > 1U)
        return 0;
    model->vocabulary_size = (size_t)vocabulary;
    model->parameter_count = (size_t)parameters;
    if (parameters != parameter_count(model))
        return 0;
    /* Step 2: Accept partial failed training state while rejecting impossible empty state. */
    if (!read_unsigned(reader, "training_step", &model->training_step) ||
        !read_unsigned(reader, "training_epochs", &model->training_epochs) ||
        !read_unsigned(reader, "training_shuffle", &model->training_shuffle) ||
        model->training_step < model->training_epochs)
        return 0;
    if (*optimizer == 0U && (model->training_step != 0U || model->training_epochs != 0U ||
                             model->training_shuffle != 0U))
        return 0;
    /* Step 3: Require a conservative minimum text budget before any large allocation. */
    const size_t minimum =
        model->vocabulary_size * 12U + model->parameter_count * (*optimizer != 0U ? 30U : 15U);
    return minimum <= reader->remaining;
}

/** @brief Allocate bounded vocabulary, parameters and optional Adam moments.
 * @param model Mutable owned shell with validated shape and counts.
 * @param optimizer Nonzero when moments are serialized.
 * @return Nonzero after all allocations succeed; partial allocations remain model-owned. */
static int allocate_model(cgai_neural_model *model, uint64_t optimizer) {
    /* Step 1: Allocate fixed maximum pointer capacity and establish parameter slices. */
    model->vocabulary = calloc(CGAI_NEURAL_MAX_VOCABULARY, sizeof(*model->vocabulary));
    if (model->vocabulary == NULL || cgai_neural_allocate_parameters(model) != CGAI_STATUS_OK)
        return 0;
    /* Step 2: Allocate independent owned moments only when the header declares them. */
    if (optimizer == 0U)
        return 1;
    model->adam_first = calloc(model->parameter_count, sizeof(*model->adam_first));
    model->adam_second = calloc(model->parameter_count, sizeof(*model->adam_second));
    return model->adam_first != NULL && model->adam_second != NULL;
}

/** @brief Read a bounded space-delimited prefix word, rejecting other whitespace.
 * @param reader Mutable borrowed input descriptor.
 * @param word Writable capacity-byte buffer including its terminator.
 * @param capacity Buffer capacity.
 * @return Nonzero for a nonempty word ending with one literal space. */
static int read_word(checkpoint_reader *reader, char *word, size_t capacity) {
    /* Step 1: Prefix words cannot cross lines or contain embedded control bytes. */
    size_t count = 0U;
    int byte = read_byte(reader);
    while (byte != EOF && byte != ' ') {
        if (byte < 33 || byte > 126 || count + 1U >= capacity)
            return 0;
        word[count++] = (char)byte;
        byte = read_byte(reader);
    }
    /* Step 2: Publish only a nonempty completely delimited word. */
    word[count] = '\0';
    return byte == ' ' && count != 0U;
}

/** @brief Read the exact token prefix and bound its encoded byte length.
 * @param reader Mutable borrowed input descriptor.
 * @param index Expected vocabulary ID.
 * @param length Writable decoded spelling length.
 * @return Nonzero before a sufficiently sized hexadecimal payload. */
static int read_token_prefix(checkpoint_reader *reader, size_t index, size_t *length) {
    /* Step 1: Require the expected marker, ID and bounded byte count. */
    char word[32];
    uint64_t parsed_index = 0U;
    uint64_t parsed_length = 0U;
    if (!read_word(reader, word, sizeof(word)) || strcmp(word, "token") != 0 ||
        !read_word(reader, word, sizeof(word)) || !parse_unsigned(word, &parsed_index) ||
        parsed_index != index || !read_word(reader, word, sizeof(word)) ||
        !parse_unsigned(word, &parsed_length) || parsed_length == 0U ||
        parsed_length > CGAI_NEURAL_MAX_TOKEN_BYTES || parsed_length * 2U + 1U > reader->remaining)
        return 0;
    /* Step 2: Narrow only after the full encoded span is proven to fit the file budget. */
    *length = (size_t)parsed_length;
    return 1;
}

/** @brief Decode one lowercase hexadecimal digit.
 * @param byte Input byte, or EOF.
 * @return Value zero through fifteen, or minus one for invalid input. */
static int hex_digit(int byte) {
    /* Step 1: Accept canonical lowercase ASCII digits only. */
    if (byte >= '0' && byte <= '9')
        return byte - '0';
    if (byte >= 'a' && byte <= 'f')
        return byte - 'a' + 10;
    return -1;
}

/** @brief Consume one complete LF or CRLF ending after a streamed spelling.
 * @param reader Mutable borrowed input descriptor.
 * @return Nonzero for exactly one accepted line ending. */
static int read_ending(checkpoint_reader *reader) {
    /* Step 1: Normalize only the optional CR immediately before LF. */
    int byte = read_byte(reader);
    if (byte == '\r')
        byte = read_byte(reader);
    return byte == '\n';
}

/** @brief Decode a spelling without accepting embedded zero or malformed hex bytes.
 * @param reader Mutable borrowed input descriptor.
 * @param spelling Writable terminated length-plus-one-byte allocation.
 * @param length Number of decoded spelling bytes.
 * @return Nonzero for a complete non-NUL spelling and its line ending. */
static int read_hex(checkpoint_reader *reader, char *spelling, size_t length) {
    /* Step 1: Decode exactly the declared byte count, never an unbounded token string. */
    for (size_t i = 0U; i < length; ++i) {
        const int high = hex_digit(read_byte(reader));
        const int low = hex_digit(read_byte(reader));
        if (high < 0 || low < 0 || (high == 0 && low == 0))
            return 0;
        spelling[i] = (char)(high * 16 + low);
    }
    /* Step 2: Add the local terminator and reject extra payload characters. */
    spelling[length] = '\0';
    return read_ending(reader);
}

/** @brief Recognize a normalized C-locale word byte without calling locale-sensitive ctype.
 * @param byte Unsigned spelling byte.
 * @return Nonzero for lowercase word bytes, apostrophes or preserved high bytes. */
static int word_byte(unsigned char byte) {
    /* Step 1: Match the shared tokenizer's default C-locale ordinary word class. */
    return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '\'' ||
           byte >= 128U;
}

/** @brief Validate one ordinary spelling against the shared tokenizer's C-locale contract.
 * @param spelling Borrowed nonempty terminated spelling.
 * @return Nonzero for one normalized word or one non-control punctuation byte. */
static int regular_spelling(const char *spelling) {
    /* Step 1: Reject whitespace, controls, uppercase and mixed multi-token spellings. */
    const size_t length = strlen(spelling);
    for (size_t i = 0U; i < length; ++i) {
        const unsigned char byte = (unsigned char)spelling[i];
        if (byte <= 32U || byte == 127U || (byte >= 'A' && byte <= 'Z') ||
            (!word_byte(byte) && length != 1U))
            return 0;
    }
    return length != 0U;
}

/** @brief Validate control identities and reject duplicate vocabulary spellings.
 * @param model Borrowed model with a complete prefix through index.
 * @param index Initialized vocabulary ID.
 * @return Nonzero for a valid unique spelling at the expected ID. */
static int valid_spelling(const cgai_neural_model *model, size_t index) {
    /* Step 1: Preserve the fixed BOS, EOS and UNK control IDs. */
    static const char *const controls[3] = {"<bos>", "<eos>", "<unk>"};
    const char *spelling = model->vocabulary[index];
    if (index < 3U)
        return strcmp(spelling, controls[index]) == 0;
    if (!regular_spelling(spelling))
        return 0;
    /* Step 2: Compare only the initialized prefix to prevent ambiguous lookup. */
    for (size_t i = 0U; i < index; ++i)
        if (strcmp(spelling, model->vocabulary[i]) == 0)
            return 0;
    return 1;
}

/** @brief Read the fixed-ID vocabulary into individually owned bounded spellings.
 * @param reader Mutable borrowed input descriptor.
 * @param model Mutable model with its zeroed vocabulary pointer allocation.
 * @return Nonzero for the complete valid vocabulary; partial allocations stay owned. */
static int read_vocabulary(checkpoint_reader *reader, cgai_neural_model *model) {
    /* Step 1: Require the section marker before any spelling allocation. */
    if (!read_marker(reader, "vocabulary"))
        return 0;
    /* Step 2: Bound, decode and validate each ID in serialized order. */
    for (size_t i = 0U; i < model->vocabulary_size; ++i) {
        size_t length = 0U;
        if (!read_token_prefix(reader, i, &length))
            return 0;
        model->vocabulary[i] = calloc(length + 1U, 1U);
        if (model->vocabulary[i] == NULL || !read_hex(reader, model->vocabulary[i], length) ||
            !valid_spelling(model, i))
            return 0;
    }
    return 1;
}

/** @brief Split one nonempty literal-space-delimited field in a mutable short line.
 * @param cursor Borrowed pointer to a cursor updated after the delimiter.
 * @return Borrowed terminated field, or NULL for a missing or empty field. */
static char *split_field(char **cursor) {
    /* Step 1: Terminate one field in place without accepting repeated spaces. */
    char *field = *cursor;
    char *space = strchr(field, ' ');
    if (space == NULL || space == field)
        return NULL;
    *space = '\0';
    *cursor = space + 1;
    return field;
}

/** @brief Read one indexed parameter or pair of Adam moments.
 * @param reader Mutable borrowed input descriptor.
 * @param name Borrowed expected entry marker.
 * @param index Expected scalar index.
 * @param first Writable first value.
 * @param second Writable optional second value, or NULL for a parameter.
 * @return Nonzero for an exact indexed finite entry. */
static int read_scalar(checkpoint_reader *reader, const char *name, size_t index, double *first,
                       double *second) {
    /* Step 1: Parse the complete bounded entry and verify its sequential index. */
    char line[256];
    char *cursor = line;
    uint64_t parsed_index = 0U;
    if (!read_line(reader, line, sizeof(line)))
        return 0;
    const char *marker = split_field(&cursor);
    const char *number = split_field(&cursor);
    if (marker == NULL || number == NULL || strcmp(marker, name) != 0 ||
        !parse_unsigned(number, &parsed_index) || parsed_index != index)
        return 0;
    /* Step 2: Require complete exact representations for one or two scalar values. */
    if (second == NULL)
        return parse_real(cursor, first);
    const char *value = split_field(&cursor);
    return value != NULL && parse_real(value, first) && parse_real(cursor, second) &&
           *second >= 0.0;
}

/** @brief Read all parameters in contiguous-layout order.
 * @param reader Mutable borrowed input descriptor.
 * @param model Mutable allocated model receiving parameter values.
 * @return Nonzero for a complete parameter section. */
static int read_parameters(checkpoint_reader *reader, cgai_neural_model *model) {
    /* Step 1: Require every expected sequential parameter index. */
    if (!read_marker(reader, "parameters"))
        return 0;
    for (size_t i = 0U; i < model->parameter_count; ++i)
        if (!read_scalar(reader, "parameter", i, &model->parameters[i], NULL))
            return 0;
    return 1;
}

/** @brief Read all optional Adam moment pairs.
 * @param reader Mutable borrowed input descriptor.
 * @param model Mutable allocated model receiving scalar state.
 * @param optimizer Presence flag from the validated header.
 * @return Nonzero for a complete moment section with nonnegative second moments. */
static int read_moments(checkpoint_reader *reader, cgai_neural_model *model, uint64_t optimizer) {
    /* Step 1: Read moments only when declared, rejecting negative second moments. */
    if (!read_marker(reader, "moments"))
        return 0;
    if (optimizer != 0U)
        for (size_t i = 0U; i < model->parameter_count; ++i)
            if (!read_scalar(reader, "adam", i, &model->adam_first[i], &model->adam_second[i]))
                return 0;
    return 1;
}

/** @brief Read weights and moments, then require physical EOF.
 * @param reader Mutable borrowed input descriptor.
 * @param model Mutable allocated model receiving scalar state.
 * @param optimizer Presence flag from the validated header.
 * @return Nonzero for complete state with no trailing content. */
static int read_state(checkpoint_reader *reader, cgai_neural_model *model, uint64_t optimizer) {
    /* Step 1: Require both complete scalar sections before the final EOF checks. */
    if (!read_parameters(reader, model) || !read_moments(reader, model, optimizer))
        return 0;
    return read_marker(reader, "end") && reader->remaining == 0U && fgetc(reader->stream) == EOF &&
           !ferror(reader->stream);
}

/** @brief Decode a checkpoint in allocation-safe dependency order.
 * @param reader Mutable borrowed input descriptor with an open stream.
 * @param model Mutable zeroed owned shell.
 * @return Nonzero for complete validated state; partial allocations stay owned. */
static int read_model(checkpoint_reader *reader, cgai_neural_model *model) {
    /* Step 1: Validate file size, configuration and minimum byte budget before allocations. */
    uint64_t optimizer = 0U;
    return measure_input(reader) && read_config(reader, model) &&
           read_counts(reader, model, &optimizer) && allocate_model(model, optimizer) &&
           read_vocabulary(reader, model) && read_state(reader, model, optimizer);
}

/** @brief Load exact standalone weights and Adam state from a bounded text checkpoint.
 * @param path Borrowed nonempty checkpoint path.
 * @return Owned model or NULL with a diagnostic; destroy with cgai_neural_destroy(). */
cgai_neural_model *cgai_neural_checkpoint_load(const char *path) {
    /* Step 1: Validate locale/path before acquiring owned model and stream resources. */
    cgai_error_clear();
    if (path == NULL || *path == '\0' || !numeric_locale()) {
        (void)cgai_fail("checkpoint load requires a path and the C numeric locale");
        return NULL;
    }
    checkpoint_reader reader = {fopen(path, "rb"), 0U};
    cgai_neural_model *model = calloc(1U, sizeof(*model));
    int ok = reader.stream != NULL && model != NULL && read_model(&reader, model);
    /* Step 2: Close every acquired stream before publishing successfully loaded state. */
    if (reader.stream != NULL && fclose(reader.stream) != 0)
        ok = 0;
    if (!ok) {
        cgai_neural_destroy(model);
        (void)cgai_fail("could not load checkpoint: invalid, oversized, truncated, or unreadable");
        return NULL;
    }
    return model;
}

/** @brief Validate optimizer ownership and counters, including partially failed training.
 * @param model Borrowed initialized standalone model.
 * @return Nonzero for consistent presence and counters. */
static int valid_optimizer(const cgai_neural_model *model) {
    /* Step 1: Reject partial moment ownership or epochs beyond successful update counts. */
    if ((model->adam_first == NULL) != (model->adam_second == NULL) ||
        model->training_step < model->training_epochs)
        return 0;
    /* Step 2: A model without an optimizer must carry no training continuation counters. */
    return model->adam_first != NULL ||
           (model->training_step == 0U && model->training_epochs == 0U &&
            model->training_shuffle == 0U);
}

/** @brief Validate finite weights and finite nonnegative second moments.
 * @param model Borrowed model with consistent optimizer ownership.
 * @return Nonzero for serializable scalar arrays. */
static int valid_scalars(const cgai_neural_model *model) {
    /* Step 1: Validate every numeric scalar before opening a replacing destination. */
    for (size_t i = 0U; i < model->parameter_count; ++i) {
        if (!isfinite(model->parameters[i]))
            return 0;
        if (model->adam_first != NULL &&
            (!isfinite(model->adam_first[i]) || !isfinite(model->adam_second[i]) ||
             model->adam_second[i] < 0.0))
            return 0;
    }
    return 1;
}

/** @brief Validate standalone configuration, vocabulary, layout and optimizer state.
 * @param model Borrowed possibly NULL model.
 * @return Nonzero for complete state accepted by the text loader. */
static int saveable(const cgai_neural_model *model) {
    /* Step 1: Bound dimensions and counts before calculating parameter layout. */
    if (model == NULL || model->parameters == NULL || model->vocabulary == NULL ||
        cgai_neural_validate_config(&model->config) != CGAI_STATUS_OK ||
        model->vocabulary_size < 3U || model->vocabulary_size > CGAI_NEURAL_MAX_VOCABULARY ||
        (model->output_size != 0U && model->output_size != model->vocabulary_size) ||
        model->parameter_count > CGAI_NEURAL_MAX_PARAMETERS ||
        model->parameter_count != parameter_count(model) || !valid_optimizer(model))
        return 0;
    /* Step 2: Require complete bounded valid spellings before numeric serialization. */
    for (size_t i = 0U; i < model->vocabulary_size; ++i)
        if (model->vocabulary[i] == NULL ||
            strlen(model->vocabulary[i]) > CGAI_NEURAL_MAX_TOKEN_BYTES || !valid_spelling(model, i))
            return 0;
    return valid_scalars(model);
}

/** @brief Measure or write a bounded raw byte span.
 * @param writer Mutable borrowed output descriptor.
 * @param bytes Borrowed readable byte span.
 * @param count Span byte length.
 * @return Nonzero for an accepted complete span. */
static int write_bytes(checkpoint_writer *writer, const char *bytes, size_t count) {
    /* Step 1: Prove the aggregate physical bound before writing any span. */
    if (count > CGAI_CHECKPOINT_LIMIT - writer->bytes ||
        (writer->stream != NULL && fwrite(bytes, 1U, count, writer->stream) != count))
        return 0;
    /* Step 2: Publish bytes only after successful measurement or output. */
    writer->bytes += count;
    return 1;
}

/** @brief Format and measure or write one short canonical text line.
 * @param writer Mutable borrowed output descriptor.
 * @param format Borrowed bounded printf format with an explicit LF ending.
 * @param ... Scalar arguments used by the format.
 * @return Nonzero after complete bounded formatting and output. */
static int write_line(checkpoint_writer *writer, const char *format, ...) {
    /* Step 1: Format into a fixed buffer, never an allocation based on model fields. */
    char line[256];
    va_list arguments;
    va_start(arguments, format);
    const int count = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    /* Step 2: Reject formatting failure or truncation before measuring or writing. */
    return count >= 0 && (size_t)count < sizeof(line) && write_bytes(writer, line, (size_t)count);
}

/** @brief Serialize fixed version, shape, counts and training counters in canonical order.
 * @param writer Mutable borrowed output descriptor.
 * @param model Borrowed validated standalone model.
 * @return Nonzero after the complete header. */
static int write_header(checkpoint_writer *writer, const cgai_neural_model *model) {
    /* Step 1: Keep every scalar field independent of compiler structure layout. */
    const cgai_neural_config *config = &model->config;
    return write_line(writer, "CGAI-CHECKPOINT 1\n") &&
           write_line(writer, "embedding_dimensions %zu\n", config->embedding_dimensions) &&
           write_line(writer, "hidden_dimensions %zu\n", config->hidden_dimensions) &&
           write_line(writer, "centroid_count %zu\n", config->centroid_count) &&
           write_line(writer, "context_window %zu\n", config->context_window) &&
           write_line(writer, "seed %" PRIu64 "\n", config->seed) &&
           write_line(writer, "routing_temperature %a\n", config->routing_temperature) &&
           write_line(writer, "vocabulary_size %zu\n", model->vocabulary_size) &&
           write_line(writer, "parameter_count %zu\n", model->parameter_count) &&
           write_line(writer, "optimizer %u\n", model->adam_first != NULL ? 1U : 0U) &&
           write_line(writer, "training_step %" PRIu64 "\n", model->training_step) &&
           write_line(writer, "training_epochs %" PRIu64 "\n", model->training_epochs) &&
           write_line(writer, "training_shuffle %" PRIu64 "\n", model->training_shuffle);
}

/** @brief Encode one bounded spelling span as lowercase hexadecimal bytes.
 * @param output Writable buffer with at least twice count bytes.
 * @param input Borrowed source span of count spelling bytes.
 * @param count Source byte count. */
static void encode_hex(char *output, const char *input, size_t count) {
    /* Step 1: Preserve unsigned bytes without locale-sensitive conversion. */
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < count; ++i) {
        const unsigned char byte = (unsigned char)input[i];
        output[i * 2U] = digits[byte >> 4U];
        output[i * 2U + 1U] = digits[byte & 15U];
    }
}

/** @brief Measure or write a spelling as canonical lowercase hexadecimal bytes.
 * @param writer Mutable borrowed output descriptor.
 * @param spelling Borrowed validated nonempty spelling.
 * @return Nonzero after its complete encoded payload and LF. */
static int write_hex(checkpoint_writer *writer, const char *spelling) {
    /* Step 1: Measurement needs only the already validated spelling length. */
    const size_t length = strlen(spelling);
    if (writer->stream == NULL)
        return write_bytes(writer, "", length * 2U) && write_bytes(writer, "\n", 1U);
    /* Step 2: Encode bounded blocks to avoid per-byte stream calls for long spellings. */
    for (size_t offset = 0U; offset < length;) {
        char block[128];
        const size_t count =
            length - offset > sizeof(block) / 2U ? sizeof(block) / 2U : length - offset;
        encode_hex(block, spelling + offset, count);
        if (!write_bytes(writer, block, count * 2U))
            return 0;
        offset += count;
    }
    return write_bytes(writer, "\n", 1U);
}

/** @brief Serialize the complete fixed-ID vocabulary in canonical order.
 * @param writer Mutable borrowed output descriptor.
 * @param model Borrowed validated standalone model.
 * @return Nonzero after every encoded spelling. */
static int write_vocabulary(checkpoint_writer *writer, const cgai_neural_model *model) {
    /* Step 1: Emit the marker, then unambiguous ID and byte-length prefixes. */
    if (!write_line(writer, "vocabulary\n"))
        return 0;
    for (size_t i = 0U; i < model->vocabulary_size; ++i)
        if (!write_line(writer, "token %zu %zu ", i, strlen(model->vocabulary[i])) ||
            !write_hex(writer, model->vocabulary[i]))
            return 0;
    return 1;
}

/** @brief Serialize every standalone parameter exactly.
 * @param writer Mutable borrowed output descriptor.
 * @param model Borrowed validated standalone model.
 * @return Nonzero after the complete parameter section. */
static int write_parameters(checkpoint_writer *writer, const cgai_neural_model *model) {
    /* Step 1: Preserve contiguous parameter order with lossless hexadecimal numbers. */
    if (!write_line(writer, "parameters\n"))
        return 0;
    for (size_t i = 0U; i < model->parameter_count; ++i)
        if (!write_line(writer, "parameter %zu %a\n", i, model->parameters[i]))
            return 0;
    return 1;
}

/** @brief Serialize optional Adam moment pairs exactly.
 * @param writer Mutable borrowed output descriptor.
 * @param model Borrowed validated standalone model.
 * @return Nonzero after the complete moment section. */
static int write_moments(checkpoint_writer *writer, const cgai_neural_model *model) {
    /* Step 1: Keep an empty section for untrained models, otherwise emit all moment pairs. */
    if (!write_line(writer, "moments\n"))
        return 0;
    if (model->adam_first != NULL)
        for (size_t i = 0U; i < model->parameter_count; ++i)
            if (!write_line(writer, "adam %zu %a %a\n", i, model->adam_first[i],
                            model->adam_second[i]))
                return 0;
    return 1;
}

/** @brief Measure or write every canonical checkpoint byte.
 * @param writer Mutable borrowed output descriptor.
 * @param model Borrowed validated standalone model.
 * @return Nonzero for a complete checkpoint within the aggregate byte bound. */
static int write_model(checkpoint_writer *writer, const cgai_neural_model *model) {
    /* Step 1: Use exactly the same serialization for preflight and actual replacement. */
    return write_header(writer, model) && write_vocabulary(writer, model) &&
           write_parameters(writer, model) && write_moments(writer, model) &&
           write_line(writer, "end\n");
}

/** @brief Save exact standalone weights and optimizer state in a reviewable text checkpoint.
 * @param model Borrowed immutable standalone model; training requires exclusive access.
 * @param path Borrowed nonempty destination path; existing content is replaced nonatomically.
 * @return OK after complete write and close, otherwise ERROR with a diagnostic. */
cgai_status cgai_neural_checkpoint_save(const cgai_neural_model *model, const char *path) {
    /* Step 1: Validate complete state and exact output size before replacing any file. */
    cgai_error_clear();
    checkpoint_writer writer = {NULL, 0U};
    if (path == NULL || *path == '\0' || !numeric_locale() || !saveable(model) ||
        !write_model(&writer, model))
        return cgai_fail(
            "checkpoint save requires valid standalone state, a path and C numeric locale");
    /* Step 2: Serialize with LF bytes and always close the acquired stream. */
    writer.stream = fopen(path, "wb");
    writer.bytes = 0U;
    if (writer.stream == NULL)
        return cgai_fail("could not open checkpoint for writing");
    int ok = write_model(&writer, model);
    if (fclose(writer.stream) != 0)
        ok = 0;
    return ok ? CGAI_STATUS_OK : cgai_fail("could not completely write checkpoint");
}
