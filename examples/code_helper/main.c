#include "centroid_code_helper.h"
#include "centroid_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static int context_lifecycle(void) {
  static const char source_path[] = "src/editor_buffer.c";
  static const unsigned char original[] =
      "int editor_original_symbol(void) { return 1; }\n";
  static const unsigned char changed[] =
      "int editor_changed_symbol(void) { return 2; }\n";
  static const unsigned char private_source[] =
      "int independent_private_symbol(void) { return 3; }\n";
  cr_context_options limits;
  cr_context_options_init(&limits);
  limits.max_records = 4u;
  limits.max_record_bytes = 4096u;
  limits.max_total_bytes = 8192u;
  cr_context *editor = NULL, *independent = NULL;
  cr_query_options options;
  cr_query_options_init(&options);
  options.max_hits = 2u;
  options.byte_budget = 128u;
  options.excerpt_bytes = 96u;
  cr_evidence evidence[CR_CONTEXT_MAX_HITS];
  cr_query_report report;
  cr_record_view snapshot, history, private_snapshot;
  uint64_t original_id = 0u, changed_id = 0u, private_id = 0u;
  const char *stage = "create editor context";
  cr_status status = cr_context_create(&limits, &editor);
  if (status != CR_OK)
    goto done;
  stage = "create independent context";
  status = cr_context_create(&limits, &independent);
  if (status != CR_OK)
    goto done;

  puts("Dirty editor-buffer SOURCE sample: host-admitted unsaved bytes; "
       "no provider or filesystem writes.");
  stage = "admit original unsaved source";
  status = cr_context_admit(editor, CR_SOURCE, CR_TRAIN, source_path,
                            "host dirty editor buffer", original,
                            sizeof(original) - 1u, &original_id);
  if (status != CR_OK)
    goto done;
  status = cr_context_record(editor, 0u, &snapshot);
  if (status != CR_OK)
    goto done;
  if (snapshot.id != original_id || snapshot.version != 1u ||
      snapshot.length != sizeof(original) - 1u ||
      memcmp(snapshot.bytes, original, snapshot.length)) {
    status = CR_CORRUPT;
    goto done;
  }
  printf(
      "Borrowed snapshot: path=%s id=%llu version=%llu bytes=%zu sha256=%s\n",
      snapshot.path, (unsigned long long)snapshot.id,
      (unsigned long long)snapshot.version, snapshot.length, snapshot.digest);

  stage = "admit independent private source at the same path";
  status = cr_context_admit(independent, CR_SOURCE, CR_TRAIN, source_path,
                            "independent host editor buffer", private_source,
                            sizeof(private_source) - 1u, &private_id);
  if (status != CR_OK)
    goto done;
  status = cr_context_record(independent, 0u, &private_snapshot);
  if (status != CR_OK)
    goto done;

  stage = "admit changed unsaved source at the same path";
  status = cr_context_admit(editor, CR_SOURCE, CR_TRAIN, source_path,
                            "host dirty editor buffer", changed,
                            sizeof(changed) - 1u, &changed_id);
  if (status != CR_OK)
    goto done;
  status = cr_context_record(editor, 0u, &history);
  if (status != CR_OK)
    goto done;
  if (changed_id == original_id || history.current ||
      history.id != snapshot.id || history.version != 1u ||
      strcmp(history.digest, snapshot.digest) ||
      history.length != snapshot.length ||
      memcmp(snapshot.bytes, original, snapshot.length) ||
      memcmp(history.bytes, snapshot.bytes, snapshot.length)) {
    status = CR_CORRUPT;
    goto done;
  }
  printf("Historical bytes preserved: id=%llu version=%llu current=%u "
         "borrowed views remain valid until context destruction.\n",
         (unsigned long long)history.id, (unsigned long long)history.version,
         (unsigned)history.current);

  stage = "old editor symbol must abstain after refresh";
  status =
      cr_context_query(editor, (const unsigned char *)"editor_original_symbol",
                       sizeof("editor_original_symbol") - 1u, &options,
                       evidence, CR_CONTEXT_MAX_HITS, &report);
  if (status != CR_NOT_FOUND || !report.abstained || report.returned) {
    status = CR_CORRUPT;
    goto done;
  }
  puts("ABSTAIN: old editor symbol after same-path refresh.");
  stage = "new editor symbol must return version 2";
  status =
      cr_context_query(editor, (const unsigned char *)"editor_changed_symbol",
                       sizeof("editor_changed_symbol") - 1u, &options, evidence,
                       CR_CONTEXT_MAX_HITS, &report);
  if (status != CR_OK)
    goto done;
  if (report.returned != 1u || evidence[0].record.id != changed_id ||
      evidence[0].record.version != 2u ||
      strcmp(evidence[0].record.path, source_path) ||
      evidence[0].record.length != sizeof(changed) - 1u ||
      memcmp(evidence[0].record.bytes, changed, sizeof(changed) - 1u) ||
      report.excerpt_bytes > options.byte_budget) {
    status = CR_CORRUPT;
    goto done;
  }
  printf("Refreshed SOURCE: path=%s id=%llu version=%llu bytes=%zu "
         "attribution=%s excerpt_bytes=%llu omitted=%llu\n",
         evidence[0].record.path, (unsigned long long)evidence[0].record.id,
         (unsigned long long)evidence[0].record.version,
         evidence[0].record.length, evidence[0].record.attribution,
         (unsigned long long)report.excerpt_bytes,
         (unsigned long long)report.omitted);

  stage = "private symbol must be absent from editor context";
  status = cr_context_query(editor,
                            (const unsigned char *)"independent_private_symbol",
                            sizeof("independent_private_symbol") - 1u, &options,
                            evidence, CR_CONTEXT_MAX_HITS, &report);
  if (status != CR_NOT_FOUND || !report.abstained || report.returned) {
    status = CR_CORRUPT;
    goto done;
  }
  stage = "editor symbol must be absent from independent context";
  status = cr_context_query(independent,
                            (const unsigned char *)"editor_changed_symbol",
                            sizeof("editor_changed_symbol") - 1u, &options,
                            evidence, CR_CONTEXT_MAX_HITS, &report);
  if (status != CR_NOT_FOUND || !report.abstained || report.returned) {
    status = CR_CORRUPT;
    goto done;
  }
  stage = "private source must remain owned by independent context";
  status = cr_context_query(independent,
                            (const unsigned char *)"independent_private_symbol",
                            sizeof("independent_private_symbol") - 1u, &options,
                            evidence, CR_CONTEXT_MAX_HITS, &report);
  if (status != CR_OK)
    goto done;
  if (report.returned != 1u || evidence[0].record.id != private_id ||
      evidence[0].record.version != 1u ||
      evidence[0].record.length != sizeof(private_source) - 1u ||
      memcmp(evidence[0].record.bytes, private_source,
             sizeof(private_source) - 1u) ||
      memcmp(private_snapshot.bytes, private_source,
             sizeof(private_source) - 1u) ||
      cr_context_count(editor) != 2u || cr_context_count(independent) != 1u) {
    status = CR_CORRUPT;
    goto done;
  }
  printf("Independent context: same path=%s version=%llu owns private bytes; "
         "both cross-context queries abstain.\n",
         evidence[0].record.path,
         (unsigned long long)evidence[0].record.version);
  puts("Context lifecycle passes: refresh, complete history and independent "
       "ownership (API demonstration; no quality claim).");
done:
  if (status != CR_OK)
    fprintf(stderr, "context-lifecycle: %s: %s\n", stage,
            cr_status_string(status));
  cr_context_destroy(independent);
  cr_context_destroy(editor);
  return status == CR_OK ? 0 : 1;
}

int main(int argc, char **argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (argc == 2 && !strcmp(argv[1], "--context-lifecycle"))
    return context_lifecycle();
  if (argc < 3 || argc > 4) {
    fprintf(stderr,
            "usage: %s SOURCE_ROOT QUERY [MODEL_BUNDLE]\n"
            "       %s --context-lifecycle\n",
            argv[0], argv[0]);
    return 2;
  }
  cr_context_options limits;
  cr_context_options_init(&limits);
  cr_context *context = NULL;
  cr_model *model = NULL;
  cr_status status = cr_context_create(&limits, &context);
  if (status != CR_OK) {
    fprintf(stderr, "%s\n", cr_status_string(status));
    return 1;
  }
  cr_scan_report scan;
  status = cr_context_scan(context, argv[1], &scan);
  if (status != CR_OK) {
    fprintf(stderr, "scan: %s; admitted=%llu failed=%llu\n",
            cr_status_string(status), (unsigned long long)scan.admitted,
            (unsigned long long)scan.failed);
    cr_context_destroy(context);
    return 1;
  }
  fprintf(stderr, "records=%llu excluded=%llu bytes=%llu profile=%s\n",
          (unsigned long long)scan.admitted, (unsigned long long)scan.excluded,
          (unsigned long long)scan.bytes, CR_CONTEXT_PROFILE);
  if (argc == 4) {
    status = cr_model_load_file(argv[3], &model);
    if (status != CR_OK) {
      fprintf(stderr, "model: %s\n", cr_status_string(status));
      cr_context_destroy(context);
      return 1;
    }
    cr_model_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.api_version = CR_API_VERSION;
    status = cr_model_info_get(model, &info);
    if (status == CR_OK) {
      const char *reference = info.metadata.qualification_reference;
      const char *slash = strrchr(argv[3], '/');
      const char *backslash = strrchr(argv[3], '\\');
      if (backslash && (!slash || backslash > slash))
        slash = backslash;
      size_t parent_length = slash ? (size_t)(slash - argv[3]) + 1 : 0;
      char sidecar[CR_CONTEXT_MAX_PATH + 1];
      if (!*reference || strchr(reference, '/') || strchr(reference, '\\') ||
          strchr(reference, ':') || strstr(reference, ".."))
        status = CR_UNSUPPORTED;
      else if (parent_length > CR_CONTEXT_MAX_PATH ||
               parent_length + strlen(reference) > CR_CONTEXT_MAX_PATH)
        status = CR_LIMIT;
      else {
        memcpy(sidecar, argv[3], parent_length);
        memcpy(sidecar + parent_length, reference, strlen(reference) + 1);
        status = cr_model_check_qualification_file(model, sidecar);
      }
    }
    if (status != CR_OK) {
      fprintf(stderr, "qualification reference: %s\n",
              cr_status_string(status));
      cr_model_destroy(model);
      cr_context_destroy(context);
      return 1;
    }
    if (status == CR_OK)
      fprintf(stderr, "model=%s qualification=%s provenance=%s\n",
              info.metadata.model_digest,
              info.metadata.qualification == CR_QUALIFIED
                  ? "qualified-reference"
                  : "experimental",
              info.metadata.provenance);
    int32_t values[2048];
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
      values[i] = (int32_t)(i * 3);
    cr_code_recommendation choice;
    memset(&choice, 0, sizeof(choice));
    choice.struct_size = sizeof(choice);
    choice.api_version = CR_API_VERSION;
    status = cr_code_recommend(model, values, 2048, 17, 15, &choice);
    size_t index = 0;
    uint64_t comparisons = 0;
    if (status == CR_OK)
      status = cr_code_execute(choice.action, values, 2048, 17, &index,
                               &comparisons);
    if (status != CR_OK) {
      fprintf(stderr, "code profile: %s\n", cr_status_string(status));
      cr_model_destroy(model);
      cr_context_destroy(context);
      return 1;
    }
    fprintf(stderr,
            "native strategy demo: action=%u index=%zu comparisons=%llu "
            "profile=%s (selection overhead measured separately)\n",
            (unsigned)choice.action, index, (unsigned long long)comparisons,
            CR_CODE_HELPER_PROFILE);
  }
  cr_query_options options;
  cr_query_options_init(&options);
  cr_evidence evidence[CR_CONTEXT_MAX_HITS];
  cr_query_report report;
  status =
      cr_context_query(context, (const unsigned char *)argv[2], strlen(argv[2]),
                       &options, evidence, CR_CONTEXT_MAX_HITS, &report);
  if (status == CR_NOT_FOUND)
    puts("ABSTAIN: no supported source evidence");
  else if (status == CR_OK) {
    size_t required = 0;
    status = cr_evidence_format(evidence, (size_t)report.returned, NULL, 0,
                                &required);
    unsigned char *formatted = status == CR_LIMIT ? malloc(required) : NULL;
    if (!formatted)
      status = CR_NOMEM;
    else {
      status = cr_evidence_format(evidence, (size_t)report.returned, formatted,
                                  required, &required);
      if (status == CR_OK &&
          fwrite(formatted, 1, required - 1, stdout) != required - 1)
        status = CR_IO;
      free(formatted);
    }
    fprintf(
        stderr, "returned=%llu matched=%llu omitted=%llu excerpt_bytes=%llu\n",
        (unsigned long long)report.returned, (unsigned long long)report.matched,
        (unsigned long long)report.omitted,
        (unsigned long long)report.excerpt_bytes);
  }
  if (status != CR_OK && status != CR_NOT_FOUND)
    fprintf(stderr, "%s\n", cr_status_string(status));
  cr_model_destroy(model);
  cr_context_destroy(context);
  return status == CR_OK || status == CR_NOT_FOUND ? 0 : 1;
}
