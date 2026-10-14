#include "lyrics_visual.h"
#include "spotify.h"
#include "arabic_shape.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Lyrics visual: one word at a time in an animated box over a procedural scene.
// Every new word rolls its own "look" from a hash of the word key: scene, box entry animation, box
// decoration, colour flipping (inverted box / strobe / scan bar / negative scene), letter effect and a
// screen transition. All of it is drawn live from millis(), nothing is stored per frame.

namespace lyrics_visual {
namespace {

const uint32_t ENTRY_MS = 240;     // box entry animation
const uint32_t TRANS_MS = 190;     // screen transition (reveals the scene)

int lastWordKey = -1;
int lastScene = -1;
uint32_t transitionAt = 0;
int gKick = 0;                      // 0..256, decays after every new word; scenes use it as a "beat"

struct Look {
  int scene;
  uint32_t seed;
  int style;     // 0 plain, 1 marching ants, 2 breathing brackets, 3 drop shadow, 4 rounded + orbiting dot, 5 ruler ticks + chevrons
  int entry;     // 0 CRT grow, 1 drop + bounce, 2 slide, 3 card flip, 4 zoom kick, 5 shake
  int trans;     // 0 none, 1 blinds, 2 shutter, 3 curtains, 4 scanline dissolve
  int flip;      // 0 normal, 1 inverted box, 2 strobe then pulses, 3 scan bar
  int neg;       // 0 positive scene, 1 negative scene, 2 flickering positive/negative
  int fx;        // letter effect: 0 none, 1 pop-in, 2 wave, 3 jitter
  bool big;      // try the large font for short words
  bool flash;    // whole-screen flash on the new word
} look = {0, 0, 0, 0, 0, 0, 0, 0, false, false};

uint32_t mix(uint32_t k, uint32_t salt) {
  uint32_t x = k * 2654435761U + salt * 40503U + 0x9E3779B9U;
  x ^= x >> 13; x *= 1274126177U; x ^= x >> 16;
  return x;
}

struct WordFrame {
  bool rtl;
  char word[40];
  int index;
  int count;
  int key;
};

void copyWord(const char *src, size_t n, char *dst, size_t cap) {
  if (cap == 0) return;
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

WordFrame selectWord(const spotify::LyricsView &v) {
  WordFrame out = {false, {0}, 0, 0, -1};
  if (!v.lyricsReady || !v.line[0]) return out;
  const char *text = v.line;
  if (text[0] == 0x1B) { out.rtl = true; text++; }          // Urdu / Arabic line: raw UTF-8, shaped when the word is drawn

  int starts[24], lengths[24], count = 0;
  const char *p = text;
  while (*p && count < 24) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ' ') ++p;
    starts[count] = (int)(start - text);
    lengths[count] = (int)(p - start);
    ++count;
  }
  if (!count) return out;

  // which word is being sung comes from spotify::getLyricsView(): real Enhanced-LRC word times when the
  // lyrics have them, otherwise a syllable-based estimate with the last word held until the line ends
  int index = v.focusWord < 0 ? 0 : (v.focusWord >= count ? count - 1 : v.focusWord);
  copyWord(text + starts[index], (size_t)lengths[index], out.word, sizeof(out.word));
  out.index = index;
  out.count = count;
  out.key = (int)(v.lineStartMs / 10U) * 23 + index;
  return out;
}

// ---- tiny trig (256 steps per turn, results -127..127) so scenes do not need float math per frame
int8_t sinTab[256];
bool sinReady = false;
inline int sn(int a) {
  if (!sinReady) {
    for (int i = 0; i < 256; i++) sinTab[i] = (int8_t)(sinf(i * 6.2831853f / 256.0f) * 127.0f);
    sinReady = true;
  }
  return sinTab[a & 255];
}
inline int cs(int a) { return sn(a + 64); }
inline uint32_t rnd(uint32_t &s) { s = s * 1664525UL + 1013904223UL; return s >> 16; }

// U8g2 coordinates are unsigned 8-bit, so a line with an end point off the screen is drawn wrongly.
// This clips the line to the 128x64 screen first (Liang-Barsky) and only then draws it.
void ln(U8G2 &d, int x0, int y0, int x1, int y1) {
  float t0 = 0.0f, t1 = 1.0f;
  float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  const float p[4] = {-dx, dx, -dy, dy};
  const float q[4] = {(float)x0, (float)(127 - x0), (float)y0, (float)(63 - y0)};
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0.0f) { if (q[i] < 0.0f) return; continue; }
    float r = q[i] / p[i];
    if (p[i] < 0.0f) { if (r > t1) return; if (r > t0) t0 = r; }
    else { if (r < t0) return; if (r < t1) t1 = r; }
  }
  d.drawLine((int)(x0 + t0 * dx + 0.5f), (int)(y0 + t0 * dy + 0.5f), (int)(x0 + t1 * dx + 0.5f), (int)(y0 + t1 * dy + 0.5f));
}

// ---- clipped primitives: U8g2 coordinates are unsigned 8-bit, so anything that can leave the screen goes through these
inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
void px(U8G2 &d, int x, int y) { if ((unsigned)x < 128U && (unsigned)y < 64U) d.drawPixel(x, y); }
void hl(U8G2 &d, int x, int y, int w) {
  if (y < 0 || y > 63 || w <= 0) return;
  int x1 = imin(x + w, 128); x = imax(x, 0);
  if (x1 > x) d.drawHLine(x, y, x1 - x);
}
void vl(U8G2 &d, int x, int y, int h) {
  if (x < 0 || x > 127 || h <= 0) return;
  int y1 = imin(y + h, 64); y = imax(y, 0);
  if (y1 > y) d.drawVLine(x, y, y1 - y);
}
void bx(U8G2 &d, int x, int y, int w, int h) {
  int x1 = imin(x + w, 128), y1 = imin(y + h, 64);
  x = imax(x, 0); y = imax(y, 0);
  if (x1 > x && y1 > y) d.drawBox(x, y, x1 - x, y1 - y);
}
void fr(U8G2 &d, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  if (w < 3 || h < 3) { bx(d, x, y, w, h); return; }
  hl(d, x, y, w); hl(d, x, y + h - 1, w); vl(d, x, y, h); vl(d, x + w - 1, y, h);
}
// point on a rectangle's perimeter, pos = 0 .. 2(w-1)+2(h-1)-1, clockwise from the top-left corner
void perim(int x, int y, int w, int h, int pos, int &ox, int &oy) {
  int P = 2 * (w - 1) + 2 * (h - 1);
  pos %= P; if (pos < 0) pos += P;
  if (pos < w - 1) { ox = x + pos; oy = y; return; }
  pos -= w - 1;
  if (pos < h - 1) { ox = x + w - 1; oy = y + pos; return; }
  pos -= h - 1;
  if (pos < w - 1) { ox = x + w - 1 - pos; oy = y + h - 1; return; }
  pos -= w - 1;
  ox = x; oy = y + h - 1 - pos;
}

// ---- scenes (inspired by the reel): each one fills the screen behind the word box --------------------

void sceneWaves(U8G2 &d, uint32_t now, uint32_t) {          // crossing sine waves with dotted centre lines
  for (int w = 0; w < 3; w++) {
    int amp = 6 + w * 3, yc = 32 + (w - 1) * 7;
    int px = 0, py = yc;
    for (int x = 0; x < 128; x += 2) {
      int y = yc + sn(x * (3 + w) + (int)(now / (7 + w * 3))) * amp / 127;
      if (x) ln(d, px, py, x, y);
      px = x; py = y;
    }
    for (int x = (w * 3) % 6; x < 128; x += 6) d.drawPixel(x, yc);
  }
}

void sceneRain(U8G2 &d, uint32_t now, uint32_t) {           // falling dashed streaks
  for (int col = 0; col < 16; col++) {
    int x = 4 + col * 8;
    int speed = 45 + (col * 37) % 55;
    int len = 8 + (col * 13) % 14;
    int head = (int)((now * speed / 1000 + col * 29) % (uint32_t)(64 + len));
    for (int i = 0; i < len; i += 2) {
      int y = head - i;
      if (y < 0 || y > 63) continue;
      d.drawPixel(x, y);
      if (i % 4 == 0) d.drawPixel(x + 1, y);
    }
  }
}

void sceneSynth(U8G2 &d, uint32_t now, uint32_t) {          // sun, mountains and a scrolling perspective grid
  const int hz = 48;
  d.drawDisc(64, hz, 24, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  d.setDrawColor(0);
  for (int k = 0; k < 4; k++) d.drawHLine(36, hz - 4 - k * 6 - (int)((now / 90) % 6) / 2, 56);
  d.setDrawColor(1);
  static const int8_t mx[] = {0, 10, 18, 27, 36};
  static const int8_t my[] = {0, 11, 5, 14, 2};
  for (int i = 0; i + 1 < 5; i++) {
    ln(d, mx[i], hz - my[i], mx[i + 1], hz - my[i + 1]);
    ln(d, 127 - mx[i], hz - my[i], 127 - mx[i + 1], hz - my[i + 1]);
  }
  d.drawHLine(0, hz, 128);
  for (int i = -6; i <= 6; i++) ln(d, 64 + i * 2, hz, 64 + i * 24, 63);
  for (int k = 0; k < 5; k++) {
    int t = (int)((now / 45 + k * 20) % 100);
    d.drawHLine(0, hz + 1 + (t * t / 100) * (63 - hz - 1) / 100, 128);
  }
}

void sceneRadar(U8G2 &d, uint32_t now, uint32_t) {          // arcs, crosshair and an equalizer along the bottom
  int pulse = (int)((now / 160) % 4);
  d.drawCircle(64, 30, 27 + pulse, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  d.drawCircle(64, 30, 36 + pulse, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  d.drawHLine(2, 30, 26);
  d.drawHLine(100, 30, 26);
  d.drawVLine(64, 0, 8);
  for (int i = 0; i < 25; i++) {
    int v = sn(i * 23 + (int)(now / 6)) + sn(i * 11 - (int)(now / 9));
    int h = 3 + (v + 254) * 11 / 508;
    d.drawBox(2 + i * 5, 63 - h, 4, h);
  }
}

void sceneVortex(U8G2 &d, uint32_t now, uint32_t) {         // swirling broken spiral arms
  for (int arm = 0; arm < 8; arm++) {
    int px = 64, py = 32;
    for (int st = 0; st < 18; st++) {
      int r = 5 + st * 3;
      int a = arm * 32 + st * 9 + (int)(now / 12);
      int x = 64 + cs(a) * r * 3 / 254;
      int y = 32 + sn(a) * r * 3 / 330;
      if (st % 4 != 3) ln(d, px, py, x, y);
      px = x; py = y;
    }
  }
}

void scenePoly(U8G2 &d, uint32_t now, uint32_t seed) {      // spinning hexagram / square / scribble web
  int a = (int)(now / 11);
  int variant = (int)(seed % 3);
  if (variant == 0) {
    for (int t = 0; t < 2; t++) {
      int vx[3], vy[3];
      for (int k = 0; k < 3; k++) {
        int ang = (t ? -a : a) + k * 85 + t * 43;
        vx[k] = 64 + cs(ang) * 36 / 127;
        vy[k] = 32 + sn(ang) * 29 / 127;
      }
      for (int k = 0; k < 3; k++) ln(d, vx[k], vy[k], vx[(k + 1) % 3], vy[(k + 1) % 3]);
    }
  } else if (variant == 1) {
    for (int t = 0; t < 2; t++) {
      int vx[4], vy[4], r = t ? 22 : 33;
      for (int k = 0; k < 4; k++) {
        int ang = (t ? -a : a) + k * 64;
        vx[k] = 64 + cs(ang) * r * 3 / 254;
        vy[k] = 32 + sn(ang) * r / 127;
      }
      for (int k = 0; k < 4; k++) ln(d, vx[k], vy[k], vx[(k + 1) % 4], vy[(k + 1) % 4]);
    }
  } else {
    uint32_t s = seed * 7919U + (uint32_t)(now / 140) * 31U;       // a new tangle every ~140 ms
    int px = 64 + (int)(rnd(s) % 60) - 30, py = (int)(rnd(s) % 64);
    for (int i = 0; i < 9; i++) {
      int x = 64 + (int)(rnd(s) % 120) - 60, y = (int)(rnd(s) % 64);
      ln(d, px, py, x, y);
      px = x; py = y;
    }
  }
}

void sceneTicks(U8G2 &d, uint32_t now, uint32_t seed) {     // calm: flickering chevrons and dashes
  uint32_t s = seed * 104729U + (uint32_t)(now / 90) * 17U;
  for (int i = 0; i < 7; i++) {
    int x = (int)(rnd(s) % 120) + 4, y = (int)(rnd(s) % 56) + 4;
    switch (rnd(s) % 3) {
      case 0: ln(d, x - 3, y - 2, x, y); ln(d, x, y, x + 3, y - 2); break;
      case 1: d.drawHLine(x - 3, y, 7); break;
      default: d.drawPixel(x, y); d.drawPixel(x + 2, y + 1); break;
    }
  }
}

// The inverted "glitch" scene: white screen, cracks and a sliding XOR band; the box is drawn black on top.
void sceneGlitch(U8G2 &d, uint32_t now, uint32_t seed) {
  d.setDrawColor(1);
  d.drawBox(0, 0, 128, 64);
  d.setDrawColor(0);
  uint32_t s = seed * 2654435761U;
  for (int c = 0; c < 3; c++) {
    int x = (int)(rnd(s) % 128), y = (int)(rnd(s) % 64);
    for (int k = 0; k < 7; k++) {
      int nx = x + (int)(rnd(s) % 19) - 9, ny = y + (int)(rnd(s) % 15) - 4;
      ln(d, x, y, nx, ny);
      x = nx; y = ny;
    }
  }
  d.setDrawColor(2);
  d.drawBox(0, (int)((now / 37 * 13) % 64), 128, 2 + (int)((now / 90) & 3));
  d.setDrawColor(1);
}

void sceneStars(U8G2 &d, uint32_t now, uint32_t seed) {      // warp-speed starfield streaming out of the centre
  for (int i = 0; i < 26; i++) {
    int ang = (i * 47 + (int)(seed & 255U)) & 255;
    int speed = 40 + (i * 37) % 50;
    int phase = (int)(((uint64_t)now * (uint32_t)speed / 100 + (uint32_t)i * 97) % 1000);
    int r0 = phase * phase / 14000;
    int r1 = r0 + 1 + r0 / 7;
    ln(d, 64 + cs(ang) * r0 / 127, 32 + sn(ang) * r0 / 254, 64 + cs(ang) * r1 / 127, 32 + sn(ang) * r1 / 254);
  }
}

void sceneTunnel(U8G2 &d, uint32_t now, uint32_t) {          // rectangles rushing towards you
  for (int k = 0; k < 6; k++) {
    int t = (int)((now / 12 + (uint32_t)k * 43) % 256);
    int z = t * t / 256;
    int hw = 3 + z * 68 / 256, hh = 2 + z * 36 / 256;
    fr(d, 64 - hw, 32 - hh, hw * 2, hh * 2);
  }
  ln(d, 61, 30, 0, 0); ln(d, 67, 30, 127, 0); ln(d, 61, 34, 0, 63); ln(d, 67, 34, 127, 63);
}

void sceneRipples(U8G2 &d, uint32_t now, uint32_t seed) {    // rings spreading from the centre and a second drop
  for (int k = 0; k < 4; k++) {
    int r = (int)((now / 24 + (uint32_t)k * 16) % 64);
    if (r > 1) d.drawCircle(64, 32, r);
  }
  int cx = 20 + (int)(seed % 88U), cy = 12 + (int)((seed >> 8) % 40U);
  for (int k = 0; k < 3; k++) {
    int r = (int)((now / 32 + (uint32_t)k * 17) % 48);
    if (r > 1) d.drawCircle(cx, cy, r);
  }
}

void sceneChecker(U8G2 &d, uint32_t now, uint32_t) {         // checkerboard rows sliding in opposite directions
  int scroll = (int)(now / 38);
  for (int r = 0; r < 8; r++) {
    int dir = (r & 1) ? 1 : -1;
    int sh = (((dir * scroll) % 16) + 16) % 16;
    if (r & 1) sh = (sh + 8) % 16;
    for (int c = 0; c <= 18; c += 2) bx(d, c * 8 + sh - 16, r * 8, 8, 8);
  }
}

void sceneSunburst(U8G2 &d, uint32_t now, uint32_t) {        // rotating rays
  int a0 = (int)(now / 14);
  for (int i = 0; i < 12; i++) {
    int a = a0 + i * 21;
    ln(d, 64, 32, 64 + cs(a) * 100 / 127, 32 + sn(a) * 100 / 127);
    ln(d, 64, 32, 64 + cs(a + 6) * 100 / 127, 32 + sn(a + 6) * 100 / 127);
  }
  d.drawCircle(64, 32, 5 + (int)((now / 90) & 3U));
}

void sceneDna(U8G2 &d, uint32_t now, uint32_t) {             // double helix with rungs
  int p1x = 0, p1y = 0, p2x = 0, p2y = 0;
  for (int x = 0; x < 128; x += 2) {
    int s = sn(x * 3 + (int)(now / 6));
    int y1 = 32 + s * 15 / 127, y2 = 32 - s * 15 / 127;
    if (x) { ln(d, p1x, p1y, x, y1); ln(d, p2x, p2y, x, y2); }
    if (x % 12 == 0) ln(d, x, y1, x, y2);
    p1x = p2x = x; p1y = y1; p2y = y2;
  }
}

void sceneSpectrum(U8G2 &d, uint32_t now, uint32_t) {        // mirrored equalizer bars that kick on every word
  for (int i = 0; i < 32; i++) {
    int v = sn(i * 29 + (int)(now / 8)) + sn(i * 13 - (int)(now / 11));
    int h = 2 + (v + 254) * 20 / 508 + gKick * 8 / 256;
    bx(d, i * 4, 32 - h, 3, h * 2);
  }
}

void sceneOrbits(U8G2 &d, uint32_t now, uint32_t) {          // dotted ellipses with planets
  for (int k = 0; k < 3; k++) {
    int rx = 18 + k * 14, ry = 8 + k * 6;
    int a = (int)(now / (uint32_t)(9 + k * 4)) * ((k & 1) ? -1 : 1) + k * 80;
    for (int q = 0; q < 256; q += 8) px(d, 64 + cs(q) * rx / 127, 32 + sn(q) * ry / 127);
    d.drawDisc(64 + cs(a) * rx / 127, 32 + sn(a) * ry / 127, k == 0 ? 3 : 2);
  }
}

const int SCENE_COUNT = 16;
bool sceneInverted(int sc) { return sc == 7; }

void drawScene(U8G2 &d, int sc, uint32_t now, uint32_t seed) {
  switch (sc) {
    case 0: sceneWaves(d, now, seed); break;
    case 1: sceneRain(d, now, seed); break;
    case 2: sceneSynth(d, now, seed); break;
    case 3: sceneRadar(d, now, seed); break;
    case 4: sceneVortex(d, now, seed); break;
    case 5: scenePoly(d, now, seed); break;
    case 6: sceneTicks(d, now, seed); break;
    case 7: sceneGlitch(d, now, seed); break;
    case 8: sceneStars(d, now, seed); break;
    case 9: sceneTunnel(d, now, seed); break;
    case 10: sceneRipples(d, now, seed); break;
    case 11: sceneChecker(d, now, seed); break;
    case 12: sceneSunburst(d, now, seed); break;
    case 13: sceneDna(d, now, seed); break;
    case 14: sceneSpectrum(d, now, seed); break;
    default: sceneOrbits(d, now, seed); break;
  }
}

void drawAmbient(U8G2 &d, uint32_t now) {                    // no lyrics yet: just a slow synthwave horizon
  sceneSynth(d, now / 2, 0);
}

// "What is it doing" pill at the bottom, shown over the interlude until lyrics are on screen.
void drawStatus(U8G2 &d, const char *text, bool busy, uint32_t now) {
  if (!text[0]) return;
  d.setFont(u8g2_font_5x7_tf);
  char l1[26], l2[26] = "";
  int len = (int)strlen(text);
  if (len <= 24) { strncpy(l1, text, 25); l1[25] = 0; }
  else {
    int cut = 24;
    while (cut > 8 && text[cut] != ' ') cut--;
    memcpy(l1, text, (size_t)cut); l1[cut] = 0;
    const char *rest = text + cut + (text[cut] == ' ' ? 1 : 0);
    strncpy(l2, rest, 24); l2[24] = 0;
  }
  int dots = busy ? (int)((now / 320) % 4) : 0;
  int w1 = d.getStrWidth(l1), w2 = d.getStrWidth(l2);
  int w = (w1 > w2 ? w1 : w2) + (busy ? 15 : 0) + 10;
  int h = l2[0] ? 21 : 12;
  int x = (128 - w) / 2, y = 64 - h;
  d.setDrawColor(0);
  d.drawBox(x, y, w, h);
  d.setDrawColor(1);
  d.drawFrame(x, y, w, h);
  int tx = x + 5;
  if (!l2[0]) {
    d.drawStr(tx, 61, l1);
    for (int i = 0; i < dots; i++) d.drawPixel(tx + w1 + 2 + i * 4, 60);
  } else {
    d.drawStr(tx, 53, l1);
    d.drawStr(tx, 61, l2);
  }
}

// ---- word box ---------------------------------------------------------------------------------------

int ease(int t) { return 256 - (256 - t) * (256 - t) / 256; }                 // 0..256 ease-out
int bounce(int t) { return (256 - t) * cs(t * 154 / 256) / 127; }              // 256 -> 0 with a small overshoot

// screen transition: black shapes that shrink away and reveal the scene
void drawTransition(U8G2 &d, int type, uint32_t age) {
  if (type == 0 || age >= TRANS_MS) return;
  int t = (int)(age * 256U / TRANS_MS);
  d.setDrawColor(0);
  switch (type) {
    case 1:                                                      // vertical blinds
      for (int i = 0; i < 8; i++) {
        int ti = imax(0, imin(256, t * 3 / 2 - i * 16));
        bx(d, i * 16, 0, 16 - 16 * ti / 256, 64);
      }
      break;
    case 2:                                                      // horizontal shutter
      for (int i = 0; i < 8; i++) {
        int ti = imax(0, imin(256, t * 3 / 2 - i * 16));
        bx(d, 0, i * 8, 128, 8 - 8 * ti / 256);
      }
      break;
    case 3: {                                                    // curtains parting towards the edges
      int w = 64 * (256 - t) / 256;
      bx(d, 0, 0, w, 64); bx(d, 128 - w, 0, w, 64);
      break;
    }
    default: {                                                   // scanline dissolve
      int s = imin(3, t * 4 / 256);
      for (int y = 0; y < 64; y++) if (((y * 3) & 3) >= s) hl(d, 0, y, 128);
      break;
    }
  }
  d.setDrawColor(1);
}

// box decorations drawn outside the box (colour oc), before the box itself so the box covers any overlap
void drawDecor(U8G2 &d, int style, int x, int y, int w, int h, uint32_t now, int oc) {
  d.setDrawColor(oc);
  switch (style) {
    case 1: {                                                    // marching ants
      int rx = x - 3, ry = y - 3, rw = w + 6, rh = h + 6;
      int P = 2 * (rw - 1) + 2 * (rh - 1), ph = (int)(now / 45);
      for (int pos = 0; pos < P; pos++) {
        if ((((pos + ph) / 3) & 1) == 0) { int ox, oy; perim(rx, ry, rw, rh, pos, ox, oy); px(d, ox, oy); }
      }
      break;
    }
    case 2: {                                                    // breathing corner brackets
      int dd = 2 + (sn((int)(now / 5)) + 127) * 3 / 254, L = 6;
      int x1 = x - dd, y1 = y - dd, x2 = x + w + dd, y2 = y + h + dd;
      hl(d, x1, y1, L); vl(d, x1, y1, L);
      hl(d, x2 - L, y1, L); vl(d, x2 - 1, y1, L);
      hl(d, x1, y2 - 1, L); vl(d, x1, y2 - L, L);
      hl(d, x2 - L, y2 - 1, L); vl(d, x2 - 1, y2 - L, L);
      break;
    }
    case 3:                                                      // hard drop shadow
      bx(d, x + 3, y + 3, w, h);
      break;
    case 4: {                                                    // a dot with a tail circling the box
      int rx = x - 3, ry = y - 3, rw = w + 6, rh = h + 6;
      int head = (int)((now / 5) % 100000U);
      int ox, oy;
      perim(rx, ry, rw, rh, head, ox, oy);
      bx(d, ox - 1, oy - 1, 3, 3);
      for (int k = 1; k <= 7; k++) { perim(rx, ry, rw, rh, head - k * 3, ox, oy); px(d, ox, oy); }
      break;
    }
    case 5: {                                                    // scrolling ruler ticks and blinking chevrons
      int ph = (int)((now / 60) % 4U);
      for (int xx = x; xx < x + w; xx += 4) {
        int len = (((xx - x) / 4) % 3 == 0) ? 3 : 1;
        vl(d, xx + ph, y - 2 - len, len);
        vl(d, xx + ph, y + h + 2, len);
      }
      if ((now / 300) & 1U) {
        int cy = y + h / 2;
        ln(d, x - 9, cy - 3, x - 6, cy); ln(d, x - 6, cy, x - 9, cy + 3);
        ln(d, x + w + 8, cy - 3, x + w + 5, cy); ln(d, x + w + 5, cy, x + w + 8, cy + 3);
      }
      break;
    }
    default: break;
  }
  d.setDrawColor(1);
}

// LTR word with a per-letter effect. fx: 1 pop-in, 2 wave, 3 jitter
void drawLetters(U8G2 &d, const char *s, int x0, int base, int fx, uint32_t age, uint32_t now, uint32_t seed) {
  int len = (int)strlen(s), cx = x0;
  for (int i = 0; i < len; i++) {
    char one[2] = {s[i], 0};
    int cw = d.getStrWidth(one), dx = 0, dy = 0;
    bool vis = true;
    if (fx == 1) {
      int start = i * 140 / (len > 0 ? len : 1);
      int p = (int)age - start;
      if (p < 0) vis = false; else if (p < 100) dy = -(100 - p) / 16;
    } else if (fx == 2) {
      dy = (int)age < (int)ENTRY_MS ? 0 : sn(i * 38 + (int)(now / 5)) * 2 / 127;
    } else if (fx == 3) {
      uint32_t hv = mix((uint32_t)(now / 100) * 31U + (uint32_t)i, seed);
      if (age < 150 || (hv & 7U) == 0) { dx = (int)((hv >> 4) & 3U) - 1; dy = (int)((hv >> 8) & 3U) - 1; }
    }
    if (vis && cx + dx >= 0 && base + dy >= 0) d.drawGlyph((u8g2_uint_t)(cx + dx), (u8g2_uint_t)(base + dy), (uint16_t)(uint8_t)s[i]);
    cx += cw;
  }
}

void drawWord(U8G2 &d, const char *word, uint32_t now, const Look &lk, bool rtl, int wi, int wc, int key) {
  char fitted[40];
  strncpy(fitted, word, sizeof(fitted) - 1);
  fitted[sizeof(fitted) - 1] = 0;
  uint16_t glyphs[24];
  int glyphCount = 0, textW, boxH = 23, baseOff = 15;
  bool big = false;
  if (rtl) {                                                   // Urdu / Arabic word: shaped glyphs of the unifont Urdu font
    glyphCount = arabic::shapeWord(fitted, (int)strlen(fitted), glyphs, 24);
    d.setFont(u8g2_font_unifont_t_urdu);
    while (glyphCount > 1 && arabic::rowWidth(d, glyphs, glyphCount) > 104) glyphCount--;
    textW = arabic::rowWidth(d, glyphs, glyphCount);
    baseOff = 16;
  } else {
    if (lk.big) { d.setFont(u8g2_font_logisoso16_tf); big = d.getStrWidth(fitted) <= 84; }
    if (big) { boxH = 27; baseOff = 21; }
    else {
      d.setFont(u8g2_font_7x13B_tf);
      if (d.getStrWidth(fitted) > 96) d.setFont(u8g2_font_6x10_tf);
      if (d.getStrWidth(fitted) > 104) d.setFont(u8g2_font_5x7_tf);
    }
    while (fitted[0] && d.getStrWidth(fitted) > 104) fitted[strlen(fitted) - 1] = 0;
    textW = d.getStrWidth(fitted);
  }
  int boxW = textW + 12;
  if (boxW < 28) boxW = 28;
  if (boxW > 116) boxW = 116;
  const int fx0 = (128 - boxW) / 2, fy0 = (64 - boxH) / 2, cy0 = fy0 + boxH / 2;

  uint32_t age = now - transitionAt;
  gKick = age < 220 ? (int)((220 - age) * 256 / 220) : 0;
  const bool settled = age >= ENTRY_MS;
  const int t = settled ? 256 : (int)(age * 256 / ENTRY_MS);

  // ---- entry animation -> current box rectangle
  int x = fx0, y = fy0, w = boxW, h = boxH, tdx = 0, tdy = 0;
  bool faceBack = false, textOn = true;
  if (!settled) {
    switch (lk.entry) {
      case 0: {                                                // CRT turn-on: a line widens, then opens up
        if (t < 100) { w = boxW * ease(t * 256 / 100) / 256; h = 3; }
        else h = 3 + (boxH - 3) * ease((t - 100) * 256 / 156) / 256;
        x = 64 - w / 2; y = cy0 - h / 2;
        textOn = t > 200;
        break;
      }
      case 1: {                                                // drop in from the top and bounce
        int bo = bounce(t);
        y = fy0 - fy0 * bo / 256; tdy = y - fy0;
        break;
      }
      case 2: {                                                // slide in from a side with a small overshoot
        int bo = bounce(t);
        bool fromRight = (lk.seed & 1U) != 0;
        int span = fromRight ? 128 - fx0 : fx0 + boxW;
        x = fx0 + (fromRight ? 1 : -1) * span * bo / 256; tdx = x - fx0;
        break;
      }
      case 3: {                                                // card flip around the horizontal axis
        int ang = t / 2;
        h = imax(2, boxH * abs(cs(ang)) / 127);
        y = cy0 - h / 2;
        faceBack = ang < 64;
        textOn = !faceBack && h * 2 >= boxH;
        break;
      }
      case 4: {                                                // zoom kick: starts big and settles
        int s = 256 + 110 * (256 - ease(t)) / 256;
        w = boxW * s / 256; h = boxH * s / 256;
        x = 64 - w / 2; y = cy0 - h / 2;
        break;
      }
      default: {                                               // shake that dies down
        uint32_t sd = lk.seed + (uint32_t)(age / 30);
        int amp = 256 - ease(t);
        tdx = ((int)(rnd(sd) % 13U) - 6) * amp / 256;
        tdy = ((int)(rnd(sd) % 5U) - 2) * amp / 256;
        x += tdx; y += tdy;
        break;
      }
    }
  }

  // ---- colour logic
  int negNow = lk.neg == 1 ? 1 : (lk.neg == 2 ? (int)((now / 520) & 1U) : 0);
  bool bgWhite = sceneInverted(lk.scene) != (negNow != 0);
  bool baseInv = false;
  if (lk.flip == 1) baseInv = true;
  else if (lk.flip == 2) baseInv = age < 540 ? ((age / 90) & 1U) != 0 : (age % 700U) < 70;
  if (faceBack) baseInv = !baseInv;
  bool boxInv = baseInv != bgWhite;
  int fg = boxInv ? 0 : 1;

  // ---- scene, negative, transition
  drawScene(d, lk.scene, now, lk.seed);
  if (negNow) { d.setDrawColor(2); d.drawBox(0, 0, 128, 64); d.setDrawColor(1); }
  drawTransition(d, lk.trans, age);

  // ---- decoration, box, text
  drawDecor(d, lk.style, x, y, w, h, now, bgWhite ? 0 : 1);
  d.setDrawColor(boxInv ? 1 : 0);
  bx(d, x, y, w, h);
  d.setDrawColor(fg);
  int inset = (boxInv && !bgWhite) ? 2 : 0;
  if (lk.style == 4 && settled && w - 2 * inset >= 12) d.drawRFrame(x + inset, y + inset, w - 2 * inset, h - 2 * inset, 4);
  else fr(d, x + inset, y + inset, w - 2 * inset, h - 2 * inset);

  if (textOn) {
    int tx = (128 - textW) / 2 + tdx, base = fy0 + baseOff + tdy;
    if (rtl) arabic::drawGlyphs(d, tx, base, glyphs, glyphCount);
    else if (lk.fx == 0) d.drawStr(tx, base, fitted);
    else drawLetters(d, fitted, tx, base, lk.fx, age, now, lk.seed);
  }

  // ---- colour flipping on top: scan bar through the box, whole-screen flash, glitch band
  if (lk.flip == 3 && age < ENTRY_MS + 120) {
    const int bw = 14;
    int pos = (int)(age * (uint32_t)(w + bw) / (ENTRY_MS + 120)), lo = imax(x + pos - bw, x), hi = imin(x + pos, x + w);
    if (hi > lo) { d.setDrawColor(2); bx(d, lo, y, hi - lo, h); }
  }
  if (lk.flash && age < 45) { d.setDrawColor(2); d.drawBox(0, 0, 128, 64); }
  else if (age < 90) { d.setDrawColor(2); bx(d, 0, (int)((lk.seed * 13U + age * 7U) % 60U), 128, 2); }
  d.setDrawColor(1);

  // ---- word progress along the bottom
  if (wc > 1) {
    d.setDrawColor(0); bx(d, 13, 59, 102, 5);
    d.setDrawColor(1); fr(d, 14, 60, 100, 3);
    bx(d, 14, 60, 100 * (wi + 1) / wc, 3);
  }
  (void)key;
}

}  // namespace

void enter() {
  lastWordKey = -1;
  lastScene = -1;
  transitionAt = millis();
  spotify::enter();
}

void leave() {
  spotify::leave();
}

void draw(U8G2 &d, uint32_t now) {
  spotify::LyricsView view;
  spotify::getLyricsView(view, now);

  if (!view.haveTrack || !view.lyricsReady || !view.line[0]) {
    drawAmbient(d, now);                                       // quiet interlude, plus a small indicator of what it is waiting for
    drawStatus(d, view.status, view.statusBusy, now);
    return;
  }

  WordFrame word = selectWord(view);
  if (!word.word[0]) {
    drawAmbient(d, now);
    return;
  }
  if (word.key != lastWordKey) {
    lastWordKey = word.key;
    transitionAt = now;
    uint32_t k = (uint32_t)word.key;
    int sc = (int)((mix(k, 1) >> 8) % (uint32_t)SCENE_COUNT);
    if (sc == lastScene) sc = (sc + 1) % SCENE_COUNT;           // never the same scene twice in a row
    lastScene = sc;
    look.scene = sc;
    look.seed = mix(k, 2) >> 4;
    look.style = (int)(mix(k, 3) % 6U);
    look.entry = (int)(mix(k, 4) % 6U);
    look.trans = (int)(mix(k, 5) % 5U);
    uint32_t fv = mix(k, 6) % 20U;
    look.flip = fv < 9 ? 0 : fv < 13 ? 1 : fv < 16 ? 2 : 3;
    uint32_t nv = mix(k, 7) % 8U;
    look.neg = nv < 5 ? 0 : nv < 7 ? 1 : 2;
    uint32_t tv = mix(k, 8) % 8U;
    look.fx = tv < 2 ? 0 : tv < 4 ? 1 : tv < 6 ? 2 : 3;
    look.big = (mix(k, 9) % 3U) == 0;
    look.flash = (mix(k, 10) % 5U) == 0;
  }
  drawWord(d, word.word, now, look, word.rtl, word.index, word.count, word.key);
}

}  // namespace lyrics_visual
