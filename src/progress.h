#pragma once
// Long-term progress: XP and growth stages, the daily-goal streak and the badges. Pure C++ (tested on the PC in
// tools/steptest). main.cpp keeps one `Data` in flash and feeds it; this file only holds the rules.
#include <stdint.h>

namespace progress {

enum Badge {
  B_FIRST,      // first 100 steps
  B_GOAL,       // first daily goal
  B_STREAK3,    // goal 3 days in a row
  B_STREAK7,    // goal 7 days in a row
  B_TENK,       // 10,000 steps in one day
  B_LIFE10K,    // 10,000 steps lifetime
  B_LIFE100K,   // 100,000 steps lifetime
  B_PETS100,    // petted 100 times
  B_PETS1000,   // petted 1000 times
  B_SONG,       // danced to a song
  B_GAMER,      // played 10 games
  B_FOCUS,      // finished 5 focus sessions
  B_KILO,       // reached stage Kilo
  B_MEGA,       // reached stage Mega
  B_GIGA,       // reached stage Giga
  B_ALL,        // every other badge
  COUNT
};

struct Data {
  uint32_t magic = 0x50524732;   // "PRG2"
  uint32_t lifeSteps = 0;
  uint32_t songs = 0;            // songs the pet danced to
  uint32_t games = 0;            // games started
  uint32_t focus = 0;            // finished timer / pomodoro sessions
  uint32_t badges = 0;           // bit per Badge
  uint32_t bestDay = 0;          // most steps in one day
  uint16_t streak = 0;           // days in a row with the goal reached (counts today once it is reached)
  uint16_t bestStreak = 0;
};

const int STAGES = 5;
const char *stageName(int stage);                 // Bit, Byte, Kilo, Mega, Giga
uint32_t xp(const Data &d, uint32_t pets);        // steps/50 + pets*2 + songs*25 + games*10 + focus*40
int stage(uint32_t xp);                           // 0..4
uint32_t stageFloor(int stage);                   // xp at which the stage starts
uint32_t stageCeil(int stage);                    // xp at which the next one starts (the last stage returns its own floor)

const char *badgeName(int b);                     // short, fits a toast
const char *badgeHint(int b);                     // how to earn it
inline bool has(const Data &d, int b) { return (d.badges >> b) & 1u; }

// Re-checks every badge against the current numbers; returns a mask of the ones earned just now (and records them).
uint32_t evaluate(Data &d, uint32_t pets, uint32_t todaySteps);

void goalReached(Data &d);                                          // today's goal was just reached (once a day)
void dayEnded(Data &d, bool goalMet, int daysPassed);               // the day rolled over (daysPassed >= 1)
int badgeCount(const Data &d);

}  // namespace progress
