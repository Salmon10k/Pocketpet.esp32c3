#pragma once
// Drawing for the step counter (Stats "STEPS" page + the pet's walking / goal decorations).
// Kept free of main.cpp state so tools/steptest can render the same frames on the PC.
#include <U8g2lib.h>
#include "steps.h"
#include "progress.h"

namespace stepsview {

struct Info {
  const steps::History *h = nullptr;
  uint32_t goal = 6000;
  bool walking = false;
  int cadence = 0;       // steps per minute
  bool clockOk = true;
  bool flash = false;    // goal just reached: ring blinks
  int streak = 0;        // days in a row with the goal reached (0 = hide)
};

void draw(U8G2 &d, const Info &in, uint32_t now);                                     // whole page below the title line
void decorWalk(U8G2 &d, int eyeY, int rightEyeX, int cadence, uint32_t now);          // speed lines (+ sweat drop when jogging)
void decorCheer(U8G2 &d, uint32_t startMs, uint32_t now, const char *banner = "GOAL!");   // confetti + a blinking banner

// Stats "BADGES" page body: 8x2 grid (earned = solid with its number, locked = outline) and a line that cycles through hints.
void badges(U8G2 &d, const progress::Data &p, uint32_t now);
// Toast banner at the top (slides in / out), age = ms since it appeared (shown for ~3.4 s).
void toast(U8G2 &d, const char *line1, const char *line2, uint32_t age);

}  // namespace stepsview
