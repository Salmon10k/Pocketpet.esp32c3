// Host test for src/fluid.cpp: runs scripted tilt scenarios and dumps 1-bit frames
// (1024 bytes each, SSD1306 page layout) to a binary file that render.py turns into PNGs.
//   g++ -O2 -std=gnu++17 -I../../src fluidtest.cpp ../../src/fluid.cpp -o fluidtest && ./fluidtest out.bin
#include "fluid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <chrono>

static void dump(FILE *f) {
  static uint8_t buf[1024];
  memset(buf, 0, sizeof(buf));
  fluid::render(buf);
  fwrite(buf, 1, sizeof(buf), f);
}

int main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : "frames.bin";
  int preset = argc > 2 ? atoi(argv[2]) : 0;
  FILE *f = fopen(out, "wb");
  fluid::init(preset);
  int frame = 0;
  auto run = [&](int n, double gx, double gy, bool snap, const char *label, int snapEvery) {
    for (int i = 0; i < n; ++i, ++frame) {
      fluid::step((int32_t)(gx * 256), (int32_t)(gy * 256));
      if (snap && (i % snapEvery == snapEvery - 1)) { dump(f); printf("frame %d: %s step %d\n", frame, label, i + 1); }
    }
  };
  auto t0 = std::chrono::steady_clock::now();
  dump(f); printf("frame 0: initial\n");
  run(60, 0, 1.0, true, "gravity down", 30);
  run(60, 0, 1.0, true, "settled", 60);
  {
    // Surface roughness: top pixel row per column over the middle of the screen.
    static uint8_t b[1024]; memset(b, 0, sizeof(b)); fluid::render(b);
    int lo = 99, hi = -1; double sum = 0; int cnt = 0;
    for (int x = 10; x < 118; ++x) {
      int top = -1;
      for (int y = 0; y < 64 && top < 0; ++y) if (b[(y >> 3) * 128 + x] >> (y & 7) & 1) top = y;
      if (top < 0) continue;
      if (top < lo) lo = top; if (top > hi) hi = top; sum += top; ++cnt;
    }
    printf("SURFACE after settle: top y min=%d max=%d spread=%d mean=%.1f\n", lo, hi, hi - lo, cnt ? sum / cnt : 0.0);
  }
  run(12, 0.8, 0.6, true, "tilt right (early)", 6);
  run(40, 0.8, 0.6, true, "tilt right (late)", 40);
  run(30, -0.7, 0.7, true, "tilt left", 15);
  run(20, 0, -1.0, true, "upside down", 10);
  run(60, 0, -1.0, true, "upside down late", 60);
  run(40, 0, 1.0, true, "back to normal", 10);
  // Shake: random gravity jitter.
  srand(7);
  for (int i = 0; i < 25; ++i, ++frame) {
    double gx = (rand() % 2001 - 1000) / 1000.0 * 1.4;
    double gy = 1.0 + (rand() % 2001 - 1000) / 1000.0 * 1.2;
    fluid::step((int32_t)(gx * 256), (int32_t)(gy * 256));
    if (i % 12 == 11) { dump(f); printf("frame %d: shaking\n", frame); }
  }
  run(80, 0, 1.0, true, "calm after shake", 80);
  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  printf("host time per frame: %.3f ms (%d frames) -- the C3 is far slower, use only for relative cost\n", ms / frame, frame);
  fclose(f);
  return 0;
}
