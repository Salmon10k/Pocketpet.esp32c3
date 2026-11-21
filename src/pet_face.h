#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// The pet's face, in the style of the RoboEyes look: solid rounded-rectangle eyes with no pupils, eyelids that cut the
// top corners (tired / angry), a "happy" lower lid that turns the eye into a crescent, eyes that slide around instead
// of pupils, a taller outer eye when it looks sideways (curiosity), eased movement and random blinking.
// Our own implementation on U8g2 (RoboEyes itself targets Adafruit_GFX). The things RoboEyes does not have are kept
// and drawn in the same style: spiral eyes (dizzy), heart and star eyes, the mouth, hearts, zzz.
// Lives in its own file so the PC previews (tools/petpreview, tools/steptest) draw exactly what the OLED shows.
namespace petface {

enum EyeStyle { EY_NORMAL, EY_ARC, EY_SPIRAL, EY_CLOSED, EY_STAR, EY_HEART };
enum MouthType { M_FLAT, M_SMILE, M_CAT, M_BIG, M_O, M_FROWN, M_WAVY, M_TINY, M_YAWN };

// Eye centres and mouth row (the pet's size changes with its growth stage, the layout does not).
const int EYE_L_X = 40;
const int EYE_R_X = 88;
const int EYE_Y = 24;
const int MOUTH_Y = 52;

struct Geometry { int w, h, r; };
// Growth stage 0..4 (see progress.h): baby = big round eyes, later stages get squarer and wider.
void setStage(int stage);
const Geometry &geometry();

// What the eyes should look like (the mood). The animation eases toward it.
struct Target {
  EyeStyle style = EY_NORMAL;
  float open = 1.0f;      // eye height, 0..1.2
  float lid = 0.0f;       // flat upper lid droop 0..1 (sleepy)
  float slant = 0.0f;     // + angry (inner corner drops), - tired (outer corner drops)
  float lower = 0.0f;     // happy lower lid 0..1 (cheeks push up)
};

struct EyeAnim {
  float open = 1, lid = 0, slant = 0, lower = 0, glint = 0;
  float lookX = 0, lookY = 0, blink = 1, wink = 1;
  EyeStyle style = EY_NORMAL;
  uint32_t nextBlinkAt = 0, blinkUntil = 0, nextWinkAt = 0, winkUntil = 0, nextIdleAt = 0;
  int idleX = 0, idleY = 0;
  uint32_t rng = 0x9E3779B9u;

  // tilted: the device is tilted, the eyes follow (tx, ty about -7..7); otherwise they wander on their own.
  void update(uint32_t now, const Target &t, bool tilted, float tx, float ty, bool allowWink = true);
  // dx / cy: the whole face moves (breathing, dance, bounce). Draws both eyes.
  void draw(U8G2 &d, int dx, int cy, uint32_t now) const;
  // Same face at scale k (1 = full size) with the pair centred on x; for small faces on other screens (timer).
  void drawAt(U8G2 &d, int cx, int cy, float k, uint32_t now) const;
};

// A little face for other screens (timer ...): two eyes at scale `s` (1 = full size) centred on x, y.
void drawMini(U8G2 &d, int x, int y, int s, const Target &t, uint32_t now);

// dx shifts the whole mouth sideways (the dance sways the face).
void drawMouth(U8G2 &display, MouthType m, int y, uint32_t now, int dx = 0);

}  // namespace petface
