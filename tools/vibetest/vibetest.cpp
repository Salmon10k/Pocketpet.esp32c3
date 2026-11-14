// PC test for src/vibe.cpp (genre -> vibe, guessing, feel-tempo). Run: ./run.sh
#include "../../src/vibe.h"
#include <cstdio>
#include <cstring>
using namespace vibe;

static int fails = 0, total = 0;
static void expect(const char *what, Vibe got, Vibe want) {
  total++;
  if (got != want) { fails++; printf("FAIL %-44s got %-7s want %s\n", what, name(got)[0] ? name(got) : "none", name(want)[0] ? name(want) : "none"); }
}
static void expectInt(const char *what, int got, int want) {
  total++;
  if (got != want) { fails++; printf("FAIL %-44s got %d want %d\n", what, got, want); }
}

int main() {
  // genres (how Spotify sends them: lower case, joined by commas)
  expect("rap, hip hop, trap", fromGenres("rap, hip hop, trap"), V_HYPE);
  expect("pakistani hip hop", fromGenres("pakistani hip hop"), V_DESI);               // desi beats hip hop for that one string
  expect("dream pop, indie pop", fromGenres("dream pop, indie pop"), V_DREAMY);
  expect("dance pop, pop", fromGenres("dance pop, pop"), V_POP);
  expect("bollywood", fromGenres("bollywood"), V_DESI);
  expect("pakistani pop, desi pop", fromGenres("pakistani pop, desi pop"), V_DESI);
  expect("lo-fi beats, chillhop", fromGenres("lo-fi beats, chillhop"), V_CHILL);
  expect("hard rock, metal", fromGenres("hard rock, metal"), V_ROCK);
  expect("edm, big room", fromGenres("edm, big room"), V_EDM);
  expect("pop rap", fromGenres("pop rap"), V_HYPE);
  expect("k-pop", fromGenres("k-pop"), V_POP);
  expect("synthpop", fromGenres("synthpop"), V_POP);
  expect("alt z, bedroom pop (SZA-like)", fromGenres("alt z, bedroom pop"), V_DREAMY);
  expect("r&b, pop -> tie, first genre wins", fromGenres("r&b, pop"), V_DREAMY);
  expect("pop, r&b -> tie, first genre wins", fromGenres("pop, r&b"), V_POP);
  expect("pop, dance pop, r&b -> pop wins 2:1", fromGenres("pop, dance pop, r&b"), V_POP);
  expect("classic rock", fromGenres("classic rock"), V_ROCK);
  expect("deep house", fromGenres("deep house"), V_EDM);
  expect("uppercase works", fromGenres("HIP HOP"), V_HYPE);
  expect("empty", fromGenres(""), V_NONE);
  expect("null", fromGenres(nullptr), V_NONE);
  expect("unknown genre", fromGenres("polka, yodeling"), V_NONE);
  expect("one unknown, one known", fromGenres("polka, jazz"), V_CHILL);
  expect("very long genre does not overflow", fromGenres("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa rap"), V_NONE);

  // guessing without genres
  expect("title: Lofi Remix -> chill (lofi first)", guess("Song (Lofi Remix)", "", false, 0), V_CHILL);
  expect("title: Club Mix", guess("Name - Club Mix", "", false, 0), V_EDM);
  expect("title: phonk", guess("PHONK EDIT", "", false, 0), V_HYPE);
  expect("urdu / arabic script", guess("x", "y", true, 0), V_DESI);
  expect("fast lyrics -> hype", guess("x", "y", false, 60), V_HYPE);
  expect("medium lyrics -> pop", guess("x", "y", false, 40), V_POP);
  expect("slowish lyrics -> dreamy", guess("x", "y", false, 28), V_DREAMY);
  expect("slow lyrics -> chill", guess("x", "y", false, 15), V_CHILL);
  expect("nothing known", guess("Some Song", "Someone", false, 0), V_NONE);

  // tempo + energy
  expectInt("bpm chill", bpmFor(V_CHILL, 0), 72);
  expectInt("bpm rock fast lyrics (+12 cap)", bpmFor(V_ROCK, 90), 152);
  expectInt("bpm hype slow lyrics (-12 cap)", bpmFor(V_HYPE, 5), 80);
  expectInt("bpm unknown vibe", bpmFor(V_NONE, 0), 100);
  for (int v = 0; v < V_COUNT; ++v) for (int p = 0; p <= 120; p += 5) {
    int b = bpmFor((Vibe)v, p);
    total++; if (b < 60 || b > 170) { fails++; printf("FAIL bpm out of range v=%d pace=%d -> %d\n", v, p, b); }
  }
  expectInt("energy unknown", energyFor(0), 50);
  expectInt("energy slow", energyFor(15), 0);
  expectInt("energy fast", energyFor(65), 100);
  expectInt("energy above range", energyFor(200), 100);

  // names are short enough for the 5x7 label (max 6 letters + '?')
  for (int v = 1; v < V_COUNT; ++v) { total++; if (strlen(name((Vibe)v)) == 0 || strlen(name((Vibe)v)) > 6) { fails++; printf("FAIL name length %d\n", v); } }

  printf("%d checks, %d failed\n", total, fails);
  return fails ? 1 : 0;
}
