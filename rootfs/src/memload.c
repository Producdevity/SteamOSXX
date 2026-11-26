// Synthetic game-like memory load for comparing swap/zram settings without launching a Steam game.
//   memload <MB> <hot%> <seconds>
// Allocates MB of anonymous memory: ~35 % of the pages random (incompressible, like textures and audio), the rest
// patterned (compressible, like heap and code). Then it repeatedly touches the hot% of the pages that make up the
// working set for <seconds>. Under memory pressure the cold pages swap out and the hot ones must stay resident, so the
// number of touch passes per second says how smooth the load stays. Prints: touches_per_s=<n> passes=<n>.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <time.h>

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: memload MB hot%% seconds\n"); return 2; }
  size_t mb = strtoul(argv[1], NULL, 10), hot_pct = strtoul(argv[2], NULL, 10);
  double secs = atof(argv[3]);
  const size_t page = 4096, pages = mb * 1048576 / page;
  uint8_t *mem = mmap(NULL, pages * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) { perror("mmap"); return 1; }
  for (size_t p = 0; p < pages; p++) {
    uint8_t *pg = mem + p * page;
    if (p % 20 < 7) {  // 35 % random
      if (getrandom(pg, page, 0) < 0) memset(pg, (int)p, page);
    } else {  // patterned: repeating 64-bit words that vary slowly
      uint64_t *w = (uint64_t *)pg;
      for (size_t i = 0; i < page / 8; i++) w[i] = (p << 8) ^ (i % 16);
    }
  }
  size_t hot = pages * hot_pct / 100 ? pages * hot_pct / 100 : 1;
  unsigned long passes = 0, touches = 0, sum = 0;
  double start = now();
  while (now() - start < secs) {
    for (size_t p = 0; p < hot; p++) {
      volatile uint8_t *pg = mem + p * page;
      pg[64] = (uint8_t)(pg[64] + 1);
      sum += pg[128];
      touches++;
    }
    passes++;
  }
  double el = now() - start;
  printf("touches_per_s=%.0f passes=%lu hot_pages=%zu sum=%lu\n", touches / el, passes, hot, sum & 1);
  return 0;
}
