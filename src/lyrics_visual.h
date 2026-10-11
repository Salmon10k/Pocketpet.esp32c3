#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

namespace lyrics_visual {
void enter();
void leave();
void tick(uint32_t now);
void input(bool a, bool b, bool c, uint32_t now);
void draw(U8G2 &d, uint32_t now);
bool active();
}
