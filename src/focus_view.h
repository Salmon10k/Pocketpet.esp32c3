#pragma once
#include <U8g2lib.h>
#include "focus.h"
#include "pet_face.h"

namespace focus {
// The pet's mood (eyes) for this clock: serious while counting down, happy on a break, wide for the last seconds,
// happy arcs when time is up.
petface::Target mood(const Clock &c, uint32_t now);
// The whole page (time, label, progress bar, hint, the pet's small face). Flashes when finished.
void draw(U8G2 &d, const Clock &c, const petface::EyeAnim &eyes, uint32_t now);
}  // namespace focus
