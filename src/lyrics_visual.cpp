#include "lyrics_visual.h"
#include "spotify.h"
#include <string.h>

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

void drawCorners(U8G2 &d, int x, int y, int w, int h, int len, int phase) {
  int n = len + phase % 3;
  d.drawHLine(x - 5, y - 5, n); d.drawVLine(x - 5, y - 5, n);
  d.drawHLine(x + w - n + 5, y - 5, n); d.drawVLine(x + w + 4, y - 5, n);
  d.drawHLine(x - 5, y + h + 4, n); d.drawVLine(x - 5, y + h - n + 5, n);
  d.drawHLine(x + w - n + 5, y + h + 4, n); d.drawVLine(x + w + 4, y + h - n + 5, n);
}

void drawRings(U8G2 &d, uint32_t now, int phase) {
  int pulse = (int)((now / 180) % 4);
  d.drawCircle(64, 32, 13 + pulse, (phase & 1) ? U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_RIGHT : U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_LEFT);
  d.drawCircle(64, 32, 22, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  d.drawLine(15, 14 + (phase % 3), 29, 14 + (phase % 3));
  d.drawLine(99, 49 - (phase % 3), 113, 49 - (phase % 3));
  d.drawPixel(18, 48); d.drawPixel(109, 16);
}

void drawWave(U8G2 &d, uint32_t now, int phase) {
  int shift = (int)((now / 130) % 5);
  for (int i = 0; i < 13; ++i) {
    int h = 2 + ((i * 3 + phase + shift) % 7);
    int x = 8 + i * 9;
    d.drawVLine(x, 51 - h, h);
    if ((i + phase) % 3 == 0) d.drawPixel(x + 2, 51 - h - 2);
  }
  d.drawLine(8, 13, 24, 7 + (phase % 5));
  d.drawLine(104, 7 + (phase % 5), 120, 13);
}

void drawBars(U8G2 &d, uint32_t now, int phase) {
  int pulse = (int)((now / 100) % 6);
  for (int side = 0; side < 2; ++side) {
    int base = side ? 110 : 18;
    for (int i = 0; i < 4; ++i) {
      int h = 3 + ((i * 2 + phase + pulse) % 9);
      int x = base + (side ? i * 3 : -i * 3);
      d.drawVLine(x, 32 - h / 2, h);
    }
  }
  d.drawLine(13, 15, 28, 9);
  d.drawLine(100, 9, 115, 15);
  d.drawLine(13, 49, 28, 55);
  d.drawLine(100, 55, 115, 49);
}

void drawScribble(U8G2 &d, int phase, uint32_t now) {
  int wobble = (int)((now / 150) % 4);
  d.drawLine(7, 25 + wobble, 18, 14);
  d.drawLine(18, 14, 27, 19 + phase % 5);
  d.drawLine(101, 18 + phase % 4, 111, 11 + wobble);
  d.drawLine(111, 11 + wobble, 121, 24);
  d.drawLine(8, 43, 18, 53 - wobble);
  d.drawLine(18, 53 - wobble, 29, 47);
  d.drawLine(99, 47, 111, 54 - wobble);
  d.drawLine(111, 54 - wobble, 120, 42);
  for (int i = 0; i < 5; ++i) {
    int x = 38 + i * 13;
    int y = (i & 1) ? 8 + phase % 3 : 55 - phase % 3;
    d.drawPixel(x, y);
  }
}

void drawAmbient(U8G2 &d, uint32_t now) {
  int p = (int)((now / 170) % 8);
  drawRings(d, now, p);
  for (int i = 0; i < 3; ++i) {
    int rise = (p + i * 3) % 8;
    d.drawDisc(54 + i * 10, 34 - rise / 2, 1);
  }
}

void drawWord(U8G2 &d, const char *word, uint32_t now, int phase) {
  d.setFont(u8g2_font_7x13B_tf);
  if (d.getStrWidth(word) > 96) d.setFont(u8g2_font_6x10_tf);
  if (d.getStrWidth(word) > 104) d.setFont(u8g2_font_5x7_tf);

  char fitted[28];
  strncpy(fitted, word, sizeof(fitted) - 1);
  fitted[sizeof(fitted) - 1] = 0;
  while (fitted[0] && d.getStrWidth(fitted) > 104) fitted[strlen(fitted) - 1] = 0;

  int textW = d.getStrWidth(fitted);
  int boxW = textW + 12;
  if (boxW < 28) boxW = 28;
  if (boxW > 116) boxW = 116;
  const int boxH = 23;
  int x = (128 - boxW) / 2;
  int y = 21 + ((now - transitionAt < 110) ? (int)(110 - (now - transitionAt)) / 22 : 0);
  int textX = (128 - textW) / 2;
  int baseline = y + 15;

  // The box is the anchor. The surrounding drawing changes on each sung word.
  d.drawFrame(x, y, boxW, boxH);
  d.drawHLine(x + 4, y, 5 + (phase % 7));
  d.drawHLine(x + boxW - 10, y + boxH - 1, 6 + (phase % 5));

  switch (phase & 3) {
    case 0: drawRings(d, now, phase); break;
    case 1: drawWave(d, now, phase); break;
    case 2: drawBars(d, now, phase); break;
    default: drawScribble(d, phase, now); break;
  }

  d.setFont(u8g2_font_7x13B_tf);
  if (d.getStrWidth(fitted) > 96) d.setFont(u8g2_font_6x10_tf);
  if (d.getStrWidth(fitted) > 104) d.setFont(u8g2_font_5x7_tf);
  d.drawStr(textX, baseline, fitted);
  drawCorners(d, x, y, boxW, boxH, 7, phase);
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
  int phase = (word.key ^ (word.index * 13)) & 3;
  drawWord(d, word.word, now, phase);
}

}  // namespace lyrics_visual
