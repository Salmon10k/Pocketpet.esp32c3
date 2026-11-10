#pragma once
#include <U8g2lib.h>

// Clipped drawing primitives. U8g2 coordinates are unsigned 8-bit, so anything that can touch or cross a screen
// edge (sliding icons, big circles) must go through these instead of the raw U8g2 calls. All of them use the
// current draw colour and accept any int coordinates.
namespace gfx {
int sn(int a);                                   // sine, angle in 1/256 turns (0 = right, 64 = down), result -127..127
int cs(int a);
void px(U8G2 &d, int x, int y);
void hl(U8G2 &d, int x, int y, int w);
void vl(U8G2 &d, int x, int y, int h);
void box(U8G2 &d, int x, int y, int w, int h);
void frame(U8G2 &d, int x, int y, int w, int h);
void line(U8G2 &d, int x0, int y0, int x1, int y1);
void circle(U8G2 &d, int cx, int cy, int r);
void disc(U8G2 &d, int cx, int cy, int r);
void arc(U8G2 &d, int cx, int cy, int r, int a0, int a1);   // a0..a1 in 1/256 turns, a1 > a0
void rframe(U8G2 &d, int x, int y, int w, int h, int r);    // rounded outline
void rbox(U8G2 &d, int x, int y, int w, int h, int r);      // rounded filled box
}  // namespace gfx
