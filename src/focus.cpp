#include "focus.h"

namespace focus {

namespace {
const int POMO_FOCUS[3] = {25, 50, 15};
const int POMO_BREAK[3] = {5, 10, 3};
}  // namespace

int focusMin(const Clock &c) { return POMO_FOCUS[c.preset % 3]; }
int breakMin(const Clock &c) { return c.round >= 4 ? POMO_BREAK[c.preset % 3] * 3 : POMO_BREAK[c.preset % 3]; }

static void startSegment(Clock &c) {
  c.doneMs = 0;
  if (c.kind == K_TIMER) c.durMs = (uint32_t)c.setMin * 60000UL;
  else if (c.kind == K_POMO) c.durMs = (uint32_t)(c.onBreak ? breakMin(c) : focusMin(c)) * 60000UL;
  else c.durMs = 0;
}

void init(Clock &c, Kind k) {
  c = Clock();
  c.kind = k;
  if (k == K_POMO) c.setMin = 25;
  startSegment(c);
}

void reset(Clock &c) {
  c.running = false; c.alert = false; c.lapMs = 0; c.lapSplit = 0;
  if (c.kind == K_POMO) { c.onBreak = false; c.round = 0; }
  startSegment(c);
}

bool atStart(const Clock &c) { return !c.running && c.doneMs == 0 && !c.alert; }

void adjust(Clock &c, int delta) {
  if (!atStart(c)) return;
  if (c.kind == K_TIMER) {
    int m = (int)c.setMin + delta;
    c.setMin = (uint16_t)(m < 1 ? 1 : (m > 180 ? 180 : m));
    startSegment(c);
  } else if (c.kind == K_POMO) {
    c.preset = (uint8_t)((c.preset + 3 + (delta < 0 ? -1 : 1)) % 3);
    startSegment(c);
  }
}

void addMinute(Clock &c) {
  if (c.kind != K_TIMER || c.alert) return;
  c.durMs += 60000UL;
}

void toggle(Clock &c, uint32_t now) {
  if (c.alert) {                                         // dismiss; a pomodoro goes on to its next phase when started again
    c.alert = false;
    if (c.kind == K_TIMER) { startSegment(c); }
    return;
  }
  c.running = !c.running;
  c.lastTick = now;
}

void skip(Clock &c) {
  if (c.kind != K_POMO) return;
  c.doneMs = c.durMs;                                    // the next update() finishes the phase
  c.running = true;
}

void lap(Clock &c) {
  if (c.kind != K_WATCH || !c.running) return;
  c.lapSplit = c.doneMs - c.lapMs;
  c.lapMs = c.doneMs;
}

Event update(Clock &c, uint32_t now) {
  if (!c.running) { c.lastTick = now; return EV_NONE; }
  uint32_t dt = now - c.lastTick;
  c.lastTick = now;
  c.doneMs += dt;
  if (c.kind == K_WATCH || c.doneMs < c.durMs) return EV_NONE;

  if (c.kind == K_TIMER) {
    c.running = false; c.alert = true; c.doneMs = c.durMs;
    return EV_TIMER_DONE;
  }
  // Pomodoro: focus -> break (starts by itself) -> waits for you before the next focus.
  if (!c.onBreak) {
    c.round++;
    c.onBreak = true;
    startSegment(c);                                     // uses round to pick short / long break
    c.alert = true;
    return EV_FOCUS_DONE;
  }
  c.onBreak = false;
  if (c.round >= 4) c.round = 0;
  startSegment(c);
  c.running = false;
  c.alert = true;
  return EV_BREAK_DONE;
}

uint32_t shownMs(const Clock &c) {
  if (c.kind == K_WATCH) return c.doneMs;
  return c.durMs > c.doneMs ? c.durMs - c.doneMs : 0;
}

float fraction(const Clock &c) {
  if (c.kind == K_WATCH || c.durMs == 0) return 0;
  float f = (float)c.doneMs / (float)c.durMs;
  return f > 1 ? 1 : f;
}

}  // namespace focus
