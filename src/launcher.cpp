#include "launcher.h"
#include "gfx.h"
#include <math.h>
#include <string.h>

using namespace gfx;

namespace launcher {
namespace {

const Item HOME[APP_COUNT] = {
  {"Fluid", ICON_FLUID}, {"Stats", ICON_STATS}, {"Motion", ICON_MOTION}, {"Tools", ICON_TOOLS}, {"Games", ICON_GAMES},
  {"Online", ICON_ONLINE}, {"Spotify", ICON_SPOTIFY}, {"Visualizer", ICON_VISUAL}, {"Living OLED", ICON_LIVING}, {"Settings", ICON_SETTINGS}};

const Item *items = HOME;                // what the menu shows right now (home apps or a sub-menu)
int n = APP_COUNT;
const char *(*valFn)(int) = nullptr;     // optional "current value" text per item (Settings, Online status)

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
  int a = -64 + i * 256 / n;
  x = RING_CX + cs(a) * RING_RX / 127;
  y = RING_CY + sn(a) * RING_RY / 127;
}

void drawName(U8G2 &d, const char *name, int cx, int baseline) {
  d.setFont(u8g2_font_7x13B_tf);
  int w = d.getStrWidth(name);
  if (w > 56) { d.setFont(u8g2_font_6x10_tf); w = d.getStrWidth(name); }
  int x = cx - w / 2;
  if (x >= 0 && x + w < 128 && baseline >= 0) d.drawStr(x, baseline, name);
}

// the current value in a small rounded pill (Settings rows, Online connection status)
void drawPill(U8G2 &d, const char *txt, int cx, int y) {
  if (!txt || !*txt) return;
  d.setFont(u8g2_font_6x10_tf);
  int w = d.getStrWidth(txt);
  int x = cx - w / 2;
  if (x < 5 || x + w + 5 > 128) return;           // still sliding in from the right
  rframe(d, x - 4, y, w + 8, 11, 3);
  d.drawStr(x, y + 9, txt);
}

// ------------------------------------------------------------------------------------------ carousel
void drawCarousel(U8G2 &d, uint32_t now) {
  uint32_t since = now - enterAt;
  int introOff = since >= 420 ? 0 : (int)(150 * (256 - ease((int)(since * 256 / 420))) / 256);   // everything glides in from the right
  const bool hv = valFn != nullptr;
  const int STEP = 38, ICON_Y = hv ? 19 : 25, R_MAX = hv ? 10 : 13;

  for (int pass = 0; pass < 2; pass++) {                    // neighbours first, the middle one on top
    for (int i = 0; i < n; i++) {
      float dd = (float)i - pos;
      while (dd > n / 2.0f) dd -= n;
      while (dd < -n / 2.0f) dd += n;
      float ad = fabsf(dd);
      if (ad > 2.7f) continue;
      bool mid = ad < 0.5f;
      if ((pass == 1) != mid) continue;
      int x = 64 + (int)(dd * STEP) + introOff;
      float s = ad < 1.0f ? 1.0f - ad * 0.52f : 0.48f - (ad - 1.0f) * 0.2f;
      int r = imax(3, (int)(R_MAX * s + 0.5f));
      bool isSel = ad < 0.12f;
      uint32_t t = isSel ? now - enterAt + 1 : 0;
      d.setDrawColor(1);
      drawIcon(d, items[i].icon, x, ICON_Y, r, t);
      if (ad < 0.5f) {                                      // corner brackets around the icon in the middle
        int e = r + 5 + (isSel ? (int)((sn((int)(now / 6)) + 127) * 2 / 254) : 0), L = 5;
        int x0 = x - e, y0 = ICON_Y - e, x1 = x + e, y1 = ICON_Y + e;
        hl(d, x0, y0, L); vl(d, x0, y0, L); hl(d, x1 - L + 1, y0, L); vl(d, x1, y0, L);
        hl(d, x0, y1, L); vl(d, x0, y1 - L + 1, L); hl(d, x1 - L + 1, y1, L); vl(d, x1, y1 - L + 1, L);
      }
    }
  }

  // label: slides up from below when the item in the middle changes
  int nearest = (int)(pos + 0.5f) % n;
  float dn = (float)nearest - pos;
  while (dn > n / 2.0f) dn -= n;
  while (dn < -n / 2.0f) dn += n;
  int lift = (int)(fabsf(dn) * 2.0f * 12.0f);
  if (lift < 14) { d.setDrawColor(1); drawName(d, items[nearest].name, 64 + introOff, (hv ? 47 : 55) + lift); }
  if (hv && lift < 4) { d.setDrawColor(1); drawPill(d, valFn(nearest), 64 + introOff, 51); }

  if (!hv) {                                                // page dots, the long one follows the carousel position
    const int DOT = 6, x0 = 64 - n * DOT / 2 + 1;
    for (int i = 0; i < n; i++) box(d, x0 + i * DOT, 61, 2, 2);
    int px0 = x0 + (int)(pos * DOT + 0.5f);
    box(d, px0 - 1, 61, 4, 2);
    if (pos > n - 1) box(d, x0 + (int)((pos - n) * DOT + 0.5f) - 1, 61, 4, 2);   // wrapping from the last item to the first
  }
}

// ------------------------------------------------------------------------------------------ tilt ring
void drawRing(U8G2 &d, uint32_t now) {
  uint32_t since = now - enterAt;
  int k = since >= 480 ? 256 : ease((int)(since * 256 / 480));            // 0..256: icons fly out of the middle
  int swirl = (256 - k) * 40 / 256;                                      // and swing round into place
  for (int i = 0; i < n; i++) {
    int a = -64 + i * 256 / n + swirl;
    int x = RING_CX + (cs(a) * RING_RX / 127) * k / 256;
    int y = RING_CY + (sn(a) * RING_RY / 127) * k / 256;
    bool isSel = i == sel;
    if (isSel && k == 256) {
      d.setDrawColor(1);
      disc(d, x, y, 8);                                                  // highlight: the icon inverted on a white disc
      d.setDrawColor(0);
      drawIcon(d, items[i].icon, x, y, 6, now - enterAt + 1);
      d.setDrawColor(1);
    } else {
      d.setDrawColor(1);
      drawIcon(d, items[i].icon, x, y, 6, 0);
    }
  }
  d.setDrawColor(1);
  if (k > 200) {
    drawName(d, items[sel].name, RING_CX, valFn ? RING_CY - 1 : RING_CY + 5);
    if (valFn) drawPill(d, valFn(sel), RING_CX, RING_CY + 3);
  }
}

}  // namespace

const char *appName(int app) { return (app >= 0 && app < APP_COUNT) ? HOME[app].name : ""; }
int selected() { return sel; }
int count() { return n; }
uint32_t lastInputAt() { return lastIn; }

void enterSub(const Item *its, int cnt, int selected, int st, uint32_t now, const char *(*value)(int)) {
  items = its;
  n = cnt < 1 ? 1 : cnt;
  valFn = value;
  sel = constrain(selected, 0, n - 1);
  style = st == STYLE_RING ? STYLE_RING : STYLE_CAROUSEL;
  pos = (float)sel;
  enterAt = now;
  lastIn = now;
  lastDrawAt = now;
  tiltAngle = 0;
  ringTilting = false;
}

void enter(int selected, int st, uint32_t now) { enterSub(HOME, APP_COUNT, selected, st, now, nullptr); }

void restyle(int st, uint32_t now) {
  style = st == STYLE_RING ? STYLE_RING : STYLE_CAROUSEL;
  pos = (float)sel;
  enterAt = now;
}

int input(bool a, bool b, bool c, uint32_t now, float tx, float ty, bool tiltValid) {
  if (a) { sel = (sel + n - 1) % n; lastIn = now; }
  if (c) { sel = (sel + 1) % n; lastIn = now; }
  if (style == STYLE_RING && tiltValid) {
    float m = sqrtf(tx * tx + ty * ty);
    if (m > 0.13f) {                                       // tilted enough: pick the ring item that lies in that direction
      lastIn = now;
      float ang = atan2f(ty, tx);
      int best = sel;
      float bestDiff = 99.0f, curDiff = 99.0f;
      for (int i = 0; i < n; i++) {
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
    while (delta > n / 2.0f) delta -= n;
    while (delta < -n / 2.0f) delta += n;
    pos += delta * (dt / (dt + 70.0f));                    // exponential glide, ~70 ms time constant
    if (fabsf(delta) < 0.01f) pos = (float)sel;
    while (pos < 0) pos += n;
    while (pos >= n) pos -= n;
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
    case ICON_FLUID: {                                                  // a drop with water sloshing inside
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
    case ICON_STATS: {                                                  // bar chart
      hl(d, X(-9), Y(8), S(18) + 1);
      const int base[3] = {5, 10, 15}, xs[3] = {-7, -1, 5};
      for (int k = 0; k < 3; k++) {
        int hh = imax(1, S(base[k]) + sn(ph * 3 + k * 70) * S(2) / 127);
        box(d, X(xs[k]), Y(8) - hh, imax(2, S(4)), hh);
      }
      break;
    }
    case ICON_MOTION: {                                                 // spirit level with a moving bubble
      circle(d, cx, cy, imax(3, S(9)));
      hl(d, X(-9), cy, S(5)); hl(d, X(4), cy, S(5));
      vl(d, cx, Y(-9), S(5)); vl(d, cx, Y(4), S(5));
      int bx0 = t ? cx + cs(ph * 2) * S(3) / 127 : cx + S(2);
      int by0 = t ? cy + sn(ph * 2) * S(3) / 127 : cy - S(1);
      disc(d, bx0, by0, imax(1, S(2)));
      break;
    }
    case ICON_TOOLS: {                                                  // thermometer
      rframe(d, X(-3), Y(-10), imax(3, S(6) + 1), S(14), imax(1, S(3)));
      disc(d, cx, Y(6), imax(2, S(4)));
      int top = Y(-2) - (t ? (sn(ph * 2) + 127) * S(3) / 254 : 0);
      vl(d, cx, top, Y(6) - top);
      if (r >= 8) { hl(d, X(4), Y(-7), S(3)); hl(d, X(4), Y(-3), S(2)); hl(d, X(4), Y(1), S(3)); }
      break;
    }
    case ICON_GAMES: {                                                  // pad
      rframe(d, X(-10), Y(-5), S(20) + 1, S(11), imax(2, S(4)));
      hl(d, X(-8), cy, S(6)); vl(d, X(-5), Y(-3), S(6));
      disc(d, X(4), Y(-1), imax(1, S(1))); disc(d, X(7), Y(2), imax(1, S(1)));
      line(d, X(-7), Y(6), X(-8), Y(9));
      line(d, X(7), Y(6), X(8), Y(9));
      break;
    }
    case ICON_ONLINE: {                                                 // wifi
      disc(d, cx, Y(7), imax(1, S(1)));
      int lit = t ? 1 + (ph / 14) % 4 : 3;
      for (int k = 0; k < 3 && k < lit; k++) arc(d, cx, Y(7), imax(2, S(4 + 4 * k)), 164, 220);
      break;
    }
    case ICON_SPOTIFY: {                                                // music note
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
    case ICON_VISUAL: {                                                 // mirrored equalizer
      const int base[5] = {5, 10, 14, 8, 11}, xs[5] = {-8, -4, 0, 4, 8};
      for (int k = 0; k < 5; k++) {
        int hh = imax(2, S(base[k]) + sn(ph * 4 + k * 50) * S(3) / 127);
        box(d, X(xs[k]) - (r >= 8 ? 1 : 0), cy - hh / 2, imax(1, S(2)) + (r >= 8 ? 1 : 0), hh);
      }
      break;
    }
    case ICON_LIVING: {                                                 // an eye that looks around
      const int pts[9][2] = {{-10, 0}, {-5, -5}, {0, -6}, {5, -5}, {10, 0}, {5, 5}, {0, 6}, {-5, 5}, {-10, 0}};
      for (int i = 0; i < 8; i++) line(d, X(pts[i][0]), Y(pts[i][1]), X(pts[i + 1][0]), Y(pts[i + 1][1]));
      int look = t ? sn(ph * 2) * S(3) / 127 : 0;
      disc(d, cx + look, cy, imax(2, S(3)));
      break;
    }
    case ICON_SETTINGS: {                                               // gear
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

    // ---------------------------------------------------------------- sub-menu icons
    case ICON_BACK: {                                                   // arrow pointing left, nudges while selected
      int nud = t ? sn(ph * 3) * S(1) / 127 : 0;
      hl(d, X(-8) + nud, cy, S(16) + 1);
      line(d, X(-8) + nud, cy, X(-2) + nud, Y(-6));
      line(d, X(-8) + nud, cy, X(-2) + nud, Y(6));
      if (r >= 8) { hl(d, X(-8) + nud, cy + 1, S(16) + 1); line(d, X(-8) + nud, cy + 1, X(-2) + nud, Y(-5)); line(d, X(-8) + nud, cy - 1, X(-2) + nud, Y(5)); }
      break;
    }
    case ICON_8BALL: {                                                  // ball with a wobbling answer window
      int wob = t ? sn(ph * 4) * S(2) / 127 : 0;
      circle(d, cx, cy, imax(3, S(10)));
      circle(d, cx + wob, cy - S(1), imax(2, S(5)));
      if (r >= 8) {
        line(d, cx + wob, Y(-4), X(-2) + wob, Y(1));
        line(d, cx + wob, Y(-4), X(2) + wob, Y(1));
        hl(d, X(-2) + wob, Y(1), S(4) + 1);
      }
      break;
    }
    case ICON_DICE: {                                                   // die: the pips change while selected
      rframe(d, X(-8), Y(-8), S(16) + 1, S(16) + 1, imax(1, S(3)));
      int pr = imax(1, S(1));
      bool five = t && ((ph / 20) & 1);
      disc(d, X(-4), Y(-4), pr); disc(d, X(4), Y(4), pr);
      if (!five) disc(d, cx, cy, pr);
      else { disc(d, X(4), Y(-4), pr); disc(d, X(-4), Y(4), pr); }
      break;
    }
    case ICON_STAR: {                                                   // star fighter with an engine flame and a shot
      line(d, cx, Y(-9), X(-7), Y(6));
      line(d, cx, Y(-9), X(7), Y(6));
      line(d, X(-7), Y(6), cx, Y(3));
      line(d, X(7), Y(6), cx, Y(3));
      if (t) vl(d, cx, Y(5), 1 + (ph / 3) % 3 * imax(1, S(1)));
      if (r >= 8) disc(d, cx, cy, 1);
      break;
    }
    case ICON_SNIPER: {                                                 // scope reticle that sways a little
      int sx = t ? sn(ph * 2) * S(1) / 127 : 0, sy = t ? cs(ph * 3) * S(1) / 127 : 0;
      circle(d, cx, cy, imax(3, S(9)));
      hl(d, X(-9) + sx, cy + sy, S(6)); hl(d, X(3) + sx, cy + sy, S(6));
      vl(d, cx + sx, Y(-9) + sy, S(6)); vl(d, cx + sx, Y(3) + sy, S(6));
      px(d, cx + sx, cy + sy);
      break;
    }
    case ICON_ROAD: {                                                   // road in perspective with moving centre dashes
      line(d, X(-1), Y(-8), X(-9), Y(9));
      line(d, X(1), Y(-8), X(9), Y(9));
      int sp = t ? (ph / 3) % 3 : 0;
      for (int k = 0; k < 3; k++) {
        int f = (k * 3 + sp) % 9;                                       // 0..8 down the road
        int y0 = Y(-7) + S(f * 2 - 1);
        vl(d, cx, y0, imax(1, S(1 + f / 3)));
      }
      break;
    }
    case ICON_PINBALL: {                                                // table with a bumper, a ball and two flippers
      rframe(d, X(-9), Y(-10), S(18) + 1, S(20) + 1, imax(1, S(3)));
      circle(d, X(-3), Y(-4), imax(1, S(3)));
      int by = t ? sn(ph * 3) * S(2) / 127 : 0;
      disc(d, X(4), Y(0) + by, imax(1, S(1)));
      line(d, X(-7), Y(5), X(-2), Y(8));
      line(d, X(7), Y(5), X(2), Y(8));
      break;
    }
    case ICON_MAZE: {                                                   // a little maze with a ball
      frame(d, X(-9), Y(-9), S(18) + 1, S(18) + 1);
      hl(d, X(-9), Y(-3), S(11)); hl(d, X(-3), Y(3), S(12)); vl(d, X(3), Y(-3), S(6));
      int bx0 = t ? sn(ph * 2) * S(2) / 127 : 0;
      disc(d, X(-6) + bx0, Y(-6), imax(1, S(1)));
      break;
    }
    case ICON_SNAKE: {                                                  // body segments, head and food
      int sz = imax(2, S(4));
      const int seg[6][2] = {{-9, 5}, {-5, 5}, {-1, 5}, {-1, 1}, {3, 1}, {3, -3}};
      for (int k = 0; k < 6; k++) box(d, X(seg[k][0]), Y(seg[k][1]), sz, sz);
      frame(d, X(3), Y(-7), sz + 1, sz + 1);
      if (!t || ((ph / 12) & 1)) px(d, X(-7), Y(-6));
      else disc(d, X(-7), Y(-6), imax(1, S(1)));
      break;
    }
    case ICON_BRIGHT: {                                                 // sun with rays
      circle(d, cx, cy, imax(2, S(4)));
      int rot = t ? ph / 2 : 0;
      for (int k = 0; k < 8; k++) {
        int a = k * 32 + rot;
        int r0 = imax(3, S(6)), r1 = imax(4, S(9));
        line(d, cx + cs(a) * r0 / 127, cy + sn(a) * r0 / 127, cx + cs(a) * r1 / 127, cy + sn(a) * r1 / 127);
      }
      break;
    }
    case ICON_SCREEN_OFF: {                                             // screen with a sleepy z
      rframe(d, X(-9), Y(-8), S(18) + 1, S(13) + 1, imax(1, S(2)));
      hl(d, X(-4), Y(8), S(8)); vl(d, cx, Y(5), S(3));
      if (!t || (ph / 16) % 3) {
        hl(d, X(-3), Y(-4), S(5)); line(d, X(2), Y(-4), X(-3), Y(0)); hl(d, X(-3), Y(0), S(5));
      }
      break;
    }
    case ICON_FLIP: {                                                   // two turning arrows (rotate 180)
      int rot = t ? ph / 2 : 0;
      for (int k = 0; k < 2; k++) {
        int a0 = k * 128 + 12 + rot, a1 = a0 + 100;
        int rr = imax(3, S(7));
        arc(d, cx, cy, rr, a0, a1);
        int tx = cx + cs(a1) * rr / 127, ty = cy + sn(a1) * rr / 127;
        int tgx = -sn(a1), tgy = cs(a1);                                // direction of travel
        int hx = imax(3, S(5));
        line(d, tx, ty, tx - tgx * hx / 127 + cs(a1) * hx / 254, ty - tgy * hx / 127 + sn(a1) * hx / 254);
        line(d, tx, ty, tx - tgx * hx / 127 - cs(a1) * hx / 254, ty - tgy * hx / 127 - sn(a1) * hx / 254);
      }
      break;
    }
    case ICON_STYLE: {                                                  // small - BIG - small (carousel look)
      int g = t ? (ph / 10) % 3 : 1;
      frame(d, X(-10), Y(-2), S(5), S(5));
      frame(d, X(-3), Y(-5), S(8) + 1, S(8) + 1);
      frame(d, X(7), Y(-2), S(5), S(5));
      for (int k = 0; k < 3; k++) box(d, X(-2 + k * 3), Y(8), 2, 2 + (k == g ? 1 : 0));
      break;
    }
    case ICON_SYNC: drawIcon(d, ICON_SPOTIFY, cx, cy, r, t); break;     // lyrics sync: the note
    case ICON_RESET: {                                                  // pet face
      circle(d, cx, cy, imax(3, S(9)));
      bool blink = t && ((ph / 14) % 6 == 0);
      if (blink) { hl(d, X(-5), Y(-2), S(3)); hl(d, X(2), Y(-2), S(3)); }
      else { disc(d, X(-3), Y(-2), imax(1, S(1))); disc(d, X(3), Y(-2), imax(1, S(1))); }
      arc(d, cx, Y(1), imax(2, S(4)), 20, 108);
      break;
    }

    // ---------------------------------------------------------------- Online screens
    case ICON_CLOCK: {
      circle(d, cx, cy, imax(3, S(9)));
      int am = -64 + (t ? ph * 2 : 20), ah = -64 + (t ? ph / 6 : 5);
      line(d, cx, cy, cx + cs(am) * imax(2, S(7)) / 127, cy + sn(am) * imax(2, S(7)) / 127);
      line(d, cx, cy, cx + cs(ah) * imax(1, S(4)) / 127, cy + sn(ah) * imax(1, S(4)) / 127);
      break;
    }
    case ICON_WEATHER: {                                                // sun behind a cloud
      int sxc = X(6), syc = Y(-6);
      circle(d, sxc, syc, imax(2, S(3)));
      for (int k = 0; k < 5; k++) {                                     // rays only on the top / right, so they never run into the cloud
        int a = -96 + k * 32, r1 = S(6) + ((t && ((ph / 10 + k) & 1)) ? 1 : 0);
        line(d, sxc + cs(a) * S(5) / 127, syc + sn(a) * S(5) / 127, sxc + cs(a) * r1 / 127, syc + sn(a) * r1 / 127);
      }
      hl(d, X(-8), Y(8), S(16) + 1);                                    // cloud: flat bottom and three puffs
      arc(d, X(-5), Y(5), imax(1, S(3)), 128, 256);
      arc(d, X(0), Y(2), imax(2, S(5)), 128, 256);
      arc(d, X(5), Y(5), imax(1, S(3)), 192, 320);
      break;
    }
    case ICON_FORECAST: {                                               // a trend line
      const int p[5][2] = {{-9, 5}, {-4, -1}, {1, 2}, {6, -6}, {9, -4}};
      for (int k = 0; k < 4; k++) line(d, X(p[k][0]), Y(p[k][1]), X(p[k + 1][0]), Y(p[k + 1][1]));
      hl(d, X(-9), Y(8), S(18) + 1);
      disc(d, X(9), Y(-4), imax(1, S(1)) + ((t && ((ph / 12) & 1)) ? 1 : 0));
      break;
    }
    case ICON_AQI: {                                                    // wind
      int sh = t ? (ph / 4) % 4 : 0;
      hl(d, X(-9) + sh, Y(-5), S(12)); circle(d, X(5) + sh, Y(-7), imax(1, S(2)));
      hl(d, X(-9), cy, S(18) + 1);
      hl(d, X(-9) + sh, Y(5), S(11)); circle(d, X(4) + sh, Y(3), imax(1, S(2)));
      break;
    }
    case ICON_CURRENCY: {                                               // coin
      circle(d, cx, cy, imax(3, S(9)));
      circle(d, cx, cy, imax(2, S(6)));
      int hh = imax(2, S(8) - ((t && ((ph / 6) & 1)) ? S(3) : 0));
      vl(d, cx, cy - hh / 2, hh);
      break;
    }
    case ICON_WORLD: {                                                  // globe
      circle(d, cx, cy, imax(3, S(9)));
      hl(d, X(-9), cy, S(18) + 1);
      int m = t ? sn(ph * 2) * S(4) / 127 : 0;
      vl(d, cx + m, Y(-9), S(18) + 1);
      hl(d, X(-7), Y(-5), S(14)); hl(d, X(-7), Y(5), S(14));
      break;
    }
    case ICON_FACT: {                                                   // light bulb
      circle(d, cx, Y(-3), imax(2, S(6)));
      hl(d, X(-3), Y(4), S(6)); hl(d, X(-2), Y(7), S(4));
      if (!t || ((ph / 10) & 1)) { line(d, X(-9), Y(-3), X(-7), Y(-3)); line(d, X(7), Y(-3), X(9), Y(-3)); line(d, cx, Y(-10), cx, Y(-9)); }
      break;
    }
    case ICON_ADVICE: {                                                 // speech bubble with typing dots
      rframe(d, X(-9), Y(-8), S(18) + 1, S(12) + 1, imax(1, S(3)));
      line(d, X(-4), Y(4), X(-6), Y(9)); line(d, X(-6), Y(9), X(0), Y(4));
      int lit = t ? (ph / 10) % 4 : 3;
      for (int k = 0; k < 3; k++) if (k < lit) disc(d, X(-4 + k * 4), Y(-2), imax(1, S(1)));
      break;
    }
    case ICON_MOON: {                                                   // crescent
      arc(d, cx, cy, imax(3, S(9)), 64, 192);
      arc(d, X(5), cy, imax(3, S(9)), 76, 180);
      if (r >= 8) { px(d, X(5), Y(-5)); px(d, X(7), Y(3)); }
      break;
    }
    case ICON_NET: {                                                    // signal bars
      int lit = t ? 1 + (ph / 12) % 4 : 4;
      for (int k = 0; k < 4; k++) {
        int hh = imax(2, S(3 + 3 * k));
        if (k < lit) box(d, X(-9 + 5 * k), Y(8) - hh, imax(2, S(3)), hh);
        else frame(d, X(-9 + 5 * k), Y(8) - hh, imax(2, S(3)), hh);
      }
      break;
    }
    case ICON_ISS: {                                                    // station with solar panels
      box(d, X(-2), Y(-2), imax(2, S(4)) + 1, imax(2, S(4)) + 1);
      frame(d, X(-10), Y(-4), S(6), S(8)); frame(d, X(4), Y(-4), S(6), S(8));
      hl(d, X(-4), cy, S(2)); hl(d, X(2), cy, S(2));
      vl(d, cx, Y(-9), S(5));
      if (t && ((ph / 10) & 1)) px(d, cx, Y(-10));
      break;
    }
    case ICON_GITHUB: {                                                 // git branch
      int rr = imax(1, S(2));
      circle(d, X(-5), Y(-6), rr); circle(d, X(-5), Y(6), rr); circle(d, X(5), Y(-2), rr);
      vl(d, X(-5), Y(-6) + rr + 1, Y(6) - Y(-6) - 2 * rr - 1);
      line(d, X(-5), Y(2), X(5) , Y(-2) + rr + 1);
      break;
    }
    default: frame(d, X(-8), Y(-8), S(16) + 1, S(16) + 1); break;
  }
}

}  // namespace launcher
