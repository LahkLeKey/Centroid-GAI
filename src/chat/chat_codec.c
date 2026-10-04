/** @file chat_codec.c @brief Bounded versioned little-endian conversation artifacts. */
#include "internal/chat_internal.h"
#include "internal/error.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(double) == 8U && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "Chat artifacts require binary64 doubles");

/** Bounded cursor; failure is sticky and no operation crosses the buffer. */
typedef struct chat_codec {
    uint8_t *data; /**< Borrowed input or output bytes. */
    size_t size;   /**< Total capacity. */
    size_t offset; /**< Next byte. */
    int reading;   /**< Decode when nonzero. */
    int ok;        /**< Sticky success. */
} chat_codec;

/** @brief Transfer a bounded byte span.
 * @param codec Mutable cursor.
 * @param bytes Writable decode destination or encode source.
 * @param count Byte length. */
static void codec_bytes(chat_codec *codec, void *bytes, size_t count) {
    if (!codec->ok || count > codec->size - codec->offset) {
        codec->ok = 0;
        return;
    }
    if (codec->reading)
        memcpy(bytes, codec->data + codec->offset, count);
    else
        memcpy(codec->data + codec->offset, bytes, count);
    codec->offset += count;
}

/** @brief Transfer an unsigned little-endian 64-bit integer.
 * @param codec Mutable cursor.
 * @param value Writable scalar. */
static void codec_integer(chat_codec *codec, uint64_t *value) {
    uint8_t bytes[8] = {0};
    if (!codec->reading)
        for (size_t i = 0U; i < 8U; ++i)
            bytes[i] = (uint8_t)(*value >> (8U * i));
    codec_bytes(codec, bytes, sizeof(bytes));
    if (codec->reading) {
        *value = 0U;
        for (size_t i = 0U; i < 8U; ++i)
            *value |= (uint64_t)bytes[i] << (8U * i);
    }
}

/** @brief Transfer a finite IEEE-754 binary64 value.
 * @param codec Mutable cursor.
 * @param value Writable scalar. */
static void codec_double(chat_codec *codec, double *value) {
    uint64_t bits = 0U;
    if (!codec->reading)
        memcpy(&bits, value, sizeof(bits));
    codec_integer(codec, &bits);
    if (codec->reading)
        memcpy(value, &bits, sizeof(bits));
    if (!isfinite(*value))
        codec->ok = 0;
}

/** @brief Validate protocol fields and apply decoded dimensions.
 * @param codec Mutable cursor.
 * @param model Mutable shell.
 * @param fields Borrowed nine header integers. */
static void header_fields(chat_codec *codec, cgai_chat_model *model, const uint64_t *fields) {
    if ((fields[0] != 1U && fields[0] != 2U) || fields[1] != fields[0] ||
        fields[8] != CGAI_CHAT_TOKENIZER_VERSION)
        codec->ok = 0;
    for (size_t i = 2U; i < 7U; ++i)
        if (fields[i] > 256U)
            codec->ok = 0;
    if (codec->reading && codec->ok) {
        const cgai_chat_config config = {(size_t)fields[2],
                                         (size_t)fields[3],
                                         (size_t)fields[4],
                                         (size_t)fields[5],
                                         (size_t)fields[6],
                                         fields[7],
                                         1.0,
                                         0U};
        model->config = config;
        model->protocol_version = (unsigned int)fields[1];
    }
}

/** @brief Transfer the explicit evidence budget present only in version two.
 * @param codec Mutable cursor.
 * @param model Mutable shell or local encoding copy. */
static void codec_evidence(chat_codec *codec, cgai_chat_model *model) {
    if (model->protocol_version < 2U)
        return;
    uint64_t evidence = model->config.evidence_window;
    codec_integer(codec, &evidence);
    if (evidence > 256U)
        codec->ok = 0;
    if (codec->reading && codec->ok)
        model->config.evidence_window = (size_t)evidence;
}

/** @brief Transfer and validate the protocol header.
 * @param codec Mutable cursor.
 * @param model Writable shell, or borrowed encode model.
 * @param vocabulary Writable vocabulary count.
 * @param parameters Writable parameter count. */
static void codec_header(chat_codec *codec, cgai_chat_model *model, uint64_t *vocabulary,
                         uint64_t *parameters) {
    uint8_t magic[8] = {'C', 'G', 'A', 'I', 'C', 'H', 'A', 'T'};
    codec_bytes(codec, magic, 8U);
    if (memcmp(magic, "CGAICHAT", 8U) != 0)
        codec->ok = 0;
    uint64_t fields[9] = {model->protocol_version,
                          model->protocol_version,
                          model->config.embedding_dimensions,
                          model->config.hidden_dimensions,
                          model->config.centroid_count,
                          model->config.prompt_window,
                          model->config.response_window,
                          model->config.seed,
                          1U};
    for (size_t i = 0U; i < 9U; ++i)
        codec_integer(codec, &fields[i]);
    header_fields(codec, model, fields);
    codec_double(codec, &model->config.routing_temperature);
    codec_integer(codec, vocabulary);
    codec_integer(codec, parameters);
    codec_evidence(codec, model);
    if (!cgai_chat_validate_config(&model->config, &model->network->config) || *vocabulary < 8U ||
        *vocabulary > CGAI_NEURAL_MAX_VOCABULARY || *parameters > CGAI_NEURAL_MAX_PARAMETERS)
        codec->ok = 0;
}

/** @brief Check one normalized lexical spelling or exact structural control.
 * @param model Borrowed partial model.
 * @param index Vocabulary index.
 * @param count Final vocabulary count.
 * @param text Borrowed spelling.
 * @return Nonzero for a valid unique spelling. */
static int valid_spelling(const cgai_chat_model *model, size_t index, size_t count,
                          const char *text) {
    const char *special[] = {
        "<bos>",           "<eos>",     "<unk>", "<chat:user>", "<chat:assistant>",
        "<chat:evidence>", "<chat:end>"};
    if (index < 3U)
        return strcmp(text, special[index]) == 0;
    if (index >= count - 4U)
        return strcmp(text, special[3U + index - (count - 4U)]) == 0;
    cgai_token_list tokens = {0};
    int ok =
        cgai_tokenize(text, &tokens) && tokens.count == 1U && strcmp(text, tokens.items[0]) == 0;
    cgai_token_list_destroy(&tokens);
    for (size_t i = 0U; ok && i < index; ++i)
        if (strcmp(text, model->network->vocabulary[i]) == 0)
            ok = 0;
    return ok;
}

/** @brief Decode one bounded unique vocabulary entry.
 * @param codec Mutable reader.
 * @param model Mutable vocabulary owner.
 * @param index Entry index.
 * @param count Final vocabulary count. */
static void read_spelling(chat_codec *codec, cgai_chat_model *model, size_t index, size_t count) {
    uint64_t length = 0U;
    codec_integer(codec, &length);
    if (!codec->ok || length == 0U || length > CGAI_NEURAL_MAX_TOKEN_BYTES ||
        length > codec->size - codec->offset) {
        codec->ok = 0;
        return;
    }
    char *text = calloc((size_t)length + 1U, 1U);
    if (text == NULL) {
        codec->ok = 0;
        return;
    }
    codec_bytes(codec, text, (size_t)length);
    codec->ok = codec->ok && strlen(text) == length && valid_spelling(model, index, count, text) &&
                cgai_neural_append_spelling(model->network, text);
    free(text);
}

/** @brief Decode parameters after validating header and vocabulary.
 * @param codec Mutable bounded cursor.
 * @param model Mutable empty network owner. */
static void decode_payload(chat_codec *codec, cgai_chat_model *model) {
    uint64_t vocabulary = 0U, parameters = 0U;
    codec_header(codec, model, &vocabulary, &parameters);
    model->network->vocabulary = calloc(CGAI_NEURAL_MAX_VOCABULARY, sizeof(char *));
    if (model->network->vocabulary == NULL)
        codec->ok = 0;
    for (size_t i = 0U; codec->ok && i < vocabulary; ++i)
        read_spelling(codec, model, i, (size_t)vocabulary);
    if (codec->ok)
        codec->ok = cgai_neural_allocate_parameters(model->network) &&
                    parameters == model->network->parameter_count;
    for (size_t i = 0U; codec->ok && i < parameters; ++i)
        codec_double(codec, &model->network->parameters[i]);
}

/** @brief Allocate the two independently owned empty model shells.
 * @return Owned shell or NULL after cleanup. */
static cgai_chat_model *empty_model(void) {
    cgai_chat_model *model = calloc(1U, sizeof(*model));
    if (model != NULL)
        model->network = calloc(1U, sizeof(*model->network));
    if (model == NULL || model->network == NULL) {
        cgai_chat_destroy(model);
        cgai_fail("could not allocate chat model");
        return NULL;
    }
    return model;
}

cgai_chat_model *cgai_chat_decode(const uint8_t *data, size_t size) {
    if (data == NULL || size < 104U || size > CGAI_CHAT_MAX_ARTIFACT_BYTES) {
        cgai_fail("invalid chat artifact size");
        return NULL;
    }
    cgai_chat_model *model = empty_model();
    if (model == NULL)
        return NULL;
    chat_codec codec = {(uint8_t *)data, size, 0U, 1, 1};
    decode_payload(&codec, model);
    if (!codec.ok || codec.offset != size) {
        cgai_chat_destroy(model);
        cgai_fail("invalid, incompatible or truncated chat artifact");
        return NULL;
    }
    model->network->output_size -= CGAI_CHAT_CONTROL_COUNT;
    return model;
}

/** @brief Encode vocabulary and parameters with a bounded cursor.
 * @param codec Mutable writer.
 * @param network Borrowed immutable network. */
static void write_payload(chat_codec *codec, const cgai_neural_model *network) {
    for (size_t i = 0U; i < network->vocabulary_size; ++i) {
        uint64_t length = strlen(network->vocabulary[i]);
        codec_integer(codec, &length);
        codec_bytes(codec, network->vocabulary[i], (size_t)length);
    }
    for (size_t i = 0U; i < network->parameter_count; ++i) {
        double value = network->parameters[i];
        codec_double(codec, &value);
    }
}

/** @brief Encode through local metadata copies so readers never mutate the model.
 * @param codec Mutable writer.
 * @param model Borrowed immutable model. */
static void encode_model(chat_codec *codec, const cgai_chat_model *model) {
    cgai_chat_model copy = *model;
    cgai_neural_model network = *model->network;
    copy.network = &network;
    uint64_t vocabulary = network.vocabulary_size, parameters = network.parameter_count;
    codec_header(codec, &copy, &vocabulary, &parameters);
    write_payload(codec, &network);
}

/** @brief Measure a model's bounded encoded length.
 * @param model Borrowed initialized model.
 * @return Total bytes including the fixed header. */
static size_t encoded_size(const cgai_chat_model *model) {
    size_t bytes =
        (model->protocol_version >= 2U ? 112U : 104U) + model->network->parameter_count * 8U;
    for (size_t i = 0U; i < model->network->vocabulary_size; ++i)
        bytes += 8U + strlen(model->network->vocabulary[i]);
    return bytes;
}

cgai_status cgai_chat_encode(const cgai_chat_model *model, uint8_t **data, size_t *size) {
    if (model == NULL || data == NULL || size == NULL)
        return cgai_fail("chat codec arguments required");
    const size_t bytes = encoded_size(model);
    if (bytes > CGAI_CHAT_MAX_ARTIFACT_BYTES)
        return cgai_fail("chat artifact too large");
    uint8_t *buffer = malloc(bytes);
    if (buffer == NULL)
        return cgai_fail("could not allocate chat artifact");
    chat_codec codec = {buffer, bytes, 0U, 0, 1};
    encode_model(&codec, model);
    if (!codec.ok || codec.offset != bytes) {
        free(buffer);
        return cgai_fail("invalid chat artifact");
    }
    *data = buffer;
    *size = bytes;
    return CGAI_STATUS_OK;
}

void cgai_chat_buffer_free(uint8_t *data) { free(data); }
