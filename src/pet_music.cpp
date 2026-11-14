#include "pet_music.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace petmusic {
namespace {

const float TWO_PI_F = 6.2831853f;

// ---------------------------------------------------------------- small drawings
void note(U8G2 &d, int x, int y) {                          // an eighth note, head at (x, y)
  gfx::disc(d, x, y, 2);
  gfx::vl(d, x + 2, y - 8, 9);
  gfx::line(d, x + 2, y - 8, x + 5, y - 5);
  gfx::px(d, x + 5, y - 4);
}

void heart(U8G2 &d, int x, int y) {
  gfx::disc(d, x - 2, y, 2);
  gfx::disc(d, x + 2, y, 2);
  d.drawTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 6);
}

void spark(U8G2 &d, int x, int y, int s) {                  // four-point twinkle
  gfx::hl(d, x - s, y, 2 * s + 1);
  gfx::vl(d, x, y - s, 2 * s + 1);
  if (s >= 2) { gfx::px(d, x - 1, y - 1); gfx::px(d, x + 1, y - 1); gfx::px(d, x - 1, y + 1); gfx::px(d, x + 1, y + 1); }
}

void bolt(U8G2 &d, int x, int y) {                          // lightning, 2 px thick
  for (int t = 0; t < 2; t++) {
    gfx::line(d, x + 4 + t, y, x + t, y + 9);
    gfx::line(d, x + t, y + 9, x + 5 + t, y + 9);
    gfx::line(d, x + 5 + t, y + 9, x + 1 + t, y + 20);
  }
}

// Things that drift up the left and right edges (the face is in the middle): notes or hearts. speed = ms per pixel.
void floaters(U8G2 &d, uint32_t now, int speed, bool hearts, bool narrow = false) {
  static const int8_t NX[3] = {9, 118, 16};
  static const int8_t NXN[3] = {16, 108, 19};                  // narrow lane: between the headphone cups and the face
  for (int i = 0; i < 3; i++) {
    int t = (int)((now / (uint32_t)speed + (uint32_t)i * 21) % 64);
    int y = 62 - t;
    int x = (narrow ? NXN[i] : NX[i]) + gfx::sn((int)(now / 14) + i * 70) * (narrow ? 2 : 3) / 127;
    if (y < 13 || y > 60) continue;                           // keep clear of the weather badge in the top right corner
    if (hearts) heart(d, x, y); else note(d, x, y);
  }
}

void twinkles(U8G2 &d, uint32_t now, bool beatPop, float hit) {
  static const int8_t SX[6] = {10, 118, 20, 108, 6, 122};
  static const int8_t SY[6] = {15, 24, 48, 46, 34, 40};
  for (int i = 0; i < 6; i++) {
    int s = (gfx::sn((int)(now / 6) + i * 50) + 127) * 3 / 254;      // 0..3 over time
    if (beatPop) s = 1 + (int)(hit * 2.0f);
    if (s > 0) spark(d, SX[i], SY[i], s);
  }
}

void headphones(U8G2 &d, int dy) {
  for (int r = 56; r <= 58; r++) {
    gfx::arc(d, 64, 36 + dy, r, 128, 156);                    // the band leaves the screen at the top
    gfx::arc(d, 64, 36 + dy, r, 228, 256);
  }
  gfx::rbox(d, 1, 19 + dy, 9, 24, 3);                         // ear cups
  gfx::rbox(d, 118, 19 + dy, 9, 24, 3);
}

void shades(U8G2 &d, int dx, int cy) {
  int xl = petface::EYE_L_X + dx, xr = petface::EYE_R_X + dx;
  d.setDrawColor(0);                                          // wipe the eyes, then draw lenses
  gfx::box(d, xl - 17, cy - 17, 34, 34);
  gfx::box(d, xr - 17, cy - 17, 34, 34);
  d.setDrawColor(1);
  gfx::rbox(d, xl - 16, cy - 8, 32, 17, 5);
  gfx::rbox(d, xr - 16, cy - 8, 32, 17, 5);
  gfx::box(d, xl + 15, cy - 6, xr - xl - 30, 3);              // bridge
  gfx::line(d, xl - 16, cy - 5, xl - 24, cy - 9);             // arms
  gfx::line(d, xr + 16, cy - 5, xr + 24, cy - 9);
  d.setDrawColor(0);                                          // glints
  for (int k = 0; k < 2; k++) {
    int x = k ? xr : xl;
    gfx::line(d, x - 10, cy + 6, x, cy - 6);
    gfx::line(d, x - 6, cy + 6, x + 4, cy - 6);
  }
  d.setDrawColor(1);
}

void equalizer(U8G2 &d, uint32_t pos, uint32_t beat, float hit) {
  uint32_t quarter = beat / 4 ? beat / 4 : 1;
  uint32_t step = pos / quarter;                               // new bar heights four times per beat
  for (int i = 0; i < 21; i++) {
    uint32_t h = ((uint32_t)i * 2654435761u) ^ (step * 40503u);
    int bar = 1 + (int)((h >> 9) % 5) + (hit > 0.3f ? 1 : 0);
    if (bar > 6) bar = 6;
    gfx::box(d, i * 6 + 1, 64 - bar, 4, bar);
  }
}

// The vibe name for a moment when it is first known or changes. "name?" = a guess, no genre data.
void label(U8G2 &d, const vibe::Music &m, uint32_t now) {
  static uint32_t until = 0;
  static vibe::Vibe seen = vibe::V_NONE;
  static bool seenGuess = false;
  static uint32_t seenSeq = 0;
  if (m.vibe != seen || m.guessed != seenGuess || m.trackSeq != seenSeq) {
    seen = m.vibe; seenGuess = m.guessed; seenSeq = m.trackSeq;
    until = now + 1800;
  }
  if (m.vibe == vibe::V_NONE || (int32_t)(now - until) >= 0) return;
  char buf[12];
  snprintf(buf, sizeof(buf), "%s%s", vibe::name(m.vibe), m.guessed ? "?" : "");
  d.setFont(u8g2_font_5x7_tf);
  int w = d.getStrWidth(buf);
  int x = (128 - w) / 2;
  d.setDrawColor(0);
  gfx::box(d, x - 3, 0, w + 6, 9);
  d.setDrawColor(1);
  d.drawStr(x, 7, buf);
}

}  // namespace

Pose pose(const vibe::Music &m, uint32_t now) {
  (void)now;
  using namespace petface;
  Pose p = {};
  p.eye = EY_NORMAL; p.open = 1.0f; p.pupil = 7; p.lid = 0; p.slant = 0; p.lower = 0; p.lookY = 0; p.mouth = M_SMILE;

  uint32_t beat = m.beatMs ? m.beatMs : 600;
  uint32_t pos = m.positionMs;
  float ph = (float)(pos % beat) / (float)beat;               // 0 = on the beat
  float hit = 1.0f - ph * 2.2f;                               // 1 on the beat, 0 after 45 % of it
  if (hit < 0) hit = 0;
  int bar = (int)((pos / beat) % 4);
  float sway = sinf((float)pos * TWO_PI_F / (float)(beat * 4));   // one slow swing per 4 beats
  float slow = sinf((float)pos * TWO_PI_F / (float)(beat * 2));
  float amp = 0.6f + (float)m.energy * 0.008f;                // 0.6 .. 1.4: livelier songs move more
  bool flap = m.singing && m.word >= 0 && (m.word & 1);       // the mouth opens on every other word
  auto bounce = [&](float px) { return (int)lroundf(hit * px * amp); };

  switch (m.vibe) {
    case vibe::V_CHILL:                                       // eyes closed, headphones, slow sway
      p.eye = EY_CLOSED;
      p.mouth = flap ? M_O : M_SMILE;
      p.dx = (int8_t)lroundf(sway * 4);
      p.dy = (int8_t)lroundf(slow * 1.5f);
      p.headphones = true;
      break;
    case vibe::V_POP:                                         // happy eyes, bouncing
      p.eye = EY_ARC;
      p.mouth = (flap || hit > 0.6f) ? M_BIG : M_CAT;
      p.dy = (int8_t)-bounce(4);
      p.dx = (int8_t)lroundf(sway * 2);
      break;
    case vibe::V_HYPE:                                        // shades on, head nods on the beat
      p.open = 0.9f; p.pupil = 6; p.lid = 0.3f; p.slant = 0.25f;
      p.mouth = flap ? M_O : M_FLAT;
      p.dy = (int8_t)bounce(5);
      p.shades = true;
      break;
    case vibe::V_ROCK:                                        // angry eyes, headbanging
      p.pupil = 5; p.lid = 0.1f; p.slant = 0.9f;
      p.mouth = (flap || hit > 0.25f) ? M_BIG : M_FLAT;
      p.dy = (int8_t)bounce(6);
      p.dx = (int8_t)(((bar & 1) ? 1 : -1) * (int)lroundf(hit * 3 * amp));
      break;
    case vibe::V_EDM:                                         // pupils pulse, strobe on the first beat of a bar
      p.open = 1.15f; p.pupil = 3 + hit * 5;
      p.mouth = M_BIG;
      p.dy = (int8_t)-bounce(3);
      p.flash = (bar == 0 && (pos % beat) < 45);
      break;
    case vibe::V_DESI:                                        // dreamy happy eyes, wide swaying
      p.eye = EY_ARC;
      p.mouth = flap ? M_O : M_SMILE;
      p.dx = (int8_t)lroundf(sway * 6);
      p.dy = (int8_t)lroundf(slow * 2);
      break;
    case vibe::V_DREAMY:                                      // big starry-eyed pupils, slow sway
      p.open = 1.0f; p.pupil = 10; p.lid = 0; p.lookY = 0;
      p.mouth = flap ? M_O : M_SMILE;
      p.dx = (int8_t)lroundf(sway * 3);
      p.dy = (int8_t)lroundf(slow * 1.5f);
      break;
    default:                                                  // no idea what it is: a happy little bob
      p.mouth = flap ? M_BIG : M_SMILE;
      p.dy = (int8_t)-bounce(2);
      break;
  }
  return p;
}

void decor(U8G2 &d, const vibe::Music &m, const Pose &p, int eyeCy, uint32_t now) {
  uint32_t beat = m.beatMs ? m.beatMs : 600;
  uint32_t pos = m.positionMs;
  float hit = 1.0f - ((float)(pos % beat) / (float)beat) * 2.2f;
  if (hit < 0) hit = 0;
  int bar = (int)((pos / beat) % 4);
  d.setDrawColor(1);

  if (p.headphones) headphones(d, p.dy);
  if (p.shades) shades(d, p.dx, eyeCy);

  switch (m.vibe) {
    case vibe::V_CHILL:  floaters(d, now, 60, false, true); break;
    case vibe::V_POP:    floaters(d, now, 34, false); twinkles(d, now, true, hit); break;
    case vibe::V_HYPE:                                        // impact lines beside the head on the beat
      if (hit > 0.5f) {
        gfx::hl(d, 4, 22 + p.dy, 9); gfx::hl(d, 6, 27 + p.dy, 7);
        gfx::hl(d, 115, 22 + p.dy, 9); gfx::hl(d, 115, 27 + p.dy, 7);
      }
      break;
    case vibe::V_ROCK:
      if (hit > 0.15f) bolt(d, (bar & 1) ? 112 : 8, 14);
      break;
    case vibe::V_EDM:    equalizer(d, pos, beat, hit); break;
    case vibe::V_DESI:   floaters(d, now, 52, true); twinkles(d, now, false, 0); break;
    case vibe::V_DREAMY: twinkles(d, now, false, 0); break;
    default:             floaters(d, now, 46, false); break;
  }

  label(d, m, now);
  if (p.flash) {                                              // last: invert everything for an instant
    d.setDrawColor(2);
    d.drawBox(0, 0, 128, 64);
    d.setDrawColor(1);
  }
}

}  // namespace petmusic
