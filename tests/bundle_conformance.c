#include "centroid_context.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CASES 15u

static cr_status input_case(unsigned number, cr_context *context,
                            uint64_t request, uint64_t empty_request,
                            const uint64_t evidence[3], unsigned char *raw,
                            unsigned char *framed, const unsigned char **bytes,
                            size_t *length) {
  if (number < 8) {
    raw[0] = 1;
    raw[1] = (unsigned char)number;
    *bytes = raw;
    *length = 2;
    return CR_OK;
  }
  if (number == 8) {
    *bytes = NULL;
    *length = 0;
    return CR_OK;
  }
  /* Reset raw bytes because the first eight cases used its first two entries.
   */
  for (size_t i = 0; i <= CR_MAX_INPUT_BYTES; ++i)
    raw[i] = (unsigned char)i;
  if (number == 9 || number == 10) {
    *bytes = raw;
    *length = number == 9 ? 256 : CR_MAX_INPUT_BYTES;
    return CR_OK;
  }
  cr_status status;
  size_t needed = 0;
  if (number == 11 || number == 12)
    status = cr_text_frame_context(context, request, evidence, 3, raw,
                                   number == 11 ? 0 : 256, framed,
                                   CR_TEXT_MAX_FRAME_BYTES, &needed);
  else if (number == 13)
    status = cr_text_frame_context(context, empty_request, NULL, 0, NULL, 0,
                                   framed, CR_TEXT_MAX_FRAME_BYTES, &needed);
  else {
    status = cr_text_frame_context(context, empty_request, NULL, 0, NULL, 0,
                                   NULL, 0, &needed);
    if (status != CR_LIMIT)
      return CR_INVALID;
    status = cr_text_frame_context(context, empty_request, NULL, 0, raw,
                                   CR_TEXT_MAX_FRAME_BYTES - needed, framed,
                                   CR_TEXT_MAX_FRAME_BYTES, &needed);
    if (status == CR_OK && needed != CR_TEXT_MAX_FRAME_BYTES)
      return CR_INVALID;
  }
  *bytes = framed;
  *length = needed;
  return status;
}

static int write_vectors(const char *bundle, const char *path) {
  cr_model *model = NULL;
  cr_context *context = NULL, *identities = NULL;
  cr_context_options options;
  unsigned char *raw = NULL, *framed = NULL;
  FILE *file = NULL;
  int ok = 0;
  cr_model_info info = {0};
  info.struct_size = sizeof(info);
  info.api_version = CR_API_VERSION;
  if (cr_model_load_file(bundle, &model) != CR_OK ||
      cr_model_info_get(model, &info) != CR_OK)
    goto done;
  cr_context_options_init(&options);
  if (cr_context_create(&options, &context) != CR_OK ||
      cr_context_create(&options, &identities) != CR_OK)
    goto done;
  raw = malloc(CR_MAX_INPUT_BYTES + 1u);
  framed = malloc(CR_TEXT_MAX_FRAME_BYTES);
  if (!raw || !framed)
    goto done;
  for (size_t i = 0; i <= CR_MAX_INPUT_BYTES; ++i)
    raw[i] = (unsigned char)i;
  uint64_t request, empty_request, evidence[3];
  static const unsigned char question[] = {'q', 0, 255, '?'};
  static const unsigned char proposal[] = {'p', 128, 0, 254};
  if (cr_context_admit(context, CR_ACTIVITY, CR_TRAIN, "request", "host",
                       question, sizeof(question), &request) != CR_OK ||
      cr_context_admit(context, CR_SOURCE, CR_TRAIN, "source.c", "editor", raw,
                       256, evidence) != CR_OK ||
      cr_context_admit(context, CR_LLM_PROPOSAL, CR_TRAIN, "proposal",
                       "LLM input", proposal, sizeof(proposal),
                       evidence + 1) != CR_OK ||
      cr_context_admit(context, CR_ACTIVITY, CR_TRAIN, "work", "host", NULL, 0,
                       evidence + 2) != CR_OK ||
      cr_context_admit(context, CR_ACTIVITY, CR_TRAIN, "empty", "host", NULL, 0,
                       &empty_request) != CR_OK)
    goto done;
  double untouched[CR_FEATURES], failed[CR_FEATURES];
  for (unsigned i = 0; i < CR_FEATURES; ++i)
    untouched[i] = failed[i] = -17.0;
  if (cr_encode(raw, CR_MAX_INPUT_BYTES + 1u, failed) != CR_LIMIT ||
      memcmp(untouched, failed, sizeof(failed)))
    goto done;
  file = fopen(path, "wbx");
  if (!file)
    goto done;
  ok = fprintf(file, "CENTROID-VECTORS-2 %s %u %u\n",
               info.metadata.model_digest, CASES, info.groups) > 0;
  for (unsigned number = 0; number < CASES && ok; ++number) {
    const unsigned char *bytes = NULL;
    size_t length = 0;
    double features[CR_FEATURES];
    if (input_case(number, context, request, empty_request, evidence, raw,
                   framed, &bytes, &length) != CR_OK ||
        cr_encode(bytes, length, features) != CR_OK) {
      ok = 0;
      break;
    }
    char input_path[32];
    uint64_t id;
    cr_record_view input;
    if (snprintf(input_path, sizeof(input_path), "case-%u", number) < 0 ||
        cr_context_admit(identities, CR_ACTIVITY, CR_TRAIN, input_path,
                         "cross-toolchain conformance input", bytes, length,
                         &id) != CR_OK ||
        cr_context_record(identities, number, &input) != CR_OK ||
        input.id != id) {
      ok = 0;
      break;
    }
    for (unsigned head = 0; head < 2 && ok; ++head)
      for (unsigned mask = 1; mask < (1u << info.groups) && ok; ++mask) {
        double probabilities[CR_TEXT_ACTIONS],
            scratch[CR_PREDICT_SCRATCH_DOUBLES],
            mass[CR_MAX_GROUPS] = {1, 2, 3, 4};
        unsigned actions = head == CR_CODE ? CR_CODE_ACTIONS : CR_TEXT_ACTIONS,
                 choice = 0;
        if (cr_model_predict_with_scratch(model, head, features, mask, mass,
                                          scratch, CR_PREDICT_SCRATCH_DOUBLES,
                                          probabilities,
                                          CR_TEXT_ACTIONS) != CR_OK) {
          ok = 0;
          break;
        }
        for (unsigned i = 1; i < actions; ++i)
          if (probabilities[i] > probabilities[choice])
            choice = i;
        ok = fprintf(file, "%u %u %u %u %s", head, number, mask, choice,
                     input.digest) > 0;
        for (unsigned i = 0; i < CR_FEATURES && ok; ++i)
          ok = fprintf(file, " %.17g", features[i]) > 0;
        for (unsigned i = 0; i < actions && ok; ++i)
          ok = fprintf(file, " %.17g", probabilities[i]) > 0;
        if (ok)
          ok = fputc('\n', file) != EOF;
      }
  }
done:
  if (file && fclose(file))
    ok = 0;
  free(raw);
  free(framed);
  cr_context_destroy(context);
  cr_context_destroy(identities);
  cr_model_destroy(model);
  return ok ? 0 : 1;
}
static int compare_vectors(const char *first, const char *second) {
  FILE *a = fopen(first, "rb"), *b = fopen(second, "rb");
  if (!a || !b) {
    if (a)
      fclose(a);
    if (b)
      fclose(b);
    return 1;
  }
  char format_a[32], format_b[32], da[65], db[65];
  unsigned cases_a = 0, cases_b = 0, groups_a = 0, groups_b = 0;
  unsigned vectors = 0;
  double maximum = 0;
  int ok = 1;
  if (fscanf(a, "%31s %64s %u %u", format_a, da, &cases_a, &groups_a) != 4 ||
      fscanf(b, "%31s %64s %u %u", format_b, db, &cases_b, &groups_b) != 4 ||
      strcmp(format_a, "CENTROID-VECTORS-2") || strcmp(format_a, format_b) ||
      strcmp(da, db) || cases_a != CASES || cases_a != cases_b ||
      groups_a != groups_b || !groups_a || groups_a > CR_MAX_GROUPS)
    ok = 0;
  while (ok) {
    unsigned ha, ba, ma, ca, hb, bb, mb, cb;
    int na = fscanf(a, "%u %u %u %u", &ha, &ba, &ma, &ca);
    int nb = fscanf(b, "%u %u %u %u", &hb, &bb, &mb, &cb);
    if (na == EOF && nb == EOF)
      break;
    if (na != 4 || nb != 4 || ha > 1 || ha != hb || ba != bb || ma != mb ||
        ca != cb) {
      ok = 0;
      break;
    }
    unsigned actions = ha == CR_CODE ? CR_CODE_ACTIONS : CR_TEXT_ACTIONS;
    const unsigned masks = (1u << groups_a) - 1u;
    if (ba != vectors / (2u * masks) ||
        ha != (vectors % (2u * masks)) / masks || ma != vectors % masks + 1u) {
      ok = 0;
      break;
    }
    if (ca >= actions) {
      ok = 0;
      break;
    }
    if (fscanf(a, "%64s", da) != 1 || fscanf(b, "%64s", db) != 1 ||
        strcmp(da, db)) {
      ok = 0;
      break;
    }
    for (unsigned i = 0; i < CR_FEATURES + actions; ++i) {
      double pa, pb;
      if (fscanf(a, "%lf", &pa) != 1 || fscanf(b, "%lf", &pb) != 1 ||
          !isfinite(pa) || !isfinite(pb) ||
          (i >= CR_FEATURES && (pa < 0 || pb < 0))) {
        ok = 0;
        break;
      }
      double delta = fabs(pa - pb);
      if (delta > maximum)
        maximum = delta;
      if (delta > 1e-12 + fabs(pa) * 1e-10) {
        ok = 0;
        break;
      }
    }
    ++vectors;
  }
  if (ferror(a) || ferror(b) || !ok ||
      vectors != CASES * 2u * ((1u << groups_a) - 1u))
    ok = 0;
  fclose(a);
  fclose(b);
  printf("vectors=%u max_absolute_difference=%.17g decisions_equal=%u\n",
         vectors, maximum, (unsigned)ok);
  return ok ? 0 : 1;
}
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  if (!strcmp(argv[1], "write"))
    return write_vectors(argv[2], argv[3]);
  if (!strcmp(argv[1], "compare"))
    return compare_vectors(argv[2], argv[3]);
  return 2;
}
