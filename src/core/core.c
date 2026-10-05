#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

const char *c_status_string(c_status status) {
  static const char *const names[] = {"ok",
                                      "invalid argument",
                                      "capacity limit",
                                      "I/O failure",
                                      "out of memory",
                                      "corrupt or incompatible state",
                                      "no supported evidence",
                                      "deferred"};
  return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status]
                                                             : "unknown status";
}
int c_checked_add(size_t a, size_t b, size_t *out) {
  if (!out || b > SIZE_MAX - a)
    return 0;
  *out = a + b;
  return 1;
}
int c_checked_mul(size_t a, size_t b, size_t *out) {
  if (!out || (a && b > SIZE_MAX / a))
    return 0;
  *out = a * b;
  return 1;
}
uint64_t c_random(uint64_t *state) {
  uint64_t z;
  *state += UINT64_C(0x9e3779b97f4a7c15);
  z = *state;
  z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
  return z ^ (z >> 31);
}

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
void c_hash(const void *bytes, size_t length, char out[C_DIGEST_HEX]) {
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
