/** Exact command provenance and a bounded attributed activity view. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "context_internal.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#define WORK_PATH 4097u
#define WORK_VIEW 65536u

#ifdef _WIN32
static int ordinary_basename(const char *name) {
  size_t length = strlen(name), stem = 0;
  char device[16];
  if (!length || name[length - 1] == '.' || name[length - 1] == ' ' ||
      strpbrk(name, "<>:\"|?*") != NULL)
    return 0;
  for (size_t i = 0; i < length; ++i)
    if ((unsigned char)name[i] < 32)
      return 0;
  while (name[stem] && name[stem] != '.')
    ++stem;
  while (stem && name[stem - 1] == ' ')
    --stem;
  if (stem >= sizeof(device))
    return 1;
  for (size_t i = 0; i < stem; ++i)
    device[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - ('a' - 'A'))
                                                 : name[i];
  device[stem] = '\0';
  return strcmp(device, "CON") && strcmp(device, "PRN") &&
         strcmp(device, "AUX") && strcmp(device, "NUL") &&
         strcmp(device, "CONIN$") && strcmp(device, "CONOUT$") &&
         strcmp(device, "CLOCK$") &&
         !(stem == 4 &&
           (!memcmp(device, "COM", 3) || !memcmp(device, "LPT", 3)) &&
           ((device[3] >= '1' && device[3] <= '9') ||
            (unsigned char)device[3] == 0xb9 ||
            (unsigned char)device[3] == 0xb2 ||
            (unsigned char)device[3] == 0xb3));
}
#endif

static c_status working_directory(const c_process_options *options,
                                  char out[WORK_PATH]) {
  c_status status = c_context_absolute(options->cwd ? options->cwd : ".", out);
  if (status == C_OK)
    status = c_context_no_links(out, 1);
  if (status == C_OK && c_context_reserved_training_path(out))
    status = C_INVALID;
  return status;
}

/* Resolve a new output through its existing regular parent before any child is
 * launched or artifact is created. The final file need not exist; exclusive
 * creation in the process layer still protects existing files and final links.
 */
static c_status output_path(const char *path, char out[WORK_PATH]) {
  char lexical[WORK_PATH], parent[WORK_PATH], absolute[WORK_PATH];
  size_t length = strlen(path);
  if (!length || length >= sizeof(lexical))
    return C_INVALID;
  memcpy(lexical, path, length + 1);
#ifdef _WIN32
  for (size_t i = 0; i < length; ++i)
    if (lexical[i] == '\\')
      lexical[i] = '/';
  if (!strncmp(lexical, "//?/", 4) || !strncmp(lexical, "//./", 4))
    return C_INVALID;
#endif
  const char *name = strrchr(lexical, '/');
  name = name ? name + 1 : lexical;
  if (!*name || !strcmp(name, ".") || !strcmp(name, ".."))
    return C_INVALID;
#ifdef _WIN32
  if (!ordinary_basename(name))
    return C_INVALID;
  DWORD full_length = GetFullPathNameA(lexical, WORK_PATH, absolute, NULL);
  if (!full_length)
    return C_IO;
  if (full_length >= WORK_PATH)
    return C_LIMIT;
  for (size_t i = 0; i < full_length; ++i)
    if (absolute[i] == '\\')
      absolute[i] = '/';
  memcpy(lexical, absolute, (size_t)full_length + 1);
  name = strrchr(lexical, '/');
  if (!name)
    return C_INVALID;
  ++name;
#endif
  size_t parent_length = (size_t)(name - lexical);
  if (!parent_length)
    memcpy(parent, ".", 2);
  else {
    memcpy(parent, lexical, parent_length);
    parent[parent_length] = '\0';
  }
  c_status status = c_context_absolute(parent, absolute);
  if (status == C_OK)
    status = c_context_no_links(absolute, 1);
  if (status != C_OK)
    return status;
  int size = snprintf(out, WORK_PATH, "%s%s%s", absolute,
                      absolute[strlen(absolute) - 1] == '/' ? "" : "/", name);
  if (size < 0 || (size_t)size >= WORK_PATH)
    return C_LIMIT;
  return c_context_reserved_training_path(out) ? C_INVALID : C_OK;
}

static void escaped(c_writer *writer, const unsigned char *bytes,
                    size_t length) {
  for (size_t i = 0; i < length; ++i) {
    unsigned char value = bytes[i];
    if (value < 32 || value >= 127 || value == '\\') {
      char escape[5];
      int n = snprintf(escape, sizeof(escape), "\\x%02x", (unsigned)value);
      if (n != 4) {
        writer->status = C_IO;
        return;
      }
      c_put_bytes(writer, escape, 4);
    } else
      c_put_bytes(writer, &value, 1);
  }
}

static void text_field(c_writer *writer, const char *name, const char *text) {
  char header[192], digest[C_DIGEST_HEX];
  size_t length = strlen(text);
  c_hash(text, length, digest);
  int n = snprintf(header, sizeof(header), "%s-bytes=%zu sha256=%s\n", name,
                   length, digest);
  if (n < 0 || (size_t)n >= sizeof(header)) {
    writer->status = C_LIMIT;
    return;
  }
  c_put_bytes(writer, header, (size_t)n);
  escaped(writer, (const unsigned char *)text, length);
  c_put_bytes(writer, "\n", 1);
}

static c_status receipt(const c_process_options *options, const char *cwd,
                        const char *raw_path, c_status process_status,
                        const c_process_result *result,
                        const unsigned char *raw, size_t raw_length,
                        c_writer *writer) {
  static const char header[] = "centroid-native-work/1\nAttributed activity; "
                               "never a verified utility target.\n";
  c_put_bytes(writer, header, sizeof(header) - 1);
  text_field(writer, "program", options->program);
  text_field(writer, "working-directory", cwd);
  text_field(writer, "raw-output-path", raw_path);
  for (size_t i = 0; options->argv[i]; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "argv-%zu", i);
    text_field(writer, name, options->argv[i]);
  }
  size_t view = result->length < WORK_VIEW ? result->length : WORK_VIEW;
  char raw_digest[C_DIGEST_HEX], view_digest[C_DIGEST_HEX], summary[768];
  c_hash(raw, raw_length, raw_digest);
  c_hash(result->output, view, view_digest);
  int complete =
      process_status != C_LIMIT && raw_length == result->observed_bytes;
  int n = snprintf(
      summary, sizeof(summary),
      "process-status=%u\nexit-code=%d\ntimed-out=%d\ntimeout-ms=%u\nelapsed-"
      "ms=%llu\n"
      "observed-output-bytes=%zu\nraw-output-bytes=%zu\nraw-output-complete=%"
      "d\nraw-output-sha256=%s\n"
      "view-output-bytes=%zu\nview-omitted-bytes=%zu\nview-output-sha256=%s\n"
      "output-prefix-escaped:\n",
      (unsigned)process_status, result->exit_code, result->timed_out,
      options->timeout_ms, (unsigned long long)result->elapsed_ms,
      result->observed_bytes, raw_length, complete, raw_digest, view,
      result->observed_bytes >= view ? result->observed_bytes - view : 0,
      view_digest);
  if (n < 0 || (size_t)n >= sizeof(summary))
    return C_LIMIT;
  c_put_bytes(writer, summary, (size_t)n);
  escaped(writer, result->output, view);
  c_put_bytes(writer, "\n", 1);
  return writer->status;
}

c_status c_context_capture_run(c_context *context,
                               const c_process_options *options,
                               const char *attribution,
                               const char *raw_output_path, uint64_t *id,
                               c_process_result *result) {
  if (!context || !options || !attribution || !*attribution ||
      strlen(attribution) > C_CONTEXT_PROVENANCE_BYTES || !raw_output_path ||
      !*raw_output_path || strlen(raw_output_path) >= WORK_PATH || !id ||
      !result)
    return C_INVALID;
  char cwd[WORK_PATH], canonical[WORK_PATH];
  c_status status = working_directory(options, cwd);
  if (status == C_OK)
    status = output_path(raw_output_path, canonical);
  if (status != C_OK)
    return status;
  c_process_result local = {0};
  c_process_options launch = *options;
  launch.cwd = cwd;
  c_status executed = c_process_capture_file(&launch, canonical, &local);
  if (!local.output)
    return executed;
  unsigned char *raw = NULL;
  size_t raw_length = 0;
  status = c_context_no_links(canonical, 0);
  if (status == C_OK)
    status = c_read_file(canonical, &raw, &raw_length);
  c_writer writer = {0};
  if (status == C_OK)
    status = receipt(options, cwd, canonical, executed, &local, raw, raw_length,
                     &writer);
  if (status == C_OK)
    status = c_context_admit(context, C_ACTIVITY, C_TRAIN, canonical,
                             attribution, writer.data, writer.length, id);
  free(raw);
  free(writer.data);
  *result = local;
  return status != C_OK ? status : executed;
}
