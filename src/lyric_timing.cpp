#include "lyric_timing.h"
#include <string.h>

namespace lyric_timing {
namespace {

bool isVowel(unsigned char c) {
  unsigned char l = (unsigned char)(c | 0x20);
  if (l == 'a' || l == 'e' || l == 'i' || l == 'o' || l == 'u' || l == 'y') return c < 0x80;
  if (c < 0xC0) return false;                                   // Latin-1 accented vowels
  switch (c) {
    case 0xC7: case 0xE7: case 0xD1: case 0xF1: case 0xD7: case 0xF7:
    case 0xDF: case 0xD0: case 0xF0: case 0xDE: case 0xFE: return false;
    default: return true;
  }
}
bool isLetter(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0; }
bool isPunct(char c) { return c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?' || c == '"' || c == ')' || c == '(' || c == '\'' || c == '-'; }

}  // namespace

int syllables(const char *w, int len) {
  int end = len;
  while (end > 0 && isPunct(w[end - 1])) end--;
  int syl = 0;
  bool prev = false, anyLetter = false;
  for (int i = 0; i < end; i++) {
    unsigned char c = (unsigned char)w[i];
    if (isLetter(c)) anyLetter = true;
    bool v = isVowel(c);
    if (v && !prev) syl++;
    prev = v;
  }
  if (!anyLetter) return 1;
  if (syl > 1 && end > 2 && (w[end - 1] == 'e' || w[end - 1] == 'E') && !isVowel((unsigned char)w[end - 2])) {
    bool le = (w[end - 2] == 'l' || w[end - 2] == 'L') && end > 3 && !isVowel((unsigned char)w[end - 3]);
    if (!le) syl--;                                                // silent final e ("love", "time")
  }
  return syl < 1 ? 1 : syl;
}

int estimate(const char *text, uint32_t spanMs, uint32_t *starts, int maxWords) {
  bool utf8 = false;
  if (text[0] == 0x1B) { utf8 = true; text++; }
  if (maxWords > MAX_WORDS) maxWords = MAX_WORDS;
  uint32_t dur[MAX_WORDS];
  int n = 0;
  const char *p = text;
  while (*p && n < maxWords) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char *s = p;
    while (*p && *p != ' ') ++p;
    int len = (int)(p - s);
    uint32_t w;
    if (utf8) {
      int cps = 0;
      for (int i = 0; i < len; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) cps++;
      w = (uint32_t)((cps + 1) / 2) * 200 + 60;                    // roughly two letters per syllable
    } else {
      w = (uint32_t)syllables(s, len) * 200 + 60;
      char last = s[len - 1];
      if (last == ',' || last == ';' || last == ':') w += 120;
      else if (last == '.' || last == '?' || last == '!') w += 220;
    }
    dur[n++] = w;
  }
  if (n == 0) return 0;

  uint32_t span = spanMs < 600 ? 600 : (spanMs > 20000 ? 20000 : spanMs);
  uint32_t target = span * 9 / 10;                                 // leave a breath before the next line
  uint32_t total = 0;
  for (int i = 0; i < n; i++) total += dur[i];

  if (total * 100 >= target * 60) {                                // slow line: stretch the words over the whole line
    dur[n - 1] = dur[n - 1] * 14 / 10;                             // and favour the last (usually held) word
    total = 0;
    for (int i = 0; i < n; i++) total += dur[i];
    uint32_t acc = 0;
    for (int i = 0; i < n; i++) {
      starts[i] = (uint32_t)((uint64_t)acc * target / total);
      acc += dur[i];
    }
  } else {                                                         // short line: some of the spare time stretches every word,
    uint32_t left = target - total;                                // the rest is the held last note
    uint32_t scale = 256 + (uint32_t)((uint64_t)256 * 4 * left / (10ULL * total));   // 40% of the spare time, as a factor of 256
    if (scale > 563) scale = 563;                                  // never slower than ~2.2x the base pace
    uint32_t acc = 0;
    for (int i = 0; i < n; i++) { starts[i] = (uint32_t)((uint64_t)acc * scale / 256); acc += dur[i]; }
  }
  return n;
}

int focusIndex(const uint32_t *starts, int n, uint32_t elapsedMs) {
  if (n <= 0) return -1;
  int idx = 0;
  for (int i = 0; i < n; i++) if (starts[i] <= elapsedMs) idx = i;
  return idx;
}

}  // namespace lyric_timing
