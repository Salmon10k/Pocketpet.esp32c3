#pragma once
// Fixed-point particle fluid for the 128x64 1-bit OLED.
//
// No Arduino dependencies on purpose: the ESP32-C3 has no FPU, so everything in fluid.cpp is integer
// maths, and the same file is compiled on a PC by tools/fluidtest/ to check the look and stability.
//
// Usage (see the Fluid screen in main.cpp):
//   fluid::init(preset);                // fill the bottom of the screen with particles
//   each frame: fluid::step(gx, gy);    // gravity in screen "g" units, Q8 (256 = 1 g), +x right, +y down
//               display.clearBuffer(); fluid::render(display.getBufferPtr());
#include <stdint.h>

namespace fluid {

enum Preset { PRESET_WATER = 0, PRESET_HONEY = 1, PRESET_MERCURY = 2, PRESET_COUNT = 3 };

void init(int preset);                 // (re)fill the container and select the preset
void setPreset(int preset);            // change fluid behaviour without resetting the particles
const char *presetName(int preset);
int particleCount();

// Advance the simulation by one display frame (several fixed substeps inside).
// gx, gy: effective gravity in screen space, Q8 fixed point (256 = 1 g). Values are clamped.
void step(int32_t gx_q8, int32_t gy_q8);

// OR the fluid into a 128x64 page-format buffer (SSD1306/SH1106 layout: byte = page*128 + x,
// bit = y & 7). The caller clears the buffer first.
void render(uint8_t *buf);

}  // namespace fluid
