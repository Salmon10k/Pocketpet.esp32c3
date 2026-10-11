#include "lyrics_visual.h"
#include "spotify.h"
#include <string.h>
#include <math.h>

namespace lyrics_visual {
namespace {

int lastWordKey = -1;
uint32_t transitionAt = 0;

struct WordFrame {
  char word[28];
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
  WordFrame out = {{0}, 0, 0, -1};
  if (!v.lyricsReady || !v.line[0]) return out;

  int starts[20], lengths[20], count = 0;
  const char *p = v.line;
  while (*p && count < 20) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ' ') ++p;
    starts[count] = (int)(start - v.line);
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
  copyWord(v.line + starts[index], (size_t)lengths[index], out.word, sizeof(out.word));
  out.index = index;
  out.count = count;
  out.key = (int)(v.lineStartMs / 10U) * 23 + index;
  return out;
}


void drawWaveLine(U8G2 &d, int yBase, int amp, int phase, int x0, int x1, int step = 4) {
  int px = x0;
  int py = yBase + (int)lroundf(sinf((px + phase) * 0.12f) * amp);
  for (int x = x0 + step; x <= x1; x += step) {
    int y = yBase + (int)lroundf(sinf((x + phase) * 0.12f) * amp);
    d.drawLine(px, py, x, y);
    px = x;
    py = y;
  }
}

void drawLoop(U8G2 &d, int cx, int cy, int rx, int ry, int phase, int wobble) {
  int px = cx + rx;
  int py = cy;
  for (int a = 12; a <= 360; a += 12) {
    float t = a * 0.0174532925f;
    int rX = rx + (int)lroundf(sinf(t * 3.0f + phase * 0.17f) * wobble);
    int rY = ry + (int)lroundf(cosf(t * 2.0f + phase * 0.13f) * wobble / 2.0f);
    int x = cx + (int)lroundf(cosf(t) * rX);
    int y = cy + (int)lroundf(sinf(t) * rY);
    d.drawLine(px, py, x, y);
    px = x;
    py = y;
  }
}

void drawWaveScene(U8G2 &d, uint32_t now, int phase) {
  int shift = (int)(now / 35);
  drawWaveLine(d, 13, 4, shift + phase * 17, 5, 122);
  drawWaveLine(d, 52, 7, -shift * 2 + phase * 23, 3, 125);
  drawWaveLine(d, 59, 3, shift * 2 + 31, 14, 114);
  for (int x = 8; x < 124; x += 18) d.drawPixel(x, 18 + ((x + phase) & 3));
}

void drawLoopScene(U8G2 &d, uint32_t now, int phase) {
  int wobble = 2 + (int)((now / 100) & 1);
  drawLoop(d, 64, 32, 43, 22, phase + now / 90, wobble);
  drawLoop(d, 64, 32, 35, 18, phase * 3 + now / 120, 2);
  d.drawLine(5, 32, 25, 32);
  d.drawLine(103, 32, 123, 32);
  d.drawPixel(12, 22);
  d.drawPixel(116, 42);
}

void drawBarScene(U8G2 &d, uint32_t now, int phase) {
  int pulse = (int)(now / 75);
  for (int i = 0; i < 14; ++i) {
    int h = 2 + ((i * 7 + pulse + phase * 3) % 15);
    int x = 6 + i * 9;
    d.drawBox(x, 61 - h, 5, h);
    if ((i + phase) % 3 == 0) d.drawVLine(x + 2, 3, 8 + (h % 5));
  }
  d.drawLine(8, 15, 25, 8 + (phase & 3));
  d.drawLine(103, 8 + (phase & 3), 120, 15);
  d.drawLine(15, 53, 29, 46);
  d.drawLine(99, 46, 113, 53);
}

void drawScribbleScene(U8G2 &d, uint32_t now, int phase) {
  int s = (int)(now / 45);
  drawWaveLine(d, 25, 12, s + phase * 19, -4, 132, 3);
  drawWaveLine(d, 42, 10, -s * 2 + phase * 11, -4, 132, 3);
  for (int i = 0; i < 5; ++i) {
    int x = 5 + i * 29 + ((s + i) % 5);
    d.drawLine(x, 8, x + 8, 17);
    d.drawLine(x + 8, 17, x - 2, 22);
  }
  d.drawLine(4, 55, 18, 48);
  d.drawLine(110, 48, 124, 55);
}

void drawGraphScene(U8G2 &d, uint32_t now, int phase) {
  int shift = (int)(now / 80);
  for (int x = 8; x < 124; x += 8) {
    int top = 10 + ((x * 3 + shift + phase * 5) % 13);
    for (int y = top; y < 55; y += 6) d.drawPixel(x, y);
  }
  int px = 6;
  int py = 48;
  for (int x = 12; x <= 122; x += 6) {
    int y = 43 - (int)lroundf(sinf((x + shift) * 0.16f) * 14.0f);
    d.drawLine(px, py, x, y);
    px = x;
    py = y;
  }
  d.drawLine(8, 15, 28, 22);
  d.drawLine(100, 22, 120, 15);
}

void drawMinimalScene(U8G2 &d, uint32_t now, int phase) {
  int x = 63 + (int)lroundf(sinf(now / 150.0f) * 7.0f);
  int y = 32;
  d.drawLine(x - 18, y, x, y - 15);
  d.drawLine(x, y - 15, x + 18, y);
  d.drawLine(x + 18, y, x, y + 15);
  d.drawLine(x, y + 15, x - 18, y);
  d.drawLine(8, 12 + phase, 27, 12 + phase);
  d.drawLine(101, 51 - phase, 120, 51 - phase);
}

void drawScene(U8G2 &d, uint32_t now, int scene) {
  switch (scene) {
    case 0: drawWaveScene(d, now, scene); break;
    case 1: drawLoopScene(d, now, scene); break;
    case 2: drawBarScene(d, now, scene); break;
    case 3: drawScribbleScene(d, now, scene); break;
    case 4: drawGraphScene(d, now, scene); break;
    default: drawMinimalScene(d, now, scene); break;
  }
}

void drawWord(U8G2 &d, const char *word, uint32_t now, int scene, bool inverted) {
  d.setFont(u8g2_font_10x20_tf);
  if (d.getStrWidth(word) > 106) d.setFont(u8g2_font_7x13B_tf);
  if (d.getStrWidth(word) > 108) d.setFont(u8g2_font_6x10_tf);

  char fitted[28];
  strncpy(fitted, word, sizeof(fitted) - 1);
  fitted[sizeof(fitted) - 1] = 0;
  while (fitted[0] && d.getStrWidth(fitted) > 108) fitted[strlen(fitted) - 1] = 0;

  int textW = d.getStrWidth(fitted);
  int boxW = textW + 14;
  if (boxW < 30) boxW = 30;
  if (boxW > 120) boxW = 120;
  const int boxH = 27;
  int x = (128 - boxW) / 2;
  int y = 19;
  uint32_t age = now - transitionAt;

  if (inverted) {
    d.setDrawColor(1);
    d.drawBox(0, 0, 128, 64);
    d.setDrawColor(0);
  }

  drawScene(d, now, scene);
  d.setDrawColor(inverted ? 0 : 1);
  d.drawFrame(x, y, boxW, boxH);
  d.drawHLine(x - 5, y + 4, 5 + (scene & 5));
  d.drawHLine(x + boxW, y + boxH - 5, 5 + ((scene * 3) & 5));

  int tx = (128 - textW) / 2;
  int base = y + 19;
  if (age < 140) {
    int slide = (int)(140 - age) / 20;
    tx += (scene & 1) ? slide : -slide;
  }
  d.drawStr(tx, base, fitted);

  if (inverted) d.setDrawColor(1);
}

}  // namespace

void enter() {
  lastWordKey = -1;
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
    // No metadata, status labels, or controls: only a quiet generative interlude until lyrics arrive.
    drawAmbient(d, now);
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
  }
  int scene = word.key % 6;
  if (scene < 0) scene += 6;
  uint32_t age = now - transitionAt;

  // The reference occasionally drops the lyric for a graph-only transition.
  if (scene == 5 && age < 230) {
    drawGraphScene(d, now, scene);
    return;
  }

  // Occasional high-contrast flash, matching the bright-frame cuts in the reference.
  bool inverted = scene == 2 && ((word.key / 6) & 3) == 0 && age >= 45 && age < 240;
  drawWord(d, word.word, now, scene, inverted);
}

}  // namespace lyrics_visual
