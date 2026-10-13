#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

namespace companion {
void begin();
void end();
void tick(uint32_t now);
bool active();
void input(bool a, bool b, bool c);
void draw(U8G2 &display, uint32_t now);
void pet();
void gamePlayed(uint32_t score);
}
