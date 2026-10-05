#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "core line %d: %s\n", __LINE__, #x);                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main(void) {
  char digest[65];
  size_t n;
  uint64_t seed = 0;
  unsigned char *raw = NULL;
  size_t length = 0;
  c_writer w = {0};
  c_reader r;
  c_hash("", 0, digest);
  CHECK(!strcmp(
      digest,
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  c_hash("abc", 3, digest);
  CHECK(!strcmp(
      digest,
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  c_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
         digest);
  CHECK(!strcmp(
      digest,
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
  CHECK(c_checked_add(3, 4, &n) && n == 7);
  CHECK(!c_checked_add(SIZE_MAX, 1, &n));
  CHECK(c_checked_mul(3, 4, &n) && n == 12);
  CHECK(!c_checked_mul(SIZE_MAX, 2, &n));
  CHECK(c_random(&seed) == UINT64_C(0xe220a8397b1dcdaf));
  c_put_u32(&w, 0x12345678);
  c_put_u64(&w, UINT64_C(0xfedcba9876543210));
  c_put_double(&w, 1.25);
  CHECK(w.status == C_OK && w.length == 20 && w.data[0] == 0x78);
  r.data = w.data;
  r.length = w.length;
  r.offset = 0;
  r.status = C_OK;
  CHECK(c_get_u32(&r) == 0x12345678);
  CHECK(c_get_u64(&r) == UINT64_C(0xfedcba9876543210));
  CHECK(c_get_double(&r) == 1.25);
  CHECK(r.status == C_OK && r.offset == r.length);
  c_get_u32(&r);
  CHECK(r.status == C_CORRUPT);
  CHECK(c_envelope_write("test-core.cctx", "CORETEST", w.data, w.length) ==
        C_OK);
  CHECK(c_envelope_read("test-core.cctx", "CORETEST", &raw, &length) == C_OK &&
        length == w.length && !memcmp(raw, w.data, length));
  free(raw);
  free(w.data);
  remove("test-core.cctx");
  puts("core contracts passed");
  return 0;
}
