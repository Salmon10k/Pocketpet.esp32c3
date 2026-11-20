#include "progress.h"

namespace progress {

namespace {
const char *STAGE_NAMES[STAGES] = {"Bit", "Byte", "Kilo", "Mega", "Giga"};
const uint32_t STAGE_XP[STAGES] = {0, 300, 1200, 3000, 7000};   // about 3 days, 10 days, 25 days, 58 days of ~6000 steps

struct Info { const char *name; const char *hint; };
const Info BADGES[COUNT] = {
  {"First steps", "Walk 100 steps"},
  {"Goal getter", "Reach a daily goal"},
  {"On a roll", "Goal 3 days in a row"},
  {"Unstoppable", "Goal 7 days in a row"},
  {"10K day", "10,000 steps in a day"},
  {"Wanderer", "10,000 steps total"},
  {"Marathoner", "100,000 steps total"},
  {"Best friend", "Pet it 100 times"},
  {"Cuddle king", "Pet it 1000 times"},
  {"Dancer", "Play music near it"},
  {"Gamer", "Play 10 games"},
  {"Focused", "Finish 5 focus timers"},
  {"Kilo!", "Grow to Kilo"},
  {"Mega!", "Grow to Mega"},
  {"Giga!", "Grow to Giga"},
  {"Collector", "Earn every badge"},
};
}  // namespace

const char *stageName(int s) { return STAGE_NAMES[s < 0 ? 0 : (s >= STAGES ? STAGES - 1 : s)]; }

uint32_t xp(const Data &d, uint32_t pets) {
  return d.lifeSteps / 50 + pets * 2 + d.songs * 25 + d.games * 10 + d.focus * 40;
}

int stage(uint32_t x) {
  int s = 0;
  for (int i = 1; i < STAGES; ++i) if (x >= STAGE_XP[i]) s = i;
  return s;
}

uint32_t stageFloor(int s) { return STAGE_XP[s < 0 ? 0 : (s >= STAGES ? STAGES - 1 : s)]; }
uint32_t stageCeil(int s) { return s + 1 >= STAGES ? STAGE_XP[STAGES - 1] : STAGE_XP[s + 1]; }

const char *badgeName(int b) { return (b < 0 || b >= COUNT) ? "" : BADGES[b].name; }
const char *badgeHint(int b) { return (b < 0 || b >= COUNT) ? "" : BADGES[b].hint; }

int badgeCount(const Data &d) {
  int n = 0;
  for (int i = 0; i < COUNT; ++i) n += has(d, i);
  return n;
}

uint32_t evaluate(Data &d, uint32_t pets, uint32_t today) {
  if (today > d.bestDay) d.bestDay = today;
  int st = stage(xp(d, pets));
  bool want[COUNT] = {};
  want[B_FIRST]     = d.lifeSteps >= 100;
  want[B_GOAL]      = d.bestStreak >= 1;
  want[B_STREAK3]   = d.bestStreak >= 3;
  want[B_STREAK7]   = d.bestStreak >= 7;
  want[B_TENK]      = d.bestDay >= 10000;
  want[B_LIFE10K]   = d.lifeSteps >= 10000;
  want[B_LIFE100K]  = d.lifeSteps >= 100000;
  want[B_PETS100]   = pets >= 100;
  want[B_PETS1000]  = pets >= 1000;
  want[B_SONG]      = d.songs >= 1;
  want[B_GAMER]     = d.games >= 10;
  want[B_FOCUS]     = d.focus >= 5;
  want[B_KILO]      = st >= 2;
  want[B_MEGA]      = st >= 3;
  want[B_GIGA]      = st >= 4;
  uint32_t fresh = 0;
  for (int i = 0; i < B_ALL; ++i)
    if (want[i] && !has(d, i)) { d.badges |= 1u << i; fresh |= 1u << i; }
  uint32_t others = (1u << B_ALL) - 1;
  if (!has(d, B_ALL) && (d.badges & others) == others) { d.badges |= 1u << B_ALL; fresh |= 1u << B_ALL; }
  return fresh;
}

void goalReached(Data &d) {
  d.streak++;
  if (d.streak > d.bestStreak) d.bestStreak = d.streak;
}

void dayEnded(Data &d, bool goalMet, int daysPassed) {
  if (!goalMet || daysPassed > 1) d.streak = 0;
}

}  // namespace progress
