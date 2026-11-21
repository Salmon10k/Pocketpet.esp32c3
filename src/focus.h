#pragma once
// Timer, Pomodoro and Stopwatch (Tools menu). Pure logic on a millis() value you pass in, tested on the PC
// (tools/steptest); the page is drawn by focus_view.cpp. The three clocks run in the background: when one finishes,
// main.cpp wakes the screen and opens its page. There is no buzzer on the board, so the alert is visual.
#include <stdint.h>

namespace focus {

enum Kind { K_TIMER, K_POMO, K_WATCH };
enum Event { EV_NONE, EV_TIMER_DONE, EV_FOCUS_DONE, EV_BREAK_DONE };

struct Clock {
  Kind kind = K_TIMER;
  bool running = false;
  bool onBreak = false;          // pomodoro: in a break
  bool alert = false;            // finished and waiting to be dismissed (any button)
  uint8_t round = 0;             // pomodoro: focus sessions finished in this set of four
  uint8_t preset = 0;            // pomodoro preset 0..2
  uint16_t setMin = 10;          // timer length in minutes
  uint32_t durMs = 0;            // length of the current countdown segment (0 for the stopwatch)
  uint32_t doneMs = 0;           // time elapsed in the segment (frozen while paused)
  uint32_t lastTick = 0;
  uint32_t lapMs = 0;            // stopwatch: time of the last lap
  uint32_t lapSplit = 0;         // stopwatch: length of the last lap
};

void init(Clock &c, Kind k);
void toggle(Clock &c, uint32_t now);                 // start / pause (also dismisses a finished alert)
void reset(Clock &c);                                // back to the start of the current segment / the setup
void adjust(Clock &c, int delta);                    // before it starts: timer minutes, pomodoro preset
void addMinute(Clock &c);                            // timer: +1 minute while running
void skip(Clock &c);                                 // pomodoro: end the current phase now
void lap(Clock &c);                                  // stopwatch
Event update(Clock &c, uint32_t now);                // call often; returns an event when something just finished
bool atStart(const Clock &c);                        // nothing elapsed yet (setup state)
uint32_t shownMs(const Clock &c);                    // remaining (timer, pomodoro) or elapsed (stopwatch)
float fraction(const Clock &c);                      // 0..1 progress through the segment (0 for the stopwatch)
int focusMin(const Clock &c);                        // pomodoro focus length of the preset
int breakMin(const Clock &c);                        // short break (or long break after the fourth round)

}  // namespace focus
