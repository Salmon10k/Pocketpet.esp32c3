#include "pet_face.h"
#include <math.h>

namespace petface {

void drawEye(U8G2 &display, int cx, int cy, int side, EyeStyle style, float open, float pupil,
             float lid, float slant, float lower, int lx, int ly, uint32_t now) {
  display.setDrawColor(1);

  if (style == EY_ARC) {
    for (int t = -1; t <= 1; t++) {
      display.drawLine(cx - 11, cy + 5 + t, cx, cy - 6 + t);
      display.drawLine(cx, cy - 6 + t, cx + 11, cy + 5 + t);
    }
    return;
  }

  if (style == EY_CLOSED) {
    display.drawHLine(cx - 11, cy, 22);
    display.drawHLine(cx - 10, cy + 1, 20);
    return;
  }

  if (style == EY_SPIRAL) {
    display.drawCircle(cx, cy, 12);
    display.drawCircle(cx, cy, 7);
    float a = (now / 90.0f) * (float)side;
    display.drawDisc(cx + (int)(cosf(a) * 9.5f), cy + (int)(sinf(a) * 9.5f), 2);
    display.drawDisc(cx, cy, 1);
    return;
  }

  int w = EYE_W;
  int h = max(3, (int)(EYE_H * open));
  int top = cy - h / 2;
  display.drawRBox(cx - w / 2, top, w, h, min(10, h / 2));

  int pr = (int)pupil;
  if (h >= 2 * pr + 2) {
    int px = cx + lx;
    int py = constrain(cy + ly, top + pr, top + h - pr);
    display.setDrawColor(0);
    display.drawDisc(px, py, pr);
    display.setDrawColor(1);
    display.drawDisc(px - pr / 3, py - pr / 3, max(1, pr / 4));   // little shine
  }

  display.setDrawColor(0);
  if (lid > 0.01f) {
    display.drawBox(cx - w / 2 - 1, top - 1, w + 2, (int)(h * lid) + 1);
  }
  if (fabsf(slant) > 0.05f) {
    int outerX = cx + side * (w / 2 + 1);
    int innerX = cx - side * (w / 2 + 1);
    int sd = (int)(fabsf(slant) * h * 0.55f);
    if (slant > 0) {   // angry: inner corner drops
      display.drawTriangle(outerX, top - 1, innerX, top - 1, innerX, top + sd);
    } else {           // sad: outer corner drops
      display.drawTriangle(outerX, top - 1, innerX, top - 1, outerX, top + sd);
    }
  }
  if (lower > 0.01f) {
    int lh = (int)(h * lower);
    display.drawBox(cx - w / 2 - 1, top + h - lh, w + 2, lh + 1);
  }
  display.setDrawColor(1);
}

void drawMouth(U8G2 &display, MouthType m, int y, uint32_t now, int dx) {
  display.setDrawColor(1);
  switch (m) {
    case M_FLAT:
      display.drawHLine(58 + dx, y, 12);
      break;
    case M_SMILE:
      display.drawLine(56 + dx, y - 2, 60 + dx, y + 1);
      display.drawHLine(60 + dx, y + 1, 8);
      display.drawLine(68 + dx, y + 1, 72 + dx, y - 2);
      break;
    case M_CAT:
      display.drawLine(56 + dx, y - 1, 60 + dx, y + 2);
      display.drawLine(60 + dx, y + 2, 64 + dx, y - 1);
      display.drawLine(64 + dx, y - 1, 68 + dx, y + 2);
      display.drawLine(68 + dx, y + 2, 72 + dx, y - 1);
      break;
    case M_BIG:
      display.drawDisc(64 + dx, y - 3, 7, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
      break;
    case M_O:
      display.drawCircle(64 + dx, y, 3);
      break;
    case M_FROWN:
      display.drawLine(57 + dx, y + 2, 61 + dx, y - 1);
      display.drawHLine(61 + dx, y - 1, 6);
      display.drawLine(67 + dx, y - 1, 71 + dx, y + 2);
      break;
    case M_WAVY:
      for (int i = 0; i < 6; i++) {
        int y0 = y + ((i % 2) ? 2 : -2);
        int y1 = y + ((i % 2) ? -2 : 2);
        display.drawLine(52 + dx + i * 4, y0, 56 + dx + i * 4, y1);
      }
      break;
    case M_TINY:
      display.drawHLine(61 + dx, y, 6);
      break;
    case M_YAWN: {
      int r = 2 + (int)((sinf(now / 300.0f) + 1.0f) * 2.5f);
      display.drawDisc(64 + dx, y, r);
      break;
    }
  }
}

}  // namespace petface
