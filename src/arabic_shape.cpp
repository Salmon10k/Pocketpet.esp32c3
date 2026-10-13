#include "arabic_shape.h"
#include <string.h>

namespace arabic {
namespace {

struct ArForm { uint16_t base, iso, fin, ini, med; };
struct ArLig { uint16_t alef, iso, fin; };
#include "arabic_forms.inc"
const int FORM_COUNT = (int)(sizeof(AR_FORMS) / sizeof(AR_FORMS[0]));
const int LIG_COUNT = (int)(sizeof(AR_LIGS) / sizeof(AR_LIGS[0]));

const ArForm *findForm(uint32_t cp) {
  int lo = 0, hi = FORM_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (AR_FORMS[mid].base == cp) return &AR_FORMS[mid];
    if (AR_FORMS[mid].base < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

const ArLig *findLig(uint32_t alef) {
  for (int i = 0; i < LIG_COUNT; i++)
    if (AR_LIGS[i].alef == alef) return &AR_LIGS[i];
  return nullptr;
}

bool isMark(uint32_t cp) {                                      // vowel marks and similar: dropped (they would not join anyway)
  return (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06ED) || cp == 0x0640 /* tatweel */;
}

// UTF-8 helpers (3 byte max here is enough for everything we draw except emoji, which become '?')
int decode(const unsigned char *s, uint32_t &cp) {
  if (s[0] < 0x80) { cp = s[0]; return 1; }
  if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) { cp = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu); return 2; }
  if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
    cp = ((s[0] & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu); return 3;
  }
  if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) { cp = 0xFFFD; return 4; }
  cp = '?';
  return 1;
}

int encode(uint32_t cp, char *out) {
  if (cp < 0x80) { out[0] = (char)cp; return 1; }
  if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
  return 3;
}

bool isLatinRun(uint32_t cp) {                                  // characters that keep left-to-right order inside RTL text
  return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0xC0 && cp <= 0x24F);
}

uint32_t mirror(uint32_t cp) {
  switch (cp) {
    case '(': return ')'; case ')': return '(';
    case '[': return ']'; case ']': return '[';
    case '<': return '>'; case '>': return '<';
    case '{': return '}'; case '}': return '{';
    default: return cp;
  }
}

const int MAXCP = 160;

}  // namespace

bool isArabicCp(uint32_t cp) {
  return (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF);
}

bool hasArabic(const char *utf8) {
  for (const unsigned char *p = (const unsigned char *)utf8; *p;) {
    uint32_t cp;
    p += decode(p, cp);
    if (isArabicCp(cp)) return true;
  }
  return false;
}

size_t shapeVisual(const char *utf8, char *out, size_t cap) {
  if (cap == 0) return 0;
  // 1. decode, dropping marks
  uint32_t in[MAXCP];
  int n = 0;
  for (const unsigned char *p = (const unsigned char *)utf8; *p && n < MAXCP;) {
    uint32_t cp;
    p += decode(p, cp);
    if (isMark(cp)) continue;
    in[n++] = cp;
  }
  // 2. joining behaviour of every letter: 0 none, 1 right-joining (alef, dal, ...), 2 dual-joining
  uint8_t jt[MAXCP];
  for (int i = 0; i < n; i++) {
    const ArForm *f = findForm(in[i]);
    jt[i] = !f ? 0 : (f->ini ? 2 : (f->fin ? 1 : 0));
  }
  // 3. choose a form for each letter (logical order), making lam+alef ligatures on the way
  uint32_t sh[MAXCP];
  int m = 0;
  bool prevJoinsNext = false;                                   // previous letter connects to the one after it
  for (int i = 0; i < n; i++) {
    uint32_t cp = in[i];
    const ArForm *f = findForm(cp);
    if (!f) { sh[m++] = cp; prevJoinsNext = false; continue; }
    if (cp == 0x0644 && i + 1 < n) {
      const ArLig *lg = findLig(in[i + 1]);
      if (lg) {
        uint32_t g = prevJoinsNext && lg->fin ? lg->fin : lg->iso;
        sh[m++] = g ? g : cp;
        prevJoinsNext = false;                                  // the ligature ends the join
        i++;
        continue;
      }
    }
    bool nextAccepts = i + 1 < n && jt[i + 1] != 0;             // the next letter can connect to this one
    bool joinPrev = prevJoinsNext;
    bool joinNext = jt[i] == 2 && nextAccepts;
    uint32_t g;
    if (joinPrev && joinNext) g = f->med;
    else if (joinPrev) g = f->fin;
    else if (joinNext) g = f->ini;
    else g = f->iso;
    if (!g) g = f->iso ? f->iso : cp;
    sh[m++] = g;
    prevJoinsNext = joinNext;
  }
  // 4. visual order: reverse everything, but keep Latin letters and digits runs left to right; mirror brackets
  uint32_t vis[MAXCP];
  int v = 0;
  for (int i = m - 1; i >= 0;) {
    if (isLatinRun(sh[i])) {
      int j = i;
      while (j > 0 && isLatinRun(sh[j - 1])) j--;
      for (int k = j; k <= i; k++) vis[v++] = sh[k];
      i = j - 1;
    } else {
      vis[v++] = mirror(sh[i]);
      i--;
    }
  }
  // 5. encode
  size_t w = 0;
  for (int i = 0; i < v; i++) {
    char tmp[4];
    int l = encode(vis[i], tmp);
    if (w + (size_t)l + 1 > cap) break;
    memcpy(out + w, tmp, (size_t)l);
    w += (size_t)l;
  }
  out[w] = 0;
  return w;
}

int shapeWord(const char *utf8, int len, uint16_t *out, int cap) {
  char tmp[96], vis[160];
  if (len > (int)sizeof(tmp) - 1) len = (int)sizeof(tmp) - 1;
  memcpy(tmp, utf8, (size_t)len);
  tmp[len] = 0;
  shapeVisual(tmp, vis, sizeof(vis));
  int n = 0;
  for (const unsigned char *p = (const unsigned char *)vis; *p && n < cap;) {
    uint32_t cp;
    p += decode(p, cp);
    out[n++] = cp > 0xFFFF ? '?' : (uint16_t)cp;
  }
  return n;
}

// Splits the line into words and fills rows of at most maxCols glyphs. Inside a row the first word of the
// lyric is on the right (right-to-left reading). totalWords gets the number of words laid out.
int layoutRows(const char *utf8, int maxCols, int maxRows, Row *rows, int *totalWords) {
  int nRows = 0, wordIdx = 0;
  if (totalWords) *totalWords = 0;
  uint16_t wg[MAX_ROW_GLYPHS];
  const char *p = utf8;
  Row cur;
  cur.n = 0;
  cur.nWords = 0;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ' ') p++;
    int wn = shapeWord(start, (int)(p - start), wg, MAX_ROW_GLYPHS);
    if (wn == 0) continue;
    int need = wn + (cur.nWords ? 1 : 0);
    if (cur.nWords && (cur.n + need > maxCols || cur.nWords >= MAX_ROW_WORDS || cur.n + need > MAX_ROW_GLYPHS)) {
      if (nRows + 1 >= maxRows) { rows[nRows++] = cur; if (totalWords) *totalWords = wordIdx; return nRows; }   // last row full: stop
      rows[nRows++] = cur;
      cur.n = 0;
      cur.nWords = 0;
    }
    if (wn > MAX_ROW_GLYPHS - cur.n - (cur.nWords ? 1 : 0)) wn = MAX_ROW_GLYPHS - cur.n - (cur.nWords ? 1 : 0);
    // a new word goes on the LEFT of the words already in the row: shift the row right and insert it at 0
    int add = wn + (cur.nWords ? 1 : 0);
    memmove(cur.g + add, cur.g, (size_t)cur.n * sizeof(uint16_t));
    for (int k = 0; k < wn; k++) cur.g[k] = wg[k];
    if (cur.nWords) cur.g[wn] = ' ';
    for (int k = 0; k < cur.nWords; k++) cur.words[k].start += add;
    cur.words[cur.nWords].idx = wordIdx;
    cur.words[cur.nWords].start = 0;
    cur.words[cur.nWords].len = wn;
    cur.nWords++;
    cur.n += add;
    wordIdx++;
  }
  if (cur.nWords && nRows < maxRows) rows[nRows++] = cur;
  if (totalWords) *totalWords = wordIdx;
  return nRows;
}

int rowWidth(U8G2 &d, const uint16_t *g, int n) {
  int w = 0;
  for (int i = 0; i < n; i++) w += d.getGlyphWidth(g[i]);
  return w;
}

void drawGlyphs(U8G2 &d, int x, int baseline, const uint16_t *g, int n) {
  for (int i = 0; i < n; i++) x += d.drawGlyph(x, baseline, g[i]);
}

}  // namespace arabic
