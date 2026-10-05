#ifndef C_CONTEXT_INTERNAL_H
#define C_CONTEXT_INTERNAL_H
#include "internal.h"

/* Exact admitted bytes are never cropped. History and quarantined records count
 * against the same store limits; provenance has an independent string bound. */
#define C_CONTEXT_RECORD_LIMIT 4096u
#define C_CONTEXT_RECORD_BYTES (1024u * 1024u)
#define C_CONTEXT_TOTAL_BYTES (8u * 1024u * 1024u)
#define C_CONTEXT_PROVENANCE_BYTES 4096u

typedef struct {
  c_record view;
  double centroid[C_FEATURES];
} c_owned_record;

struct c_context {
  c_owned_record **records;
  size_t count, byte_count;
  uint64_t next_id;
};

int c_context_family(const c_record *a, const c_record *b);
int c_context_metadata(c_record_kind kind, c_split split);
c_status c_context_valid(const c_context *context);
c_status c_context_read_regular(const char *path, unsigned char **bytes,
                                size_t *length);
c_status c_context_absolute(const char *path,
                            char absolute[C_CONTEXT_PROVENANCE_BYTES + 1u]);
c_status c_context_no_links(const char *absolute, int directory);
/* Known physical evaluation locations cannot be imported into TRAIN. Raw-byte
 * admission still relies on the caller's declared split and provenance. */
int c_context_reserved_training_path(const char *absolute);
#endif
