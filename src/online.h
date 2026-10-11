#pragma once
// Online mode: Wi-Fi is OFF until a screen needs the internet, then the radio comes up, fetches,
// caches the result in RAM and goes back to sleep a few seconds later.
// All state lives in online.cpp. main.cpp only forwards input, tick and draw.
#include <Arduino.h>
#include <U8g2lib.h>

namespace online {

enum PetHint : uint8_t {
  HINT_HOT   = 1,   // temperature >= 35 C
  HINT_RAIN  = 2,   // rain chance >= 60% (or it is raining)
  HINT_SMOG  = 4,   // US AQI >= 150
  HINT_NIGHT = 8    // sun is down
};

void enter();                                          // entering Online mode (shows the list)
void leave();                                          // leaving Online mode: cancels jobs, radio off
void tick(uint32_t now);                               // call every loop(); runs at most one request per call
bool input(bool a, bool b, bool c, uint32_t now);      // returns true when the user chose Back
void draw(U8G2 &d, uint32_t now);

// Pet integration (cached data only, never triggers a request).
uint8_t petHint();                                     // bit mask of PetHint
void drawPetBadge(U8G2 &d);                            // up to two tiny icons in the top-right corner
int takeTreats();                                      // successful fetches since the last call

}  // namespace online
