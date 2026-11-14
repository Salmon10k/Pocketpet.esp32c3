// PC preview of the pet's music reactions: the REAL face code (pet_face.cpp) and pose / decorations (pet_music.cpp)
// drawn into a U8g2 frame buffer, one raw 1024-byte frame per sample. See build.sh.
#include <Arduino.h>
#include <U8g2lib.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../../src/pet_face.h"
#include "../../src/pet_music.h"
#include "../../src/vibe.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
static std::string OUTDIR = "/tmp/pocketpet-petpreview";
static FILE *openOut(const char *n) { std::string p = OUTDIR + "/" + n; FILE *f = fopen(p.c_str(), "wb"); if (!f) { perror(p.c_str()); exit(1); } return f; }

// What drawPet() in main.cpp does for a dancing pet (no blinking / wandering pupils here).
static void drawDancing(const vibe::Music &m, uint32_t now) {
  using namespace petface;
  display.clearBuffer();
  petmusic::Pose p = petmusic::pose(m, now);
  int cy = EYE_Y + p.dy;
  drawEye(display, EYE_L_X + p.dx, cy, -1, p.eye, p.open, p.pupil, p.lid, p.slant, p.lower, 0, p.lookY, now);
  drawEye(display, EYE_R_X + p.dx, cy, +1, p.eye, p.open, p.pupil, p.lid, p.slant, p.lower, 0, p.lookY, now);
  drawMouth(display, p.mouth, MOUTH_Y + p.dy, now, p.dx);
  petmusic::decor(display, m, p, cy, now);
}

static vibe::Music make(vibe::Vibe v, int pace10, uint32_t pos, bool singing, int word, bool guessed) {
  vibe::Music m = {};
  m.active = true; m.vibe = v; m.guessed = guessed;
  m.energy = (uint8_t)vibe::energyFor(pace10);
  m.beatMs = (uint16_t)(60000 / vibe::bpmFor(v, pace10));
  m.positionMs = pos; m.singing = singing; m.word = (int16_t)word; m.trackSeq = 1;
  return m;
}

int main(int argc, char **argv) {
  if (argc > 1) OUTDIR = argv[1];
  display.begin();
  const int paces[vibe::V_COUNT] = {35, 20, 38, 60, 40, 45, 38, 28};      // typical lyric speed per vibe (x10)
  // 1) the label that shows for a moment when the vibe is known (first frame after a change), "?" = guessed
  {
    FILE *f = openOut("labels.bin");
    for (int v = 0; v < vibe::V_COUNT; ++v) {
      drawDancing(make((vibe::Vibe)v, paces[v], 20000, false, -1, v >= 6), 100000 + v * 10000);
      fwrite(display.getBufferPtr(), 1, 1024, f);
    }
    fclose(f);
  }
  // 2) the dance: 8 samples over two beats per vibe, singing (mouth flaps per word), label already gone
  for (int v = 0; v < vibe::V_COUNT; ++v) {
    char name[32]; snprintf(name, sizeof(name), "dance_%d.bin", v);
    FILE *f = openOut(name);
    uint32_t t0 = 200000 + v * 50000;
    vibe::Music first = make((vibe::Vibe)v, paces[v], 40000, false, -1, false);
    drawDancing(first, t0);                                              // sets the label state, then let it expire
    for (int k = 0; k < 8; ++k) {
      vibe::Music m = make((vibe::Vibe)v, paces[v], 40000 + (uint32_t)k * (first.beatMs / 4), true, k / 2, false);
      drawDancing(m, t0 + 5000 + (uint32_t)k * (first.beatMs / 4));
      fwrite(display.getBufferPtr(), 1, 1024, f);
    }
    fclose(f);
    printf("%s: %d bpm feel, beat %u ms\n", vibe::name((vibe::Vibe)v)[0] ? vibe::name((vibe::Vibe)v) : "none", 60000 / first.beatMs, first.beatMs);
  }
  // 3) EDM strobe: a frame inside the flash window (first 45 ms of beat 0 of a bar) and one outside
  {
    FILE *f = openOut("edm_flash.bin");
    vibe::Music e = make(vibe::V_EDM, 45, 0, true, 1, false);
    drawDancing(e, 300000); drawDancing(e, 300000);
    for (uint32_t off : {10u, 200u}) {
      e.positionMs = (uint32_t)e.beatMs * 8 + off;                       // beat index multiple of 4 -> bar 0
      drawDancing(e, 306000 + off);
      fwrite(display.getBufferPtr(), 1, 1024, f);
    }
    fclose(f);
  }
  // 4) torture: every vibe, odd positions / beats / words, a long run; run with -fsanitize to catch out-of-range drawing
  {
    uint32_t seed = 12345;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (int i = 0; i < 20000; ++i) {
      vibe::Music m = make((vibe::Vibe)(rnd() % vibe::V_COUNT), (int)(rnd() % 120), rnd() % 400000, rnd() % 2, (int)(rnd() % 30) - 1, rnd() % 2);
      if (rnd() % 50 == 0) m.beatMs = 0;                                  // unknown beat must not divide by zero
      drawDancing(m, 400000 + (uint32_t)i * 40);
    }
    printf("torture: 20000 random frames ok\n");
  }
  return 0;
}
