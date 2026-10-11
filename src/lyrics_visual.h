#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

namespace lyrics_visual {
void enter();
void leave();
void draw(U8G2 &d, uint32_t now);
}
