// PC test for src/steps.cpp: simulated walking / jogging / shaking / idle / fidgeting.
#include "../../src/steps.h"
#include "../../src/progress.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

static float frand() { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

// Walk for `sec` seconds at `spm` steps/min, impact amplitude `amp` (g), noise `noise`, jitter on the timing.
static int run(steps::Detector &d, uint32_t &t, float sec, float spm, float amp, float noise, float jitter = 0.05f) {
  int total = 0;
  float period = 60000.0f / spm, nextStep = t + period;
  uint32_t end = t + (uint32_t)(sec * 1000);
  for (; t < end; t += 20) {
    float a = 1.0f + noise * frand();
    float dt = (float)t - nextStep;
    if (spm > 0 && fabsf(dt) < 90) a += amp * expf(-dt * dt / (2 * 30.0f * 30.0f));   // impact bump
    if (spm > 0 && dt >= 90) nextStep += period * (1.0f + jitter * frand());
    total += steps::feed(d, 0.05f * frand(), 0.05f * frand(), a, t, false);
  }
  return total;
}

int main() {
  srand(7);
  { steps::Detector d; uint32_t t = 1000; int n = run(d, t, 60, 110, 0.35f, 0.03f);
    printf("walk 60s @110spm: %d (expect ~110)\n", n); CHECK(n >= 98 && n <= 115, "walk count %d", n); }
  { steps::Detector d; uint32_t t = 1000; int n = run(d, t, 60, 80, 0.30f, 0.03f);
    printf("slow walk 60s @80spm: %d\n", n); CHECK(n >= 70 && n <= 86, "slow count %d", n); }
  { steps::Detector d; uint32_t t = 1000; int n = run(d, t, 60, 170, 0.8f, 0.05f);
    printf("jog 60s @170spm: %d\n", n); CHECK(n >= 150 && n <= 178, "jog count %d", n); }
  { steps::Detector d; uint32_t t = 1000; int n = run(d, t, 60, 110, 0.12f, 0.03f);
    printf("soft walk 60s @110spm amp .12: %d\n", n); CHECK(n >= 80, "soft walk count %d", n); }
  { steps::Detector d; uint32_t t = 1000; int n = run(d, t, 120, 0, 0, 0.04f);
    printf("idle 120s noise: %d\n", n); CHECK(n == 0, "idle counted %d", n); }
  { steps::Detector d; uint32_t t = 1000; int n = 0;                       // random fidgeting: irregular bumps
    for (int k = 0; k < 40; ++k) { n += run(d, t, 1.0f + 3.0f * fabsf(frand()), 0, 0, 0.03f);
      for (int i = 0; i < 3; ++i) { n += steps::feed(d, 0, 0, 1.5f, t, false); t += 20; for (int j = 0; j < 20; ++j) { n += steps::feed(d, 0, 0, 1.0f, t, false); t += 20; } } }
    printf("fidget (random taps) : %d\n", n); CHECK(n <= 8, "fidget counted %d", n); }
  { steps::Detector d; uint32_t t = 1000; int n = 0;                       // shaking: fast big swings, blocked flag
    for (int i = 0; i < 1500; ++i, t += 20) n += steps::feed(d, 2.5f * sinf(t / 90.0f), 0, 1.0f + 1.5f * sinf(t / 90.0f), t, true);
    printf("shake 30s (blocked): %d\n", n); CHECK(n == 0, "shake counted %d", n); }
  { steps::Detector d; uint32_t t = 1000; int n = 0;                       // fast rhythmic shaking, NOT flagged: must not exceed 5 steps/s
    for (int i = 0; i < 1500; ++i, t += 20) n += steps::feed(d, 0, 0, 1.0f + 1.2f * sinf(t / 80.0f), t, false);
    printf("fast shake unflagged 30s: %d\n", n); CHECK(n <= 30 * 3, "fast shake counted %d", n); }
  { steps::Detector d; uint32_t t = 1000; int n = run(d, t, 20, 110, 0.35f, 0.03f);   // walk, stop, walk again
    int n2 = run(d, t, 10, 0, 0, 0.03f); int n3 = run(d, t, 20, 110, 0.35f, 0.03f);
    printf("walk/stop/walk: %d %d %d\n", n, n2, n3); CHECK(n2 <= 1 && n3 >= 30, "stop/start %d %d", n2, n3);
    CHECK(!steps::walking(d, t - 20 + 3000), "still walking 3 s after the end"); }
  { steps::Detector d; uint32_t t = 1000; run(d, t, 15, 110, 0.35f, 0.03f);
    int c = steps::cadence(d, t - 100); printf("cadence: %d (expect ~110)\n", c); CHECK(c > 95 && c < 125, "cadence %d", c); }

  { steps::Detector d; uint32_t t = 0; long n = 0;                          // fuzz: wild values must not crash or explode
    for (int i = 0; i < 400000; ++i, t += 20) n += steps::feed(d, 8 * frand(), 8 * frand(), 8 * frand(), t, (rand() % 50) == 0);
    printf("fuzz 400k random samples: %ld steps\n", n); CHECK(n < 400000 / 4, "fuzz counted %ld", n);
    steps::feed(d, 0, 0, 0, t, false); }

  // Calendar helpers.
  CHECK(steps::daysFromCivil(1970, 1, 1) == 0, "epoch");
  CHECK(steps::daysFromCivil(2026, 11, 16) - steps::daysFromCivil(2026, 11, 15) == 1, "adjacent days");
  CHECK(steps::daysFromCivil(2024, 3, 1) - steps::daysFromCivil(2024, 2, 28) == 2, "leap year");
  CHECK(steps::daysFromCivil(2027, 1, 1) - steps::daysFromCivil(2026, 12, 31) == 1, "new year");
  { steps::History h; steps::rollTo(h, 100); h.cnt[0] = 500;
    steps::rollTo(h, 100); CHECK(h.cnt[0] == 500, "same day keeps");
    steps::rollTo(h, 101); CHECK(h.cnt[0] == 0 && h.cnt[1] == 500, "roll 1");
    h.cnt[0] = 7; steps::rollTo(h, 103); CHECK(h.cnt[0] == 0 && h.cnt[1] == 0 && h.cnt[2] == 7 && h.cnt[3] == 500, "roll 2");
    steps::rollTo(h, 110); CHECK(steps::weekTotal(h) == 0, "roll > week clears");
    steps::rollTo(h, 50); CHECK(h.day == 110, "clock going backwards ignored"); }

  // Progress: stages, streaks, badges.
  { progress::Data d;
    CHECK(progress::stage(0) == 0 && progress::stage(299) == 0 && progress::stage(300) == 1 && progress::stage(7000) == 4 && progress::stage(999999) == 4, "stage thresholds");
    CHECK(progress::stageCeil(0) == 300 && progress::stageCeil(4) == 7000, "stage ceil");
    d.lifeSteps = 6000; CHECK(progress::xp(d, 10) == 140, "xp %u", progress::xp(d, 10));
    d.lifeSteps = 0; CHECK(progress::evaluate(d, 0, 50) == 0, "nothing yet");
    d.lifeSteps = 120; uint32_t f = progress::evaluate(d, 0, 120);
    CHECK(f == (1u << progress::B_FIRST), "first steps only (%x)", f);
    CHECK(progress::evaluate(d, 0, 120) == 0, "badges only fire once");
    progress::goalReached(d); progress::dayEnded(d, true, 1); progress::goalReached(d);
    CHECK(d.streak == 2 && d.bestStreak == 2, "streak builds %d", d.streak);
    progress::dayEnded(d, true, 1); progress::goalReached(d);
    CHECK(d.streak == 3, "streak 3");
    CHECK((progress::evaluate(d, 0, 6000) & (1u << progress::B_STREAK3)), "streak3 badge");
    progress::dayEnded(d, true, 2);
    CHECK(d.streak == 0 && d.bestStreak == 3, "a missed day resets");
    progress::dayEnded(d, false, 1); CHECK(d.streak == 0, "goal not met resets");
    d.lifeSteps = 200000; d.songs = 3; d.games = 12; d.focus = 6; d.bestDay = 12000;
    progress::evaluate(d, 1500, 0);
    CHECK(progress::has(d, progress::B_LIFE100K) && progress::has(d, progress::B_PETS1000) && progress::has(d, progress::B_TENK) && progress::has(d, progress::B_GIGA), "big numbers");
    d.bestStreak = 7; progress::evaluate(d, 1500, 0);
    CHECK(progress::has(d, progress::B_ALL) && progress::badgeCount(d) == progress::COUNT, "collector at %d", progress::badgeCount(d));
    for (int b = 0; b < progress::COUNT; ++b) CHECK(progress::badgeName(b)[0] && progress::badgeHint(b)[0], "badge text %d", b);
  }

  printf(fails ? "\n%d FAILED\n" : "\nall step tests passed\n", fails);
  return fails ? 1 : 0;
}
