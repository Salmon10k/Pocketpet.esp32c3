#include "launcher.h"
#include "gfx.h"
#include <math.h>
#include <string.h>

using namespace gfx;

namespace launcher {
namespace {

const char *NAMES[APP_COUNT] = {"Fluid", "Stats", "Motion", "Tools", "Games", "Online", "Spotify", "Visualizer", "Living OLED", "Settings"};

int sel = 0, style = STYLE_CAROUSEL;
float pos = 0.0f;                    // carousel: continuous index of the item in the middle
uint32_t enterAt = 0, lastIn = 0, lastDrawAt = 0;
float tiltAngle = 0.0f;              // ring: last direction the device was tilted
bool ringTilting = false;

inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
int ease(int t) { if (t < 0) t = 0; if (t > 256) t = 256; return 256 - (256 - t) * (256 - t) / 256; }

// ring geometry: item i sits on an ellipse, starting at the top and going clockwise
const int RING_CX = 64, RING_CY = 32, RING_RX = 52, RING_RY = 24;
void ringPos(int i, int &x, int &y) {
  int a = -64 + i * 256 / APP_COUNT;
  x = RING_CX + cs(a) * RING_RX / 127;
  y = RING_CY + sn(a) * RING_RY / 127;
}

void drawName(U8G2 &d, const char *name, int cx, int baseline) {
  d.setFont(u8g2_font_7x13B_tf);
  int w = d.getStrWidth(name);
  if (w > 56) { d.setFont(u8g2_font_6x10_tf); w = d.getStrWidth(name); }
  int x = cx - w / 2;
  if (x >= 0 && baseline >= 0) d.drawStr(x, baseline, name);
}

// ------------------------------------------------------------------------------------------ carousel
void drawCarousel(U8G2 &d, uint32_t now) {
  uint32_t since = now - enterAt;
  int introOff = since >= 420 ? 0 : (int)(150 * (256 - ease((int)(since * 256 / 420))) / 256);   // everything glides in from the right

  const int STEP = 38, ICON_Y = 25;
  for (int pass = 0; pass < 2; pass++) {                    // neighbours first, the middle one on top
    for (int i = 0; i < APP_COUNT; i++) {
      float dd = (float)i - pos;
      while (dd > APP_COUNT / 2.0f) dd -= APP_COUNT;
      while (dd < -APP_COUNT / 2.0f) dd += APP_COUNT;
      float ad = fabsf(dd);
      if (ad > 2.7f) continue;
      bool mid = ad < 0.5f;
      if ((pass == 1) != mid) continue;
      int x = 64 + (int)(dd * STEP) + introOff;
      float s = ad < 1.0f ? 1.0f - ad * 0.52f : 0.48f - (ad - 1.0f) * 0.2f;
      int r = imax(3, (int)(13.0f * s + 0.5f));
      bool isSel = ad < 0.12f;
      uint32_t t = isSel ? now - enterAt + 1 : 0;
      d.setDrawColor(1);
      drawIcon(d, i, x, ICON_Y, r, t);
      if (ad < 0.5f) {                                      // corner brackets around the icon in the middle
        int e = r + 5 + (isSel ? (int)((sn((int)(now / 6)) + 127) * 2 / 254) : 0), L = 5;
        int x0 = x - e, y0 = ICON_Y - e, x1 = x + e, y1 = ICON_Y + e;
        hl(d, x0, y0, L); vl(d, x0, y0, L); hl(d, x1 - L + 1, y0, L); vl(d, x1, y0, L);
        hl(d, x0, y1, L); vl(d, x0, y1 - L + 1, L); hl(d, x1 - L + 1, y1, L); vl(d, x1, y1 - L + 1, L);
      }
    }
  }

  // label: slides up from below when the item in the middle changes
  int nearest = (int)(pos + 0.5f) % APP_COUNT;
  float dn = (float)nearest - pos;
  while (dn > APP_COUNT / 2.0f) dn -= APP_COUNT;
  while (dn < -APP_COUNT / 2.0f) dn += APP_COUNT;
  int lift = (int)(fabsf(dn) * 2.0f * 12.0f);
  if (lift < 14) { d.setDrawColor(1); drawName(d, NAMES[nearest], 64 + introOff, 55 + lift); }

  // page dots, the long one follows the carousel position
  const int DOT = 6, x0 = 64 - APP_COUNT * DOT / 2 + 1;
  for (int i = 0; i < APP_COUNT; i++) box(d, x0 + i * DOT, 61, 2, 2);
  float pp = pos;
  int px0 = x0 + (int)(pp * DOT + 0.5f);
  box(d, px0 - 1, 61, 4, 2);
  if (pp > APP_COUNT - 1) box(d, x0 + (int)((pp - APP_COUNT) * DOT + 0.5f) - 1, 61, 4, 2);   // wrapping from the last item to the first
}

// ------------------------------------------------------------------------------------------ tilt ring
void drawRing(U8G2 &d, uint32_t now) {
  uint32_t since = now - enterAt;
  int k = since >= 480 ? 256 : ease((int)(since * 256 / 480));            // 0..256: icons fly out of the middle
  int swirl = (256 - k) * 40 / 256;                                      // and swing round into place
  for (int i = 0; i < APP_COUNT; i++) {
    int a = -64 + i * 256 / APP_COUNT + swirl;
    int x = RING_CX + (cs(a) * RING_RX / 127) * k / 256;
    int y = RING_CY + (sn(a) * RING_RY / 127) * k / 256;
    bool isSel = i == sel;
    if (isSel && k == 256) {
      d.setDrawColor(1);
      disc(d, x, y, 8);                                                  // highlight: the icon inverted on a white disc
      d.setDrawColor(0);
      drawIcon(d, i, x, y, 6, now - enterAt + 1);
      d.setDrawColor(1);
    } else {
      d.setDrawColor(1);
      drawIcon(d, i, x, y, 6, 0);
    }
  }
  d.setDrawColor(1);
  if (k > 200) drawName(d, NAMES[sel], RING_CX, RING_CY + 5);
}

}  // namespace

const char *appName(int app) { return (app >= 0 && app < APP_COUNT) ? NAMES[app] : ""; }
int selected() { return sel; }
uint32_t lastInputAt() { return lastIn; }

void enter(int selected, int st, uint32_t now) {
  sel = constrain(selected, 0, APP_COUNT - 1);
  style = st == STYLE_RING ? STYLE_RING : STYLE_CAROUSEL;
  pos = (float)sel;
  enterAt = now;
  lastIn = now;
  lastDrawAt = now;
  tiltAngle = 0;
  ringTilting = false;
}

int input(bool a, bool b, bool c, uint32_t now, float tx, float ty, bool tiltValid) {
  if (a) { sel = (sel + APP_COUNT - 1) % APP_COUNT; lastIn = now; }
  if (c) { sel = (sel + 1) % APP_COUNT; lastIn = now; }
  if (style == STYLE_RING && tiltValid) {
    float m = sqrtf(tx * tx + ty * ty);
    if (m > 0.13f) {                                       // tilted enough: pick the ring item that lies in that direction
      lastIn = now;
      float ang = atan2f(ty, tx);
      int best = sel;
      float bestDiff = 99.0f, curDiff = 99.0f;
      for (int i = 0; i < APP_COUNT; i++) {
        int ix, iy;
        ringPos(i, ix, iy);
        float ia = atan2f((float)(iy - RING_CY), (float)(ix - RING_CX));
        float df = fabsf(ang - ia);
        if (df > 3.14159f) df = 6.28318f - df;
        if (i == sel) curDiff = df;
        if (df < bestDiff) { bestDiff = df; best = i; }
      }
      if (best != sel && bestDiff + 0.14f < curDiff) sel = best;   // hysteresis: no flicker between neighbours
    }
  }
  if (b) { lastIn = now; return sel; }
  return -1;
}

void draw(U8G2 &d, uint32_t now) {
  float dt = (float)(now - lastDrawAt);
  lastDrawAt = now;
  if (dt > 100.0f) dt = 100.0f;
  if (style == STYLE_CAROUSEL) {
    float delta = (float)sel - pos;
    while (delta > APP_COUNT / 2.0f) delta -= APP_COUNT;
    while (delta < -APP_COUNT / 2.0f) delta += APP_COUNT;
    pos += delta * (dt / (dt + 70.0f));                    // exponential glide, ~70 ms time constant
    if (fabsf(delta) < 0.01f) pos = (float)sel;
    while (pos < 0) pos += APP_COUNT;
    while (pos >= APP_COUNT) pos -= APP_COUNT;
    drawCarousel(d, now);
  } else {
    drawRing(d, now);
  }
}

// ------------------------------------------------------------------------------------------ icons
void drawIcon(U8G2 &d, int app, int cx, int cy, int r, uint32_t t) {
  auto S = [&](int v) { return (v * r + (v >= 0 ? 5 : -5)) / 10; };     // size in tenths of r
  auto X = [&](int v) { return cx + S(v); };
  auto Y = [&](int v) { return cy + S(v); };
  int ph = (int)(t / 8);                                                // animation phase
  switch (app) {
    case 0: {                                                           // Fluid: a drop with water sloshing inside
      line(d, cx, Y(-10), X(-6), Y(-1));
      line(d, cx, Y(-10), X(6), Y(-1));
      arc(d, cx, Y(3), S(7), -23, 151);
      int px0 = 0, py0 = 0;
      for (int i = 0; i <= 8; i++) {
        int xs = -4 + i;
        int x = X(xs), y = Y(3) + sn(i * 36 + ph * 3) * imax(1, S(1)) / 127;
        if (i) line(d, px0, py0, x, y);
        px0 = x; py0 = y;
      }
      break;
    }
    case 1: {                                                           // Stats: bar chart
      hl(d, X(-9), Y(8), S(18) + 1);
      const int base[3] = {5, 10, 15}, xs[3] = {-7, -1, 5};
      for (int k = 0; k < 3; k++) {
        int hh = imax(1, S(base[k]) + sn(ph * 3 + k * 70) * S(2) / 127);
        box(d, X(xs[k]), Y(8) - hh, imax(2, S(4)), hh);
      }
      break;
    }
    case 2: {                                                           // Motion: spirit level with a moving bubble
      circle(d, cx, cy, imax(3, S(9)));
      hl(d, X(-9), cy, S(5)); hl(d, X(4), cy, S(5));
      vl(d, cx, Y(-9), S(5)); vl(d, cx, Y(4), S(5));
      int bx0 = t ? cx + cs(ph * 2) * S(3) / 127 : cx + S(2);
      int by0 = t ? cy + sn(ph * 2) * S(3) / 127 : cy - S(1);
      disc(d, bx0, by0, imax(1, S(2)));
      break;
    }
    case 3: {                                                           // Tools: thermometer
      rframe(d, X(-3), Y(-10), imax(3, S(6) + 1), S(14), imax(1, S(3)));
      disc(d, cx, Y(6), imax(2, S(4)));
      int top = Y(-2) - (t ? (sn(ph * 2) + 127) * S(3) / 254 : 0);
      vl(d, cx, top, Y(6) - top);
      if (r >= 8) { hl(d, X(4), Y(-7), S(3)); hl(d, X(4), Y(-3), S(2)); hl(d, X(4), Y(1), S(3)); }
      break;
    }
    case 4: {                                                           // Games: pad
      rframe(d, X(-10), Y(-5), S(20) + 1, S(11), imax(2, S(4)));
      hl(d, X(-8), cy, S(6)); vl(d, X(-5), Y(-3), S(6));
      disc(d, X(4), Y(-1), imax(1, S(1))); disc(d, X(7), Y(2), imax(1, S(1)));
      line(d, X(-7), Y(6), X(-8), Y(9));
      line(d, X(7), Y(6), X(8), Y(9));
      break;
    }
    case 5: {                                                           // Online: wifi
      disc(d, cx, Y(7), imax(1, S(1)));
      int lit = t ? 1 + (ph / 14) % 4 : 3;
      for (int k = 0; k < 3 && k < lit; k++) arc(d, cx, Y(7), imax(2, S(4 + 4 * k)), 164, 220);
      break;
    }
    case 6: {                                                           // Spotify: music note
      int bob = t ? sn(ph * 3) * S(1) / 127 : 0;
      int hr = imax(2, S(3));
      int x1 = X(-5), x2 = X(4);
      disc(d, x1, Y(6) + bob, hr);
      disc(d, x2, Y(4) + bob, hr);
      int s1 = x1 + hr - 1, s2 = x2 + hr - 1;
      vl(d, s1, Y(-7) + bob, Y(6) - Y(-7) + 1);
      vl(d, s2, Y(-9) + bob, Y(4) - Y(-9) + 1);
      line(d, s1, Y(-7) + bob, s2, Y(-9) + bob);
      line(d, s1, Y(-5) + bob, s2, Y(-7) + bob);
      break;
    }
    case 7: {                                                           // Lyrics visual: mirrored equalizer
      const int base[5] = {5, 10, 14, 8, 11}, xs[5] = {-8, -4, 0, 4, 8};
      for (int k = 0; k < 5; k++) {
        int hh = imax(2, S(base[k]) + sn(ph * 4 + k * 50) * S(3) / 127);
        box(d, X(xs[k]) - (r >= 8 ? 1 : 0), cy - hh / 2, imax(1, S(2)) + (r >= 8 ? 1 : 0), hh);
      }
      break;
    }
    case 8: {                                                           // Living OLED: an eye that looks around
      const int pts[9][2] = {{-10, 0}, {-5, -5}, {0, -6}, {5, -5}, {10, 0}, {5, 5}, {0, 6}, {-5, 5}, {-10, 0}};
      for (int i = 0; i < 8; i++) line(d, X(pts[i][0]), Y(pts[i][1]), X(pts[i + 1][0]), Y(pts[i + 1][1]));
      int look = t ? sn(ph * 2) * S(3) / 127 : 0;
      disc(d, cx + look, cy, imax(2, S(3)));
      break;
    }
    default: {                                                          // Settings: gear
      int rot = ph / 2;
      circle(d, cx, cy, imax(2, S(6)));
      circle(d, cx, cy, imax(1, S(2)));
      for (int k = 0; k < 8; k++) {
        int a = k * 32 + rot;
        int r0 = imax(2, S(6)), r1 = imax(3, S(9));
        int ax0 = cx + cs(a - 3) * r0 / 127, ay0 = cy + sn(a - 3) * r0 / 127;
        int ax1 = cx + cs(a - 2) * r1 / 127, ay1 = cy + sn(a - 2) * r1 / 127;
        int bx1 = cx + cs(a + 2) * r1 / 127, by1 = cy + sn(a + 2) * r1 / 127;
        int bx0 = cx + cs(a + 3) * r0 / 127, by0 = cy + sn(a + 3) * r0 / 127;
        line(d, ax0, ay0, ax1, ay1); line(d, ax1, ay1, bx1, by1); line(d, bx1, by1, bx0, by0);
      }
      break;
    }
  }
}

}  // namespace launcher
