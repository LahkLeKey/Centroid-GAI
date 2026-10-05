/* Preregistered bounded research comparisons. Alternate schedulers and
 * singleton owners are numerical references, never production mutation entry
 * points. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../../data/audit/research_fixtures.h"
#include "internal.h"
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define R_PATH 4096u
#define R_SEEDS 3u
#define R_CONDITIONS 11u
#define R_FAMILIES 6u
#define R_OFFSETS 12u
#define R_TAIL 16u
#define R_EPOCHS 4u
#define R_UPDATES (2u * R_OFFSETS * R_EPOCHS)
#define R_GENERATIONS 2048u
#define R_PAIRS 32u
#define R_SPLITS 4u

typedef struct {
  const char *name, *file, *split;
  const unsigned char *bytes;
  size_t length;
} r_family;
static const r_family families[R_FAMILIES] = {
    {"binary-search", "train/binary-search.c", "TRAIN", r_train_search,
     sizeof(r_train_search) - 1},
    {"byte-reversal", "train/byte-reversal.c", "TRAIN", r_train_reverse,
     sizeof(r_train_reverse) - 1},
    {"byte-count", "development/byte-count.c", "DEV", r_dev_count,
     sizeof(r_dev_count) - 1},
    {"insertion-sort", "development/insertion-sort.c", "DEV", r_dev_insert,
     sizeof(r_dev_insert) - 1},
    {"ring-buffer", "audit/ring-buffer.c", "AUDIT", r_audit_ring,
     sizeof(r_audit_ring) - 1},
    {"unsigned-gcd", "audit/unsigned-gcd.c", "AUDIT", r_audit_gcd,
     sizeof(r_audit_gcd) - 1}};
static const uint64_t seeds[R_SEEDS] = {UINT64_C(113), UINT64_C(271),
                                        UINT64_C(659)};
typedef struct {
  const char *name;
  unsigned groups, scheduler, representation, evidence, frozen;
} r_condition;
static const r_condition conditions[R_CONDITIONS] = {
    {"LIFE", 2, 0, 0, 0, 0},
    {"FROZEN_LIFE", 2, 1, 0, 0, 0},
    {"ROUND_ROBIN", 2, 2, 0, 0, 0},
    {"SINGLE_EXPERT", 1, 2, 0, 0, 0},
    {"FOUR_SPECIALISTS", 4, 0, 0, 0, 0},
    {"PAIR_SUBWORDS", 2, 0, 1, 0, 0},
    {"SOURCE_ONLY", 2, 0, 0, 1, 0},
    {"SOURCE_LLM", 2, 0, 0, 3, 0},
    {"SOURCE_ACTIVITY", 2, 0, 0, 5, 0},
    {"SOURCE_LLM_ACTIVITY", 2, 0, 0, 7, 0},
    {"FROZEN_NO_LEARNING", 2, 0, 0, 0, 1}};

typedef struct {
  unsigned short pair[R_PAIRS];
  size_t count;
} r_tokenizer;
typedef struct {
  size_t cases, correct;
  double loss;
} r_summary;
typedef struct {
  c_status status;
  c_training_report training;
  r_summary quality[2][R_SPLITS];
  size_t sequence_cases[2], sequence_exact[2], sequence_correct[2],
      sequence_targets[2], sequence_eos[2];
  uint64_t train_ms, evaluate_ms, scheduler_ms, total_ms, peak_bytes;
  size_t context_records, parameters, owner_updates, changed;
  int retained_code;
} r_result;
typedef struct {
  c_trainer *trainer;
  c_model *initial;
  uint64_t sources[2], request, evidence[3];
  r_tokenizer tokenizer;
} r_owner;

static c_status path_join(char out[R_PATH], const char *directory,
                          const char *name) {
  int length = snprintf(out, R_PATH, "%s/%s", directory, name);
  return length > 0 && (size_t)length < R_PATH ? C_OK : C_LIMIT;
}
static c_status artifact(const char *directory, const char *name,
                         const void *bytes, size_t length) {
  char path[R_PATH];
  c_status status = path_join(path, directory, name);
  return status == C_OK ? c_write_atomic(path, bytes, length) : status;
}
static void text(c_writer *writer, const char *format, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= sizeof(buffer))
    writer->status = C_LIMIT;
  else
    c_put_bytes(writer, buffer, (size_t)length);
}
static c_status publish(c_writer *writer, const char *directory,
                        const char *name) {
  c_status status =
      writer->status == C_OK
          ? artifact(directory, name, writer->data, writer->length)
          : writer->status;
  free(writer->data);
  memset(writer, 0, sizeof(*writer));
  return status;
}
static c_status new_directory(const char *path) {
#ifdef _WIN32
  return _mkdir(path) == 0 ? C_OK : errno == EEXIST ? C_INVALID : C_IO;
#else
  return mkdir(path, 0700) == 0 ? C_OK : errno == EEXIST ? C_INVALID : C_IO;
#endif
}
static size_t offset_for(const r_family *family, size_t index) {
  static const size_t offsets[] = {0, 8, 32, 64, 96, 128, 160, 192};
  if (index < sizeof(offsets) / sizeof(offsets[0]))
    return offsets[index];
  return index == 8    ? family->length - 64
         : index == 9  ? family->length - 16
         : index == 10 ? family->length - 1
                       : family->length;
}

static c_status protocol(const char *directory) {
  c_writer writer = {0};
  text(&writer,
       "# Frozen matched research protocol\n\nRegistered by the native harness "
       "before model creation, fitting, DEV/AUDIT prediction or result "
       "selection. "
       "The authored fixtures and this protocol are immutable per run.\n\n"
       "Recipe: `%s`. Seeds: 113,271,659. All learned conditions use the same "
       "96 joint optimizer-update budget, four epochs of 24 immutable TRAIN "
       "next-byte/EOS tasks; maximum2048 scheduling generations. Raw two-owner "
       "scheduler conditions start with identical parameters, optimizer, "
       "records "
       "and task order. Life advances B3/S23 and renews every8 generations; "
       "FROZEN_LIFE reads physical contacts from its unchanged initial world; "
       "ROUND_ROBIN is a deterministic research comparator. No alternate "
       "scheduler is a production update authority.\n\n",
       C_RECIPE);
  text(&writer,
       "TRAIN families: binary-search and byte-reversal. DEV: byte-count and "
       "insertion-sort. AUDIT: ring-buffer and unsigned-gcd. All six are newly "
       "authored C sources, disjoint from the earlier range/clamp benchmark. "
       "Only TRAIN answers are admitted or fitted. DEV/AUDIT are local frozen "
       "evaluation bytes, never context records, retrieval inputs or teacher "
       "tasks. Twelve predeclared positions per family include long prefixes "
       "and EOS; an additional contiguous16-byte tail plus EOS is "
       "teacher-forced "
       "and independently rolled out from its true starting prefix. Thus each "
       "split has58 teacher-forced cases per phase; repeated selected/tail "
       "positions remain reported as separate declared probes.\n\n");
  text(&writer,
       "Conditions: LIFE, FROZEN_LIFE, ROUND_ROBIN, SINGLE_EXPERT numerical "
       "reference, FOUR_SPECIALISTS, PAIR_SUBWORDS, SOURCE_ONLY, SOURCE_LLM, "
       "SOURCE_ACTIVITY, SOURCE_LLM_ACTIVITY, and FROZEN_NO_LEARNING (zero "
       "updates, explicitly unmatched). Single/four-owner comparisons have "
       "different active parameter and owner-update counts; these mismatches "
       "must be reported, never described as compute matched. Routing probes "
       "use the trained LIFE model with distance-canceling mass to force "
       "uniform "
       "routing, and the trained FOUR_SPECIALISTS model with a pair selected "
       "from causal-prefix SHA256. These are inference ablations; they are not "
       "separately fitted recipes.\n\n");
  text(&writer,
       "Representation: native nonrecursive frequent-pair subwords,32 pairs "
       "selected by frequency from TRAIN bytes only with bytewise tie breaks. "
       "Greedy left-to-right tokens are exactly reversible. IDs0..255 preserve "
       "all bytes and256..287 denote pairs; canonical wire uses two bytes per "
       "token. Training/evaluation consume c_encode(token_wire) and retain "
       "original byte/EOS targets. Report token count, wire bytes, dictionary "
       "bytes, exact reconstruction, quality and runtime; shorter token count "
       "does not imply fewer storage bytes. No shared encoder updates "
       "occur.\n\n");
  text(
      &writer,
      "Context: every condition admits the identical two TRAIN source answers, "
      "one generic source-evidence record, one visible request, one attributed "
      "Codex proposal and one factual native activity observation. Typed "
      "conditions vary evidence roles only; full request and causal prefixes "
      "are preserved. Proposal claims may be wrong; only immutable TRAIN "
      "source bytes/EOS are targets. Retention uses two predeclared CODE "
      "probability anchors with targets3 and1, not utility supervision. "
      "Report exact code-head value/moment preservation and output drift.\n\n");
  text(
      &writer,
      "Acceptance registered before predictions: all learned conditions must "
      "complete96 updates or report failure/deferral; zero DEV/AUDIT context "
      "records; lossless token roundtrip; finite normalized distributions; "
      "unused CODE heads unchanged. A scoped quality improvement needs "
      "positive "
      "mean AUDIT loss gain across3 seeds, no seed loss regression above0.1, "
      "and CODE-retention mean loss increase at most0.05. A scheduler/context/"
      "specialist/representation advantage needs at least0.02 AUDIT mean loss "
      "improvement against its declared comparator with no seed worse by0.1. "
      "Do not select model recipes on these AUDIT results or claim general "
      "coding ability. Failed gates and exact ties remain reportable outcomes. "
      "Matched update budgets are not matched total compute; schedule-only "
      "replays, full training time, evaluation time, total time, active "
      "parameters, owner updates and process high-water memory are reported. "
      "Peak memory is process-global cumulative, not condition-isolated "
      "RSS.\n\n");
  for (size_t i = 0; i < R_FAMILIES; ++i) {
    text(&writer, "- %s %s: offsets=", families[i].split, families[i].name);
    for (size_t j = 0; j < R_OFFSETS; ++j)
      text(&writer, "%s%zu", j ? "," : "", offset_for(&families[i], j));
    text(&writer, "; tail=%zu..%zu inclusive.\n", families[i].length - R_TAIL,
         families[i].length);
  }
  return publish(&writer, directory, "protocol.md");
}

static void fit_tokenizer(r_tokenizer *tokenizer) {
  size_t *counts = calloc(65536u, sizeof(*counts));
  if (counts == NULL)
    return;
  for (size_t family = 0; family < 2; ++family)
    for (size_t i = 1; i < families[family].length; ++i)
      ++counts[(unsigned)families[family].bytes[i - 1] * 256u +
               families[family].bytes[i]];
  for (size_t token = 0; token < R_PAIRS; ++token) {
    size_t best = 0;
    for (size_t pair = 1; pair < 65536u; ++pair)
      if (counts[pair] > counts[best])
        best = pair;
    if (counts[best] < 2)
      break;
    tokenizer->pair[tokenizer->count++] = (unsigned short)best;
    counts[best] = 0;
  }
  free(counts);
}
static c_status tokenize(const r_tokenizer *tokenizer,
                         const unsigned char *bytes, size_t length,
                         c_writer *wire, size_t *units) {
  if (tokenizer->count != R_PAIRS)
    return C_NOMEM;
  *units = 0;
  for (size_t at = 0; at < length;) {
    unsigned token = bytes[at++];
    if (at < length) {
      unsigned pair = token * 256u + bytes[at];
      for (size_t i = 0; i < tokenizer->count; ++i)
        if (tokenizer->pair[i] == pair) {
          token = 256u + (unsigned)i;
          ++at;
          break;
        }
    }
    unsigned char encoded[2] = {(unsigned char)token,
                                (unsigned char)(token >> 8u)};
    c_put_bytes(wire, encoded, sizeof(encoded));
    ++*units;
  }
  return wire->status;
}
static c_status roundtrip(const r_tokenizer *tokenizer,
                          const unsigned char *wire, size_t length,
                          const unsigned char *original,
                          size_t original_length) {
  size_t at = 0;
  if (length % 2)
    return C_CORRUPT;
  for (size_t i = 0; i < length; i += 2) {
    unsigned token = wire[i] | ((unsigned)wire[i + 1] << 8u);
    if (token < 256) {
      if (at >= original_length || original[at++] != token)
        return C_CORRUPT;
    } else {
      token -= 256;
      if (token >= tokenizer->count || original_length - at < 2 ||
          original[at++] != (tokenizer->pair[token] >> 8u) ||
          original[at++] != (tokenizer->pair[token] & 255u))
        return C_CORRUPT;
    }
  }
  return at == original_length ? C_OK : C_CORRUPT;
}
static c_status freeze(const char *directory, const r_tokenizer *tokenizer) {
  char path[R_PATH];
  const char *subdirectories[] = {"train", "development", "audit"};
  for (size_t i = 0; i < 3; ++i) {
    c_status status = path_join(path, directory, subdirectories[i]);
    if (status == C_OK)
      status = c_make_directory(path);
    if (status != C_OK)
      return status;
  }
  c_writer manifest = {0}, dictionary = {0};
  text(&manifest,
       "split\tfamily\tsha256\tbytes\ttoken_units\twire_bytes\troundtrip\n");
  text(&dictionary, "token\tfirst_byte\tsecond_byte\n");
  for (size_t i = 0; i < tokenizer->count; ++i)
    text(&dictionary, "%zu\t%u\t%u\n", i + 256, tokenizer->pair[i] >> 8u,
         tokenizer->pair[i] & 255u);
  c_status status = publish(&dictionary, directory, "dictionary.tsv");
  for (size_t i = 0; status == C_OK && i < R_FAMILIES; ++i) {
    c_writer tokens = {0};
    size_t units = 0;
    char digest[C_DIGEST_HEX];
    c_hash(families[i].bytes, families[i].length, digest);
    status = artifact(directory, families[i].file, families[i].bytes,
                      families[i].length);
    if (status == C_OK)
      status = tokenize(tokenizer, families[i].bytes, families[i].length,
                        &tokens, &units);
    if (status == C_OK)
      status = roundtrip(tokenizer, tokens.data, tokens.length,
                         families[i].bytes, families[i].length);
    if (status == C_OK) {
      char name[128];
      int n = snprintf(name, sizeof(name), "%s.tokens", families[i].file);
      status = n > 0 && (size_t)n < sizeof(name)
                   ? artifact(directory, name, tokens.data, tokens.length)
                   : C_LIMIT;
      text(&manifest, "%s\t%s\t%s\t%zu\t%zu\t%zu\t1\n", families[i].split,
           families[i].name, digest, families[i].length, units, tokens.length);
    }
    free(tokens.data);
  }
  if (status == C_OK)
    status = publish(&manifest, directory, "fixtures.tsv");
  else
    free(manifest.data);
  return status;
}

static c_status owners(r_owner *owner, const r_condition *condition,
                       uint64_t seed) {
  c_status status =
      c_trainer_research_create(condition->groups, seed, &owner->trainer);
  if (status == C_OK)
    status = c_model_create(condition->groups, seed, &owner->initial);
  c_context *context = c_trainer_context(owner->trainer);
  for (size_t i = 0; status == C_OK && i < 2; ++i)
    status =
        c_context_admit(context, C_SOURCE, C_TRAIN, families[i].file,
                        "native-research/frozen-train-v1", families[i].bytes,
                        families[i].length, &owner->sources[i]);
  static const unsigned char request[] =
      "Continue the immutable C source using exact bytes and terminate at EOS.";
  static const unsigned char source[] =
      "/* C source evidence: preserve punctuation, case, indentation and exact "
      "byte order. Never substitute a claim for a measured target. */\n";
  static const unsigned char proposal[] =
      "Attributed Codex research proposal: compare byte features, source "
      "evidence and visible activity. Unverified hypothesis: emitting EOS at "
      "every position might work. This claim is input only and must not teach.";
  char activity[512], first[C_DIGEST_HEX], second[C_DIGEST_HEX];
  c_hash(families[0].bytes, families[0].length, first);
  c_hash(families[1].bytes, families[1].length, second);
  int count = snprintf(activity, sizeof(activity),
                       "Native observed work: froze TRAIN source bytes and "
                       "computed SHA256 identities %s and %s before fitting. "
                       "No independent evaluation result is asserted.",
                       first, second);
  if (status == C_OK && (count < 0 || (size_t)count >= sizeof(activity)))
    status = C_LIMIT;
  if (status == C_OK)
    status =
        c_context_admit(context, C_ACTIVITY, C_TRAIN, "request/context.txt",
                        "visible-native-research-request/v1", request,
                        sizeof(request) - 1, &owner->request);
  if (status == C_OK)
    status = c_context_admit(context, C_SOURCE, C_TRAIN, "train/evidence.c",
                             "authored-source-evidence/v1", source,
                             sizeof(source) - 1, &owner->evidence[0]);
  if (status == C_OK)
    status = c_context_admit(
        context, C_LLM_PROPOSAL, C_TRAIN, "context/codex-proposal.txt",
        "Codex authored proposal; unverified input/v1", proposal,
        sizeof(proposal) - 1, &owner->evidence[1]);
  if (status == C_OK)
    status = c_context_admit(
        context, C_ACTIVITY, C_TRAIN, "context/native-work.txt",
        "native-research actual fixture hashing/v1",
        (const unsigned char *)activity, (size_t)count, &owner->evidence[2]);
  if (status == C_OK && c_context_count(context) != 6)
    status = C_CORRUPT;
  return status;
}
static unsigned eligibility(const r_condition *condition) {
  return (1u << condition->groups) - 1u;
}
static c_status encode(const r_owner *owner, const r_condition *condition,
                       const unsigned char *bytes, size_t length,
                       double input[C_FEATURES]) {
  if (condition->evidence) {
    uint64_t ids[3];
    size_t count = 0;
    for (unsigned i = 0; i < 3; ++i)
      if (condition->evidence & (1u << i))
        ids[count++] = owner->evidence[i];
    return c_text_encode_context(c_trainer_context(owner->trainer),
                                 owner->request, ids, count, bytes, length,
                                 input);
  }
  if (condition->representation) {
    c_writer tokens = {0};
    size_t units = 0;
    c_status status =
        tokenize(&owner->tokenizer, bytes, length, &tokens, &units);
    if (status == C_OK)
      status = c_encode(tokens.data, tokens.length, input);
    free(tokens.data);
    return status;
  }
  return c_encode(bytes, length, input);
}
static c_status predict(const c_model *model, c_head head,
                        const double input[C_FEATURES], unsigned eligible,
                        unsigned routing,
                        double probabilities[C_TEXT_ACTIONS]) {
  double mass[C_MAX_GROUPS] = {1, 1, 1, 1};
  if (routing) {
    double distance[C_MAX_GROUPS] = {0}, maximum = 0;
    for (unsigned g = 0; g < model->groups; ++g) {
      for (size_t i = 0; i < C_FEATURES; ++i) {
        double delta = input[i] - model->expert[g].centroid[i].value;
        distance[g] += delta * delta;
      }
      if (distance[g] > maximum)
        maximum = distance[g];
    }
    for (unsigned g = 0; g < model->groups; ++g)
      mass[g] = exp(distance[g] - maximum);
  }
  return c_model_predict(model, head, input, eligible, mass, probabilities,
                         C_TEXT_ACTIONS);
}
static unsigned top(const double *probabilities, unsigned actions) {
  unsigned winner = 0;
  for (unsigned i = 1; i < actions; ++i)
    if (probabilities[i] > probabilities[winner])
      winner = i;
  return winner;
}
static void add(r_summary *summary, unsigned target, unsigned prediction,
                double probability) {
  ++summary->cases;
  summary->correct += target == prediction;
  summary->loss -= log(probability);
}
static c_status row(FILE *file, const char *condition, uint64_t seed,
                    const char *phase, const char *probe, const char *split,
                    const char *family, size_t offset, unsigned target,
                    unsigned eligible, c_head head,
                    const double probabilities[C_TEXT_ACTIONS]) {
  unsigned actions = head == C_TEXT ? C_TEXT_ACTIONS : C_CODE_ACTIONS;
  unsigned prediction = top(probabilities, actions);
  double sum = 0;
  for (unsigned i = 0; i < actions; ++i) {
    if (!isfinite(probabilities[i]) || probabilities[i] <= 0)
      return C_CORRUPT;
    sum += probabilities[i];
  }
  if (target >= actions || fabs(sum - 1) > 1e-10)
    return C_CORRUPT;
  fprintf(file, "%s\t%llu\t%s\t%s\t%s\t%s\t%zu\t%u\t%u\t%u\t%.17g\t%.17g",
          condition, (unsigned long long)seed, phase, probe, split, family,
          offset, target, prediction, eligible, probabilities[target],
          -log(probabilities[target]));
  for (unsigned i = 0; i < C_TEXT_ACTIONS; ++i)
    if (i < actions)
      fprintf(file, "\t%.17g", probabilities[i]);
    else
      fputs("\tNA", file);
  fputc('\n', file);
  return ferror(file) ? C_IO : C_OK;
}
static unsigned prefix_pair(const unsigned char *bytes, size_t length) {
  char digest[C_DIGEST_HEX];
  c_hash(bytes, length, digest);
  unsigned index = (unsigned char)digest[0] % 4u;
  return (1u << index) | (1u << ((index + 1u) % 4u));
}
static c_status evaluate(r_owner *owner, const r_condition *condition,
                         uint64_t seed, unsigned phase, unsigned ablation,
                         const char *name, FILE *predictions,
                         r_result *result) {
  const c_model *model =
      phase ? c_trainer_model(owner->trainer) : owner->initial;
  uint64_t start = c_monotonic_ms();
  const char *phase_name = phase ? "TRAINED" : "INITIAL";
  c_status status = C_OK;
  for (size_t family = 0; status == C_OK && family < R_FAMILIES; ++family) {
    for (size_t probe = 0; status == C_OK && probe < R_OFFSETS + R_TAIL + 1;
         ++probe) {
      size_t offset = probe < R_OFFSETS ? offset_for(&families[family], probe)
                                        : families[family].length - R_TAIL +
                                              probe - R_OFFSETS;
      unsigned target = offset == families[family].length
                            ? C_EOS
                            : families[family].bytes[offset];
      unsigned eligible = ablation == 2
                              ? prefix_pair(families[family].bytes, offset)
                              : eligibility(condition);
      double input[C_FEATURES], probabilities[C_TEXT_ACTIONS] = {0};
      status = encode(owner, condition, families[family].bytes, offset, input);
      if (status == C_OK)
        status = predict(model, C_TEXT, input, eligible, ablation == 1,
                         probabilities);
      if (status == C_OK)
        status = row(predictions, name, seed, phase_name,
                     probe < R_OFFSETS ? "SELECTED" : "CAUSAL_TAIL",
                     families[family].split, families[family].name, offset,
                     target, eligible, C_TEXT, probabilities);
      if (status == C_OK) {
        add(&result->quality[phase][family / 2], target,
            top(probabilities, C_TEXT_ACTIONS), probabilities[target]);
        if (phase && ablation == 0) {
          double before[C_TEXT_ACTIONS] = {0};
          status = predict(owner->initial, C_TEXT, input, eligible, 0, before);
          if (status == C_OK && memcmp(before, probabilities, sizeof(before)))
            ++result->changed;
        }
      }
    }
  }
  static const unsigned char anchors[2][32] = {"retention binary search bounds",
                                               "retention reversal byte order"};
  static const unsigned targets[2] = {3, 1};
  for (unsigned i = 0; status == C_OK && i < 2; ++i) {
    double input[C_FEATURES], probabilities[C_TEXT_ACTIONS] = {0};
    status = c_encode(anchors[i], strlen((const char *)anchors[i]), input);
    if (status == C_OK)
      status = predict(model, C_CODE, input, eligibility(condition), 0,
                       probabilities);
    if (status == C_OK)
      status = row(predictions, name, seed, phase_name, "RETENTION",
                   "RETENTION", i ? "reversal-anchor" : "search-anchor", 0,
                   targets[i], eligibility(condition), C_CODE, probabilities);
    if (status == C_OK)
      add(&result->quality[phase][3], targets[i], top(probabilities, 4),
          probabilities[targets[i]]);
  }
  result->evaluate_ms += c_monotonic_ms() - start;
  return status;
}

static c_status rollout(r_owner *owner, const r_condition *condition,
                        uint64_t seed, unsigned phase, FILE *predictions,
                        FILE *sequences, r_result *result) {
  uint64_t start = c_monotonic_ms();
  const c_model *model =
      phase ? c_trainer_model(owner->trainer) : owner->initial;
  for (size_t family = 0; family < R_FAMILIES; ++family) {
    unsigned char bytes[1024];
    size_t prefix = families[family].length - R_TAIL;
    if (families[family].length + R_TAIL >= sizeof(bytes))
      return C_LIMIT;
    memcpy(bytes, families[family].bytes, prefix);
    int exact = 1;
    fprintf(sequences, "%s\t%llu\t%s\t%s\t%zu", condition->name,
            (unsigned long long)seed, phase ? "TRAINED" : "INITIAL",
            families[family].name, prefix);
    for (size_t step = 0; step <= R_TAIL; ++step) {
      double input[C_FEATURES], probabilities[C_TEXT_ACTIONS] = {0};
      c_status status = encode(owner, condition, bytes, prefix, input);
      if (status == C_OK)
        status = predict(model, C_TEXT, input, eligibility(condition), 0,
                         probabilities);
      unsigned target =
          step == R_TAIL
              ? C_EOS
              : families[family].bytes[families[family].length - R_TAIL + step];
      if (status == C_OK)
        status = row(predictions, condition->name, seed,
                     phase ? "TRAINED" : "INITIAL", "ROLLOUT",
                     families[family].split, families[family].name,
                     families[family].length - R_TAIL + step, target,
                     eligibility(condition), C_TEXT, probabilities);
      if (status != C_OK)
        return status;
      unsigned prediction = top(probabilities, C_TEXT_ACTIONS);
      fprintf(sequences, "\t%u", prediction);
      ++result->sequence_targets[phase];
      result->sequence_correct[phase] += prediction == target;
      result->sequence_eos[phase] += step == R_TAIL && prediction == C_EOS;
      if (prediction != target)
        exact = 0;
      /* Early EOS does not truncate the declared denominator: it is recorded
       * and the same prefix is evaluated for every remaining scheduled step. */
      if (prediction != C_EOS)
        bytes[prefix++] = (unsigned char)prediction;
    }
    fprintf(sequences, "\t%d\n", exact);
    ++result->sequence_cases[phase];
    result->sequence_exact[phase] += exact != 0;
  }
  result->evaluate_ms += c_monotonic_ms() - start;
  return ferror(sequences) ? C_IO : C_OK;
}
static c_status train(r_owner *owner, const r_condition *condition,
                      uint64_t seed, FILE *inputs, r_result *result) {
  if (condition->frozen)
    return c_trainer_report(owner->trainer, &result->training);
  uint64_t start = c_monotonic_ms();
  for (size_t epoch = 0; epoch < R_EPOCHS; ++epoch) {
    for (size_t family = 0; family < 2; ++family)
      for (size_t probe = 0; probe < R_OFFSETS; ++probe) {
        size_t offset = offset_for(&families[family], probe);
        double input[C_FEATURES];
        c_status status =
            encode(owner, condition, families[family].bytes, offset, input);
        if (status == C_OK)
          status =
              c_trainer_research_enqueue(owner->trainer, owner->sources[family],
                                         offset, eligibility(condition), input);
        if (status != C_OK)
          return status;
        fprintf(inputs, "%s\t%llu\t%zu\t%s\t%zu\t%u\t%u", condition->name,
                (unsigned long long)seed, epoch, families[family].name, offset,
                offset == families[family].length
                    ? C_EOS
                    : families[family].bytes[offset],
                eligibility(condition));
        for (size_t i = 0; i < C_FEATURES; ++i)
          fprintf(inputs, "\t%.17g", input[i]);
        fputc('\n', inputs);
      }
    uint64_t target = (epoch + 1) * 2 * R_OFFSETS;
    while (result->training.updates < target &&
           result->training.generation < R_GENERATIONS) {
      c_status status = c_trainer_research_step(
          owner->trainer, 1, condition->scheduler, &result->training);
      if (status != C_OK)
        return status;
    }
    if (result->training.updates != target)
      return C_DEFERRED;
  }
  result->train_ms = c_monotonic_ms() - start;
  return ferror(inputs) ? C_IO : C_OK;
}
static uint64_t scheduler_cost(const r_condition *condition, uint64_t seed,
                               uint64_t generations) {
  c_trainer *owner = NULL;
  if (c_trainer_research_create(condition->groups, seed, &owner) != C_OK)
    return UINT64_MAX;
  c_training_report report;
  uint64_t start = c_monotonic_ms();
  c_status status = c_trainer_research_step(owner, (size_t)generations,
                                            condition->scheduler, &report);
  uint64_t elapsed = c_monotonic_ms() - start;
  c_trainer_destroy(owner);
  return status == C_OK ? elapsed : UINT64_MAX;
}
static double mean(const r_summary *summary) {
  return summary->cases ? summary->loss / (double)summary->cases : 0;
}
static double accuracy(const r_summary *summary) {
  return summary->cases ? (double)summary->correct / (double)summary->cases : 0;
}
static c_status metrics(const char *directory,
                        r_result results[R_SEEDS][R_CONDITIONS],
                        r_result ablations[R_SEEDS][2], uint64_t total) {
  c_writer writer = {0};
  text(&writer,
       "condition\tseed\tstatus\tbudget\tupdates\tgenerations\tcontacts\tdeferr"
       "ed\treseeds\towner0\towner1\towner2\towner3\towner_updates\tactive_"
       "parameters\tcontext_records\tdev_audit_records\tcode_"
       "unchanged\tchanged_distributions\ttrain_ms\tschedule_only_ms\tevaluate_"
       "ms\ttotal_ms\tprocess_peak_bytes");
  for (unsigned phase = 0; phase < 2; ++phase) {
    const char *label = phase ? "trained" : "initial";
    const char *splits[] = {"train", "dev", "audit", "retention"};
    for (unsigned split = 0; split < R_SPLITS; ++split)
      text(&writer, "\t%s_%s_cases\t%s_%s_correct\t%s_%s_loss\t%s_%s_accuracy",
           label, splits[split], label, splits[split], label, splits[split],
           label, splits[split]);
    text(&writer,
         "\t%s_sequence_cases\t%s_sequence_exact\t%s_sequence_correct\t%s_"
         "sequence_targets\t%s_sequence_eos",
         label, label, label, label, label);
  }
  text(&writer, "\n");
  for (unsigned seed = 0; seed < R_SEEDS; ++seed)
    for (unsigned condition = 0; condition < R_CONDITIONS + 2; ++condition) {
      const r_result *r = condition < R_CONDITIONS
                              ? &results[seed][condition]
                              : &ablations[seed][condition - R_CONDITIONS];
      const char *name = condition < R_CONDITIONS ? conditions[condition].name
                         : condition == R_CONDITIONS ? "UNIFORM_ROUTING"
                                                     : "PREFIX_ELIGIBILITY";
      text(&writer, "%s\t%llu\t%s\t%u\t%llu\t%llu\t%llu\t%llu\t%llu", name,
           (unsigned long long)seeds[seed], c_status_string(r->status),
           condition < R_CONDITIONS && !conditions[condition].frozen ? R_UPDATES
                                                                     : 0u,
           (unsigned long long)r->training.updates,
           (unsigned long long)r->training.generation,
           (unsigned long long)r->training.contacts,
           (unsigned long long)r->training.deferred,
           (unsigned long long)r->training.reseeds);
      for (unsigned group = 0; group < C_MAX_GROUPS; ++group)
        text(&writer, "\t%llu",
             (unsigned long long)r->training.group_updates[group]);
      text(&writer, "\t%zu\t%zu\t%zu\t0\t%d\t%zu\t%llu\t%llu\t%llu\t%llu\t%llu",
           r->owner_updates, r->parameters, r->context_records,
           r->retained_code, r->changed, (unsigned long long)r->train_ms,
           (unsigned long long)r->scheduler_ms,
           (unsigned long long)r->evaluate_ms, (unsigned long long)r->total_ms,
           (unsigned long long)r->peak_bytes);
      for (unsigned phase = 0; phase < 2; ++phase) {
        for (unsigned split = 0; split < R_SPLITS; ++split)
          text(&writer, "\t%zu\t%zu\t%.17g\t%.17g",
               r->quality[phase][split].cases, r->quality[phase][split].correct,
               mean(&r->quality[phase][split]),
               accuracy(&r->quality[phase][split]));
        text(&writer, "\t%zu\t%zu\t%zu\t%zu\t%zu", r->sequence_cases[phase],
             r->sequence_exact[phase], r->sequence_correct[phase],
             r->sequence_targets[phase], r->sequence_eos[phase]);
      }
      text(&writer, "\n");
    }
  c_status status = publish(&writer, directory, "metrics.tsv");
  if (status == C_OK) {
    text(&writer,
         "metric\tvalue\nseeds\t3\nlearned_"
         "conditions\t10\nconditions\t11\ninference_ablations\t2\nupdates_per_"
         "learned_condition\t96\ntrain_cases_per_phase\t58\ndev_cases_per_"
         "phase\t58\naudit_cases_per_phase\t58\nretention_cases_per_"
         "phase\t2\nsequence_cases_per_phase\t6\nsequence_targets_per_"
         "phase\t102\nall_run_total_ms\t%llu\nprocess_peak_bytes\t%llu\n",
         (unsigned long long)total,
         (unsigned long long)c_process_peak_memory_bytes());
    status = publish(&writer, directory, "summary.tsv");
  }
  return status;
}
static c_status report(const char *directory,
                       r_result results[R_SEEDS][R_CONDITIONS],
                       r_result ablations[R_SEEDS][2]) {
  c_writer writer = {0};
  text(&writer,
       "# Matched native research results\n\nThe protocol and fixtures were "
       "frozen before predictions. `predictions.tsv` retains complete "
       "distributions for every selected prefix, contiguous causal tail, "
       "autoregressive rollout step and retention probe. `inputs.tsv` retains "
       "every TRAIN target and exact feature vector; `sequences.tsv` records "
       "all attempted17-symbol rollouts. `metrics.tsv` contains every seed, "
       "denominator, failure, update/owner budget, coverage and cost. No "
       "DEV/AUDIT record was admitted.\n\n");
  text(&writer, "| Condition | Updates per seed | Mean TRAIN loss before → "
                "after | Mean DEV loss before → after | Mean AUDIT loss before "
                "→ after | Mean AUDIT accuracy after | Retention loss delta "
                "|\n| --- | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (unsigned condition = 0; condition < R_CONDITIONS; ++condition) {
    double losses[2][R_SPLITS] = {{0}}, audit_accuracy = 0;
    for (unsigned seed = 0; seed < R_SEEDS; ++seed) {
      for (unsigned phase = 0; phase < 2; ++phase)
        for (unsigned split = 0; split < R_SPLITS; ++split)
          losses[phase][split] +=
              mean(&results[seed][condition].quality[phase][split]) / R_SEEDS;
      audit_accuracy +=
          accuracy(&results[seed][condition].quality[1][2]) / R_SEEDS;
    }
    text(
        &writer,
        "| %s | %u | %.8g → %.8g | %.8g → %.8g | %.8g → %.8g | %.6g | %.8g |\n",
        conditions[condition].name,
        conditions[condition].frozen ? 0u : R_UPDATES, losses[0][0],
        losses[1][0], losses[0][1], losses[1][1], losses[0][2], losses[1][2],
        audit_accuracy, losses[1][3] - losses[0][3]);
  }
  text(&writer,
       "\nPreregistered gates (an improvement is positive comparator loss "
       "minus candidate loss; no seed may regress by more than0.1):\n\n");
  static const unsigned candidate[] = {0, 1, 2, 3, 4, 5, 7, 8, 9};
  static const unsigned comparator[] = {10, 0, 0, 0, 0, 0, 6, 6, 6};
  for (size_t probe = 0; probe < sizeof(candidate) / sizeof(candidate[0]);
       ++probe) {
    double gain = 0, retention = 0, worst = INFINITY;
    int budgets = 1;
    for (unsigned seed = 0; seed < R_SEEDS; ++seed) {
      const r_result *a = &results[seed][candidate[probe]],
                     *b = &results[seed][comparator[probe]];
      double improvement = mean(&b->quality[1][2]) - mean(&a->quality[1][2]);
      gain += improvement / R_SEEDS;
      retention +=
          (mean(&a->quality[1][3]) - mean(&a->quality[0][3])) / R_SEEDS;
      if (improvement < worst)
        worst = improvement;
      if (a->status != C_OK || a->training.updates != R_UPDATES)
        budgets = 0;
    }
    double margin = probe ? 0.02 : 0;
    int gate = budgets && gain > margin && worst >= -0.1 && retention <= 0.05;
    text(&writer,
         "- %s versus %s: mean AUDIT loss gain=%.9g, worst seed gain=%.9g, "
         "retention delta=%.9g, margin=%.2g, gate=%s.\n",
         conditions[candidate[probe]].name, conditions[comparator[probe]].name,
         gain, worst, retention, margin,
         gate ? "PASS" : "FAIL_OR_INCONCLUSIVE");
  }
  for (unsigned ablation = 0; ablation < 2; ++ablation) {
    double gain = 0;
    unsigned parent = ablation ? 4u : 0u;
    for (unsigned seed = 0; seed < R_SEEDS; ++seed)
      gain += (mean(&results[seed][parent].quality[1][2]) -
               mean(&ablations[seed][ablation].quality[1][2])) /
              R_SEEDS;
    text(&writer,
         "- Inference %s versus trained %s: mean AUDIT loss gain=%.9g; no "
         "separate fitting budget or production promotion.\n",
         ablation ? "PREFIX_ELIGIBILITY" : "UNIFORM_ROUTING",
         conditions[parent].name, gain);
  }
  text(&writer,
       "\nInterpretation limits: multiple seeds assess this fixed authored "
       "experiment, not broad transfer. Task-update budgets match among "
       "learned conditions, but single/four owners and token/context inputs "
       "have different parameter, scalar-update, encoding and inference costs. "
       "Millisecond measurements may quantize schedule-only time to zero. Peak "
       "memory is the cumulative native process high-water mark and includes "
       "report buffers and previous conditions. No held-out result selects a "
       "recipe, authorizes a merge or proves general coding ability. Exact "
       "ties or failed margins resolve a research question as inconclusive; "
       "Life optimality and causal LLM utility must not be inferred from "
       "infrastructure passes. Research owners are deliberately not "
       "serializable as production checkpoints; retained fixture, recipe, "
       "seeds and exact TRAIN features allow independent deterministic reruns. "
       "Production continuation is tested separately.\n");
  return publish(&writer, directory, "report.md");
}

static FILE *open_artifact(const char *directory, const char *name) {
  char path[R_PATH];
  return path_join(path, directory, name) == C_OK ? fopen(path, "wb") : NULL;
}
c_status c_research_run(const char *directory) {
  if (directory == NULL || directory[0] == '\0')
    return C_INVALID;
  if (strlen(directory) > 3800)
    return C_LIMIT;
  c_status status = new_directory(directory);
  if (status != C_OK)
    return status;
  uint64_t started = c_monotonic_ms();
  r_tokenizer tokenizer = {{0}, 0};
  status = protocol(directory);
  if (status == C_OK) {
    fit_tokenizer(&tokenizer);
    status = tokenizer.count == R_PAIRS ? C_OK : C_NOMEM;
  }
  if (status == C_OK)
    status = freeze(directory, &tokenizer);
  FILE *predictions = NULL, *inputs = NULL, *sequences = NULL;
  if (status == C_OK) {
    predictions = open_artifact(directory, "predictions.tsv");
    inputs = open_artifact(directory, "inputs.tsv");
    sequences = open_artifact(directory, "sequences.tsv");
    if (!predictions || !inputs || !sequences)
      status = C_IO;
  }
  if (status == C_OK) {
    fputs("condition\tseed\tphase\tprobe\tsplit\tfamily\toffset\ttarget\tpredic"
          "ted\teligible\tp_target\tloss",
          predictions);
    for (unsigned action = 0; action < C_TEXT_ACTIONS; ++action)
      fprintf(predictions, "\tp%u", action);
    fputc('\n', predictions);
    fputs("condition\tseed\tepoch\tfamily\toffset\ttarget\teligible", inputs);
    for (unsigned feature = 0; feature < C_FEATURES; ++feature)
      fprintf(inputs, "\tf%u", feature);
    fputc('\n', inputs);
    fputs("condition\tseed\tphase\tfamily\tstarting_prefix", sequences);
    for (unsigned step = 0; step <= R_TAIL; ++step)
      fprintf(sequences, "\ta%u", step);
    fputs("\texact\n", sequences);
  }
  r_result results[R_SEEDS][R_CONDITIONS] = {{{0}}};
  r_result ablations[R_SEEDS][2] = {{{0}}};
  c_status task_status = C_OK;
  for (unsigned seed = 0; status == C_OK && seed < R_SEEDS; ++seed)
    for (unsigned condition = 0; status == C_OK && condition < R_CONDITIONS;
         ++condition) {
      r_owner owner = {0};
      owner.tokenizer = tokenizer;
      r_result *result = &results[seed][condition];
      uint64_t start = c_monotonic_ms();
      status = owners(&owner, &conditions[condition], seeds[seed]);
      if (status == C_OK && seed == 0 && condition == 0) {
        char context_path[R_PATH];
        status = path_join(context_path, directory, "context.pack");
        if (status == C_OK)
          status =
              c_context_save(c_trainer_context(owner.trainer), context_path);
      }
      if (status == C_OK)
        status = evaluate(&owner, &conditions[condition], seeds[seed], 0, 0,
                          conditions[condition].name, predictions, result);
      if (status == C_OK)
        status = rollout(&owner, &conditions[condition], seeds[seed], 0,
                         predictions, sequences, result);
      c_status trained = status == C_OK ? train(&owner, &conditions[condition],
                                                seeds[seed], inputs, result)
                                        : status;
      if (trained != C_OK)
        task_status = trained;
      if (status == C_OK && trained != C_OK && trained != C_DEFERRED)
        status = trained;
      if (status == C_OK)
        status = evaluate(&owner, &conditions[condition], seeds[seed], 1, 0,
                          conditions[condition].name, predictions, result);
      if (status == C_OK)
        status = rollout(&owner, &conditions[condition], seeds[seed], 1,
                         predictions, sequences, result);
      if (status == C_OK) {
        result->context_records =
            c_context_count(c_trainer_context(owner.trainer));
        result->parameters = conditions[condition].groups *
                             (C_FEATURES + C_FEATURES * C_TEXT_ACTIONS +
                              C_FEATURES * C_CODE_ACTIONS);
        result->retained_code = 1;
        const c_model *model = c_trainer_model(owner.trainer);
        for (unsigned group = 0; group < conditions[condition].groups;
             ++group) {
          result->owner_updates += (size_t)c_model_group_clock(model, group);
          if (memcmp(model->expert[group].code,
                     owner.initial->expert[group].code,
                     sizeof(model->expert[group].code)))
            result->retained_code = 0;
        }
        if (!result->retained_code || result->context_records != 6 ||
            (trained == C_OK && !conditions[condition].frozen &&
             result->training.updates != R_UPDATES))
          status = C_CORRUPT;
        result->scheduler_ms = scheduler_cost(
            &conditions[condition], seeds[seed], result->training.generation);
        if (result->scheduler_ms == UINT64_MAX)
          status = C_NOMEM;
      }
      if (status == C_OK && (condition == 0 || condition == 4)) {
        unsigned index = condition == 4;
        r_result *ablation = &ablations[seed][index];
        status = evaluate(&owner, &conditions[condition], seeds[seed], 1,
                          index ? 2u : 1u,
                          index ? "PREFIX_ELIGIBILITY" : "UNIFORM_ROUTING",
                          predictions, ablation);
        ablation->status = status;
        ablation->retained_code = 1;
        ablation->context_records = 6;
        ablation->parameters = result->parameters;
        ablation->peak_bytes = c_process_peak_memory_bytes();
      }
      result->total_ms = c_monotonic_ms() - start;
      result->peak_bytes = c_process_peak_memory_bytes();
      result->status = status == C_OK ? trained : status;
      c_model_destroy(owner.initial);
      c_trainer_destroy(owner.trainer);
    }
  if (predictions && fclose(predictions) != 0)
    status = C_IO;
  if (inputs && fclose(inputs) != 0)
    status = C_IO;
  if (sequences && fclose(sequences) != 0)
    status = C_IO;
  if (status == C_OK)
    status = metrics(directory, results, ablations, c_monotonic_ms() - started);
  if (status == C_OK)
    status = report(directory, results, ablations);
  if (status != C_OK) {
    c_writer writer = {0};
    text(&writer,
         "# Research attempt failed\n\nNative status: %s. All protocol, "
         "fixtures and partial attempts are retained; this directory cannot be "
         "reused.\n",
         c_status_string(status));
    (void)publish(&writer, directory, "FAILURE.md");
  }
  return status == C_OK ? task_status : status;
}
