#pragma once
// Step counter. Pure C++ (no Arduino), so tools/steptest can run it on the PC against simulated walking.
//
// Detection: accel magnitude -> low-pass -> minus slow gravity baseline -> adaptive peak threshold.
// A peak is only counted once the rhythm looks like walking (4 peaks in a row, 0.3-1.0 s apart); those first
// four are credited retroactively. After that every peak <= 1.5 s after the last one counts. Shaking / being
// played with resets the chain, so waving the keychain around does not farm steps.
#include <stdint.h>

namespace steps {

struct Detector {
  float sm = 1.0f;          // smoothed |a| in g
  float base = 1.0f;        // slow baseline (gravity)
  float env = 0.30f;        // typical peak height, drives the threshold
  float prev = 0, prev2 = 0;
  float peakMax = 0;
  bool armed = true, rising = false;
  uint32_t lastPeak = 0, holdoffUntil = 0, lastStepAt = 0;
  uint8_t chain = 0;        // candidate peaks in a row with walking-like spacing
  uint32_t chainT[4] = {0, 0, 0, 0};
  bool validated = false;
  bool primed = false;
  uint32_t recent[6] = {0, 0, 0, 0, 0, 0};   // last step times (for cadence)
  uint8_t recentN = 0;
};

void reset(Detector &d);
// One accelerometer sample (g). `blocked` = the device is being shaken / used (games, fluid ...): reset the chain.
// Returns the number of steps to add (0 normally, 4 when walking is first recognised, then 1 per step).
int feed(Detector &d, float ax, float ay, float az, uint32_t nowMs, bool blocked);
bool walking(const Detector &d, uint32_t nowMs);    // a step in the last 2 s
int cadence(const Detector &d, uint32_t nowMs);     // steps per minute, 0 if not walking

// ---- Per-day history (today + 6 days back) ----
const int DAYS = 7;
struct History {
  int32_t day = 0;               // day number of cnt[0] (0 = no clock yet)
  uint32_t cnt[DAYS] = {0};      // cnt[0] today, cnt[1] yesterday ...
};
int32_t daysFromCivil(int y, int m, int d);                         // days since 1970-01-01
void rollTo(History &h, int32_t today);                             // shifts the history when the day changed
uint32_t weekTotal(const History &h);
uint32_t bestDay(const History &h);
int distanceM(uint32_t stepsN);                                     // rough metres (0.75 m / step)
int kcal(uint32_t stepsN);                                          // rough kcal (0.04 per step)

}  // namespace steps
