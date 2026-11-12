#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// The home menu that opens from the Pet screen, and the same look for every sub-menu (Tools, Games, Settings).
// Two styles, chosen in Settings:
//   0 = side-scroll carousel (A/C scroll, icons glide in from the right)
//   1 = tilt ring (items sit on a ring, tilt the device toward one to highlight it)
// B opens the highlighted item. This module only draws and tracks the selection; main.cpp starts the apps.
namespace launcher {
const int APP_COUNT = 10;
const int STYLE_CAROUSEL = 0;
const int STYLE_RING = 1;

// Icon ids for drawIcon(). 0..9 are the home apps; the rest are used by the sub-menus.
enum Icon {
  ICON_FLUID = 0, ICON_STATS, ICON_MOTION, ICON_TOOLS, ICON_GAMES, ICON_ONLINE, ICON_SPOTIFY, ICON_VISUAL, ICON_LIVING, ICON_SETTINGS,
  ICON_BACK = 10, ICON_8BALL, ICON_DICE,
  ICON_STAR, ICON_SNIPER, ICON_ROAD, ICON_PINBALL, ICON_MAZE, ICON_SNAKE,
  ICON_BRIGHT, ICON_SCREEN_OFF, ICON_FLIP, ICON_STYLE, ICON_SYNC, ICON_RESET,
  ICON_CLOCK, ICON_WEATHER, ICON_FORECAST, ICON_AQI, ICON_CURRENCY, ICON_WORLD, ICON_FACT, ICON_ADVICE,
  ICON_MOON, ICON_NET, ICON_ISS, ICON_GITHUB
};

struct Item { const char *name; int icon; };

void enter(int selected, int style, uint32_t now);   // the home launcher (10 apps)
// A sub-menu with the same look. `items` must stay alive while the menu is open. `value` (optional) returns a short
// text for item i (shown in a pill under the name, used by Settings); return "" for no pill.
void enterSub(const Item *items, int count, int selected, int style, uint32_t now, const char *(*value)(int) = nullptr);
void restyle(int style, uint32_t now);               // switch carousel <-> ring without losing the selection
// Returns the chosen item index when B is pressed, otherwise -1. tiltX/tiltY: relative tilt
// (+x right, +y down, ~0.5 = a firm tilt), tiltValid false when there is no usable motion sensor.
int input(bool a, bool b, bool c, uint32_t now, float tiltX, float tiltY, bool tiltValid);
void draw(U8G2 &d, uint32_t now);
int selected();
int count();
uint32_t lastInputAt();
const char *appName(int app);
void drawIcon(U8G2 &d, int app, int cx, int cy, int r, uint32_t t);   // current draw colour; t = animation clock (ms), 0 = still
}  // namespace launcher
