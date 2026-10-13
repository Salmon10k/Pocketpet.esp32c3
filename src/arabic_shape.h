#pragma once
// Arabic-script (Arabic, Urdu, Persian) text for the OLED. The U8g2 Urdu/Arabic fonts only contain single
// glyphs and the presentation forms; they do not join letters or run right to left by themselves. This
// module does both: it picks the isolated / initial / medial / final form of each letter, makes lam+alef
// ligatures, drops vowel marks, reorders to visual (left to right on screen) order and lays words out in
// rows. Glyphs are Unicode code points (BMP), drawn one by one with U8g2's drawGlyph().
#include <stddef.h>
#include <stdint.h>
#include <U8g2lib.h>

namespace arabic {

const int MAX_COLS = 15;        // glyphs per row used by layoutRows (about 8 px each in the 16 px Urdu font)
const int MAX_ROW_GLYPHS = 40;
const int MAX_ROW_WORDS = 8;

struct WordSpan {
  int idx;                      // index of the word in the whole line (0 = first word of the lyric, drawn rightmost)
  int start;                    // first glyph of the word inside Row::g
  int len;                      // glyph count
};

struct Row {
  uint16_t g[MAX_ROW_GLYPHS];   // glyphs in visual order (draw left to right)
  int n;
  WordSpan words[MAX_ROW_WORDS];
  int nWords;
};

bool isArabicCp(uint32_t cp);
bool hasArabic(const char *utf8);

// pure text functions (host-tested)
size_t shapeVisual(const char *utf8, char *out, size_t cap);                 // shaped + reordered, as UTF-8
int shapeWord(const char *utf8, int len, uint16_t *out, int cap);            // one word -> visual-order glyphs
int layoutRows(const char *utf8, int maxCols, int maxRows, Row *rows, int *totalWords);

// drawing helpers
int rowWidth(U8G2 &d, const uint16_t *g, int n);
void drawGlyphs(U8G2 &d, int x, int baseline, const uint16_t *g, int n);

}  // namespace arabic
