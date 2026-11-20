#include "pet_face.h"
#include "gfx.h"
#include <math.h>

namespace petface {

namespace {
Geometry geo = {30, 30, 10};
const Geometry STAGES[5] = {{34, 34, 14}, {30, 30, 10}, {30, 28, 8}, {28, 30, 6}, {32, 26, 5}};

float ease(float cur, float tgt, float k) { return cur + (tgt - cur) * k; }
uint32_t rnd(uint32_t &s) { s = s * 1664525u + 1013904223u; return s >> 8; }
int rrange(uint32_t &s, int lo, int hi) { return lo + (int)(rnd(s) % (uint32_t)(hi - lo + 1)); }

// One eye. (cx, cy) = centre, w/h/r = size, the cuts are drawn in black over the white box.
void rawEye(U8G2 &d, int cx, int cy, int side, EyeStyle style, int w, int h, int r, float lid, float slant,
            float lower, float glint, uint32_t now) {
  h = h < 3 ? 3 : h;
  int top = cy - h / 2;
  r = min(r, (h - 1) / 2);
  r = min(r, (w - 1) / 2);
  d.setDrawColor(1);
  d.drawRBox(cx - w / 2, top, w, h, r);
  d.setDrawColor(0);

  if (style == EY_SPIRAL) {                                   // a rotating spiral cut into the eye
    float a0 = (now / 120.0f) * (float)side;
    float maxr = (float)(min(w, h) / 2 - 2);
    int px = cx, py = cy;
    for (int i = 1; i <= 40; ++i) {
      float t = i / 40.0f;
      float ang = a0 + t * 12.566f * (float)side;
      int nx = cx + (int)lroundf(cosf(ang) * maxr * t), ny = cy + (int)lroundf(sinf(ang) * maxr * t);
      gfx::line(d, px, py, nx, ny);
      px = nx; py = ny;
    }
  }
  if (style == EY_STAR && glint > 0.05f) {                    // four-point sparkle
    int s = (int)(glint * 8.0f), t = max(1, s / 3);
    d.drawTriangle(cx, cy - s, cx - t, cy, cx + t, cy);
    d.drawTriangle(cx, cy + s, cx - t, cy, cx + t, cy);
    d.drawTriangle(cx - s, cy, cx, cy - t, cx, cy + t);
    d.drawTriangle(cx + s, cy, cx, cy - t, cx, cy + t);
  }
  if (style == EY_HEART && glint > 0.05f) {                   // heart that pulses a little
    float pulse = 1.0f + 0.12f * sinf(now / 140.0f);
    int s = (int)(glint * 4.0f * pulse);
    d.drawDisc(cx - s, cy - s / 2, s);
    d.drawDisc(cx + s, cy - s / 2, s);
    d.drawTriangle(cx - 2 * s, cy - s / 2 + 1, cx + 2 * s, cy - s / 2 + 1, cx, cy + 2 * s);
  }

  if (lid > 0.01f) d.drawBox(cx - w / 2 - 1, top - 1, w + 2, (int)(h * lid) + 1);
  if (fabsf(slant) > 0.05f) {
    int outerX = cx + side * (w / 2 + 1), innerX = cx - side * (w / 2 + 1);
    int sd = (int)(fabsf(slant) * h * 0.55f);
    if (slant > 0) d.drawTriangle(outerX, top - 1, innerX, top - 1, innerX, top + sd);   // angry
    else           d.drawTriangle(outerX, top - 1, innerX, top - 1, outerX, top + sd);   // tired
  }
  if (lower > 0.01f) {                                        // happy: a big round black lid pushes up from below -> a hump
    int lh = (int)(h * lower);
    float R = w * 0.9f;
    float yc = (float)(top + h - lh) + R;
    int bottom = top + h;
    for (int x = cx - w / 2; x < cx + (w + 1) / 2; ++x) {
      float dx = (float)(x - cx);
      float yb = yc - sqrtf(fmaxf(0.0f, R * R - dx * dx));
      int y0 = (int)lroundf(yb);
      if (y0 < bottom) d.drawVLine(x, y0 < top ? top : y0, bottom - (y0 < top ? top : y0) + 1);
    }
  }
  d.setDrawColor(1);
}
}  // namespace

void setStage(int s) { geo = STAGES[s < 0 ? 0 : (s > 4 ? 4 : s)]; }
const Geometry &geometry() { return geo; }

void EyeAnim::update(uint32_t now, const Target &t, bool tilted, float tx, float ty, bool allowWink) {
  EyeStyle eff = t.style;
  float tOpen = t.open, tLower = t.lower;
  if (eff == EY_ARC)    { eff = EY_NORMAL; tOpen = 0.92f; tLower = 0.58f; }      // happy crescent
  if (eff == EY_CLOSED) { eff = EY_NORMAL; tOpen = 0.08f; }
  style = eff;
  open  = ease(open, tOpen, 0.38f);
  lid   = ease(lid, t.lid, 0.30f);
  slant = ease(slant, t.slant, 0.30f);
  lower = ease(lower, tLower, 0.34f);
  glint = ease(glint, (eff == EY_STAR || eff == EY_HEART) ? 1.0f : 0.0f, 0.25f);

  bool awake = (eff != EY_SPIRAL) && tOpen > 0.3f;
  if (awake && now >= nextBlinkAt) {
    blinkUntil = now + 110;
    nextBlinkAt = now + (uint32_t)rrange(rng, 2200, 5500);
    if (rrange(rng, 0, 5) == 0) nextBlinkAt = now + 280;                          // quick double blink
  }
  if (awake && allowWink && now >= nextWinkAt) {
    winkUntil = now + 280;
    nextWinkAt = now + (uint32_t)rrange(rng, 14000, 34000);
  }
  blink = ease(blink, now < blinkUntil ? 0.06f : 1.0f, 0.65f);
  wink  = ease(wink, now < winkUntil ? 0.06f : 1.0f, 0.65f);

  float gx, gy;
  if (tilted) { gx = tx * 1.4f; gy = ty * 0.8f; nextIdleAt = now + 600; }
  else {
    if (now >= nextIdleAt) {                                                      // look somewhere else now and then
      idleX = rrange(rng, -9, 9); idleY = rrange(rng, -5, 5);
      if (rrange(rng, 0, 2) == 0) { idleX = 0; idleY = 0; }
      nextIdleAt = now + (uint32_t)rrange(rng, 900, 2800);
    }
    gx = (float)idleX; gy = (float)idleY;
  }
  lookX = ease(lookX, gx, 0.32f);
  lookY = ease(lookY, gy, 0.32f);
}

void EyeAnim::draw(U8G2 &d, int dx, int cy, uint32_t now) const {
  int lx = (int)lroundf(lookX), ly = (int)lroundf(lookY);
  float h0 = geo.h * open * blink;
  float hL = h0, hR = h0 * wink;
  int growL = 0, growR = 0;                                   // curiosity: the eye on the side it looks at gets taller
  if (style == EY_NORMAL && open > 0.5f) {
    if (lookX <= -4) growL = (int)lroundf(fminf(6.0f, (-lookX - 3) * 1.2f));
    if (lookX >= 4)  growR = (int)lroundf(fminf(6.0f, (lookX - 3) * 1.2f));
  }
  int ecy = cy + ly;
  int heL = (int)lroundf(hL) + growL, heR = (int)lroundf(hR) + growR;
  rawEye(d, EYE_L_X + dx + lx, ecy - growL / 2, -1, style, geo.w, heL, geo.r, lid, slant, lower, glint, now);
  rawEye(d, EYE_R_X + dx + lx, ecy - growR / 2, +1, style, geo.w, heR, geo.r, lid, slant, lower, glint, now);
}

void drawMini(U8G2 &d, int x, int y, int s, const Target &t, uint32_t now) {
  float k = s / 30.0f;
  int w = (int)(geo.w * k), h = (int)(geo.h * k * t.open), r = max(1, (int)(geo.r * k));
  int gap = (int)((EYE_R_X - EYE_L_X) * k);
  float lower = t.lower;
  EyeStyle st = t.style;
  if (st == EY_ARC) { st = EY_NORMAL; h = (int)(geo.h * k * 0.92f); lower = 0.58f; }
  if (st == EY_CLOSED) { st = EY_NORMAL; h = 2; }
  rawEye(d, x - gap / 2, y, -1, st, w, h, r, t.lid, t.slant, lower, 1.0f, now);
  rawEye(d, x + gap / 2, y, +1, st, w, h, r, t.lid, t.slant, lower, 1.0f, now);
}

void drawMouth(U8G2 &display, MouthType m, int y, uint32_t now, int dx) {
  display.setDrawColor(1);
  auto thick = [&](int x0, int y0, int x1, int y1) {          // 2 px strokes to match the chunky eyes
    gfx::line(display, x0, y0, x1, y1);
    gfx::line(display, x0, y0 + 1, x1, y1 + 1);
  };
  switch (m) {
    case M_FLAT:
      display.drawRBox(58 + dx, y - 1, 12, 3, 1);
      break;
    case M_SMILE:
      thick(56 + dx, y - 2, 60 + dx, y + 1);
      display.drawHLine(60 + dx, y + 1, 8);
      display.drawHLine(60 + dx, y + 2, 8);
      thick(68 + dx, y + 1, 72 + dx, y - 2);
      break;
    case M_CAT:
      thick(56 + dx, y - 1, 60 + dx, y + 2);
      thick(60 + dx, y + 2, 64 + dx, y - 1);
      thick(64 + dx, y - 1, 68 + dx, y + 2);
      thick(68 + dx, y + 2, 72 + dx, y - 1);
      break;
    case M_BIG:
      display.drawDisc(64 + dx, y - 3, 7, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
      break;
    case M_O:
      display.drawCircle(64 + dx, y, 3);
      display.drawCircle(64 + dx, y, 2);
      break;
    case M_FROWN:
      thick(57 + dx, y + 2, 61 + dx, y - 1);
      display.drawHLine(61 + dx, y - 1, 6);
      display.drawHLine(61 + dx, y, 6);
      thick(67 + dx, y - 1, 71 + dx, y + 2);
      break;
    case M_WAVY:
      for (int i = 0; i < 6; i++) {
        int y0 = y + ((i % 2) ? 2 : -2);
        int y1 = y + ((i % 2) ? -2 : 2);
        thick(52 + dx + i * 4, y0, 56 + dx + i * 4, y1);
      }
      break;
    case M_TINY:
      display.drawRBox(61 + dx, y, 6, 2, 1);
      break;
    case M_YAWN: {
      int r = 2 + (int)((sinf(now / 300.0f) + 1.0f) * 2.5f);
      display.drawDisc(64 + dx, y, r);
      break;
    }
  }
}

}  // namespace petface
