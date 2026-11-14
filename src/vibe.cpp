#include "vibe.h"
#include <string.h>

namespace vibe {
namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool contains(const char *hay, const char *needle) {        // case-insensitive substring
  if (!hay || !needle || !needle[0]) return false;
  for (const char *h = hay; *h; ++h) {
    const char *a = h, *b = needle;
    while (*a && *b && lower(*a) == lower(*b)) { ++a; ++b; }
    if (!*b) return true;
  }
  return false;
}

struct Rule { const char *key; Vibe v; };

// One genre string is matched against this list from the top, the first hit decides. Specific phrases come before
// generic words ("dream pop" before "pop", "dance pop" before "dance", "pop rap" before "pop").
const Rule GENRE_RULES[] = {
  {"dream pop", V_DREAMY}, {"indie pop", V_DREAMY}, {"art pop", V_DREAMY}, {"bedroom pop", V_DREAMY},
  {"dance pop", V_POP}, {"electropop", V_POP}, {"synthpop", V_POP}, {"synth-pop", V_POP}, {"pop rock", V_POP}, {"k-pop", V_POP}, {"j-pop", V_POP},
  {"pop rap", V_HYPE},
  {"bollywood", V_DESI}, {"filmi", V_DESI}, {"qawwali", V_DESI}, {"ghazal", V_DESI}, {"punjabi", V_DESI}, {"pakistani", V_DESI},
  {"desi", V_DESI}, {"sufi", V_DESI}, {"hindi", V_DESI}, {"urdu", V_DESI}, {"indian", V_DESI}, {"bhangra", V_DESI}, {"lollywood", V_DESI},
  {"metal", V_ROCK}, {"punk", V_ROCK}, {"grunge", V_ROCK}, {"hardcore", V_ROCK}, {"rock", V_ROCK}, {"emo", V_ROCK},
  {"hip hop", V_HYPE}, {"hip-hop", V_HYPE}, {"rap", V_HYPE}, {"trap", V_HYPE}, {"drill", V_HYPE}, {"phonk", V_HYPE}, {"grime", V_HYPE}, {"crunk", V_HYPE},
  {"edm", V_EDM}, {"house", V_EDM}, {"techno", V_EDM}, {"trance", V_EDM}, {"dubstep", V_EDM}, {"electro", V_EDM}, {"drum and bass", V_EDM},
  {"dnb", V_EDM}, {"hardstyle", V_EDM}, {"big room", V_EDM}, {"future bass", V_EDM}, {"bass", V_EDM}, {"dance", V_EDM},
  {"lo-fi", V_CHILL}, {"lofi", V_CHILL}, {"chill", V_CHILL}, {"ambient", V_CHILL}, {"jazz", V_CHILL}, {"acoustic", V_CHILL},
  {"classical", V_CHILL}, {"piano", V_CHILL}, {"sleep", V_CHILL}, {"meditation", V_CHILL}, {"study", V_CHILL}, {"new age", V_CHILL},
  {"bossa", V_CHILL}, {"easy listening", V_CHILL}, {"instrumental", V_CHILL}, {"soundtrack", V_CHILL}, {"orchestra", V_CHILL},
  {"shoegaze", V_DREAMY}, {"dream", V_DREAMY}, {"ethereal", V_DREAMY}, {"r&b", V_DREAMY}, {"soul", V_DREAMY}, {"indie", V_DREAMY},
  {"alt z", V_DREAMY}, {"bedroom", V_DREAMY}, {"downtempo", V_DREAMY}, {"trip hop", V_DREAMY}, {"synthwave", V_DREAMY}, {"vaporwave", V_DREAMY},
  {"pop", V_POP}, {"reggaeton", V_POP}, {"latin", V_POP},
};

Vibe oneGenre(const char *g, size_t len) {
  char buf[48];
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, g, len);
  buf[len] = 0;
  for (const Rule &r : GENRE_RULES) if (contains(buf, r.key)) return r.v;
  return V_NONE;
}

}  // namespace

const char *name(Vibe v) {
  switch (v) {
    case V_CHILL: return "chill";
    case V_POP: return "pop";
    case V_HYPE: return "hype";
    case V_ROCK: return "rock";
    case V_EDM: return "edm";
    case V_DESI: return "desi";
    case V_DREAMY: return "dreamy";
    default: return "";
  }
}

Vibe fromGenres(const char *genres) {
  if (!genres || !genres[0]) return V_NONE;
  int votes[V_COUNT] = {0};
  Vibe first = V_NONE;
  const char *p = genres;
  while (*p) {
    while (*p == ',' || *p == ' ') ++p;
    const char *e = p;
    while (*e && *e != ',') ++e;
    if (e > p) {
      Vibe v = oneGenre(p, (size_t)(e - p));
      if (v != V_NONE) {
        votes[v]++;
        if (first == V_NONE) first = v;
      }
    }
    p = e;
  }
  Vibe best = first;                                           // a tie goes to the first genre that matched
  for (int v = 1; v < V_COUNT; ++v) if (votes[v] > votes[best]) best = (Vibe)v;
  return best;
}

Vibe guess(const char *title, const char *artist, bool desiScript, int pace10) {
  (void)artist;
  if (desiScript) return V_DESI;
  static const Rule WORDS[] = {
    {"lofi", V_CHILL}, {"lo-fi", V_CHILL}, {"sleep", V_CHILL}, {"chill", V_CHILL}, {"acoustic", V_CHILL}, {"piano", V_CHILL},
    {"instrumental", V_CHILL}, {"slowed", V_CHILL}, {"reverb", V_CHILL}, {"unplugged", V_CHILL},
    {"phonk", V_HYPE}, {"drill", V_HYPE}, {"freestyle", V_HYPE}, {"cypher", V_HYPE},
    {"metal", V_ROCK},
    {"extended mix", V_EDM}, {"club mix", V_EDM}, {"bass boosted", V_EDM}, {"remix", V_EDM},
  };
  for (const Rule &r : WORDS) if (contains(title, r.key)) return r.v;
  if (pace10 <= 0) return V_NONE;
  if (pace10 >= 50) return V_HYPE;                              // fast lyrics: rap
  if (pace10 >= 34) return V_POP;
  if (pace10 >= 24) return V_DREAMY;
  return V_CHILL;                                               // slow: ballad
}

int bpmFor(Vibe v, int pace10) {
  int base;
  switch (v) {
    case V_CHILL: base = 72; break;
    case V_POP: base = 108; break;
    case V_HYPE: base = 92; break;
    case V_ROCK: base = 140; break;
    case V_EDM: base = 128; break;
    case V_DESI: base = 96; break;
    case V_DREAMY: base = 84; break;
    default: base = 100; break;
  }
  if (pace10 > 0) {
    int adj = (pace10 - 35) * 2 / 5;
    if (adj > 12) adj = 12;
    if (adj < -12) adj = -12;
    base += adj;
  }
  if (base < 60) base = 60;
  if (base > 170) base = 170;
  return base;
}

int energyFor(int pace10) {
  if (pace10 <= 0) return 50;
  int e = (pace10 - 15) * 100 / 50;
  if (e < 0) e = 0;
  if (e > 100) e = 100;
  return e;
}

}  // namespace vibe
