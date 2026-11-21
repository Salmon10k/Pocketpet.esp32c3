#include "games.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace gfx;

namespace games {
uint32_t best[COUNT] = {};

namespace {
const float DT = 0.02f;                 // every game runs on a fixed 20 ms physics step
bool bestDirty = false;
Id cur = STAR;
Input in = {};
uint32_t lastStepAt = 0;
bool usesTilt(Id g) { return g != PINBALL && g != STACK && g != DUEL && g != FLAPPY; }   // these play without the MPU6050

inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float dz(float v, float d) { if (v > d) return v - d; if (v < -d) return v + d; return 0.0f; }
float frand(float lo, float hi) { return lo + (float)random(0, 1001) / 1000.0f * (hi - lo); }

bool noteScore(uint32_t s) {
  if (s > best[cur]) { best[cur] = s; bestDirty = true; return true; }
  return false;
}

void banner(U8G2 &d, const char *text, int y) {            // black box, white text, drawn over the game
  d.setFont(u8g2_font_6x10_tf);
  int w = d.getStrWidth(text), x = (128 - w) / 2;
  d.setDrawColor(0); d.drawBox(x - 3, y - 9, w + 6, 12);
  d.setDrawColor(1); d.drawFrame(x - 3, y - 9, w + 6, 12);
  d.drawStr(x, y, text);
}

void small(U8G2 &d, const char *text, int cx, int y) {      // 5x7 text centred on cx
  d.setFont(u8g2_font_5x7_tf);
  d.drawStr(cx - d.getStrWidth(text) / 2, y, text);
}

void gameOver(U8G2 &d, uint32_t score, bool isBest) {
  d.setDrawColor(0); d.drawBox(12, 12, 104, 42);
  d.setDrawColor(1); d.drawRFrame(12, 12, 104, 42, 3);
  d.setFont(u8g2_font_7x13B_tf);
  d.drawStr((128 - d.getStrWidth("GAME OVER")) / 2, 25, "GAME OVER");
  char buf[28];
  snprintf(buf, sizeof(buf), isBest ? "%lu  NEW BEST!" : "score %lu", (unsigned long)score);
  small(d, buf, 64, 36);
  small(d, "A retry    B back", 64, 49);
}

void calibrationBanner(U8G2 &d) {
  banner(d, "Hold like a phone...", 30);
  d.drawFrame(34, 35, 60, 5);
  d.drawBox(35, 36, (int)(58.0f * clampf(in.calib, 0, 1)), 3);
}

// =====================================================================================================
//  STAR BLASTER: side-scrolling shooter. Tilt moves the ship in 2D, A shoots, C = smart bomb.
// =====================================================================================================
namespace star {
const int NB = 14, NE = 9, NEB = 10, NFX = 30, NST = 24, NPU = 2;
struct Bul { float x, y, vx, vy; bool on; };
struct En { float x, y, by, vx, vy, t, cd; int8_t hp; uint8_t type, flash; bool on; };   // 0 drone, 1 gunship, 2 rock, 3 small rock
struct Pup { float x, y; uint8_t kind; bool on; };                                     // 0 weapon, 1 life, 2 bomb
struct Fx { float x, y, vx, vy; uint8_t life; bool on; };
struct Sp { float x, y, sp; };
Bul pb[NB], eb[NEB];
En en[NE];
Pup pu[NPU];
Fx fx[NFX];
Sp st[NST];
float sx, sy, fireCd, spawnCd, invuln, bombR, tsec;
int lives, weapon, bombs, kills;
uint32_t score;
bool over, isBest;
const float HX[4] = {5, 8, 6, 3}, HY[4] = {4, 5, 6, 3};   // enemy half sizes

void reset() {
  memset(pb, 0, sizeof(pb)); memset(eb, 0, sizeof(eb)); memset(en, 0, sizeof(en));
  memset(pu, 0, sizeof(pu)); memset(fx, 0, sizeof(fx));
  sx = 14; sy = 36; fireCd = 0; spawnCd = 0.6f; invuln = 1.0f; bombR = 0; tsec = 0;
  lives = 3; weapon = 1; bombs = 1; kills = 0; score = 0; over = false; isBest = false;
  for (auto &s : st) { s.x = (float)random(0, 128); s.y = (float)random(9, 64); s.sp = frand(8, 60); }
}

void explode(float x, float y, int n) {
  for (int i = 0; i < NFX && n > 0; ++i) if (!fx[i].on) {
    float a = frand(0, 6.283f), sp = frand(10, 55);
    fx[i].x = x; fx[i].y = y; fx[i].vx = cosf(a) * sp; fx[i].vy = sinf(a) * sp;
    fx[i].life = (uint8_t)random(10, 24); fx[i].on = true;
    --n;
  }
}

void drop(float x, float y) {
  for (auto &p : pu) if (!p.on) {
    int r = random(0, 100);
    p.on = true; p.x = x; p.y = y; p.kind = r < 50 ? 0 : (r < 75 ? 2 : 1);
    return;
  }
}

void killEn(En &e) {
  static const uint16_t pts[4] = {10, 30, 20, 10};
  e.on = false;
  explode(e.x, e.y, e.type == 1 ? 14 : 8);
  score += pts[e.type];
  ++kills;
  if (e.type == 2) {                                         // a big rock splits in two
    for (int k = 0; k < 2; ++k) for (auto &s : en) if (!s.on) {
      s = En{};
      s.on = true; s.type = 3; s.hp = 1; s.x = e.x; s.y = e.y;
      s.vx = -frand(18, 42); s.vy = k ? frand(18, 40) : -frand(18, 40);
      break;
    }
  }
  if (kills % 15 == 0 && bombs < 3) ++bombs;
  if (random(0, 100) < (e.type == 1 ? 60 : 9)) drop(e.x, e.y);
}

void hurt() {
  if (invuln > 0 || over) return;
  --lives; invuln = 1.5f;
  if (weapon > 1) --weapon;
  explode(sx, sy, 14);
  if (lives <= 0) { over = true; isBest = noteScore(score); }
}

void spawnEn() {
  for (auto &e : en) if (!e.on) {
    int r = random(0, 100);
    uint8_t type = r < 55 ? 0 : (r < 78 ? 2 : 1);
    if (tsec < 12 && type == 1) type = 0;
    float sp = fminf(1.6f, 1.0f + tsec * 0.004f);
    e = En{};
    e.on = true; e.type = type; e.x = 134; e.y = frand(14, 58); e.by = e.y; e.t = frand(0, 6); e.cd = frand(0.8f, 1.6f);
    if (type == 0) { e.hp = 1; e.vx = -(32 + frand(0, 12)) * sp; }
    else if (type == 1) { e.hp = 3; e.vx = -24 * sp; }
    else { e.hp = 3; e.vx = -frand(24, 40) * sp; e.vy = frand(-9, 9); }
    return;
  }
}

void enemyShot(const En &e) {
  for (auto &b : eb) if (!b.on) {
    float dx = sx - e.x, dy = sy - e.y, l = sqrtf(dx * dx + dy * dy);
    if (l < 1) l = 1;
    b.on = true; b.x = e.x - 6; b.y = e.y; b.vx = dx / l * 52; b.vy = dy / l * 52;
    return;
  }
}

void shoot() {
  auto add = [](float x, float y, float vy) {
    for (auto &b : pb) if (!b.on) { b.on = true; b.x = x; b.y = y; b.vx = 190; b.vy = vy; return; }
  };
  if (weapon == 1) add(sx + 7, sy, 0);
  else if (weapon == 2) { add(sx + 6, sy - 2, 0); add(sx + 6, sy + 2, 0); }
  else { add(sx + 7, sy, 0); add(sx + 5, sy - 3, -36); add(sx + 5, sy + 3, 36); }
}

void bomb() {
  if (over || bombs <= 0 || bombR > 0) return;
  --bombs; bombR = 1;
  for (auto &b : eb) b.on = false;
  for (auto &e : en) if (e.on && e.x < 130) { e.hp -= 4; e.flash = 6; if (e.hp <= 0) killEn(e); }
}

void step() {
  tsec += DT;
  for (auto &f : fx) if (f.on) {
    f.x += f.vx * DT; f.y += f.vy * DT;
    if (f.life == 0 || --f.life == 0) f.on = false;
  }
  for (auto &s : st) { s.x -= s.sp * DT; if (s.x < 0) { s.x += 128; s.y = (float)random(9, 64); } }
  if (over) return;

  // ship follows the tilt
  float tx = clampf(dz(in.tx, 0.03f), -0.45f, 0.45f) / 0.45f;
  float ty = clampf(dz(in.ty, 0.03f), -0.45f, 0.45f) / 0.45f;
  sx += (38 + tx * 30 - sx) * 0.3f;
  sy += (36 + ty * 22 - sy) * 0.3f;
  if (invuln > 0) invuln -= DT;
  fireCd -= DT;
  if (in.aHeld && fireCd <= 0) { shoot(); fireCd = weapon >= 3 ? 0.17f : 0.15f; }
  if (bombR > 0) { bombR += 160 * DT; if (bombR > 110) bombR = 0; }

  spawnCd -= DT;
  if (spawnCd <= 0) { spawnEn(); spawnCd = fmaxf(0.3f, 0.95f - tsec * 0.008f) * frand(0.75f, 1.25f); }

  for (auto &b : pb) if (b.on) {
    b.x += b.vx * DT; b.y += b.vy * DT;
    if (b.x > 132 || b.y < 9 || b.y > 64) b.on = false;
  }
  for (auto &b : eb) if (b.on) {
    b.x += b.vx * DT; b.y += b.vy * DT;
    if (b.x < -4 || b.x > 132 || b.y < 8 || b.y > 65) { b.on = false; continue; }
    if (invuln <= 0 && fabsf(b.x - sx) < 5 && fabsf(b.y - sy) < 4) { b.on = false; hurt(); }
  }
  for (auto &e : en) if (e.on) {
    e.t += DT; e.x += e.vx * DT;
    if (e.flash) --e.flash;
    if (e.type == 0) e.y = e.by + 12 * sinf(e.t * 3.2f);
    else if (e.type == 1) {
      if (e.x < 104 && e.vx < -6) e.vx = -6;
      e.y += clampf(sy - e.y, -14, 14) * DT * 1.5f;
      e.cd -= DT;
      if (e.cd <= 0 && e.x < 124) { e.cd = 1.5f; enemyShot(e); }
    } else {
      e.y += e.vy * DT;
      if (e.y < 11 || e.y > 59) e.vy = -e.vy;
    }
    if (e.x < -14) { e.on = false; continue; }
    if (invuln <= 0 && fabsf(e.x - sx) < HX[e.type] + 4 && fabsf(e.y - sy) < HY[e.type] + 3) {
      e.on = false; explode(e.x, e.y, 8); hurt();
    }
  }
  for (auto &b : pb) if (b.on) for (auto &e : en) if (e.on) {
    if (fabsf(b.x - e.x) < HX[e.type] + 1 && fabsf(b.y - e.y) < HY[e.type] + 1) {
      b.on = false; e.flash = 4;
      if (--e.hp <= 0) killEn(e);
      break;
    }
  }
  for (auto &p : pu) if (p.on) {
    p.x -= 24 * DT;
    if (p.x < -6) { p.on = false; continue; }
    if (fabsf(p.x - sx) < 8 && fabsf(p.y - sy) < 7) {
      p.on = false;
      if (p.kind == 0) { if (weapon < 3) ++weapon; else score += 50; }
      else if (p.kind == 1) { if (lives < 5) ++lives; else score += 50; }
      else { if (bombs < 3) ++bombs; else score += 50; }
    }
  }
}

void draw(U8G2 &d, uint32_t now) {
  for (auto &s : st) px(d, (int)s.x, (int)s.y);
  if (bombR > 0) { circle(d, (int)sx, (int)sy, (int)bombR); if (bombR > 8) circle(d, (int)sx, (int)sy, (int)bombR - 6); }
  for (auto &f : fx) if (f.on) { px(d, (int)f.x, (int)f.y); if (f.life > 12) px(d, (int)f.x + 1, (int)f.y); }
  for (auto &p : pu) if (p.on) {
    int x = (int)p.x, y = (int)p.y;
    rframe(d, x - 4, y - 4, 9, 9, 2);
    if (p.kind == 0) { hl(d, x - 2, y - 2, 5); hl(d, x - 2, y, 5); hl(d, x - 2, y + 2, 5); }
    else if (p.kind == 1) { hl(d, x - 2, y, 5); vl(d, x, y - 2, 5); }
    else disc(d, x, y, 2);
  }
  for (auto &e : en) if (e.on) {
    int x = (int)e.x, y = (int)e.y;
    if (e.flash) { disc(d, x, y, (int)HX[e.type]); continue; }
    if (e.type == 0) {                                        // drone: a diamond with a core
      line(d, x - 5, y, x, y - 4); line(d, x, y - 4, x + 5, y); line(d, x + 5, y, x, y + 4); line(d, x, y + 4, x - 5, y);
      px(d, x, y);
    } else if (e.type == 1) {                                 // gunship
      frame(d, x - 8, y - 4, 16, 9); box(d, x - 4, y - 1, 6, 3); hl(d, x - 11, y, 4); vl(d, x + 8, y - 6, 13);
    } else {                                                  // rocks
      int r = e.type == 2 ? 6 : 3;
      circle(d, x, y, r);
      if (e.type == 2) { px(d, x - 2, y - 1); px(d, x + 2, y + 2); px(d, x + 1, y - 3); }
    }
  }
  for (auto &b : pb) if (b.on) hl(d, (int)b.x - 1, (int)b.y, 4);
  for (auto &b : eb) if (b.on) { box(d, (int)b.x - 1, (int)b.y - 1, 3, 3); }

  if (!over && (invuln <= 0 || (now / 70) % 2)) {            // ship (blinks while invulnerable)
    int x = (int)sx, y = (int)sy;
    line(d, x + 7, y, x - 5, y - 5); line(d, x + 7, y, x - 5, y + 5);
    line(d, x - 5, y - 5, x - 2, y); line(d, x - 5, y + 5, x - 2, y);
    px(d, x + 1, y);
    int fl = 1 + (int)((now / 50) % 3);
    hl(d, x - 2 - fl - 1, y, fl + 1);
  }

  d.setFont(u8g2_font_5x7_tf);                               // HUD
  char buf[24];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  d.drawStr(0, 7, buf);
  for (int i = 0; i < lives; ++i) box(d, 48 + i * 7, 1, 5, 5);
  snprintf(buf, sizeof(buf), "W%d B%d", weapon, bombs);
  d.drawStr(128 - d.getStrWidth(buf), 7, buf);
  if (over) gameOver(d, score, isBest);
}
}  // namespace star

// =====================================================================================================
//  SNIPER: a scope over a night city. Tilt aims (rate control), A fires, hold C to steady your breath and zoom.
//  Enemies pop up in windows for a short time; if one survives, it shoots you. Three lives.
// =====================================================================================================
namespace sniper {
const int W = 320, GROUND = 70, MAXB = 9, R = 30;
struct Bld { int16_t x, w, top; uint8_t cols, rows; };
struct Tgt { uint8_t b, c, r; float life; bool on; };
Bld bl[MAXB];
int nb;
Tgt tg[3];
float camX, camY, zoom, breath, bolt, recoil, flashT, hurtT, spawnCd, tsec, swx, swy, popT;
int lives, kills;
uint32_t score;
bool over, isBest, gasp;
char popText[12];

// Windows are 9 x 12 world px, 12 / 14 apart (bigger than the first version: the small windows were too hard to find).
void winPos(int b, int c, int r, float &x, float &y) { x = bl[b].x + 4 + c * 12; y = bl[b].top + 5 + r * 14; }
void headPos(int b, int c, int r, float &x, float &y) { winPos(b, c, r, x, y); x += 4.5f; y += 4.0f; }

void reset() {
  nb = 0;
  int x = -6;
  while (x < W && nb < MAXB) {
    Bld &b = bl[nb++];
    b.w = (int16_t)random(34, 58); b.x = (int16_t)x; b.top = (int16_t)random(14, 36);
    b.cols = (uint8_t)imin(4, (b.w - 4) / 12);
    b.rows = (uint8_t)imin(5, (GROUND - b.top - 8) / 14);
    x += b.w + random(0, 4);
  }
  memset(tg, 0, sizeof(tg));
  camX = W / 2; camY = 38; zoom = 1; breath = 1; bolt = 0; recoil = 0; flashT = 0; hurtT = 0; spawnCd = 1.2f; tsec = 0;
  swx = swy = 0; popT = 0; popText[0] = 0;
  lives = 3; kills = 0; score = 0; over = false; isBest = false; gasp = false;
}

void pop(const char *t) { snprintf(popText, sizeof(popText), "%s", t); popT = 0.9f; }

void spawnTarget() {
  for (auto &t : tg) if (!t.on) {
    for (int tries = 0; tries < 16; ++tries) {
      int b = random(0, nb);
      if (bl[b].cols == 0 || bl[b].rows == 0) continue;
      int c = random(0, bl[b].cols), r = random(0, bl[b].rows);
      float wx, wy;
      winPos(b, c, r, wx, wy);
      if (tries < 10 && fabsf(wx - camX) > 85) continue;               // mostly pop up close to where you are looking
      bool used = false;
      for (auto &o : tg) if (o.on && o.b == b && o.c == c && o.r == r) used = true;
      if (used) continue;
      t.on = true; t.b = (uint8_t)b; t.c = (uint8_t)c; t.r = (uint8_t)r;
      t.life = clampf(4.6f - 0.07f * kills, 2.4f, 4.6f) + fabsf(wx - camX) / 110.0f;   // far away = a little more time to find him
      return;
    }
    return;
  }
}

void fire() {
  if (over || bolt > 0) return;
  bolt = 0.9f; flashT = 0.07f;
  float cxw = camX + swx, cyw = camY + swy;          // where the crosshair points in the world, before the kick
  recoil = 5.0f;
  for (auto &t : tg) if (t.on) {
    float wx, wy;
    winPos(t.b, t.c, t.r, wx, wy);
    float hx = cxw - (wx + 4.5f), hy = cyw - (wy + 4.0f);
    bool head = hx * hx + hy * hy < 3.6f * 3.6f;
    bool body = cxw >= wx + 0.0f && cxw <= wx + 9.0f && cyw >= wy + 6.5f && cyw <= wy + 12.0f;
    if (head || body) {
      t.on = false; ++kills;
      score += head ? 3 : 1;
      pop(head ? "HEAD!" : "HIT");
      return;
    }
  }
  pop("MISS");
}

void step() {
  tsec += DT;
  if (bolt > 0) bolt -= DT;
  if (flashT > 0) flashT -= DT;
  if (hurtT > 0) hurtT -= DT;
  if (popT > 0) popT -= DT;
  recoil *= 0.88f;
  if (over) return;

  bool hold = in.cHeld && !gasp && breath > 0;
  zoom += ((hold ? 2.0f : 1.0f) - zoom) * 0.25f;
  if (hold) { breath -= DT * 0.33f; if (breath <= 0) { breath = 0; gasp = true; } }
  else { breath = fminf(1.0f, breath + DT * (gasp ? 0.18f : 0.25f)); if (gasp && breath > 0.4f) gasp = false; }

  float amp = (hold ? 0.22f : 1.0f) * (gasp ? 1.8f : 1.0f);          // rifle sway
  swx = amp * (1.7f * sinf(tsec * 1.9f) + 0.7f * sinf(tsec * 5.3f));
  swy = amp * (1.5f * cosf(tsec * 1.4f) + 0.6f * sinf(tsec * 3.7f));

  camX = clampf(camX + dz(in.tx, 0.05f) * 300.0f / zoom * DT, 4, W - 4);     // tilt = how fast the scope swings
  camY = clampf(camY + dz(in.ty, 0.05f) * 170.0f / zoom * DT, 20, 58);     // was 31..45: windows on low floors could not be aimed at

  int active = 0;
  for (auto &t : tg) if (t.on) ++active;
  spawnCd -= DT;
  if (spawnCd <= 0 && active < imin(3, 1 + kills / 6)) {
    spawnTarget();
    spawnCd = clampf(1.5f - 0.04f * kills, 0.45f, 1.5f) * frand(0.7f, 1.3f);
  }
  for (auto &t : tg) if (t.on) {
    t.life -= DT;
    if (t.life <= 0) {                                               // he got a shot off
      t.on = false; --lives; hurtT = 0.35f; recoil += 3;
      if (lives <= 0) { over = true; isBest = noteScore(score); }
    }
  }
}

void draw(U8G2 &d, uint32_t now) {
  float Z = zoom;
  float cx = camX + swx, cy = camY + swy - recoil;
  auto SX = [&](float wx) { return 64 + (int)lroundf((wx - cx) * Z); };
  auto SY = [&](float wy) { return 32 + (int)lroundf((wy - cy) * Z); };

  circle(d, SX(220), SY(14), imax(2, (int)(5 * Z)));                 // moon
  for (int i = 0; i < nb; ++i) {
    const Bld &b = bl[i];
    int x0 = SX(b.x), x1 = SX(b.x + b.w), y0 = SY(b.top), y1 = SY(GROUND);
    if (x1 < 0 || x0 > 127) continue;
    frame(d, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    hl(d, x0 - 1, y0 - 1, x1 - x0 + 3);                                                         // roof edge
    for (int r = 0; r < b.rows; ++r) {                                                          // floor ledges
      float lx, ly;
      winPos(i, 0, r, lx, ly);
      hl(d, x0, SY(ly + 13), x1 - x0);
    }
    for (int c = 0; c < b.cols; ++c) for (int r = 0; r < b.rows; ++r) {
      float wx, wy;
      winPos(i, c, r, wx, wy);
      int sx0 = SX(wx), sy0 = SY(wy), ww = (int)(9 * Z), wh = (int)(12 * Z);
      if (sx0 > 127 || sx0 + ww < 0 || sy0 > 63 || sy0 + wh < 0) continue;
      d.setDrawColor(0); box(d, sx0, sy0, ww, wh);
      d.setDrawColor(1); frame(d, sx0, sy0, ww, wh);
    }
  }
  hl(d, 0, SY(GROUND), 128);
  for (auto &t : tg) if (t.on) {                                    // enemy bust in his window
    float wx, wy;
    winPos(t.b, t.c, t.r, wx, wy);
    bool blink = t.life < 0.7f && ((now / 60) % 2);
    if (blink) continue;
    disc(d, SX(wx + 4.5f), SY(wy + 4.0f), imax(2, (int)(2.8f * Z)));
    box(d, SX(wx + 1.0f), SY(wy + 7.0f), (int)(7 * Z), (int)(5 * Z));
    line(d, SX(wx + 8), SY(wy + 8), SX(wx + 12), SY(wy + 6));
  }
  if (flashT > 0) { d.setDrawColor(2); d.drawBox(34, 2, 60, 60); d.setDrawColor(1); }

  d.setDrawColor(0);                                                 // scope mask: black outside the circle
  for (int y = 0; y < 64; ++y) {
    int dy = y - 32, w2 = R * R - dy * dy;
    if (w2 <= 0) { d.drawHLine(0, y, 128); continue; }
    int w = (int)sqrtf((float)w2);
    d.drawHLine(0, y, 64 - w);
    d.drawHLine(64 + w + 1, y, 128 - (64 + w + 1));
  }
  d.setDrawColor(1);
  circle(d, 64, 32, R);
  circle(d, 64, 32, R + 1);
  hl(d, 64 - R + 2, 32, R - 8); hl(d, 64 + 7, 32, R - 8);            // reticle
  vl(d, 64, 32 - R + 2, R - 8); vl(d, 64, 32 + 7, R - 8);
  for (int k = 1; k <= 3; ++k) { px(d, 64 - 6 - k * 5, 31); px(d, 64 + 6 + k * 5, 33); px(d, 63, 32 + 6 + k * 5); px(d, 65, 32 - 6 - k * 5); }
  px(d, 64, 32);

  for (auto &t : tg) if (t.on) {                                    // blip on the ring for targets that are out of view
    float wx, wy;
    winPos(t.b, t.c, t.r, wx, wy);
    float dx = (wx + 4.5f - cx) * Z, dy = (wy + 6.0f - cy) * Z;
    if (dx * dx + dy * dy < (R - 3) * (R - 3)) continue;
    float a = atan2f(dy, dx);
    if (t.life > 1.2f || (now / 100) % 2) disc(d, 64 + (int)lroundf(cosf(a) * (R + 4)), 32 + (int)lroundf(sinf(a) * (R + 4)), 2);
  }

  d.setFont(u8g2_font_5x7_tf);                                       // HUD in the black corners
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  d.drawStr(1, 8, buf);
  for (int i = 0; i < lives; ++i) box(d, 2 + i * 7, 56, 5, 5);
  frame(d, 120, 10, 6, 42);                                          // breath meter
  int bh = (int)(40 * breath);
  box(d, 121, 51 - bh, 4, bh);
  d.drawStr(113, 8, gasp ? "!!" : "O2");
  if (Z > 1.5f) d.drawStr(101, 34, "2x");
  if (bolt > 0 && ((now / 120) % 2)) d.drawStr(99, 62, "BOLT");
  if (popT > 0) d.drawStr(1, 20, popText);
  if (hurtT > 0) { d.setDrawColor(2); d.drawBox(0, 0, 128, 64); d.setDrawColor(1); }
  if (over) gameOver(d, score, isBest);
}
}  // namespace sniper

// =====================================================================================================
//  ROAD RUSH: pseudo-3D racer. Tilt steers, A = nitro, C = brake. Beat the clock, checkpoints give time.
// =====================================================================================================
namespace road {
const int H = 22;                 // horizon row
const float ZM = 4.0f;            // metres per unit of depth
const float HWP = 46.0f;          // road half width in pixels at the bottom row
const float CK = 1.6e-3f;         // how strongly the track curvature bends the picture
struct Car { float z, x, v; uint8_t type; bool on; };      // type 0 sedan, 1 truck (slow, tall)
Car cars[6];
float dist, spd, ppx, nitro, timeLeft, invuln, shake, cpMsg, nextCp, carCd, tsec, bg;
bool over, isBest, nitroOn;
int16_t ctr[64], hwv[64];

float curveAt(float z) {
  float ramp = clampf(z / 300.0f, 0, 1);
  return ramp * (1.1f * sinf(z * 0.0042f) + 0.7f * sinf(z * 0.011f + 1.3f));
}

void reset() {
  memset(cars, 0, sizeof(cars));
  dist = 0; spd = 0.2f; ppx = 0; nitro = 1; timeLeft = 35; invuln = 0; shake = 0; cpMsg = 0; nextCp = 1000;
  carCd = 1.5f; tsec = 0; bg = 0; over = false; isBest = false; nitroOn = false;
}

void spawnCar() {
  int lane = random(0, 3);
  float z = dist + frand(110, 160);
  for (auto &c : cars) if (c.on && fabsf(c.x - (lane - 1) * 0.66f) < 0.1f && fabsf(c.z - z) < 35) return;
  for (auto &c : cars) if (!c.on) {
    c.on = true; c.x = (lane - 1) * 0.66f; c.z = z;
    c.type = (uint8_t)(random(0, 4) == 0 ? 1 : 0);
    c.v = c.type ? frand(10, 18) : frand(16, 34);
    return;
  }
}

void step() {
  tsec += DT;
  if (shake > 0) shake -= DT;
  if (cpMsg > 0) cpMsg -= DT;
  if (invuln > 0) invuln -= DT;
  if (over) return;

  bool off = fabsf(ppx) > 1.02f;
  float target = 0.62f;
  nitroOn = in.aHeld && nitro > 0.02f && !in.cHeld;
  if (nitroOn) { target = 1.0f; nitro -= 0.28f * DT; } else nitro = fminf(1.0f, nitro + 0.07f * DT);
  if (in.cHeld) target = 0.12f;
  if (off) target = fminf(target, 0.32f);
  if (spd < target) spd = fminf(target, spd + (nitroOn ? 0.7f : 0.45f) * DT);
  else spd = fmaxf(target, spd - (in.cHeld ? 1.1f : 0.45f) * DT);
  dist += spd * 75.0f * DT;

  float steer = clampf(dz(in.tx, 0.04f) * 1.8f, -0.8f, 0.8f);      // full steering at about a 0.48 tilt
  float cv = curveAt(dist);
  ppx += steer * 4.0f * (0.4f + spd) * DT;                         // about 2.5x quicker than the first version
  ppx -= cv * spd * spd * 0.95f * DT;                                // the bend throws you outwards
  ppx = clampf(ppx, -1.9f, 1.9f);
  bg += cv * spd * DT * 40.0f;
  if (off && spd > 0.2f) shake = fmaxf(shake, 0.05f);

  timeLeft -= DT;
  if (dist >= nextCp) { timeLeft += 20; nextCp += 1200; cpMsg = 1.6f; }
  if (timeLeft <= 0) { timeLeft = 0; over = true; isBest = noteScore((uint32_t)dist); return; }

  carCd -= DT;
  if (carCd <= 0) { spawnCar(); carCd = frand(0.5f, 1.4f) / (1.0f + dist / 6000.0f); }
  for (auto &c : cars) if (c.on) {
    c.z += c.v * DT;
    float rel = c.z - dist;
    if (rel < -15) { c.on = false; continue; }
    if (invuln <= 0 && rel > -1 && rel < 5 && fabsf(c.x - ppx) < 0.34f) {
      spd *= 0.3f; timeLeft -= 2; invuln = 1.2f; shake = 0.5f; c.on = false;
    }
  }
}

// Traffic is drawn hollow (black body, white outline, two solid tail lights) so it never melts into a blob on the 1-bit
// screen; trucks are tall boxes with a stripe. Our own car is solid white with a spoiler, so it is never confused with them.
void traffic(U8G2 &d, int cx, int yb, int w, int h, int type) {
  if (w < 4 || h < 3) { box(d, cx - 1, yb - 2, 3, 3); return; }
  if (type) h = h * 14 / 10;
  int x0 = cx - w / 2;
  int bodyH = type ? h : imax(2, h * 6 / 10), top = yb - h;
  d.setDrawColor(0); box(d, x0, yb - bodyH, w, bodyH + 1);
  if (!type) box(d, x0 + w / 6, top, w - w / 3 + 1, h - bodyH + 1);
  d.setDrawColor(1);
  frame(d, x0, yb - bodyH, w, bodyH + 1);
  if (!type) {
    hl(d, x0 + w / 6, top, w - w / 3 + 1);                               // roof
    vl(d, x0 + w / 6, top, h - bodyH + 1); vl(d, x0 + w - w / 6, top, h - bodyH + 1);
    if (w >= 12) hl(d, x0 + 2, yb - bodyH + bodyH / 2, w - 4);          // bumper line
  } else if (w >= 8) {
    vl(d, cx, yb - bodyH + 1, bodyH - 2);                                // cargo door seam
    hl(d, x0, yb - bodyH + 2, w);
  }
  int lw = imax(2, w / 6);
  box(d, x0 + 1, yb - imax(2, bodyH / 3), lw, imax(2, bodyH / 4));        // tail lights, solid
  box(d, x0 + w - 1 - lw, yb - imax(2, bodyH / 3), lw, imax(2, bodyH / 4));
}

void playerCar(U8G2 &d, int cx, int yb) {
  box(d, cx - 8, yb - 7, 17, 8);                                          // body
  box(d, cx - 5, yb - 11, 11, 5);                                         // cabin
  hl(d, cx - 9, yb - 11, 19); vl(d, cx - 9, yb - 11, 2); vl(d, cx + 9, yb - 11, 2);   // spoiler
  d.setDrawColor(0);
  box(d, cx - 4, yb - 10, 9, 3);                                          // rear window
  hl(d, cx - 6, yb - 3, 13);                                              // bumper gap
  px(d, cx - 6, yb - 6); px(d, cx + 6, yb - 6);
  d.setDrawColor(1);
  box(d, cx - 9, yb - 2, 3, 3); box(d, cx + 7, yb - 2, 3, 3);            // tyres
}

void draw(U8G2 &d, uint32_t now) {
  // sky: a striped sun and a ridge that slides with the bends
  int sunX = 96 - ((int)(bg * 0.08f) % 120);
  if (sunX < -20) sunX += 150;
  for (int y = H - 12; y < H; ++y) {
    int dy = H - y;
    if (dy < 7 && (dy % 3 == 0)) continue;
    int w = (int)sqrtf((float)(12 * 12 - dy * dy));
    hl(d, sunX - w, y, 2 * w + 1);
  }
  for (int x = 0; x < 128; ++x) {
    float u = (x + bg * 0.25f) * 0.07f;
    int y = H - 3 - (int)(3.0f * sinf(u) + 2.0f * sinf(u * 2.7f + 1.0f) + 5.0f);
    if (y < H - 1) px(d, x, y);
  }
  hl(d, 0, H, 128);

  // road, row by row from the nearest row to the horizon
  float xx = 0, dxv = 0;
  for (int y = 63; y > H; --y) {
    int k = y - H;
    float s = k / 42.0f, zr = 1.0f / s;
    float zrNext = k > 1 ? 42.0f / (k - 1) : 84.0f;
    float dzm = (zrNext - zr) * ZM;
    float trackPos = dist + (zr - 1.0f) * ZM;
    xx += dxv * dzm;
    dxv += curveAt(trackPos) * CK * dzm;
    int c = 64 + (int)(s * (xx - ppx) * HWP);
    int hw = (int)(s * HWP + 0.5f);
    ctr[y] = (int16_t)c; hwv[y] = (int16_t)hw;
    int x0 = c - hw, x1 = c + hw;
    bool band = ((int)(trackPos / 5.0f)) & 1;
    hl(d, x0 - 1, y, band ? 3 : 1);
    hl(d, x1 - 1, y, band ? 3 : 1);
    if (hw >= 6 && (((int)(trackPos / 6.0f)) & 1)) { px(d, c - hw / 3, y); px(d, c + hw / 3, y); }
    if (band) {
      for (int gx = y & 3; gx < x0 - 3; gx += 4) px(d, gx, y);
      for (int gx = (x1 + 4) + (y & 3); gx < 128; gx += 4) px(d, gx, y);
    }
  }

  // traffic, far to near
  int order[6], n = 0;
  for (int i = 0; i < 6; ++i) if (cars[i].on) order[n++] = i;
  for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) if (cars[order[j]].z > cars[order[i]].z) { int t = order[i]; order[i] = order[j]; order[j] = t; }
  for (int q = 0; q < n; ++q) {
    const Car &cr = cars[order[q]];
    float rel = cr.z - dist;
    if (rel < 0.5f || rel > 160) continue;
    float s = 1.0f / (rel / ZM + 1.0f);
    int y = H + (int)(s * 42.0f);
    if (y > 62) y = 62;
    if (y <= H + 1) continue;
    int cx = ctr[y] + (int)(cr.x * hwv[y]);
    traffic(d, cx, y, (int)(s * 31), (int)(s * 18), cr.type);   // big on purpose: the screen is tiny
  }

  int jx = shake > 0 ? random(-1, 2) : 0, jy = shake > 0 ? random(0, 2) : 0;   // our own car
  if (invuln <= 0 || (now / 70) % 2) playerCar(d, 64 + jx, 62 + jy);
  if (nitroOn) { vl(d, 60, 56, 2); vl(d, 68, 56, 2); }
  if (nitroOn || spd > 0.85f) { hl(d, 3, 30 + (int)(now / 40 % 12), 6); hl(d, 119, 36 + (int)(now / 50 % 14), 6); }

  d.setFont(u8g2_font_5x7_tf);                                                  // HUD
  char buf[24];
  snprintf(buf, sizeof(buf), "T%d", (int)ceilf(timeLeft));
  d.drawStr(0, 7, buf);
  snprintf(buf, sizeof(buf), "%dm", (int)dist);
  d.drawStr(64 - d.getStrWidth(buf) / 2, 7, buf);
  snprintf(buf, sizeof(buf), "%dkm/h", (int)(spd * 270.0f));
  d.drawStr(128 - d.getStrWidth(buf), 7, buf);
  frame(d, 1, 58, 26, 5);
  box(d, 2, 59, (int)(24 * nitro), 3);
  if (cpMsg > 0) banner(d, "CHECKPOINT +20s", 38);
  if (over) gameOver(d, (uint32_t)dist, isBest);
}
}  // namespace road

// =====================================================================================================
//  PINBALL: the table lies on its side (gravity pulls to the right, the flippers are on the right edge).
//  A = upper flipper, C = lower flipper, shake the keychain to nudge the ball (shake too much = TILT).
// =====================================================================================================
namespace pin {
enum State { SERVE, PLAY, DRAINED, OVER };
const float BALL_R = 2.5f, GRAV = 220.0f, VMAX = 340.0f;
const float BX[3] = {46, 46, 64}, BY[3] = {18, 46, 32}, BR = 5.5f;
struct Flip { float px, py, rest, act, ang, vel; int dir; };       // dir +1: sweeps inward from the top wall, -1: from the bottom wall
const float FL = 24.0f;
Flip fl[2];
float bx, by, bvx, bvy, bumpFlash[3], tiltMeter, nudgeCd, stall, comboT, stateT;
State state;
int balls, combo;
uint32_t score;
bool tilted, isBest, flashTilt;

void tip(const Flip &f, float &tx, float &ty) { tx = f.px + FL * sinf(f.ang); ty = f.py + f.dir * FL * cosf(f.ang); }

void serve() {
  state = SERVE; bx = 6; by = (float)random(20, 45); bvx = bvy = 0;
  tilted = false; tiltMeter = 0; stall = 0; combo = 1; comboT = 0;
}

void reset() {
  fl[0] = {100, 6, 0.61f, -0.52f, 0.61f, 0, +1};
  fl[1] = {100, 58, 0.61f, -0.52f, 0.61f, 0, -1};
  for (auto &b : bumpFlash) b = 0;
  balls = 3; score = 0; isBest = false; nudgeCd = 0; stateT = 0;
  serve();
}

void collideSeg(float ax, float ay, float qx, float qy, float rad, float rest, float ddx, float ddy, float omega) {
  float sx = qx - ax, sy = qy - ay, l2 = sx * sx + sy * sy;
  float t = l2 > 0 ? ((bx - ax) * sx + (by - ay) * sy) / l2 : 0;
  t = clampf(t, 0, 1);
  float cxp = ax + sx * t, cyp = ay + sy * t;
  float dx = bx - cxp, dy = by - cyp, d2 = dx * dx + dy * dy, R = BALL_R + rad;
  if (d2 >= R * R) return;
  float d = sqrtf(d2), nx, ny;
  if (d < 1e-4f) { nx = 0; ny = -1; } else { nx = dx / d; ny = dy / d; }
  bx = cxp + nx * R; by = cyp + ny * R;                               // push out
  float rho = t * sqrtf(l2);
  float fvx = ddx * omega * rho, fvy = ddy * omega * rho;              // speed of that spot on a moving flipper
  float vn = (bvx - fvx) * nx + (bvy - fvy) * ny;
  if (vn < 0) {
    float e = (-vn > 25.0f) ? rest : 0.0f;
    bvx -= (1 + e) * vn * nx; bvy -= (1 + e) * vn * ny;
  }
}

void sub(float h) {
  for (auto &f : fl) {
    bool held = (&f == &fl[0]) ? in.aHeld : in.cHeld;
    float target = (held && !tilted) ? f.act : f.rest;
    float sp = (target == f.act) ? 16.0f : 9.0f;
    if (f.ang > target) { f.ang = fmaxf(target, f.ang - sp * h); f.vel = -sp; }
    else if (f.ang < target) { f.ang = fminf(target, f.ang + sp * h); f.vel = sp; }
    else f.vel = 0;
    if (fabsf(f.ang - target) < 1e-4f) f.vel = 0;
  }
  bvx += GRAV * h;
  float v = sqrtf(bvx * bvx + bvy * bvy);
  if (v > VMAX) { bvx *= VMAX / v; bvy *= VMAX / v; }
  bx += bvx * h; by += bvy * h;
  if (by < BALL_R) { by = BALL_R; bvy = fabsf(bvy) * 0.75f; }
  if (by > 63 - BALL_R) { by = 63 - BALL_R; bvy = -fabsf(bvy) * 0.75f; }
  if (bx < BALL_R) { bx = BALL_R; bvx = fabsf(bvx) * 0.75f; }
  for (int i = 0; i < 3; ++i) {                                         // bumpers
    float dx = bx - BX[i], dy = by - BY[i], d2 = dx * dx + dy * dy, R = BALL_R + BR;
    if (d2 >= R * R) continue;
    float d = sqrtf(d2);
    if (d < 1e-4f) { dx = -1; dy = 0; d = 1; }
    float nx = dx / d, ny = dy / d;
    bx = BX[i] + nx * R; by = BY[i] + ny * R;
    float vn = bvx * nx + bvy * ny;
    if (vn < 0) { bvx -= 2 * vn * nx; bvy -= 2 * vn * ny; }
    bvx += nx * 70; bvy += ny * 70;
    bumpFlash[i] = 0.15f;
    combo = comboT > 0 ? imin(5, combo + 1) : 1; comboT = 2.0f;
    score += 50 * combo;
  }
  collideSeg(70, 0, 98, 6, 1.0f, 0.5f, 0, 0, 0);                         // guides that funnel the ball to the flippers
  collideSeg(70, 63, 98, 58, 1.0f, 0.5f, 0, 0, 0);
  for (auto &f : fl) {
    float tx, ty;
    tip(f, tx, ty);
    collideSeg(f.px, f.py, tx, ty, 2.0f, 0.4f, cosf(f.ang), -f.dir * sinf(f.ang), f.vel);
  }
}

void step() {
  for (auto &b : bumpFlash) if (b > 0) b -= DT;
  if (comboT > 0) { comboT -= DT; if (comboT <= 0) combo = 1; }
  if (nudgeCd > 0) nudgeCd -= DT;
  if (tiltMeter > 0) tiltMeter = fmaxf(0, tiltMeter - 0.5f * DT);
  if (state == OVER) return;
  if (state == DRAINED) {
    stateT -= DT;
    if (stateT <= 0) {
      if (balls <= 0) { state = OVER; isBest = noteScore(score); }
      else serve();
    }
    return;
  }
  if (state == SERVE) return;

  if (in.tiltOk && nudgeCd <= 0 && !tilted && sqrtf(in.shx * in.shx + in.shy * in.shy) > 0.55f) {   // shake = nudge
    bvx += clampf(in.shx, -1.5f, 1.5f) * 35.0f; bvy += clampf(in.shy, -1.5f, 1.5f) * 35.0f;
    nudgeCd = 0.3f; tiltMeter += 1.0f;
    if (tiltMeter >= 3.0f) tilted = true;
  }
  for (int i = 0; i < 4; ++i) sub(DT / 4.0f);
  float sp = sqrtf(bvx * bvx + bvy * bvy);                               // never let the ball sit still
  stall = sp < 8.0f ? stall + DT : 0;
  if (stall > 2.5f) { bvx -= 140; bvy += (float)random(-60, 61); stall = 0; }
  if (bx > 126) { --balls; state = DRAINED; stateT = 0.9f; }
}

void draw(U8G2 &d, uint32_t now) {
  hl(d, 0, 0, 128); hl(d, 0, 63, 128); vl(d, 0, 0, 64);
  line(d, 70, 0, 98, 6); line(d, 70, 63, 98, 58);
  for (int i = 0; i < 3; ++i) {
    circle(d, (int)BX[i], (int)BY[i], (int)BR);
    if (bumpFlash[i] > 0) disc(d, (int)BX[i], (int)BY[i], (int)BR - 1); else disc(d, (int)BX[i], (int)BY[i], 1);
  }
  for (auto &f : fl) {
    float tx, ty;
    tip(f, tx, ty);
    line(d, (int)f.px, (int)f.py, (int)tx, (int)ty);
    line(d, (int)f.px, (int)f.py + 1, (int)tx, (int)ty + 1);
    line(d, (int)f.px + 1, (int)f.py, (int)tx + 1, (int)ty);
    disc(d, (int)f.px, (int)f.py, 2);
  }
  if (state != DRAINED && state != OVER) disc(d, (int)lroundf(bx), (int)lroundf(by), 2);

  d.setFont(u8g2_font_5x7_tf);
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  d.drawStr(3, 12, buf);
  if (combo > 1) { snprintf(buf, sizeof(buf), "x%d", combo); d.drawStr(3, 21, buf); }
  for (int i = 0; i < balls; ++i) disc(d, 6 + i * 7, 57, 2);
  if (tiltMeter > 0.3f && !tilted) d.drawStr(3, 32, "!");
  if (tilted && ((now / 200) % 2)) banner(d, "TILT!", 34);
  if (state == SERVE) banner(d, "A / C launch", 36);
  if (state == OVER) gameOver(d, score, isBest);
}

void launch() {
  state = PLAY; bvx = 170 + (float)random(-20, 21); bvy = (float)random(-70, 71);
}
}  // namespace pin

// =====================================================================================================
//  BREAKOUT: tilt slides the paddle (tilt 0.5 = edge to edge), A or C launches the ball. Top rows take two hits.
// =====================================================================================================
namespace brk {
const int COLS = 8, ROWS = 5, BY0 = 12, BW = 16, BH = 6, PY = 59, PW = 22;
uint8_t hp[ROWS][COLS];
float padX, bx, by, bvx, bvy, speed, msgT;
int lives, level, left;
uint32_t score;
enum State { SERVE, PLAY, OVER } state;
bool isBest;
char msg[12];

void wall() {
  left = 0;
  int hard = imin(4, level);
  for (int r = 0; r < ROWS; ++r) for (int c = 0; c < COLS; ++c) {
    bool gap = level >= 3 && ((c + r * 3) % 5 == 0);
    hp[r][c] = gap ? 0 : (uint8_t)(r < hard ? 2 : 1);
    if (hp[r][c]) ++left;
  }
}

void serve() {
  state = SERVE;
  bx = padX; by = PY - 2.5f; bvx = bvy = 0;
  speed = clampf(62.0f + 8.0f * level, 62, 120);
}

void reset() {
  level = 1; score = 0; lives = 3; isBest = false; msgT = 0; padX = 64; msg[0] = 0;
  wall(); serve();
}

void launch() {
  if (state != SERVE) return;
  float a = frand(-0.45f, 0.45f);
  bvx = speed * sinf(a); bvy = -speed * cosf(a);
  state = PLAY;
}

bool hitBrick(float x, float y) {
  int c = (int)floorf(x / BW), r = (int)floorf((y - BY0) / (float)BH);
  if (c < 0 || c >= COLS || r < 0 || r >= ROWS || hp[r][c] == 0) return false;
  --hp[r][c];
  if (hp[r][c] == 0) { --left; score += 2; } else score += 1;
  speed = fminf(125.0f, speed + 0.7f);
  return true;
}

void step() {
  if (msgT > 0) msgT -= DT;
  float target = 64.0f + clampf(in.tx, -0.5f, 0.5f) * 2.0f * (64.0f - PW / 2 - 1);
  padX += (target - padX) * 0.4f;
  if (state == SERVE) { bx = padX; by = PY - 2.5f; return; }
  if (state != PLAY) return;
  for (int sub = 0; sub < 2; ++sub) {
    float h = DT * 0.5f;
    float nx = bx + bvx * h;
    if (hitBrick(nx + (bvx > 0 ? 1.5f : -1.5f), by)) bvx = -bvx; else bx = nx;
    float ny = by + bvy * h;
    if (hitBrick(bx, ny + (bvy > 0 ? 1.5f : -1.5f))) bvy = -bvy; else by = ny;
    if (bx < 1.5f) { bx = 1.5f; bvx = fabsf(bvx); }
    if (bx > 126.5f) { bx = 126.5f; bvx = -fabsf(bvx); }
    if (by < 10.5f) { by = 10.5f; bvy = fabsf(bvy); }
    if (bvy > 0 && by + 1.5f >= PY && by < PY + 3 && fabsf(bx - padX) <= PW / 2 + 2) {
      float ofs = clampf((bx - padX) / (PW / 2 + 1), -1, 1);
      bvx = speed * sinf(ofs * 1.0f); bvy = -speed * cosf(ofs * 1.0f);
      by = PY - 1.5f;
    }
  }
  if (fabsf(bvy) < 22.0f) bvy = bvy < 0 ? -22.0f : 22.0f;
  if (left <= 0) {
    ++level; wall(); serve(); msgT = 1.4f;
    snprintf(msg, sizeof(msg), "LEVEL %d", level);
    return;
  }
  if (by > 66) {
    if (--lives <= 0) { state = OVER; isBest = noteScore(score); } else serve();
  }
}

void draw(U8G2 &d, uint32_t now) {
  d.setFont(u8g2_font_5x7_tf);
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  d.drawStr(0, 7, buf);
  snprintf(buf, sizeof(buf), "L%d", level);
  d.drawStr(64 - d.getStrWidth(buf) / 2, 7, buf);
  for (int i = 0; i < lives; ++i) box(d, 122 - i * 7, 2, 5, 4);
  hl(d, 0, 9, 128);
  for (int r = 0; r < ROWS; ++r) for (int c = 0; c < COLS; ++c) {
    if (!hp[r][c]) continue;
    int x = c * BW + 1, y = BY0 + r * BH;
    if (hp[r][c] >= 2) box(d, x, y, BW - 2, BH - 1);
    else frame(d, x, y, BW - 2, BH - 1);
  }
  box(d, (int)lroundf(padX - PW / 2), PY, PW, 3);
  box(d, (int)lroundf(bx) - 1, (int)lroundf(by) - 1, 3, 3);
  if (state == SERVE && ((now / 400) % 2)) small(d, "A = launch", 64, 52);
  if (msgT > 0) banner(d, msg, 40);
  if (state == OVER) gameOver(d, score, isBest);
}
}  // namespace brk

// =====================================================================================================
//  ASTEROIDS: tilt left / right turns the ship, hold A = thrust, hold C = fire. The screen wraps around.
// =====================================================================================================
namespace ast {
struct Rock { float x, y, vx, vy; uint8_t size, seed; bool on; };      // size 3 big, 2 medium, 1 small
struct Shot { float x, y, vx, vy, life; bool on; };
const float RAD[4] = {0, 3.0f, 5.5f, 9.0f};
Rock rk[24];
Shot sh[4];
float sx, sy, svx, svy, ang, invuln, fireCd, waveCd, tsec;
int lives, wave;
uint32_t score;
bool over, isBest, thrusting;

const float TAU = 6.2831853f;

void addRock(int size, float x, float y, float vx, float vy) {
  for (auto &r : rk) if (!r.on) { r.on = true; r.size = (uint8_t)size; r.x = x; r.y = y; r.vx = vx; r.vy = vy; r.seed = (uint8_t)random(0, 256); return; }
}

void spawnWave() {
  ++wave;
  int n = imin(2 + wave, 5);
  float sp = fminf(28.0f, 8.0f + 3.0f * wave);
  for (int i = 0; i < n; ++i) {
    float x, y;
    if (random(0, 2)) { x = random(0, 2) ? 2 : 125; y = (float)random(0, 64); } else { y = random(0, 2) ? 2 : 61; x = (float)random(0, 128); }
    float a = frand(0, TAU);
    addRock(3, x, y, cosf(a) * sp * frand(0.6f, 1.2f), sinf(a) * sp * frand(0.6f, 1.2f));
  }
  waveCd = 1.2f;
}

void reset() {
  memset(rk, 0, sizeof(rk)); memset(sh, 0, sizeof(sh));
  sx = 64; sy = 32; svx = svy = 0; ang = 0.75f; invuln = 1.5f; fireCd = 0; waveCd = 0.6f; tsec = 0;
  lives = 3; wave = 0; score = 0; over = false; isBest = false; thrusting = false;
}

void splitRock(Rock &r) {
  score += (uint32_t)(4 - r.size) * 10;
  r.on = false;
  if (r.size > 1) {
    float sp = sqrtf(r.vx * r.vx + r.vy * r.vy) * 1.25f + 6.0f;
    float a = frand(0, TAU);
    addRock(r.size - 1, r.x, r.y, cosf(a) * sp, sinf(a) * sp);
    addRock(r.size - 1, r.x, r.y, -cosf(a) * sp, -sinf(a) * sp);
  }
}

void step() {
  tsec += DT;
  if (invuln > 0) invuln -= DT;
  if (fireCd > 0) fireCd -= DT;
  if (over) return;
  ang += clampf(dz(in.tx, 0.06f) * 2.2f, -1.0f, 1.0f) * 0.7f * DT;
  ang -= floorf(ang);
  thrusting = in.aHeld;
  float a = ang * TAU;
  if (thrusting) { svx += cosf(a) * 95.0f * DT; svy += sinf(a) * 95.0f * DT; }
  svx *= 0.992f; svy *= 0.992f;
  float v = sqrtf(svx * svx + svy * svy);
  if (v > 70.0f) { svx *= 70.0f / v; svy *= 70.0f / v; }
  sx += svx * DT; sy += svy * DT;
  if (sx < 0) sx += 128;
  if (sx >= 128) sx -= 128;
  if (sy < 0) sy += 64;
  if (sy >= 64) sy -= 64;

  if (in.cHeld && fireCd <= 0) {
    for (auto &s : sh) if (!s.on) {
      s.on = true; s.life = 0.9f;
      s.x = sx + cosf(a) * 5; s.y = sy + sinf(a) * 5;
      s.vx = cosf(a) * 95.0f + svx * 0.5f; s.vy = sinf(a) * 95.0f + svy * 0.5f;
      fireCd = 0.22f;
      break;
    }
  }
  for (auto &s : sh) if (s.on) {
    s.x += s.vx * DT; s.y += s.vy * DT; s.life -= DT;
    if (s.x < 0) s.x += 128;
    if (s.x >= 128) s.x -= 128;
    if (s.y < 0) s.y += 64;
    if (s.y >= 64) s.y -= 64;
    if (s.life <= 0) s.on = false;
  }
  int alive = 0;
  for (auto &r : rk) if (r.on) {
    ++alive;
    r.x += r.vx * DT; r.y += r.vy * DT;
    if (r.x < -8) r.x += 144;
    if (r.x > 136) r.x -= 144;
    if (r.y < -8) r.y += 80;
    if (r.y > 72) r.y -= 80;
  }
  for (auto &s : sh) if (s.on) for (auto &r : rk) if (r.on) {
    float dx = s.x - r.x, dy = s.y - r.y, rr = RAD[r.size] + 1.0f;
    if (dx * dx + dy * dy < rr * rr) { s.on = false; splitRock(r); break; }
  }
  if (invuln <= 0) for (auto &r : rk) if (r.on) {
    float dx = sx - r.x, dy = sy - r.y, rr = RAD[r.size] + 2.2f;
    if (dx * dx + dy * dy < rr * rr) {
      --lives; invuln = 2.2f; sx = 64; sy = 32; svx = svy = 0;
      if (lives <= 0) { over = true; isBest = noteScore(score); }
      break;
    }
  }
  if (alive == 0) { waveCd -= DT; if (waveCd <= 0) spawnWave(); }
}

void drawRock(U8G2 &d, const Rock &r) {
  int px0 = 0, py0 = 0;
  for (int k = 0; k <= 9; ++k) {
    int kk = k % 9;
    float h = (float)(((r.seed * 37 + kk * 71) >> 2) & 15) / 15.0f;
    float rad = RAD[r.size] * (0.55f + 0.45f * h);
    float a = (float)kk / 9.0f * TAU;
    int x = (int)lroundf(r.x + cosf(a) * rad), y = (int)lroundf(r.y + sinf(a) * rad);
    if (k > 0) line(d, px0, py0, x, y);
    px0 = x; py0 = y;
  }
}

void draw(U8G2 &d, uint32_t now) {
  for (auto &r : rk) if (r.on) drawRock(d, r);
  for (auto &s : sh) if (s.on) box(d, (int)lroundf(s.x) - 1, (int)lroundf(s.y) - 1, 2, 2);
  if (!over && (invuln <= 0 || (now / 90) % 2)) {
    float a = ang * TAU;
    int nx = (int)lroundf(sx + cosf(a) * 5), ny = (int)lroundf(sy + sinf(a) * 5);
    int lx = (int)lroundf(sx + cosf(a + 2.5f) * 4), ly = (int)lroundf(sy + sinf(a + 2.5f) * 4);
    int rx = (int)lroundf(sx + cosf(a - 2.5f) * 4), ry = (int)lroundf(sy + sinf(a - 2.5f) * 4);
    line(d, nx, ny, lx, ly); line(d, nx, ny, rx, ry); line(d, lx, ly, rx, ry);
    if (thrusting && ((now / 50) % 2)) {
      int fx = (int)lroundf(sx - cosf(a) * 8), fy = (int)lroundf(sy - sinf(a) * 8);
      line(d, (lx + rx) / 2, (ly + ry) / 2, fx, fy);
    }
  }
  d.setFont(u8g2_font_5x7_tf);
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  d.drawStr(0, 7, buf);
  snprintf(buf, sizeof(buf), "W%d", wave);
  d.drawStr(64 - d.getStrWidth(buf) / 2, 7, buf);
  for (int i = 0; i < lives; ++i) box(d, 122 - i * 7, 2, 5, 4);
  if (over) gameOver(d, score, isBest);
}
}  // namespace ast

// =====================================================================================================
//  STACK: a block slides back and forth, A (or C) drops it. Only the part that lands on the tower stays.
// =====================================================================================================
namespace stk {
const int SHOWN = 7, ROWH = 6, TOPY = 22;
const float MAXW = 46.0f;
float lx[8], lw[8];
int cnt, combo;
float mx, mw, dir, spd, fallX, fallW, fallY, fallV, popT;
bool over, isBest, fall;
uint32_t score;

void reset() {
  memset(lx, 0, sizeof(lx)); memset(lw, 0, sizeof(lw));
  cnt = 1; lx[0] = 42; lw[0] = 44;
  mw = 44; mx = 0; dir = 1; spd = 48; combo = 0; score = 0; over = false; isBest = false; fall = false; popT = 0;
  fallX = fallW = fallY = fallV = 0;
}

void drop() {
  if (over) return;
  int top = (cnt - 1) & 7;
  float tx = lx[top], tw = lw[top];
  float l = fmaxf(mx, tx), r = fminf(mx + mw, tx + tw), ov = r - l;
  if (ov <= 0.5f) {                                                      // missed the tower completely
    fallX = mx; fallW = mw; fallY = 16; fallV = 0; fall = true;
    over = true; isBest = noteScore(score);
    return;
  }
  if (fabsf(mx - tx) <= 2.0f) {                                          // perfect: snap, and after 3 in a row the block grows
    l = tx; ov = tw; ++combo; score += 2; popT = 0.9f;
    if (combo >= 3 && ov < MAXW) { ov = fminf(MAXW, ov + 3.0f); l = clampf(l - 1.5f, 0, 128 - ov); }
  } else {
    combo = 0; score += 1;
    fall = true; fallY = 16; fallV = 0;
    if (mx < tx) { fallX = mx; fallW = l - mx; } else { fallX = r; fallW = mx + mw - r; }
  }
  lx[cnt & 7] = l; lw[cnt & 7] = ov; ++cnt;
  mw = ov;
  bool left = (cnt & 1) != 0;
  mx = left ? 0 : 128 - mw; dir = left ? 1 : -1;
  spd = fminf(140.0f, 48.0f + 2.6f * cnt);
}

void step() {
  if (popT > 0) popT -= DT;
  if (fall) { fallV += 260.0f * DT; fallY += fallV * DT; if (fallY > 70) fall = false; }
  if (over) return;
  mx += dir * spd * DT;
  if (mx < 0) { mx = 0; dir = 1; }
  if (mx + mw > 128) { mx = 128 - mw; dir = -1; }
}

void draw(U8G2 &d, uint32_t now) {
  (void)now;
  for (int k = 0; k < SHOWN && k < cnt; ++k) {
    int idx = cnt - 1 - k;
    int x = (int)lroundf(lx[idx & 7]), w = (int)lroundf(lw[idx & 7]), y = TOPY + k * ROWH;
    if (idx & 1) frame(d, x, y, w, ROWH - 1); else box(d, x, y, w, ROWH - 1);
  }
  if (cnt <= SHOWN) hl(d, 0, TOPY + cnt * ROWH, 128);
  if (!over) box(d, (int)lroundf(mx), 16, (int)lroundf(mw), ROWH - 1);
  if (fall && fallW > 0.5f) box(d, (int)lroundf(fallX), (int)lroundf(fallY), (int)lroundf(fallW), ROWH - 1);
  d.setFont(u8g2_font_5x7_tf);
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  d.drawStr(0, 7, buf);
  if (popT > 0) d.drawStr(128 - d.getStrWidth("PERFECT!"), 7, "PERFECT!");
  if (cnt == 1) small(d, "A = drop", 64, 60);
  if (over) gameOver(d, score, isBest);
}
}  // namespace stk

// =====================================================================================================
//  QUICK DRAW: wait for DRAW!, then press A (or C) before the outlaw does. Too early = you lose. He gets faster.
// =====================================================================================================
namespace duel {
enum State { WAIT, DRAW, WON, LOST, FOUL } st;
float t, waitT, enemyT, myMs, bestMs;
int wins;
bool over, isBest;

void next() {
  st = WAIT; t = 0;
  waitT = frand(1.6f, 4.5f);
  enemyT = clampf(0.52f - 0.03f * wins, 0.24f, 0.52f) + frand(-0.03f, 0.04f);
}

void reset() { wins = 0; over = false; isBest = false; myMs = 0; bestMs = 0; next(); }

void lose(State s) { st = s; t = 0; over = true; isBest = noteScore((uint32_t)wins); }

void press() {
  if (over) return;
  if (st == WAIT) lose(FOUL);
  else if (st == DRAW) {
    myMs = t * 1000.0f;
    if (bestMs <= 0 || myMs < bestMs) bestMs = myMs;
    ++wins; st = WON; t = 0;
  }
}

void step() {
  t += DT;
  if (st == WAIT && t >= waitT) { st = DRAW; t = 0; }
  else if (st == DRAW && t > enemyT) lose(LOST);
  else if (st == WON && t > 1.4f) next();
}

void person(U8G2 &d, int x, int dirn, bool armOut, bool down, bool flash) {   // dirn +1 faces right
  if (down) {                                                            // lying on the ground
    line(d, x - 9, 51, x + 5, 51); disc(d, x + 8, 50, 3);
    line(d, x - 9, 51, x - 13, 48);
    return;
  }
  disc(d, x, 31, 3);
  hl(d, x - 5, 28, 11); box(d, x - 3, 25, 7, 3);                         // hat
  line(d, x, 34, x, 43);
  line(d, x, 43, x - 3, 52); line(d, x, 43, x + 3, 52);
  if (armOut) {
    hl(d, dirn > 0 ? x : x - 11, 37, 12);
    box(d, dirn > 0 ? x + 9 : x - 12, 35, 4, 3);                         // the gun
    if (flash) { int fx = x + dirn * 16; line(d, fx, 37, fx + dirn * 5, 34); line(d, fx, 37, fx + dirn * 6, 37); line(d, fx, 37, fx + dirn * 5, 40); }
  } else {
    line(d, x, 36, x + dirn * 4, 42);
  }
}

void draw(U8G2 &d, uint32_t now) {
  hl(d, 0, 53, 128);
  bool youDown = st == LOST || st == FOUL;
  person(d, 24, 1, st == WON, youDown, st == WON && t < 0.3f);
  person(d, 104, -1, youDown, st == WON, youDown && t < 0.3f);
  d.setFont(u8g2_font_5x7_tf);
  char buf[28];
  snprintf(buf, sizeof(buf), "wins %d", wins);
  d.drawStr(0, 7, buf);
  if (bestMs > 0) { snprintf(buf, sizeof(buf), "best %dms", (int)bestMs); d.drawStr(128 - d.getStrWidth(buf), 7, buf); }
  if (st == WAIT) {
    int dots = (int)((now / 350) % 4);
    snprintf(buf, sizeof(buf), "steady%.*s", dots, "...");
    small(d, buf, 64, 20);
  } else if (st == DRAW) {
    d.setFont(u8g2_font_logisoso16_tf);
    int w = d.getStrWidth("DRAW!");
    if ((now / 70) % 2) { d.setDrawColor(1); d.drawBox((128 - w) / 2 - 4, 10, w + 8, 22); d.setDrawColor(0); d.drawStr((128 - w) / 2, 28, "DRAW!"); d.setDrawColor(1); }
    else d.drawStr((128 - w) / 2, 28, "DRAW!");
  } else if (st == WON) {
    snprintf(buf, sizeof(buf), "BANG! %d ms", (int)myMs);
    banner(d, buf, 20);
  }
  if (st == FOUL) banner(d, "TOO EARLY!", 9);
  if (over && t > 0.5f) gameOver(d, (uint32_t)wins, isBest);
}
}  // namespace duel

// =====================================================================================================
//  FLAPPY: tap A or C to flap through the gaps. A little pet is the bird. No tilt needed.
// =====================================================================================================
namespace flp {
const float GRAV = 480.0f, FLAP = -118.0f, BX = 32.0f, PW = 14.0f, SPACING = 70.0f, FLOOR = 59.0f;
struct Pipe { float x, gy; bool passed; } pp[3];
float by, bvy, speed, gap, dist;
int score;
bool started, over, isBest;
float deadT;

void spawn(Pipe &p, float x) { p.x = x; p.gy = frand(20.0f, 41.0f); p.passed = false; }

void reset() {
  by = 30; bvy = 0; score = 0; started = false; over = false; isBest = false; speed = 46; gap = 31; deadT = 0; dist = 0;
  for (int i = 0; i < 3; ++i) spawn(pp[i], 150.0f + i * SPACING);
}

void flap() {
  if (over) return;
  started = true;
  bvy = FLAP;
}

void die() { over = true; deadT = 0; isBest = noteScore((uint32_t)score); }

void step() {
  if (over) { deadT += DT; if (by < FLOOR - 3) { bvy += GRAV * DT; by += bvy * DT; if (by > FLOOR - 3) by = FLOOR - 3; } return; }
  dist += speed * DT;
  if (!started) { by = 30.0f + 3.0f * sinf(dist * 0.12f); return; }          // hovers until the first flap
  bvy += GRAV * DT;
  if (bvy > 240.0f) bvy = 240.0f;
  by += bvy * DT;
  speed = 46.0f + fminf(32.0f, score * 1.6f);
  gap = fmaxf(24.0f, 31.0f - score * 0.25f);
  for (auto &p : pp) {
    p.x -= speed * DT;
    if (p.x + PW < BX - 5 && !p.passed) { p.passed = true; ++score; }
    if (p.x + PW < -2) {                                                      // recycle behind the farthest pipe
      float far = 0; for (auto &q : pp) far = fmaxf(far, q.x);
      spawn(p, far + SPACING);
    }
    if (BX + 5 > p.x && BX - 5 < p.x + PW && (by - 4 < p.gy - gap / 2 || by + 4 > p.gy + gap / 2)) die();
  }
  if (by + 4 >= FLOOR) { by = FLOOR - 4; die(); }
  if (by - 4 < 0) { by = 4; bvy = 0; }
}

void draw(U8G2 &d, uint32_t now) {
  for (auto &p : pp) {
    int x = (int)lroundf(p.x), top = (int)lroundf(p.gy - gap / 2), bot = (int)lroundf(p.gy + gap / 2);
    if (x > 128 || x + PW < 0) continue;
    box(d, x, 0, (int)PW, top - 3); box(d, x - 2, top - 4, (int)PW + 4, 4);              // top pipe with a cap
    box(d, x, bot + 3, (int)PW, 60 - bot - 3); box(d, x - 2, bot, (int)PW + 4, 4);       // bottom pipe
    d.setDrawColor(0); vl(d, x + 3, 1, imax(0, top - 5)); vl(d, x + 3, bot + 5, imax(0, 54 - bot)); d.setDrawColor(1);   // a highlight line
  }
  hl(d, 0, 60, 128);
  for (int x = -((int)dist % 8); x < 128; x += 8) px(d, x, 62);                         // scrolling ground
  int by0 = (int)lroundf(by);                                                           // the bird: a rounded little pet
  rbox(d, (int)BX - 6, by0 - 5, 12, 10, 3);
  d.setDrawColor(0);
  box(d, (int)BX + 1, by0 - 3, 3, 3);                                                   // eye
  if (!over) hl(d, (int)BX - 4, by0 + 2 + ((now / 120) & 1), 4);                        // flapping wing
  d.setDrawColor(1);
  px(d, (int)BX + 2, by0 - 2);
  d.setFont(u8g2_font_7x13B_tf);
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", score);
  d.drawStr((128 - d.getStrWidth(buf)) / 2, 13, buf);
  if (!started && !over) small(d, "A / C = flap", 64, 54);
  if (over && deadT > 0.4f) gameOver(d, (uint32_t)score, isBest);
}
}  // namespace flp

// =====================================================================================================
//  MEMORY: Simon with tilt. Watch the pads light up, then repeat the pattern by tilting toward them.
// =====================================================================================================
namespace mem {
enum State { WATCH, TURN, GOOD, BAD } st;
uint8_t seq[60];
int len, shown, idx, score;
float t, litT;
int lit;                      // pad lit right now (-1 none): 0 up, 1 right, 2 down, 3 left
bool armed, over, isBest;

void addRound() {
  if (len < 60) seq[len++] = (uint8_t)random(0, 4);
  st = WATCH; shown = 0; t = -0.7f; lit = -1;
}

void reset() { len = 0; score = 0; over = false; isBest = false; litT = 0; armed = false; addRound(); }

int padFromTilt() {
  float ax = fabsf(in.tx), ay = fabsf(in.ty);
  if (ax < 0.42f && ay < 0.42f) return -1;
  if (ay >= ax) return in.ty < 0 ? 0 : 2;
  return in.tx > 0 ? 1 : 3;
}

void step() {
  if (litT > 0) { litT -= DT; if (litT <= 0 && st != WATCH) lit = -1; }
  if (st == WATCH) {
    float on = fmaxf(0.24f, 0.5f - 0.015f * len), off = 0.18f;
    t += DT;
    if (t < 0) { lit = -1; return; }
    float c = t - shown * (on + off);
    if (shown >= len) { st = TURN; idx = 0; armed = false; lit = -1; return; }
    lit = (c < on) ? seq[shown] : -1;
    if (c >= on + off) ++shown;
  } else if (st == TURN) {
    if (fabsf(in.tx) < 0.22f && fabsf(in.ty) < 0.22f) armed = true;
    int pad = padFromTilt();
    if (armed && pad >= 0) {
      armed = false; lit = pad; litT = 0.22f;
      if (pad == seq[idx]) {
        if (++idx >= len) { score = len; st = GOOD; t = 0; }
      } else { st = BAD; over = true; t = 0; isBest = noteScore((uint32_t)score); }
    }
  } else if (st == GOOD) {
    t += DT;
    if (t > 0.9f) addRound();
  } else if (st == BAD) {
    t += DT;
  }
}

void pad(U8G2 &d, int k, bool on) {
  static const int cx[4] = {64, 91, 64, 37}, cy[4] = {24, 40, 56, 40};
  int x = cx[k], y = cy[k];
  if (k == 0 || k == 2) { if (on) box(d, x - 11, y - 6, 22, 12); else frame(d, x - 11, y - 6, 22, 12); }
  else                  { if (on) box(d, x - 7, y - 9, 14, 18); else frame(d, x - 7, y - 9, 14, 18); }
  d.setDrawColor(on ? 0 : 1);                                                           // arrow on the pad
  int dx = (k == 1) - (k == 3), dy = (k == 2) - (k == 0);
  line(d, x - dx * 3 - dy * 0, y - dy * 3, x + dx * 3, y + dy * 3);
  if (dx) { line(d, x + dx * 3, y, x + dx, y - 2); line(d, x + dx * 3, y, x + dx, y + 2); }
  else    { line(d, x, y + dy * 3, x - 2, y + dy); line(d, x, y + dy * 3, x + 2, y + dy); }
  d.setDrawColor(1);
}

void draw(U8G2 &d, uint32_t now) {
  (void)now;
  for (int k = 0; k < 4; ++k) pad(d, k, lit == k);
  d.setFont(u8g2_font_5x7_tf);
  char buf[24];
  snprintf(buf, sizeof(buf), "round %d", len);
  d.drawStr(0, 7, buf);
  const char *msg = st == WATCH ? "watch" : (st == TURN ? "your turn" : (st == GOOD ? "nice!" : ""));
  d.drawStr(128 - d.getStrWidth(msg), 7, msg);
  if (st == TURN) {                                                                    // where your tilt is pointing
    int dx = (int)clampf(in.tx * 24.0f, -12.0f, 12.0f), dy = (int)clampf(in.ty * 24.0f, -10.0f, 10.0f);
    circle(d, 64, 40, 2);
    disc(d, 64 + dx, 40 + dy, 2);
  }
  if (over && t > 0.5f) gameOver(d, (uint32_t)score, isBest);
}
}  // namespace mem
}  // namespace

// ------------------------------------------------------------------------------------------------ public
const char *name(Id g) {
  static const char *const N[COUNT] = {"Star Blaster", "Sniper", "Road Rush", "Pinball", "Breakout", "Asteroids", "Stack", "Quick Draw", "Flappy", "Memory"};
  return (g >= 0 && g < COUNT) ? N[g] : "";
}

bool takeBestDirty() { bool d = bestDirty; bestDirty = false; return d; }

void begin(Id g) {
  cur = g;
  in = Input{};
  lastStepAt = millis();
  switch (g) {
    case STAR: star::reset(); break;
    case SNIPER: sniper::reset(); break;
    case ROAD: road::reset(); break;
    case BREAKOUT: brk::reset(); break;
    case ASTEROIDS: ast::reset(); break;
    case STACK: stk::reset(); break;
    case DUEL: duel::reset(); break;
    case FLAPPY: flp::reset(); break;
    case MEMORY: mem::reset(); break;
    default: pin::reset(); break;
  }
}

uint8_t update(uint32_t now, const Input &i) {
  in = i;
  uint8_t flags = 0;
  bool needsTilt = usesTilt(cur);
  bool blocked = needsTilt && (!in.tiltOk || in.calibrating);
  bool again = in.a || in.c;
  switch (cur) {
    case STAR:
      if (star::over) { if (again) { star::reset(); flags |= FLAG_REZERO; } }
      else if (!blocked && in.c) star::bomb();
      break;
    case SNIPER:
      if (sniper::over) { if (again) { sniper::reset(); flags |= FLAG_REZERO; } }
      else if (!blocked && in.a) sniper::fire();
      break;
    case ROAD:
      if (road::over && again) { road::reset(); flags |= FLAG_REZERO; }
      break;
    case BREAKOUT:
      if (brk::state == brk::OVER) { if (again) { brk::reset(); flags |= FLAG_REZERO; } }
      else if (!blocked && again) brk::launch();
      break;
    case ASTEROIDS:
      if (ast::over && again) { ast::reset(); flags |= FLAG_REZERO; }
      break;
    case STACK:
      if (stk::over) { if (again) stk::reset(); }
      else if (again) stk::drop();
      break;
    case DUEL:
      if (duel::over) { if (again && duel::t > 0.7f) duel::reset(); }
      else if (again) duel::press();
      break;
    case FLAPPY:
      if (flp::over) { if (again && flp::deadT > 0.5f) flp::reset(); }
      else if (again) flp::flap();
      break;
    case MEMORY:
      if (mem::over && again && mem::t > 0.6f) { mem::reset(); flags |= FLAG_REZERO; }
      break;
    default:
      if (pin::state == pin::OVER) { if (again) pin::reset(); }
      else if (pin::state == pin::SERVE && again) pin::launch();
      break;
  }
  if (blocked) { lastStepAt = now; return flags; }
  int steps = 0;
  while (now - lastStepAt >= 20 && steps < 4) {
    switch (cur) {
      case STAR: star::step(); break;
      case SNIPER: sniper::step(); break;
      case ROAD: road::step(); break;
      case BREAKOUT: brk::step(); break;
      case ASTEROIDS: ast::step(); break;
      case STACK: stk::step(); break;
      case DUEL: duel::step(); break;
      case FLAPPY: flp::step(); break;
      case MEMORY: mem::step(); break;
      default: pin::step(); break;
    }
    lastStepAt += 20;
    ++steps;
  }
  if (now - lastStepAt > 80) lastStepAt = now;                          // fell behind: do not try to catch up
  return flags;
}

void draw(U8G2 &d, uint32_t now) {
  d.setDrawColor(1);
  bool needsTilt = usesTilt(cur);
  if (needsTilt && !in.tiltOk) {
    d.setFont(u8g2_font_7x13B_tf);
    d.drawStr(0, 12, name(cur));
    d.drawHLine(0, 15, 128);
    banner(d, "MPU6050 not found", 38);
    return;
  }
  switch (cur) {
    case STAR: star::draw(d, now); break;
    case SNIPER: sniper::draw(d, now); break;
    case ROAD: road::draw(d, now); break;
    case BREAKOUT: brk::draw(d, now); break;
    case ASTEROIDS: ast::draw(d, now); break;
    case STACK: stk::draw(d, now); break;
    case DUEL: duel::draw(d, now); break;
    case FLAPPY: flp::draw(d, now); break;
    case MEMORY: mem::draw(d, now); break;
    default: pin::draw(d, now); break;
  }
  if (needsTilt && in.calibrating) calibrationBanner(d);
}

}  // namespace games
