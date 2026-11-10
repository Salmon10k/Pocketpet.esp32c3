#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// The home menu that opens from the Pet screen. Two styles, chosen in Settings:
//   0 = side-scroll carousel (A/C scroll, icons glide in from the right)
//   1 = tilt ring (apps sit on a ring, tilt the device toward one to highlight it)
// B opens the highlighted app. This module only draws and tracks the selection; main.cpp starts the app.
namespace launcher {
const int APP_COUNT = 10;
const int STYLE_CAROUSEL = 0;
const int STYLE_RING = 1;

void enter(int selected, int style, uint32_t now);
// Returns the chosen app index (0..APP_COUNT-1) when B is pressed, otherwise -1. tiltX/tiltY: relative tilt
// (+x right, +y down, ~0.5 = a firm tilt), tiltValid false when there is no usable motion sensor.
int input(bool a, bool b, bool c, uint32_t now, float tiltX, float tiltY, bool tiltValid);
void draw(U8G2 &d, uint32_t now);
int selected();
uint32_t lastInputAt();
const char *appName(int app);
void drawIcon(U8G2 &d, int app, int cx, int cy, int r, uint32_t t);   // current draw colour; t = animation clock (ms), 0 = still
}  // namespace launcher
