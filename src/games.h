#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// The new games. They are written for this exact hardware: three buttons and the MPU6050.
//   Star Blaster : tilt to fly, A shoots (hold = rapid fire), C = bomb
//   Sniper       : tilt to aim, A fires the bolt-action rifle, hold C to steady your breath and zoom
//   Road Rush    : tilt to steer, A = nitro, C = brake (3-lane road in pseudo-3D)
//   Pinball      : A = upper flipper, C = lower flipper, shake the keychain to nudge the ball
// B always goes back (handled by main.cpp). main.cpp owns the tilt calibration and hands the readings in.
namespace games {
enum Id { STAR = 0, SNIPER = 1, ROAD = 2, PINBALL = 3, COUNT = 4 };

struct Input {
  bool a, c;               // button press events (true for one loop)
  bool aHeld, cHeld;       // button currently down
  bool tiltOk;             // the MPU6050 answers
  bool calibrating;        // hold-still calibration running
  float calib;             // 0..1 progress of that calibration
  float tx, ty;            // relative tilt: +x right, +y down, about 0.5 = a firm tilt
  float shx, shy;          // sudden push from shaking, in g (about 0 when still)
};

enum { FLAG_REZERO = 1 };  // update() asks main.cpp to re-run the tilt calibration (a new round started)

extern uint32_t best[COUNT];   // best scores, loaded / saved by main.cpp
bool takeBestDirty();          // true once after a new best score, so main.cpp can save it

const char *name(Id g);
void begin(Id g);                                    // start a fresh round
uint8_t update(uint32_t now, const Input &in);       // call every loop; returns FLAG_* bits
void draw(U8G2 &d, uint32_t now);
}  // namespace games
