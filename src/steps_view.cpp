#include "steps_view.h"
#include <math.h>
#include <stdio.h>

namespace stepsview {

// The steps page: a progress ring made of ticks (fills as you walk, flashes when the goal is hit), the numbers on the
// right and the last 7 days as bars with the goal as a dotted line.
void draw(U8G2 &display, const Info &in, uint32_t now) {
  char line[32];
  const steps::History &stepHist = *in.h;
  const uint32_t goal = in.goal;
  const uint32_t today = stepHist.cnt[0];
  const bool walkingNow = in.walking;
  const bool flash = in.flash;

  const int cx = 26, cy = 40, N = 48;
  float frac = goal ? (float)today / (float)goal : 0;
  if (frac > 1) frac = 1;
  int lit = (int)(frac * N + 0.5f);
  for (int k = 0; k < N; ++k) {
    float ang = -1.5708f + k * 6.2832f / N;
    int x = cx + (int)roundf(cosf(ang) * 21), y = cy + (int)roundf(sinf(ang) * 21);
    bool on = k < lit;
    if (on != flash) display.drawBox(x - 1, y - 1, 3, 3);
    else display.drawPixel(x, y);
  }
  // Inside: today's steps (and the %), with a footprint pair that alternates while walking.
  snprintf(line, sizeof(line), "%lu", (unsigned long)today);
  display.setFont(today >= 10000 ? u8g2_font_5x7_tf : u8g2_font_6x10_tf);
  display.drawStr(cx - display.getStrWidth(line) / 2, cy + 2, line);
  display.setFont(u8g2_font_5x7_tf);
  if (walkingNow) {
    int f = (now / 280) & 1;                                         // two little feet taking turns
    display.drawBox(cx - 5, cy + 6 - f * 2, 3, 4);
    display.drawBox(cx + 2, cy + 4 + f * 2, 3, 4);
  } else {
    snprintf(line, sizeof(line), "%d%%", (int)(frac * 100));
    display.drawStr(cx - display.getStrWidth(line) / 2, cy + 11, line);
  }
  if (today >= goal) display.drawStr(cx - 2, cy - 7, "*");

  display.setFont(u8g2_font_5x7_tf);
  snprintf(line, sizeof(line), "GOAL %lu", (unsigned long)goal);
  display.drawStr(56, 24, line);
  snprintf(line, sizeof(line), "YEST %lu", (unsigned long)stepHist.cnt[1]);
  display.drawStr(56, 32, line);
  if (walkingNow) {
    snprintf(line, sizeof(line), "walking %d/min", in.cadence);
    if ((now / 500) & 1) display.drawStr(56, 40, line);
  } else if (!in.clockOk) {
    display.drawStr(56, 40, "no clock sync");
  } else {
    snprintf(line, sizeof(line), "%d.%dkm %dkcal", steps::distanceM(today) / 1000, (steps::distanceM(today) % 1000) / 100, steps::kcal(today));
    display.drawStr(56, 40, line);
  }

  uint32_t top = steps::bestDay(stepHist);
  if (top < goal) top = goal;
  if (top == 0) top = 1;
  for (int i = steps::DAYS - 1; i >= 0; --i) {
    int bx = 58 + (steps::DAYS - 1 - i) * 10;
    int h = (int)((uint64_t)stepHist.cnt[i] * 15 / top);
    if (stepHist.cnt[i] > 0 && h < 1) h = 1;
    if (h > 0) display.drawBox(bx, 62 - h, 8, h);
    else display.drawHLine(bx, 61, 8);
    if (i == 0) display.drawHLine(bx, 63, 8);                         // underline = today
  }
  int gy = 62 - (int)((uint64_t)goal * 15 / top);
  for (int x = 56; x < 128; x += 4) display.drawPixel(x, gy);          // goal line (dotted)
}

void decorWalk(U8G2 &display, int cy, int rightEyeX, int cadence, uint32_t now) {
  for (int i = 0; i < 3; ++i) display.drawHLine(16 - (int)((now / 12 + i * 7) % 17), cy - 8 + i * 9, 3 + (i & 1) * 3);   // speed lines
  if (cadence >= 150) {                                                                                                // jogging: sweat drop
    int sx = rightEyeX + 22, sy = cy - 15 + (int)((now / 90) % 4);
    display.drawTriangle(sx, sy - 3, sx - 2, sy, sx + 2, sy);
    display.drawDisc(sx, sy + 1, 2);
  }
}

void decorCheer(U8G2 &display, uint32_t startMs, uint32_t now) {
  uint32_t age = now - startMs;
  for (int i = 0; i < 14; ++i) {
    int x = ((i * 37 + (int)(sinf(i * 1.7f + age / 400.0f) * 6)) % 128 + 128) % 128;
    int y = (int)((age / 22 + i * 13) % 70) - 4;
    if (y >= 0 && y < 62) display.drawBox(x, y, 2, 2);
  }
  display.setFont(u8g2_font_5x7_tf);
  const char *g = "GOAL!";
  if ((age / 250) & 1) display.drawStr(64 - display.getStrWidth(g) / 2, 7, g);      // blinks at the top
}

}  // namespace stepsview
