#include "lyrics_visual.h"
#include "spotify.h"
#include "arabic_shape.h"
#include <string.h>
#include <math.h>

namespace lyrics_visual {
namespace {

int lastWordKey = -1;
int lastScene = -1;
uint32_t sceneSeed = 0;
uint32_t transitionAt = 0;

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

  int starts[20], lengths[20], count = 0;
  const char *p = text;
  while (*p && count < 20) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ' ') ++p;
    starts[count] = (int)(start - text);
    lengths[count] = (int)(p - start);
    ++count;
  }
  if (!count) return out;

  uint32_t span = v.lineEndMs > v.lineStartMs ? v.lineEndMs - v.lineStartMs : 4000;
  uint32_t elapsed = v.positionMs > v.lineStartMs ? v.positionMs - v.lineStartMs : 0;
  if (elapsed > span) elapsed = span;
  uint32_t totalWeight = 0;
  for (int i = 0; i < count; ++i) totalWeight += (uint32_t)(lengths[i] + 2);
  uint32_t progress = (uint32_t)((uint64_t)elapsed * totalWeight / span);
  uint32_t sum = 0;
  int index = count - 1;
  for (int i = 0; i < count; ++i) {
    sum += (uint32_t)(lengths[i] + 2);
    if (progress < sum) { index = i; break; }
  }
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

const int SCENE_COUNT = 8;
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
    default: sceneGlitch(d, now, seed); break;
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

void drawWord(U8G2 &d, const char *word, uint32_t now, int scene, uint32_t seed, bool rtl = false) {
  char fitted[40];
  strncpy(fitted, word, sizeof(fitted) - 1);
  fitted[sizeof(fitted) - 1] = 0;
  uint16_t glyphs[24];
  int glyphCount = 0;
  int textW;
  if (rtl) {                                                   // Urdu / Arabic word: shaped glyphs of the unifont Urdu font
    glyphCount = arabic::shapeWord(fitted, (int)strlen(fitted), glyphs, 24);
    d.setFont(u8g2_font_unifont_t_urdu);
    while (glyphCount > 1 && arabic::rowWidth(d, glyphs, glyphCount) > 104) glyphCount--;
    textW = arabic::rowWidth(d, glyphs, glyphCount);
  } else {
    d.setFont(u8g2_font_7x13B_tf);
    if (d.getStrWidth(fitted) > 96) d.setFont(u8g2_font_6x10_tf);
    if (d.getStrWidth(fitted) > 104) d.setFont(u8g2_font_5x7_tf);
    while (fitted[0] && d.getStrWidth(fitted) > 104) fitted[strlen(fitted) - 1] = 0;
    textW = d.getStrWidth(fitted);
  }
  int boxW = textW + 12;
  if (boxW < 28) boxW = 28;
  if (boxW > 116) boxW = 116;
  const int boxH = 23;
  uint32_t age = now - transitionAt;
  int x = (128 - boxW) / 2;
  int y = 21 + (age < 110 ? (int)(110 - age) / 22 : 0);        // small drop-in when the word changes
  int baseline = y + (rtl ? 16 : 15);
  bool inv = sceneInverted(scene);

  drawScene(d, scene, now, seed);
  if (inv) {
    d.setDrawColor(1);                                         // keep the box itself clean white, the glitch stays outside it
    d.drawBox(x, y, boxW, boxH);
    d.setDrawColor(0);
  } else {
    d.setDrawColor(0);                                         // clear a hole so the scene never crosses the word
    d.drawBox(x, y, boxW, boxH);
    d.setDrawColor(1);
  }
  d.drawFrame(x, y, boxW, boxH);
  if (rtl) arabic::drawGlyphs(d, (128 - textW) / 2, baseline, glyphs, glyphCount);
  else d.drawStr((128 - textW) / 2, baseline, fitted);
  d.setDrawColor(1);
  if (age < 90) {                                              // quick glitch flash on every new word
    d.setDrawColor(2);
    d.drawBox(0, (int)((seed * 13 + age * 7) % 60), 128, 2);
    d.setDrawColor(1);
  }
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
    uint32_t h = (uint32_t)word.key * 2654435761U;
    int sc = (int)((h >> 20) % SCENE_COUNT);
    if (sc == lastScene) sc = (sc + 1) % SCENE_COUNT;           // never the same scene twice in a row
    lastScene = sc;
    sceneSeed = h >> 8;
  }
  drawWord(d, word.word, now, lastScene < 0 ? 0 : lastScene, sceneSeed, word.rtl);
}

}  // namespace lyrics_visual
