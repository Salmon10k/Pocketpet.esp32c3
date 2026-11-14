#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "pet_face.h"
#include "vibe.h"

// How the pet reacts to the music that is playing: a pose (eyes, mouth, head movement) per vibe plus decorations
// (headphones, shades, notes, lightning, equalizer ...). The dance is locked to the song position and a feel-tempo
// (see vibe.h): steady, but not the song's real beat.
namespace petmusic {

struct Pose {
  petface::EyeStyle eye;
  float open, pupil, lid, slant, lower;
  int8_t lookY;
  petface::MouthType mouth;
  int8_t dx, dy;          // the whole face moves by this much (sway / bounce / headbang)
  bool shades;            // drawn by decor()
  bool headphones;
  bool flash;             // EDM: invert the whole screen for a moment
};

Pose pose(const vibe::Music &m, uint32_t now);

// Draw after the face. eyeCy = the eyes' centre row (includes the breathing bob), the face is already shifted by pose dx/dy.
void decor(U8G2 &d, const vibe::Music &m, const Pose &p, int eyeCy, uint32_t now);

}  // namespace petmusic
