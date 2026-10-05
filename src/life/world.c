#include "internal.h"
#include <string.h>

static size_t cell(int x, int y) {
  return (size_t)((y + (int)C_WORLD_SIDE) % (int)C_WORLD_SIDE) * C_WORLD_SIDE +
         (size_t)((x + (int)C_WORLD_SIDE) % (int)C_WORLD_SIDE);
}
static void register_pair(unsigned a, unsigned b, unsigned *contacts) {
  unsigned i, j;
  for (i = 0; i < C_MAX_GROUPS; i++)
    for (j = i + 1; j < C_MAX_GROUPS; j++)
      if (((a & (1u << i)) && (b & (1u << j))) ||
          ((a & (1u << j)) && (b & (1u << i))))
        *contacts |= 1u << (i * C_MAX_GROUPS + j);
}
void c_life_evolve(const unsigned char before[C_WORLD_CELLS],
                   unsigned char after[C_WORLD_CELLS], unsigned *contacts) {
  int x, y, dx, dy;
  *contacts = 0;
  for (y = 0; y < (int)C_WORLD_SIDE; y++)
    for (x = 0; x < (int)C_WORLD_SIDE; x++) {
      unsigned n = 0, claims = 0;
      size_t at = cell(x, y);
      unsigned own = before[at];
      for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
          if (dx || dy) {
            unsigned other = before[cell(x + dx, y + dy)];
            if (other) {
              n++;
              claims |= other;
              if (own)
                register_pair(own, other, contacts);
            }
          }
      /* Birth claims are the physical parent union, never a task label. */
      after[at] = (unsigned char)(own ? ((n == 2 || n == 3) ? own : 0)
                                      : (n == 3 ? claims : 0));
    }
}
void c_life_seed(c_trainer *trainer) {
  unsigned g;
  int origin = (int)(c_random(&trainer->rng) % C_WORLD_SIDE);
  memset(trainer->cells, 0, sizeof(trainer->cells));
  /* Adjacent colored 2x2 blocks guarantee a real initial encounter. One
   * orientation/translation is sampled from owned RNG, independent of tasks. */
  for (g = 0; g < trainer->model->groups; g++) {
    int x = origin + (int)(2 * g), y = origin;
    trainer->cells[cell(x, y)] = (unsigned char)(1u << g);
    trainer->cells[cell(x + 1, y)] = (unsigned char)(1u << g);
    trainer->cells[cell(x, y + 1)] = (unsigned char)(1u << g);
    trainer->cells[cell(x + 1, y + 1)] = (unsigned char)(1u << g);
  }
}
