#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// The pet's eyes and mouth. They used to live in main.cpp; they are in their own file so the PC preview
// (tools/petpreview) can draw exactly the same face the OLED shows.
namespace petface {

enum EyeStyle { EY_NORMAL, EY_ARC, EY_SPIRAL, EY_CLOSED };
enum MouthType { M_FLAT, M_SMILE, M_CAT, M_BIG, M_O, M_FROWN, M_WAVY, M_TINY, M_YAWN };

const int EYE_W = 30;
const int EYE_H = 30;
const int EYE_L_X = 40;
const int EYE_R_X = 88;
const int EYE_Y = 24;
const int MOUTH_Y = 52;

// side: -1 left eye, +1 right eye. open 0..1.2 eye height, pupil radius, lid droop 0..1, slant (+ angry, - sad),
// lower = lower lid raise 0..1, lx/ly = where the pupil looks.
void drawEye(U8G2 &display, int cx, int cy, int side, EyeStyle style, float open, float pupil,
             float lid, float slant, float lower, int lx, int ly, uint32_t now);

// dx shifts the whole mouth sideways (the dance sways the face).
void drawMouth(U8G2 &display, MouthType m, int y, uint32_t now, int dx = 0);

}  // namespace petface
