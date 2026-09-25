/** @file model_decode.c @brief In-memory model deserialization. */

#include "internal/model_io.h"

#include "internal/constants.h"
#include "internal/error.h"
#include "internal/model_format.h"
#include "internal/model_validation.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Borrowed artifact bytes plus a cursor marking the next unread byte.
 *
 * The cursor owns no allocation. Every successful reader_take() advances offset;
 * bounds failures leave it unchanged. Keeping size and offset in bytes lets the
 * same cursor read fixed-width integers, strings, and complete numeric arrays.
 */
typedef struct byte_reader {
    const uint8_t *data; /**< Borrowed readable artifact memory for this decode call. */
    size_t size;         /**< Complete input length in bytes. */
    size_t offset;       /**< Bytes already consumed; never intentionally exceeds size. */
} byte_reader;

/**
 * @brief Copy the next bounded field from borrowed serialized bytes.
 *
 * The cursor owns no memory. Offset points to the next unread byte; subtraction checks the
 * remaining length without overflowing offset + size. Successful reads advance offset, while a
 * failed bounds check leaves it unchanged. This checks byte availability, not the field's meaning.
 *
 * @param reader Non-NULL cursor over a live readable artifact buffer.
 * @param output Writable destination holding at least size bytes, separate from source.
 * @param size Number of bytes to consume.
 * @return One after copying and advancing, or zero if the input does not contain the whole field.
 */
static int reader_take(byte_reader *reader, void *output, size_t size) {
    /* Step 1: Reject an invalid cursor or a field longer than the remaining input. */
    if (reader->offset > reader->size || size > reader->size - reader->offset) {
        return 0;
    }
    /* Step 2: Copy the field into caller-provided storage. */
    memcpy(output, reader->data + reader->offset, size);
    /* Step 3: Consume those bytes so the next field starts at the updated cursor. */
    reader->offset += size;
    return 1;
}

/**
 * @brief Check format identity and supported model-shape fields.
 *
 * Magic distinguishes this format from unrelated data. Dimension/context limits are checked before
 * model allocation. Vocabulary size must include the reserved entries and fit size_t. This is one
 * validation stage: initialized-centroid count and payload completeness are checked later. The
 * format remains intended for trusted data, not arbitrary hostile inputs.
 *
 * @param magic Readable array containing sizeof(CGAI_MODEL_MAGIC) bytes.
 * @param header Readable uint64_t array containing every fixed header field.
 * @return Nonzero for the checks performed here, otherwise zero.
 */
static int valid_header(const char *magic, const uint64_t *header) {
    /* Step 1: Require matching magic, positive bounded dimensions, and a representable vocabulary
     * count. */
    return memcmp(magic, CGAI_MODEL_MAGIC, sizeof(CGAI_MODEL_MAGIC)) == 0 &&
           header[CGAI_HEADER_DIMENSIONS] != 0U && header[CGAI_HEADER_CENTROID_COUNT] != 0U &&
           header[CGAI_HEADER_CONTEXT_WINDOW] != 0U &&
           header[CGAI_HEADER_DIMENSIONS] <= CGAI_MAX_DIMENSIONS &&
           header[CGAI_HEADER_CENTROID_COUNT] <= CGAI_MAX_CENTROID_COUNT &&
           header[CGAI_HEADER_CONTEXT_WINDOW] <= CGAI_MAX_CONTEXT_WINDOW &&
           header[CGAI_HEADER_VOCABULARY_SIZE] >= CGAI_SPECIAL_TOKEN_COUNT &&
           header[CGAI_HEADER_VOCABULARY_SIZE] <= SIZE_MAX &&
           header[CGAI_HEADER_EXAMPLES_SEEN] <= SIZE_MAX &&
           header[CGAI_HEADER_INITIALIZED_CENTROIDS] <= header[CGAI_HEADER_CENTROID_COUNT];
}

/**
 * @brief Consume the fixed artifact header and validate its identity and shape.
 *
 * Short-circuit AND means validation runs only when both fixed-size reads succeeded. A failed read
 * can leave the cursor after an earlier field; the decoder abandons the entire artifact rather than
 * trying to resume from a partial header.
 *
 * @param reader Non-NULL cursor initially positioned at the artifact's first byte.
 * @param header Writable array of CGAI_MODEL_HEADER_FIELD_COUNT uint64_t values.
 * @return One for a fully read, accepted header; zero for truncation or invalid fields.
 */
static int read_header(byte_reader *reader, uint64_t *header) {
    /* The magic protects against unrelated files; the header limits resource use. */
    /* Step 1: Reserve local storage for the format identifier. */
    char magic[sizeof(CGAI_MODEL_MAGIC)];
    /* Step 2: Read magic, read scalar fields, and validate only after both copies are complete. */
    return reader_take(reader, magic, sizeof(magic)) &&
           reader_take(reader, header, sizeof(uint64_t) * CGAI_MODEL_HEADER_FIELD_COUNT) &&
           valid_header(magic, header);
}

/**
 * @brief Construct a core model using previously validated serialized dimensions.
 *
 * The model constructor applies its own limits and creates numeric storage plus the default control
 * vocabulary. Restoration later replaces that vocabulary with serialized spellings. The temporary
 * configuration is copied by the constructor, so it need not survive this function.
 *
 * @param header Borrowed fixed header that already passed read_header().
 * @return Owned core model with constructor state, or NULL if validation/allocation fails.
 */
static cgai_model *create_from_header(const uint64_t *header) {
    /* Step 1: Translate the serialized fields into the native constructor's configuration type. */
    const cgai_config config = {
        (size_t)header[CGAI_HEADER_DIMENSIONS], (size_t)header[CGAI_HEADER_CENTROID_COUNT],
        (size_t)header[CGAI_HEADER_CONTEXT_WINDOW], header[CGAI_HEADER_SEED]};
    /* Step 2: Delegate allocation and invariant setup to the normal model constructor. */
    return cgai_model_create(&config);
}

/**
 * @brief Remove default vocabulary before loading the artifact's token order.
 *
 * Constructor-created BOS/EOS/UNKNOWN spellings must not precede the serialized spellings a second
 * time. Numeric centroid arrays are retained, while vocabulary strings, their pointer array, and
 * the vocabulary-dependent count matrix are released. Clearing pointers and counters leaves partial
 * decoding state safe for the ordinary model destructor.
 *
 * @param model Non-NULL newly constructed model, owned exclusively by the decoder.
 */
static void clear_constructor_vocabulary(cgai_model *model) {
    /* Step 1: Free every constructor-installed spelling. */
    for (size_t i = 0; i < model->vocabulary_size; ++i) {
        free(model->vocabulary[i]);
    }
    /* Step 2: Release the vocabulary pointer array and its dependent count matrix. */
    free(model->vocabulary);
    free(model->token_counts);
    /* Step 3: Reset vocabulary ownership and occupancy before rebuilding serialized entries. */
    model->vocabulary = NULL;
    model->token_counts = NULL;
    model->vocabulary_size = 0U;
    model->vocabulary_capacity = 0U;
}

/** @brief Compare borrowed spellings without changing serialized token IDs. */
static int compare_tokens(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/** @brief Read one owned, terminated spelling; publish only complete strings. */
static char *read_token(byte_reader *reader) {
    uint64_t length = 0U;
    if (!reader_take(reader, &length, sizeof(length)) || length == 0U ||
        length > CGAI_MAX_TOKEN_BYTES)
        return NULL;
    char *token = (char *)malloc((size_t)length + 1U);
    if (!token || !reader_take(reader, token, (size_t)length) ||
        memchr(token, '\0', (size_t)length)) {
        free(token);
        return NULL;
    }
    token[length] = '\0';
    return token;
}

/** @brief Reject duplicate spellings using a temporary sorted pointer array. */
static int unique_vocabulary(const cgai_model *model) {
    char **sorted = (char **)malloc(model->vocabulary_size * sizeof(char *));
    if (!sorted)
        return 0;
    memcpy(sorted, model->vocabulary, model->vocabulary_size * sizeof(char *));
    qsort(sorted, model->vocabulary_size, sizeof(char *), compare_tokens);
    int unique = 1;
    for (size_t i = 1U; i < model->vocabulary_size; ++i)
        if (!strcmp(sorted[i - 1U], sorted[i])) {
            unique = 0;
            break;
        }
    free(sorted);
    return unique;
}

static int read_spellings(byte_reader *reader, cgai_model *model, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) {
        char *token = read_token(reader);
        if (!token)
            return 0;
        model->vocabulary[model->vocabulary_size++] = token;
    }
    return 1;
}

/** @brief Allocate vocabulary and count storage once, retaining serialized token order. */
static int read_vocabulary(byte_reader *reader, cgai_model *model, uint64_t count) {
    if (count > SIZE_MAX / sizeof(char *))
        return 0;
    model->vocabulary = (char **)calloc((size_t)count, sizeof(char *));
    if (!model->vocabulary)
        return 0;
    model->vocabulary_capacity = (size_t)count;
    if (!read_spellings(reader, model, count))
        return 0;
    if (!unique_vocabulary(model) ||
        count > SIZE_MAX / sizeof(uint64_t) / model->config.centroid_count)
        return 0;
    model->token_counts =
        (uint64_t *)calloc(model->config.centroid_count * (size_t)count, sizeof(uint64_t));
    return model->token_counts != NULL;
}

/**
 * @brief Restore numeric arrays and reject extra trailing artifact bytes.
 *
 * The destination arrays were allocated from the accepted model configuration and rebuilt
 * vocabulary. Their expected byte lengths determine how much the reader consumes; serialized bytes
 * do not contain separate per-array offsets. No endian or floating-point representation conversion
 * is performed.
 *
 * @param reader Non-NULL cursor positioned after vocabulary records.
 * @param model Mutable model with numeric storage consistent with its dimensions and vocabulary.
 * @return One only when all arrays are copied and the cursor ends exactly at the input length.
 */
static int read_numeric_payload(byte_reader *reader, cgai_model *model) {
    /* reader_take prevents every copy from crossing the supplied artifact boundary. */
    /* Step 1: Read centroid floats, cluster sizes, and token counts in encoder order, then require
     * end-of-input. */
    return reader_take(reader, model->centroids,
                       model->config.centroid_count * model->config.dimensions * sizeof(float)) &&
           reader_take(reader, model->cluster_sizes,
                       model->config.centroid_count * sizeof(uint64_t)) &&
           reader_take(reader, model->token_counts,
                       model->config.centroid_count * model->vocabulary_size * sizeof(uint64_t)) &&
           reader->offset == reader->size;
}

/**
 * @brief Replace constructor state with serialized vocabulary, counters, and arrays.
 *
 * This operates on a model owned privately by the decoder, so partial changes are never published
 * to an API caller. If any stage fails, the outer decoder destroys that model. Header counters are
 * copied by value; the initialized-centroid count must not exceed allocated centroid capacity.
 *
 * @param reader Non-NULL cursor immediately following the validated header.
 * @param model Newly constructed mutable model owned by decoding.
 * @param header Borrowed fixed header already accepted by read_header().
 * @return One for a complete body, otherwise zero with a partially restored model still owned by
 * the caller.
 */
static int restore_model_body(byte_reader *reader, cgai_model *model, const uint64_t *header) {
    /* Step 1: Discard default token storage and rebuild the serialized token order. */
    clear_constructor_vocabulary(model);
    if (!read_vocabulary(reader, model, header[CGAI_HEADER_VOCABULARY_SIZE])) {
        return 0;
    }
    /* Step 2: Restore learned counters and reject a centroid count beyond allocated capacity. */
    model->initialized_centroids = (size_t)header[CGAI_HEADER_INITIALIZED_CENTROIDS];
    model->examples_seen = (size_t)header[CGAI_HEADER_EXAMPLES_SEEN];
    if (model->initialized_centroids > model->config.centroid_count) {
        return 0;
    }
    /* Step 3: Fill numeric arrays and require the artifact to end at the expected boundary. */
    return read_numeric_payload(reader, model);
}

/** @brief Bound minimum payload size before allocating from serialized dimensions. */
static int payload_fits(const byte_reader *reader, const uint64_t *header) {
    const uint64_t rows = header[CGAI_HEADER_CENTROID_COUNT];
    const uint64_t remaining = (uint64_t)(reader->size - reader->offset);
    const uint64_t fixed =
        rows * (header[CGAI_HEADER_DIMENSIONS] * sizeof(float) + sizeof(uint64_t));
    const uint64_t per_token = rows * sizeof(uint64_t) + sizeof(uint64_t) + 1U;
    return fixed <= remaining &&
           header[CGAI_HEADER_VOCABULARY_SIZE] <= (remaining - fixed) / per_token;
}

/**
 * @brief Decode a complete trusted artifact into a new owned model.
 *
 * Input bytes remain borrowed; every string and numeric array in the returned model has separate
 * storage. The decoder validates fixed fields before allocation, reconstructs the body, and
 * destroys a partial model on any later failure. Native byte order and numeric representations
 * require compatible producing/consuming builds.
 *
 * @param data Non-NULL readable artifact bytes, borrowed until this call returns.
 * @param size Complete byte length of the supplied artifact.
 * @return Owned model to release with cgai_model_destroy(), or NULL with a thread-local diagnostic.
 */
cgai_model *cgai_model_decode(const uint8_t *data, size_t size) {
    /* Step 1: Reject missing bytes before creating a reader over them. */
    if (data == NULL) {
        (void)cgai_fail("model bytes are required");
        return NULL;
    }
    /* Step 2: Start a cursor at offset zero and reserve local fixed-header storage. */
    byte_reader reader = {data, size, 0U};
    uint64_t header[CGAI_MODEL_HEADER_FIELD_COUNT];
    /* Validate the fixed header before allocating any variable model storage. */
    /* Step 3: Validate the fixed header before allocating a variable-sized model. */
    if (!read_header(&reader, header) || !payload_fits(&reader, header)) {
        (void)cgai_fail("invalid model header or declared payload size");
        return NULL;
    }
    /* Step 4: Create the model's base storage from the accepted configuration. */
    cgai_model *model = create_from_header(header);
    if (model == NULL) {
        return NULL;
    }
    /* Rebuild vocabulary rows, metadata, and arrays in serialized order. */
    /* Step 5: Restore all fields; destroy partial state if the artifact cannot be fully consumed.
     */
    if (!restore_model_body(&reader, model, header) ||
        cgai_model_validate_statistics(model) != CGAI_STATUS_OK) {
        cgai_model_destroy(model);
        (void)cgai_fail("model data is truncated or invalid");
        return NULL;
    }
    /* Step 6: Publish ownership only after complete restoration succeeds. */
    return model;
}
