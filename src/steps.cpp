#include "steps.h"
#include <math.h>

namespace steps {

void reset(Detector &d) { d = Detector(); }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void noteStep(Detector &d, uint32_t t) {
  d.lastStepAt = t;
  if (d.recentN < 6) d.recent[d.recentN++] = t;
  else { for (int i = 0; i < 5; ++i) d.recent[i] = d.recent[i + 1]; d.recent[5] = t; }
}

int feed(Detector &d, float ax, float ay, float az, uint32_t now, bool blocked) {
  float m = sqrtf(ax * ax + ay * ay + az * az);
  if (!d.primed) { d.sm = d.base = m; d.primed = true; }
  d.sm += (m - d.sm) * 0.35f;                         // ~3 Hz low-pass at 50 Hz sampling
  d.base += (d.sm - d.base) * 0.02f;                  // slow gravity estimate
  float s = d.sm - d.base;

  if (blocked) {                                       // shaking, game running ...
    d.chain = 0; d.validated = false; d.armed = false; d.rising = false;
    d.holdoffUntil = now + 800;
    d.recentN = 0;
    d.prev2 = d.prev; d.prev = s;
    return 0;
  }

  d.env = clampf(d.env * 0.999f, 0.11f, 1.0f);       // with no peaks the threshold slowly relaxes to find soft steps
  float thr = clampf(0.45f * d.env, 0.05f, 0.45f);
  int add = 0;

  if (now > d.lastStepAt + 1500 && d.validated) { d.validated = false; d.chain = 0; }   // stopped walking
  if (d.chain > 0 && now > d.chainT[d.chain - 1] + 1100 && !d.validated) d.chain = 0;     // too slow to be a chain

  if (!d.armed && s < thr * 0.35f && now >= d.holdoffUntil) d.armed = true;

  // Local maximum above the threshold (previous sample was the top).
  bool isPeak = d.armed && d.prev > d.prev2 && d.prev >= s && d.prev > thr;
  if (isPeak && now >= d.holdoffUntil) {
    float h = d.prev;
    uint32_t t = now - 20;                             // the peak was one sample ago
    uint32_t gap = t - d.lastPeak;
    bool tooFast = d.lastPeak && gap < 300;
    bool tooBig = h > 2.2f;                            // a bang, not a footstep
    d.armed = false;
    if (!tooFast && !tooBig) {
      d.env = clampf(d.env * 0.7f + h * 0.3f, 0.11f, 1.0f);
      d.lastPeak = t;
      if (d.validated) {
        if (gap <= 1500) { add = 1; noteStep(d, t); }
        else { d.validated = false; d.chain = 1; d.chainT[0] = t; }
      } else {
        if (d.chain > 0 && t - d.chainT[d.chain - 1] > 1000) d.chain = 0;
        if (d.chain < 4) d.chainT[d.chain++] = t;
        if (d.chain == 4) {
          uint32_t g0 = d.chainT[1] - d.chainT[0], g1 = d.chainT[2] - d.chainT[1], g2 = d.chainT[3] - d.chainT[2];
          uint32_t lo = g0 < g1 ? g0 : g1; lo = lo < g2 ? lo : g2;
          uint32_t hi = g0 > g1 ? g0 : g1; hi = hi > g2 ? hi : g2;
          if (lo >= 300 && hi <= 1000 && hi * 10 <= lo * 18) {   // steady: slowest <= 1.8x fastest
            d.validated = true;
            add = 4;
            for (int i = 0; i < 4; ++i) noteStep(d, d.chainT[i]);
          } else {                                     // slide the window by one
            d.chainT[0] = d.chainT[1]; d.chainT[1] = d.chainT[2]; d.chainT[2] = d.chainT[3]; d.chain = 3;
          }
        }
      }
    }
  } else if (d.armed && d.prev > d.prev2 && d.prev >= s && d.prev > thr && now < d.holdoffUntil) {
    d.armed = false;
  }
  d.prev2 = d.prev; d.prev = s;
  return add;
}

bool walking(const Detector &d, uint32_t now) { return d.validated && now - d.lastStepAt < 2000; }

int cadence(const Detector &d, uint32_t now) {
  if (!walking(d, now) || d.recentN < 3) return 0;
  uint32_t span = d.recent[d.recentN - 1] - d.recent[0];
  if (span == 0) return 0;
  return (int)((uint32_t)(d.recentN - 1) * 60000UL / span);
}

int32_t daysFromCivil(int y, int m, int d) {          // Howard Hinnant's algorithm
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}

void rollTo(History &h, int32_t today) {
  if (h.day == 0) { h.day = today; return; }
  int32_t diff = today - h.day;
  if (diff <= 0) return;                               // same day (or the clock went backwards: keep counting)
  if (diff >= DAYS) { for (int i = 0; i < DAYS; ++i) h.cnt[i] = 0; }
  else {
    for (int i = DAYS - 1; i >= 0; --i) h.cnt[i] = (i >= diff) ? h.cnt[i - diff] : 0;
  }
  h.day = today;
}

uint32_t weekTotal(const History &h) { uint32_t t = 0; for (int i = 0; i < DAYS; ++i) t += h.cnt[i]; return t; }
uint32_t bestDay(const History &h) { uint32_t b = 0; for (int i = 0; i < DAYS; ++i) if (h.cnt[i] > b) b = h.cnt[i]; return b; }
int distanceM(uint32_t n) { return (int)(n * 3UL / 4UL); }
int kcal(uint32_t n) { return (int)(n * 4UL / 100UL); }

}  // namespace steps
