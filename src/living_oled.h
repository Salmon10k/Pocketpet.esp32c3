#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

namespace living_oled {
void enter();
void leave();
void input(bool a, bool b, bool c, uint32_t now);
void draw(U8G2 &display, uint32_t now);
}
