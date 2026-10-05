#ifndef CENTROID_SESSION_H
#define CENTROID_SESSION_H

#include "centroid.h"

#ifdef __cplusplus
extern "C" {
#endif

#define C_SESSION_REQUEST_BYTES (64u * 1024u)
#define C_SESSION_HISTORY_BYTES (256u * 1024u)
#define C_SESSION_TURNS 16u
#define C_SESSION_EXCERPT_BYTES 4096u
#define C_SESSION_GENERATED_BYTES 1024u

typedef struct c_session c_session;
typedef enum {
  C_SESSION_SUPPORTED,
  C_SESSION_ABSTAINED,
  C_SESSION_GENERATED
} c_session_answer_kind;

typedef struct {
  c_session_answer_kind kind;
  const unsigned char *bytes;
  size_t length;
  /* Populated only when current TRAIN SOURCE evidence was retrieved. bytes is
   * an exact source span for SUPPORTED. GENERATED remains unverified output. */
  c_record evidence;
  size_t span_begin, span_length;
  double lexical_score; /* Evidence similarity, never truth confidence. */
  int eos, byte_limit_reached;
  size_t prefix_length, generated_bytes, generation_budget;
} c_session_answer;

typedef struct {
  uint64_t id;
  /* These explicit user/assistant boundaries are metadata. Literal role words,
   * NUL and unknown bytes in either message remain original content bytes. */
  const unsigned char *user_bytes;
  size_t user_length;
  c_session_answer assistant;
} c_session_turn;

/* Deep snapshots preserve the frozen model and immutable context independently
 * of the original trainer's lifetime and later admissions/updates. No session
 * operation trains, advances Life, executes a tool or contacts a service. */
c_status c_session_create(const c_model *model, const c_context *context,
                          c_session **out);
void c_session_destroy(c_session *session);

/* Commit one complete bounded user/assistant turn. Unsupported source queries
 * return C_OK with ABSTAINED and an explicit message. Failure preserves history
 * and output. Views remain borrowed until session destruction. Latest admitted
 * SOURCE versions in the frozen snapshot are used; proposals/activity cannot
 * substitute for source evidence. */
c_status c_session_ask(c_session *session, const unsigned char *request,
                       size_t length, c_session_answer *out);

/* Separate experimental greedy byte/EOS generation. prefix is causal and kept
 * exactly; maximum_new_bytes bounds model calls and total prefix+output may not
 * exceed GENERATED_BYTES. EOS is a separate token, never a emitted byte. An
 * exhausted byte budget is explicit and does not pretend EOS was predicted.
 * Full requests, typed history and an attributed exact source span enter the
 * versioned inference frame; generated content carries no correctness claim. */
c_status c_session_generate(c_session *session, const unsigned char *request,
                            size_t length, const unsigned char *prefix,
                            size_t prefix_length, size_t maximum_new_bytes,
                            c_session_answer *out);
/* Read-only target-free inspection of the same role/evidence/prefix frame used
 * by generation. No future assistant bytes or target identity is required. */
c_status c_session_encode_context(const c_session *session,
                                  const unsigned char *request, size_t length,
                                  const unsigned char *prefix,
                                  size_t prefix_length, double out[C_FEATURES]);
size_t c_session_count(const c_session *session);
c_status c_session_history(const c_session *session, size_t index,
                           c_session_turn *out);

/* Canonical integrity-protected session state contains the frozen model,
 * complete context and all role-separated history. Deterministic generation is
 * validated again on load. Compatible build/recipe required. *out must be NULL
 * on success; any failure preserves an incumbent. Atomic save preserves the
 * previous file on failure. No sampling state exists: decoding is greedy. */
c_status c_session_save(const c_session *session, const char *path);
c_status c_session_load(const char *path, c_session **out);

#ifdef __cplusplus
}
#endif
#endif
