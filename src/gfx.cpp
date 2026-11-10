#include "gfx.h"
#include <math.h>

namespace gfx {
namespace {
int8_t sinTab[256];
bool tabReady = false;
void initTab() {
  for (int i = 0; i < 256; i++) sinTab[i] = (int8_t)lroundf(sinf(i * 6.2831853f / 256.0f) * 127.0f);
  tabReady = true;
}
inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
}  // namespace

int sn(int a) { if (!tabReady) initTab(); return sinTab[a & 255]; }
int cs(int a) { return sn(a + 64); }

void px(U8G2 &d, int x, int y) { if ((unsigned)x < 128U && (unsigned)y < 64U) d.drawPixel(x, y); }

void hl(U8G2 &d, int x, int y, int w) {
  if (y < 0 || y > 63 || w <= 0) return;
  int x1 = imin(x + w, 128); x = imax(x, 0);
  if (x1 > x) d.drawHLine(x, y, x1 - x);
}
void vl(U8G2 &d, int x, int y, int h) {
  if (x < 0 || x > 127 || h <= 0) return;
  int y1 = imin(y + h, 64); y = imax(y, 0);
  if (y1 > y) d.drawVLine(x, y, y1 - y);
}
void box(U8G2 &d, int x, int y, int w, int h) {
  int x1 = imin(x + w, 128), y1 = imin(y + h, 64);
  x = imax(x, 0); y = imax(y, 0);
  if (x1 > x && y1 > y) d.drawBox(x, y, x1 - x, y1 - y);
}
void frame(U8G2 &d, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  if (w < 3 || h < 3) { box(d, x, y, w, h); return; }
  hl(d, x, y, w); hl(d, x, y + h - 1, w); vl(d, x, y, h); vl(d, x + w - 1, y, h);
}

void line(U8G2 &d, int x0, int y0, int x1, int y1) {      // Liang-Barsky clip, then the normal U8g2 line
  float t0 = 0.0f, t1 = 1.0f;
  float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  const float p[4] = {-dx, dx, -dy, dy};
  const float q[4] = {(float)x0, (float)(127 - x0), (float)y0, (float)(63 - y0)};
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0.0f) { if (q[i] < 0.0f) return; continue; }
    float r = q[i] / p[i];
    if (p[i] < 0.0f) { if (r > t1) return; if (r > t0) t0 = r; }
    else { if (r < t0) return; if (r < t1) t1 = r; }
  }
  d.drawLine((int)(x0 + t0 * dx + 0.5f), (int)(y0 + t0 * dy + 0.5f), (int)(x0 + t1 * dx + 0.5f), (int)(y0 + t1 * dy + 0.5f));
}

void circle(U8G2 &d, int cx, int cy, int r) {             // midpoint circle
  if (r <= 0) { px(d, cx, cy); return; }
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    px(d, cx + x, cy + y); px(d, cx - x, cy + y); px(d, cx + x, cy - y); px(d, cx - x, cy - y);
    px(d, cx + y, cy + x); px(d, cx - y, cy + x); px(d, cx + y, cy - x); px(d, cx - y, cy - x);
    y++;
    if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
  }
}

void disc(U8G2 &d, int cx, int cy, int r) {
  if (r <= 0) { px(d, cx, cy); return; }
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    hl(d, cx - x, cy + y, 2 * x + 1); hl(d, cx - x, cy - y, 2 * x + 1);
    hl(d, cx - y, cy + x, 2 * y + 1); hl(d, cx - y, cy - x, 2 * y + 1);
    y++;
    if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
  }
}

void arc(U8G2 &d, int cx, int cy, int r, int a0, int a1) {
  int steps = r * (a1 - a0) / 24 + 3;                      // roughly one step per pixel of arc length
  if (steps > 60) steps = 60;
  int px0 = 0, py0 = 0;
  for (int i = 0; i <= steps; i++) {
    int a = a0 + (a1 - a0) * i / steps;
    int x = cx + cs(a) * r / 127, y = cy + sn(a) * r / 127;
    if (i) line(d, px0, py0, x, y);
    px0 = x; py0 = y;
  }
}

void rframe(U8G2 &d, int x, int y, int w, int h, int r) {
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (r < 1) { frame(d, x, y, w, h); return; }
  hl(d, x + r, y, w - 2 * r); hl(d, x + r, y + h - 1, w - 2 * r);
  vl(d, x, y + r, h - 2 * r); vl(d, x + w - 1, y + r, h - 2 * r);
  int xi = r - 1;
  int cxl = x + r, cxr = x + w - 1 - r, cyt = y + r, cyb = y + h - 1 - r;
  int xx = xi, yy = 0, err = 1 - xi;
  while (xx >= yy) {
    px(d, cxr + xx, cyb + yy); px(d, cxl - xx, cyb + yy); px(d, cxr + xx, cyt - yy); px(d, cxl - xx, cyt - yy);
    px(d, cxr + yy, cyb + xx); px(d, cxl - yy, cyb + xx); px(d, cxr + yy, cyt - xx); px(d, cxl - yy, cyt - xx);
    yy++;
    if (err < 0) err += 2 * yy + 1; else { xx--; err += 2 * (yy - xx) + 1; }
  }
}

void rbox(U8G2 &d, int x, int y, int w, int h, int r) {
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (r < 1) { box(d, x, y, w, h); return; }
  box(d, x, y + r, w, h - 2 * r);
  int xx = r - 1, yy = 0, err = 1 - xx;
  int cxl = x + r, cxr = x + w - 1 - r, cyt = y + r, cyb = y + h - 1 - r;
  while (xx >= yy) {
    hl(d, cxl - xx, cyt - yy, (cxr + xx) - (cxl - xx) + 1); hl(d, cxl - xx, cyb + yy, (cxr + xx) - (cxl - xx) + 1);
    hl(d, cxl - yy, cyt - xx, (cxr + yy) - (cxl - yy) + 1); hl(d, cxl - yy, cyb + xx, (cxr + yy) - (cxl - yy) + 1);
    yy++;
    if (err < 0) err += 2 * yy + 1; else { xx--; err += 2 * (yy - xx) + 1; }
  }
}
}  // namespace gfx
