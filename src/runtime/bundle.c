#include "runtime_internal.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SHA-256 content identity. Arithmetic is explicitly unsigned modulo 2^32. */
static uint32_t rotate(uint32_t value, unsigned n) {
  return (value >> n) | (value << (32u - n));
}
static void hash_block(uint32_t state[8], const unsigned char block[64]) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t w[64], a, b, c, d, e, f, g, h;
  unsigned i;
  for (i = 0; i < 16; i++)
    w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) |
           ((uint32_t)block[4 * i + 2] << 8) | block[4 * i + 3];
  for (i = 16; i < 64; i++) {
    uint32_t s0 =
        rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 =
        rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];
  f = state[5];
  g = state[6];
  h = state[7];
  for (i = 0; i < 64; i++) {
    uint32_t t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) +
                  ((e & f) ^ ((~e) & g)) + k[i] + w[i];
    uint32_t t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) +
                  ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}
void cr_hash(const void *bytes, size_t length, char out[CR_DIGEST_HEX]) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const unsigned char *data = (const unsigned char *)bytes;
  unsigned char tail[128] = {0};
  size_t blocks = length / 64, remainder = length % 64, i,
         padding = remainder < 56 ? 64 : 128;
  uint64_t bits = (uint64_t)length * 8;
  static const char digits[] = "0123456789abcdef";
  for (i = 0; i < blocks; i++)
    hash_block(state, data + 64 * i);
  if (remainder)
    memcpy(tail, data + 64 * blocks, remainder);
  tail[remainder] = 0x80;
  for (i = 0; i < 8; i++)
    tail[padding - 1 - i] = (unsigned char)(bits >> (8 * i));
  hash_block(state, tail);
  if (padding == 128)
    hash_block(state, tail + 64);
  for (i = 0; i < 32; i++) {
    unsigned char v = (unsigned char)(state[i / 4] >> (24 - 8 * (i % 4)));
    out[2 * i] = digits[v >> 4];
    out[2 * i + 1] = digits[v & 15];
  }
  out[64] = 0;
}

void cr_hash_bytes(const void *bytes, size_t length, char out[CR_DIGEST_HEX]) {
  cr_hash(bytes, length, out);
}

#define CR_BUNDLE_HEADER_BYTES 1500u
#define CR_BUNDLE_FOOTER_BYTES 64u
#define CR_EXPERT_PARAMETER_COUNT                                              \
  (CR_FEATURES * (1u + CR_CODE_ACTIONS + CR_TEXT_ACTIONS))

/* No native structure bytes enter the format. Fixed string fields use one NUL
 * followed by zero padding. All integer and binary64 fields are little endian.
 */
typedef struct {
  unsigned char *bytes;
  size_t offset;
} bundle_writer;
typedef struct {
  const unsigned char *bytes;
  size_t length, offset;
  int invalid;
} bundle_reader;

static int binary64_supported(void) {
  return sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53 &&
         DBL_MAX_EXP == 1024 && DBL_MIN_EXP == -1021;
}

static void put_bytes(bundle_writer *writer, const void *bytes, size_t length) {
  memcpy(writer->bytes + writer->offset, bytes, length);
  writer->offset += length;
}

static void put_u32(bundle_writer *writer, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    writer->bytes[writer->offset++] = (unsigned char)(value >> (8u * i));
}

static void put_u64(bundle_writer *writer, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    writer->bytes[writer->offset++] = (unsigned char)(value >> (8u * i));
}

static void put_double(bundle_writer *writer, double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  put_u64(writer, bits);
}

static void put_string(bundle_writer *writer, const char *value,
                       size_t capacity) {
  const size_t length = strlen(value);
  memset(writer->bytes + writer->offset, 0, capacity);
  memcpy(writer->bytes + writer->offset, value, length);
  writer->offset += capacity;
}

static void get_bytes(bundle_reader *reader, void *out, size_t length) {
  if (reader->invalid || length > reader->length - reader->offset) {
    reader->invalid = 1;
    return;
  }
  memcpy(out, reader->bytes + reader->offset, length);
  reader->offset += length;
}

static uint32_t get_u32(bundle_reader *reader) {
  unsigned char bytes[4] = {0};
  uint32_t value = 0;
  get_bytes(reader, bytes, sizeof(bytes));
  for (unsigned i = 0; i < 4; ++i)
    value |= (uint32_t)bytes[i] << (8u * i);
  return value;
}

static uint64_t get_u64(bundle_reader *reader) {
  unsigned char bytes[8] = {0};
  uint64_t value = 0;
  get_bytes(reader, bytes, sizeof(bytes));
  for (unsigned i = 0; i < 8; ++i)
    value |= (uint64_t)bytes[i] << (8u * i);
  return value;
}

static double get_double(bundle_reader *reader) {
  const uint64_t bits = get_u64(reader);
  double value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static void get_string(bundle_reader *reader, char *out, size_t capacity) {
  get_bytes(reader, out, capacity);
  if (reader->invalid)
    return;
  const char *end = memchr(out, 0, capacity);
  if (!end) {
    reader->invalid = 1;
    return;
  }
  for (size_t i = (size_t)(end - out); i < capacity; ++i)
    if (out[i])
      reader->invalid = 1;
}

static size_t values_size(uint32_t groups) {
  return CR_FEATURES * 8u +
         (size_t)groups * (8u + CR_EXPERT_PARAMETER_COUNT * 8u);
}

static void put_semantics(bundle_writer *writer, const cr_model *model) {
  put_u32(writer, model->groups);
  put_u32(writer, model->shared_enabled);
  put_u32(writer, CR_FEATURES);
  put_u32(writer, CR_CODE_ACTIONS);
  put_u32(writer, CR_TEXT_ACTIONS);
  put_u32(writer, CR_FEATURES + model->groups * CR_EXPERT_PARAMETER_COUNT);
  put_string(writer, CR_RECIPE_ID, CR_ID_BYTES);
  put_string(writer, CR_ENCODER_ID, CR_ID_BYTES);
  put_string(writer, CR_FRAMING_ID, CR_ID_BYTES);
  put_string(writer, CR_ACTION_SCHEMA_ID, CR_ID_BYTES);
}

static void put_profile(bundle_writer *writer, const cr_model *model) {
  put_string(writer, model->metadata.task_profile, CR_ID_BYTES);
  put_string(writer, model->metadata.observation_schema, CR_ID_BYTES);
  put_string(writer, model->metadata.action_catalog, CR_ID_BYTES);
}

static void put_values(bundle_writer *writer, const cr_model *model) {
  for (size_t j = 0; j < CR_FEATURES; ++j)
    put_double(writer, model->shared_scale[j]);
  for (uint32_t g = 0; g < model->groups; ++g) {
    const cr_expert_values *expert = &model->experts[g];
    put_u64(writer, expert->uid);
    for (size_t j = 0; j < CR_FEATURES; ++j)
      put_double(writer, expert->centroid[j]);
    for (size_t a = 0; a < CR_CODE_ACTIONS; ++a)
      for (size_t j = 0; j < CR_FEATURES; ++j)
        put_double(writer, expert->code[a][j]);
    for (size_t a = 0; a < CR_TEXT_ACTIONS; ++a)
      for (size_t j = 0; j < CR_FEATURES; ++j)
        put_double(writer, expert->text[a][j]);
  }
}

cr_status cr_model_identify(cr_model *model) {
  if (!cr_model_values_valid(model))
    return CR_INVALID;
  if (!binary64_supported())
    return CR_UNSUPPORTED;
  const size_t length = 24u + 7u * CR_ID_BYTES + values_size(model->groups);
  unsigned char *bytes = malloc(length);
  if (!bytes)
    return CR_NOMEM;
  bundle_writer writer = {bytes, 0};
  put_semantics(&writer, model);
  put_profile(&writer, model);
  put_values(&writer, model);
  cr_hash(bytes, writer.offset, model->metadata.model_digest);
  free(bytes);
  return CR_OK;
}

size_t CR_CALL cr_model_bundle_size(const cr_model *model) {
  return model && model->groups && model->groups <= CR_MAX_GROUPS
             ? CR_BUNDLE_HEADER_BYTES + values_size(model->groups) +
                   CR_BUNDLE_FOOTER_BYTES
             : 0;
}

cr_status CR_CALL cr_model_write_bundle(const cr_model *model,
                                        unsigned char *bytes, size_t capacity,
                                        size_t *written) {
  char digest[CR_DIGEST_HEX];
  if (!written || !cr_model_values_valid(model) ||
      !cr_metadata_valid(&model->metadata))
    return CR_INVALID;
  if (!binary64_supported())
    return CR_UNSUPPORTED;
  const size_t length = cr_model_bundle_size(model);
  uintptr_t output_start = (uintptr_t)bytes, count_start = (uintptr_t)written;
  uintptr_t model_start = (uintptr_t)model;
  size_t output_span = capacity < length ? capacity : length;
  if (sizeof(*written) > UINTPTR_MAX - count_start ||
      sizeof(*model) > UINTPTR_MAX - model_start ||
      (bytes && output_span > UINTPTR_MAX - output_start))
    return CR_INVALID;
  if ((bytes && output_span && output_start < count_start + sizeof(*written) &&
       count_start < output_start + output_span) ||
      (count_start < model_start + sizeof(*model) &&
       model_start < count_start + sizeof(*written)) ||
      (bytes && output_span && output_start < model_start + sizeof(*model) &&
       model_start < output_start + output_span))
    return CR_INVALID;
  *written = length;
  if (capacity < length)
    return CR_LIMIT;
  if (!bytes)
    return CR_INVALID;
  bundle_writer writer = {bytes, 0};
  put_bytes(&writer, "CRMODEL1", 8);
  put_u32(&writer, CR_BUNDLE_VERSION);
  put_u32(&writer, CR_ABI_VERSION);
  put_u64(&writer, (uint64_t)length);
  put_semantics(&writer, model);
  put_u32(&writer, model->metadata.qualification);
  put_u32(&writer, 0u);
  put_string(&writer, model->metadata.model_digest, CR_DIGEST_HEX);
  put_string(&writer, model->metadata.parent_digest, CR_DIGEST_HEX);
  put_string(&writer, model->metadata.checkpoint_digest, CR_DIGEST_HEX);
  put_string(&writer, model->metadata.qualification_digest, CR_DIGEST_HEX);
  put_profile(&writer, model);
  put_string(&writer, model->metadata.provenance, CR_REFERENCE_BYTES);
  put_string(&writer, model->metadata.qualification_reference,
             CR_REFERENCE_BYTES);
  put_values(&writer, model);
  cr_hash(bytes, writer.offset, digest);
  put_bytes(&writer, digest, 64u);
  return writer.offset == length ? CR_OK : CR_CORRUPT;
}

cr_status CR_CALL cr_model_load_bytes(const unsigned char *bytes, size_t length,
                                      cr_model **inout) {
  char digest[CR_DIGEST_HEX], recorded_id[CR_DIGEST_HEX];
  char recipe[CR_ID_BYTES], encoder[CR_ID_BYTES];
  char framing[CR_ID_BYTES], actions[CR_ID_BYTES];
  cr_status status;
  if (!bytes || !inout)
    return CR_INVALID;
  if (!binary64_supported())
    return CR_UNSUPPORTED;
  if (length > CR_MAX_BUNDLE_BYTES)
    return CR_LIMIT;
  if (length <
          CR_BUNDLE_HEADER_BYTES + values_size(1) + CR_BUNDLE_FOOTER_BYTES ||
      memcmp(bytes, "CRMODEL1", 8))
    return CR_CORRUPT;
  cr_hash(bytes, length - CR_BUNDLE_FOOTER_BYTES, digest);
  if (memcmp(digest, bytes + length - CR_BUNDLE_FOOTER_BYTES, 64))
    return CR_CORRUPT;
  bundle_reader reader = {bytes, length - CR_BUNDLE_FOOTER_BYTES, 8, 0};
  if (get_u32(&reader) != CR_BUNDLE_VERSION ||
      get_u32(&reader) != CR_ABI_VERSION)
    return CR_UNSUPPORTED;
  if (get_u64(&reader) != length)
    return CR_CORRUPT;
  const uint32_t groups = get_u32(&reader);
  const uint32_t shared = get_u32(&reader);
  if (!groups || groups > CR_MAX_GROUPS || shared > 1u)
    return CR_CORRUPT;
  if (length !=
      CR_BUNDLE_HEADER_BYTES + values_size(groups) + CR_BUNDLE_FOOTER_BYTES)
    return CR_CORRUPT;
  if (get_u32(&reader) != CR_FEATURES || get_u32(&reader) != CR_CODE_ACTIONS ||
      get_u32(&reader) != CR_TEXT_ACTIONS ||
      get_u32(&reader) != CR_FEATURES + groups * CR_EXPERT_PARAMETER_COUNT)
    return CR_UNSUPPORTED;
  get_string(&reader, recipe, sizeof(recipe));
  get_string(&reader, encoder, sizeof(encoder));
  get_string(&reader, framing, sizeof(framing));
  get_string(&reader, actions, sizeof(actions));
  if (reader.invalid)
    return CR_CORRUPT;
  if (strcmp(recipe, CR_RECIPE_ID) || strcmp(encoder, CR_ENCODER_ID) ||
      strcmp(framing, CR_FRAMING_ID) || strcmp(actions, CR_ACTION_SCHEMA_ID))
    return CR_UNSUPPORTED;
  cr_model *candidate = calloc(1, sizeof(*candidate));
  if (!candidate)
    return CR_NOMEM;
  candidate->groups = groups;
  candidate->shared_enabled = shared;
  candidate->metadata.struct_size = sizeof(candidate->metadata);
  candidate->metadata.api_version = CR_API_VERSION;
  candidate->metadata.qualification = get_u32(&reader);
  candidate->metadata.reserved = get_u32(&reader);
  get_string(&reader, candidate->metadata.model_digest, CR_DIGEST_HEX);
  get_string(&reader, candidate->metadata.parent_digest, CR_DIGEST_HEX);
  get_string(&reader, candidate->metadata.checkpoint_digest, CR_DIGEST_HEX);
  get_string(&reader, candidate->metadata.qualification_digest, CR_DIGEST_HEX);
  get_string(&reader, candidate->metadata.task_profile, CR_ID_BYTES);
  get_string(&reader, candidate->metadata.observation_schema, CR_ID_BYTES);
  get_string(&reader, candidate->metadata.action_catalog, CR_ID_BYTES);
  get_string(&reader, candidate->metadata.provenance, CR_REFERENCE_BYTES);
  get_string(&reader, candidate->metadata.qualification_reference,
             CR_REFERENCE_BYTES);
  for (size_t j = 0; j < CR_FEATURES; ++j)
    candidate->shared_scale[j] = get_double(&reader);
  for (uint32_t g = 0; g < groups; ++g) {
    cr_expert_values *expert = &candidate->experts[g];
    expert->uid = get_u64(&reader);
    for (size_t j = 0; j < CR_FEATURES; ++j)
      expert->centroid[j] = get_double(&reader);
    for (size_t a = 0; a < CR_CODE_ACTIONS; ++a)
      for (size_t j = 0; j < CR_FEATURES; ++j)
        expert->code[a][j] = get_double(&reader);
    for (size_t a = 0; a < CR_TEXT_ACTIONS; ++a)
      for (size_t j = 0; j < CR_FEATURES; ++j)
        expert->text[a][j] = get_double(&reader);
  }
  if (reader.invalid || reader.offset != reader.length ||
      !cr_model_values_valid(candidate) ||
      !cr_metadata_valid(&candidate->metadata)) {
    free(candidate);
    return CR_CORRUPT;
  }
  memcpy(recorded_id, candidate->metadata.model_digest, sizeof(recorded_id));
  status = cr_model_identify(candidate);
  if (status != CR_OK ||
      strcmp(recorded_id, candidate->metadata.model_digest)) {
    free(candidate);
    return status != CR_OK ? status : CR_CORRUPT;
  }
  cr_model *previous = *inout;
  *inout = candidate;
  cr_model_destroy(previous);
  return CR_OK;
}

cr_status CR_CALL cr_model_load_file(const char *path, cr_model **inout) {
  unsigned char *bytes;
  size_t length;
  long end;
  cr_status status;
  if (!path || !path[0] || !inout)
    return CR_INVALID;
  FILE *file = fopen(path, "rb");
  if (!file)
    return CR_IO;
  if (fseek(file, 0, SEEK_END) || (end = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET)) {
    fclose(file);
    return CR_IO;
  }
  if ((uint64_t)end > CR_MAX_BUNDLE_BYTES) {
    fclose(file);
    return CR_LIMIT;
  }
  length = (size_t)end;
  bytes = malloc(length ? length : 1);
  if (!bytes) {
    fclose(file);
    return CR_NOMEM;
  }
  if (fread(bytes, 1, length, file) != length || ferror(file)) {
    free(bytes);
    fclose(file);
    return CR_IO;
  }
  /* Detect concurrent growth instead of silently accepting an initial prefix.
   */
  const int extra = fgetc(file);
  const int read_error = ferror(file);
  const int close_error = fclose(file);
  status = extra != EOF || read_error || close_error
               ? CR_IO
               : cr_model_load_bytes(bytes, length, inout);
  free(bytes);
  return status;
}

cr_status CR_CALL cr_model_check_qualification_file(const cr_model *model,
                                                    const char *path) {
  if (!model || !path || !*path || !cr_metadata_valid(&model->metadata))
    return CR_INVALID;
  if (model->metadata.qualification != CR_QUALIFIED)
    return CR_DEFERRED;
  FILE *file = fopen(path, "rb");
  if (!file)
    return CR_IO;
  long end;
  if (fseek(file, 0, SEEK_END) || (end = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET)) {
    fclose(file);
    return CR_IO;
  }
  if ((uint64_t)end > CR_MAX_INPUT_BYTES) {
    fclose(file);
    return CR_LIMIT;
  }
  size_t length = (size_t)end;
  unsigned char *bytes = malloc(length ? length : 1);
  if (!bytes) {
    fclose(file);
    return CR_NOMEM;
  }
  int ok = fread(bytes, 1, length, file) == length && !ferror(file);
  if (fgetc(file) != EOF || ferror(file))
    ok = 0;
  if (fclose(file))
    ok = 0;
  if (!ok) {
    free(bytes);
    return CR_IO;
  }
  char digest[CR_DIGEST_HEX];
  cr_hash(bytes, length, digest);
  free(bytes);
  return strcmp(digest, model->metadata.qualification_digest) ? CR_CORRUPT
                                                              : CR_OK;
}

cr_status CR_CALL cr_model_save_file(const cr_model *model, const char *path) {
  const size_t length = cr_model_bundle_size(model);
  size_t written = 0;
  if (!path || !path[0] || !length)
    return CR_INVALID;
  unsigned char *bytes = malloc(length);
  if (!bytes)
    return CR_NOMEM;
  cr_status status = cr_model_write_bundle(model, bytes, length, &written);
  if (status != CR_OK) {
    free(bytes);
    return status;
  }
  /* Frozen assets are immutable. C11 exclusive-create preserves any existing
   * asset and avoids silently overwriting an incumbent or concurrent writer. */
  FILE *file = fopen(path, "wbx");
  if (!file) {
    free(bytes);
    return CR_IO;
  }
  const size_t saved = fwrite(bytes, 1, written, file);
  const int write_error = ferror(file);
  const int close_error = fclose(file);
  free(bytes);
  if (saved != written || write_error || close_error) {
    remove(path);
    return CR_IO;
  }
  return CR_OK;
}
