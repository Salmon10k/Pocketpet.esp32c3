#pragma once
// Drawing for the step counter (Stats "STEPS" page + the pet's walking / goal decorations).
// Kept free of main.cpp state so tools/steptest can render the same frames on the PC.
#include <U8g2lib.h>
#include "steps.h"

namespace stepsview {

struct Info {
  const steps::History *h = nullptr;
  uint32_t goal = 6000;
  bool walking = false;
  int cadence = 0;       // steps per minute
  bool clockOk = true;
  bool flash = false;    // goal just reached: ring blinks
};

void draw(U8G2 &d, const Info &in, uint32_t now);                                     // whole page below the title line
void decorWalk(U8G2 &d, int eyeY, int rightEyeX, int cadence, uint32_t now);          // speed lines (+ sweat drop when jogging)
void decorCheer(U8G2 &d, uint32_t startMs, uint32_t now);                             // confetti + "GOAL!"

}  // namespace stepsview
