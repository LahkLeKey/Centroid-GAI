#include "../src/internal.h"
#include "centroid.h"
#include "centroid_algorithms.h"
#include "centroid_extensions.h"
#include "centroid_session.h"
#include "centroid_source.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static void usage(void) {
  puts("centroid new STATE [GROUPS SEED]\n"
       "centroid scan STATE ROOT\n"
       "centroid admit STATE source|llm|activity|dev|audit FILE ATTRIBUTION\n"
       "centroid query STATE QUERY\n"
       "centroid record STATE RECORD_ID\n"
       "centroid train STATE EPOCHS [GENERATIONS_PER_EPOCH]\n"
       "centroid next STATE SOURCE_ID BYTE_OFFSET\n"
       "centroid step STATE GENERATIONS\n"
       "centroid replay-retired STATE FIRST MAX ELIGIBILITY_MASK\n"
       "centroid report STATE\n"
       "centroid trial STATE NEW_OUTPUT_DIRECTORY COMPILER\n"
       "centroid run STATE NEW_RAW_LOG TIMEOUT_MS PROGRAM [ARGUMENTS...]\n"
       "centroid benchmark NEW_OUTPUT_DIRECTORY\n"
       "centroid research NEW_OUTPUT_DIRECTORY\n"
       "centroid extensions NEW_OUTPUT_DIRECTORY\n"
       "centroid suite STATE PROJECT_ROOT NEW_OUTPUT_DIRECTORY COMPILER\n"
       "centroid apply ACCEPTED_DIRECTORY PROJECT_ROOT PARENT_SHA256\n"
       "centroid session-new STATE SESSION\n"
       "centroid session-ask SESSION REQUEST_FILE\n"
       "centroid session-generate SESSION REQUEST_FILE MAX_NEW_BYTES\n"
       "centroid session-history SESSION\n"
       "centroid chat SESSION");
}
static int number(const char *text, uint64_t maximum, uint64_t *out) {
  char *end;
  unsigned long long value;
  if (!text || !*text || *text == '-' || *text == '+')
    return 0;
  errno = 0;
  value = strtoull(text, &end, 10);
  if (errno || *end || value > maximum)
    return 0;
  *out = (uint64_t)value;
  return 1;
}
static unsigned mask(const c_trainer *t) {
  return (1u << c_model_group_count(c_trainer_model(t))) - 1u;
}
static void print_report(const c_training_report *r) {
  unsigned g;
  printf("generation=%" PRIu64 " contact_generations=%" PRIu64
         " updates=%" PRIu64 " pending=%" PRIu64 " deferrals=%" PRIu64
         " reseeds=%" PRIu64 " last_loss=%.9g\n",
         r->generation, r->contacts, r->updates, r->queued, r->deferred,
         r->reseeds, r->last_loss);
  for (g = 0; g < C_MAX_GROUPS; g++)
    printf("owner[%u].clock=%" PRIu64 "%s", g, r->group_updates[g],
           g + 1 == C_MAX_GROUPS ? "\n" : " ");
}
static c_status admit(c_trainer *t, int argc, char **argv) {
  c_record_kind kind;
  c_split split = C_TRAIN;
  uint64_t id;
  c_status s;
  if (argc != 6)
    return C_INVALID;
  if (!strcmp(argv[3], "source"))
    kind = C_SOURCE;
  else if (!strcmp(argv[3], "llm"))
    kind = C_LLM_PROPOSAL;
  else if (!strcmp(argv[3], "activity"))
    kind = C_ACTIVITY;
  else if (!strcmp(argv[3], "dev")) {
    kind = C_DEVELOPMENT;
    split = C_DEV;
  } else if (!strcmp(argv[3], "audit")) {
    kind = C_AUDIT;
    split = C_HOLDOUT;
  } else
    return C_INVALID;
  s = c_context_admit_file(c_trainer_context(t), kind, split, argv[4], argv[5],
                           &id);
  if (s == C_OK) {
    s = c_trainer_save(t, argv[2]);
    if (s == C_OK)
      printf("record=%" PRIu64 "\n", id);
  }
  return s;
}
static c_status query(c_trainer *t, const char *text) {
  c_record record;
  double score;
  c_status s =
      c_context_retrieve(c_trainer_context(t), (const unsigned char *)text,
                         strlen(text), &record, &score);
  if (s == C_NOT_FOUND) {
    puts("abstain: no eligible current evidence supports this query");
    return C_OK;
  }
  if (s != C_OK)
    return s;
  printf("path=%s record=%" PRIu64 " version=%" PRIu64
         " byte_span=[0,%zu) sha256=%s lexical_score=%.6g\n"
         "attribution=%s kind=%u\n",
         record.path, record.id, record.version, record.length, record.digest,
         score, record.attribution, (unsigned)record.kind);
  if (record.length &&
      fwrite(record.bytes, 1, record.length, stdout) != record.length)
    return C_IO;
  return C_OK;
}
static c_status record_by_id(c_trainer *t, uint64_t id) {
  size_t i;
  c_record record;
  for (i = 0; i < c_context_count(c_trainer_context(t)); i++) {
    c_status s = c_context_record(c_trainer_context(t), i, &record);
    if (s != C_OK)
      return s;
    if (record.id != id)
      continue;
    printf("record=%" PRIu64 " version=%" PRIu64
           " current=%d kind=%u split=%u path=%s sha256=%s byte_span=[0,%zu)\n"
           "attribution=%s\n",
           record.id, record.version, record.current, (unsigned)record.kind,
           (unsigned)record.split, record.path, record.digest, record.length,
           record.attribution);
    return !record.length || fwrite(record.bytes, 1, record.length, stdout) ==
                                 record.length
               ? C_OK
               : C_IO;
  }
  return C_NOT_FOUND;
}
static c_status next_byte(c_trainer *t, uint64_t id, size_t offset) {
  c_record record = {0};
  int found = 0;
  size_t i, best = 0;
  double input[C_FEATURES], p[C_TEXT_ACTIONS],
      mass[C_MAX_GROUPS] = {1, 1, 1, 1};
  c_status s;
  for (i = 0; i < c_context_count(c_trainer_context(t)); i++) {
    s = c_context_record(c_trainer_context(t), i, &record);
    if (s != C_OK)
      return s;
    if (record.id == id) {
      found = 1;
      break;
    }
  }
  if (!found)
    return C_NOT_FOUND;
  if (record.kind != C_SOURCE || record.split != C_TRAIN ||
      offset > record.length)
    return C_INVALID;
  s = c_encode(record.bytes, offset, input);
  if (s != C_OK)
    return s;
  s = c_model_predict(c_trainer_model(t), C_TEXT, input, mask(t), mass, p,
                      C_TEXT_ACTIONS);
  if (s != C_OK)
    return s;
  for (i = 1; i < C_TEXT_ACTIONS; i++)
    if (p[i] > p[best])
      best = i;
  if (best == C_EOS)
    printf("prediction=EOS probability=%.9g\n", p[best]);
  else
    printf("prediction=byte:%zu hex:%02zx probability=%.9g\n", best, best,
           p[best]);
  return C_OK;
}
static c_status capture(c_trainer *t, int argc, char **argv, int *admitted) {
  c_process_options options;
  c_process_result result = {0};
  uint64_t timeout, id = 0;
  c_status s;
  *admitted = 0;
  if (argc < 6 || !number(argv[4], 3600000, &timeout) || !timeout)
    return C_INVALID;
  options.program = argv[5];
  options.argv = (const char *const *)(argv + 5);
  options.cwd = NULL;
  options.timeout_ms = (unsigned)timeout;
  options.output_limit = 512u * 1024u;
  s = c_context_capture_run(c_trainer_context(t), &options,
                            "centroid CLI explicit capture", argv[3], &id,
                            &result);
  if (result.length)
    fwrite(result.output, 1, result.length, stdout);
  *admitted = id != 0;
  if (*admitted)
    printf("\nactivity=%" PRIu64 " exit=%d timeout=%d omitted=%zu "
           "capture_status=%s\n",
           id, result.exit_code, result.timed_out,
           result.observed_bytes - result.length, c_status_string(s));
  c_process_dispose(&result);
  return s;
}
static c_status print_answer(const c_session_answer *answer) {
  if (answer->kind == C_SESSION_SUPPORTED)
    printf("supported path=%s record=%" PRIu64 " version=%" PRIu64
           " span=[%zu,%zu) sha256=%s\nattribution=%s\n",
           answer->evidence.path, answer->evidence.id, answer->evidence.version,
           answer->span_begin, answer->span_begin + answer->span_length,
           answer->evidence.digest, answer->evidence.attribution);
  else if (answer->kind == C_SESSION_GENERATED)
    printf("unverified generation eos=%d byte_limit=%d calls_budget=%zu\n",
           answer->eos, answer->byte_limit_reached, answer->generation_budget);
  if (answer->length &&
      fwrite(answer->bytes, 1, answer->length, stdout) != answer->length)
    return C_IO;
  return putchar('\n') == EOF ? C_IO : C_OK;
}
static c_status session_command(int argc, char **argv) {
  c_session *session = NULL;
  c_status s = c_session_load(argv[2], &session);
  if (s != C_OK)
    return s;
  if (!strcmp(argv[1], "session-history") && argc == 3) {
    for (size_t i = 0; s == C_OK && i < c_session_count(session); i++) {
      c_session_turn turn;
      s = c_session_history(session, i, &turn);
      if (s != C_OK)
        break;
      printf("turn=%" PRIu64 " user_bytes=%zu\n", turn.id, turn.user_length);
      if (turn.user_length && fwrite(turn.user_bytes, 1, turn.user_length,
                                     stdout) != turn.user_length) {
        s = C_IO;
        break;
      }
      putchar('\n');
      s = print_answer(&turn.assistant);
    }
  } else if (!strcmp(argv[1], "chat") && argc == 3) {
    unsigned char *line = malloc(C_SESSION_REQUEST_BYTES + 1u);
    if (!line)
      s = C_NOMEM;
    puts("Local supported-evidence session. Enter /quit to save and exit.");
    while (s == C_OK) {
      size_t length = 0;
      int byte, oversized = 0;
      fputs("> ", stdout);
      fflush(stdout);
      while ((byte = getchar()) != EOF && byte != '\n') {
        if (length < C_SESSION_REQUEST_BYTES)
          line[length++] = (unsigned char)byte;
        else
          oversized = 1;
      }
      if (byte == EOF && !length)
        break;
      if (!oversized && length == 5 && !memcmp(line, "/quit", 5))
        break;
      if (oversized) {
        puts("request exceeds 64 KiB; history unchanged");
        continue;
      }
      c_session_answer answer;
      s = c_session_ask(session, line, length, &answer);
      if (s == C_OK)
        s = print_answer(&answer);
      if (s == C_OK)
        s = c_session_save(session, argv[2]);
    }
    free(line);
  } else if ((!strcmp(argv[1], "session-ask") && argc == 4) ||
             (!strcmp(argv[1], "session-generate") && argc == 5)) {
    unsigned char *bytes = NULL;
    size_t length = 0;
    uint64_t limit = 0;
    c_session_answer answer;
    if (argc == 5 &&
        (!number(argv[4], C_SESSION_GENERATED_BYTES, &limit) || !limit))
      s = C_INVALID;
    if (s == C_OK)
      s = c_read_file(argv[3], &bytes, &length);
    if (s == C_OK)
      s = argc == 4 ? c_session_ask(session, bytes, length, &answer)
                    : c_session_generate(session, bytes, length, NULL, 0,
                                         (size_t)limit, &answer);
    if (s == C_OK)
      s = print_answer(&answer);
    if (s == C_OK)
      s = c_session_save(session, argv[2]);
    free(bytes);
  } else
    s = C_INVALID;
  c_session_destroy(session);
  return s;
}
int main(int argc, char **argv) {
  c_trainer *t = NULL;
  c_status s = C_INVALID;
  c_training_report report;
  uint64_t a = 0, b = 0;
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (argc < 3) {
    usage();
    return 2;
  }
  if ((!strcmp(argv[1], "benchmark") || !strcmp(argv[1], "research") ||
       !strcmp(argv[1], "extensions")) &&
      argc == 3) {
    s = !strcmp(argv[1], "benchmark")  ? c_benchmark_run(argv[2])
        : !strcmp(argv[1], "research") ? c_research_run(argv[2])
                                       : c_extensions_run(argv[2]);
    if (s != C_OK) {
      fprintf(stderr, "centroid: %s\n", c_status_string(s));
      return 1;
    }
    return 0;
  }
  if (!strcmp(argv[1], "apply") && argc == 5) {
    s = c_patch_apply(argv[2], argv[3], argv[4]);
    if (s != C_OK)
      fprintf(stderr, "centroid: %s\n", c_status_string(s));
    return s == C_OK ? 0 : 1;
  }
  if ((!strncmp(argv[1], "session-", 8) && strcmp(argv[1], "session-new")) ||
      !strcmp(argv[1], "chat")) {
    s = session_command(argc, argv);
    if (s != C_OK)
      fprintf(stderr, "centroid: %s\n", c_status_string(s));
    return s == C_OK ? 0 : 1;
  }
  if (!strcmp(argv[1], "new")) {
    if ((argc == 3) || (argc == 5 && number(argv[3], C_MAX_GROUPS, &a) &&
                        number(argv[4], UINT64_MAX, &b))) {
      s = c_trainer_create(argc == 3 ? 2 : (unsigned)a, argc == 3 ? 42 : b, &t);
      if (s == C_OK)
        s = c_trainer_save(t, argv[2]);
    }
  } else {
    s = c_trainer_load(argv[2], &t);
    if (s == C_OK) {
      if (!strcmp(argv[1], "session-new") && argc == 4) {
        c_session *session = NULL;
        s = c_session_create(c_trainer_model(t), c_trainer_context(t),
                             &session);
        if (s == C_OK)
          s = c_session_save(session, argv[3]);
        c_session_destroy(session);
      } else if (!strcmp(argv[1], "replay-retired") && argc == 6) {
        uint64_t scope = 0;
        size_t requeued = 0;
        if (!number(argv[3], SIZE_MAX, &a) ||
            !number(argv[4], C_MAX_TASKS, &b) ||
            !number(argv[5], (1u << C_MAX_GROUPS) - 1u, &scope))
          s = C_INVALID;
        else
          s = c_trainer_replay_retired(t, (size_t)a, (size_t)b, (unsigned)scope,
                                       &requeued);
        if (s == C_OK) {
          printf("retired=%zu requeued=%zu\n", c_trainer_retired_count(t),
                 requeued);
          s = c_trainer_save(t, argv[2]);
        }
      } else if (!strcmp(argv[1], "suite") && argc == 6) {
        s = c_code_suite_run(t, argv[5], argv[3], argv[4]);
        if (s == C_OK)
          s = c_trainer_save(t, argv[2]);
      } else if (!strcmp(argv[1], "scan") && argc == 4) {
        c_scan_report scan;
        s = c_context_scan(c_trainer_context(t), argv[3], &scan);
        printf("admitted=%zu unchanged=%zu excluded=%zu failed=%zu bytes=%zu\n",
               scan.admitted, scan.unchanged, scan.excluded, scan.failed,
               scan.bytes);
        if (s == C_OK)
          s = c_trainer_save(t, argv[2]);
      } else if (!strcmp(argv[1], "admit"))
        s = admit(t, argc, argv);
      else if (!strcmp(argv[1], "query") && argc == 4)
        s = query(t, argv[3]);
      else if (!strcmp(argv[1], "record") && argc == 4 &&
               number(argv[3], UINT64_MAX, &a))
        s = record_by_id(t, a);
      else if (!strcmp(argv[1], "report") && argc == 3) {
        s = c_trainer_report(t, &report);
        if (s == C_OK)
          print_report(&report);
      } else if (!strcmp(argv[1], "next") && argc == 5 &&
                 number(argv[3], UINT64_MAX, &a) &&
                 number(argv[4], SIZE_MAX, &b))
        s = next_byte(t, a, (size_t)b);
      else if (!strcmp(argv[1], "step") && argc == 4 &&
               number(argv[3], 1000000, &a)) {
        s = c_trainer_step(t, (size_t)a, &report);
        if (s == C_OK) {
          print_report(&report);
          s = c_trainer_save(t, argv[2]);
        }
      } else if (!strcmp(argv[1], "train") && (argc == 4 || argc == 5) &&
                 number(argv[3], C_SOURCE_TRAIN_EPOCHS, &a) && a &&
                 (argc == 4 ||
                  number(argv[4], C_SOURCE_TRAIN_GENERATIONS, &b))) {
        c_source_train_report source_report;
        s = c_source_train(t, (size_t)a, argc == 4 ? 2048 : (size_t)b,
                           &source_report);
        if (s == C_OK) {
          printf("epochs=%zu sources_available=%zu sources_selected=%zu "
                 "sources_omitted=%zu enqueue_attempts=%zu\n",
                 source_report.epochs_completed,
                 source_report.available_sources,
                 source_report.selected_sources, source_report.omitted_sources,
                 source_report.enqueue_attempts);
          print_report(&source_report.training);
          s = c_trainer_save(t, argv[2]);
        }
      } else if (!strcmp(argv[1], "trial") && argc == 5) {
        s = c_experiment_run(t, argv[4], argv[3]);
        if (s == C_OK)
          s = c_trainer_save(t, argv[2]);
      } else if (!strcmp(argv[1], "run")) {
        int admitted;
        s = capture(t, argc, argv, &admitted);
        if (admitted) {
          /* Failed execution is still observed work. Persist its admitted
           * provenance without turning a process/capture failure into success.
           */
          c_status saved = c_trainer_save(t, argv[2]);
          if (saved != C_OK)
            s = saved;
        }
      } else
        s = C_INVALID;
    }
  }
  c_trainer_destroy(t);
  if (s != C_OK) {
    fprintf(stderr, "centroid: %s\n", c_status_string(s));
    return 1;
  }
  return 0;
}
