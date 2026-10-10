// Fixed-point particle fluid (Clavet-style double density relaxation) + metaball renderer.
// See fluid.h. All positions are Q8 pixels (256 = 1 px). No floats anywhere: the C3 has no FPU.
#include "fluid.h"
#include <string.h>

namespace fluid {

// ---------------- Tunables ----------------
static const int W = 128, H = 64;
static const int MAXN = 160;
static const int N_PARTICLES = 150;          // more = prettier but slower
static const int SUBSTEPS = 2;               // physics steps per display frame
static const int32_t PAD = 2 * 256;          // particle centre keeps this far from the screen edge
static const int32_t RAD = 9 * 256;          // interaction radius (9 px)
static const int32_t RAD2 = RAD * RAD;
static const int32_t GRAV_STEP_Q8 = 40;      // per-substep acceleration for 1 g, Q8 px/step^2
static const int32_t MAX_SPEED = 3 * 256;    // px per substep, keeps shaking from exploding the fluid
static const int32_t MAX_PUSH = 160;         // max pressure displacement per particle per substep (Q8)
static const int MAXPAIRS = 2400;

struct PresetParams {
  const char *name;
  int32_t rho0;      // rest density (Q8)
  int32_t k;         // stiffness (Q8 fraction)
  int32_t kNear;     // near-density stiffness (Q8 fraction), keeps particles from stacking
  int32_t cohesion;  // scale (Q8) applied to the attractive (negative) pressure
  int32_t visc;      // neighbour velocity smoothing (Q8 fraction per substep)
  int32_t damp;      // velocity damping per substep (Q8, 256 = none)
  int32_t gscale;    // how strongly gravity pulls (Q8, 256 = 1x): honey creeps, mercury rushes
};

// Experiment hooks for tools/fluidtest (override with -D). Defaults are the shipped values.
#ifndef FLUID_W_RHO0
#define FLUID_W_RHO0 380
#endif
#ifndef FLUID_W_K
#define FLUID_W_K 70
#endif
#ifndef FLUID_W_KN
#define FLUID_W_KN 90
#endif
#ifndef FLUID_W_COH
#define FLUID_W_COH 60
#endif
#ifndef FLUID_W_VISC
#define FLUID_W_VISC 12
#endif
#ifndef FLUID_W_DAMP
#define FLUID_W_DAMP 254
#endif

static const PresetParams PRESETS[PRESET_COUNT] = {
  //   name       rho0  k    kNear cohesion visc damp  gscale
  {"WATER",      FLUID_W_RHO0, FLUID_W_K, FLUID_W_KN, FLUID_W_COH, FLUID_W_VISC, FLUID_W_DAMP, 256},
  {"HONEY",      380,  70,  90,   120,     90,  244,  90},    // thick and slow: weak pull, heavy damping
  {"MERCURY",    380,  80,  90,   170,     2,   255,  320},   // heavy and fast, beads up, hardly any drag
  {"SPLASH",     380,  70,  90,   40,      4,   255,  256},   // light and bouncy; main.cpp feeds it motion
};

static int curPreset = 0;
static int N = N_PARTICLES;

// ---------------- State ----------------
static int32_t px_[MAXN], py_[MAXN];     // position (Q8)
static int32_t ox_[MAXN], oy_[MAXN];     // position before this substep
static int32_t vx_[MAXN], vy_[MAXN];     // velocity (Q8 px per substep)
static int32_t rho_[MAXN], near_[MAXN];  // densities (Q8)
static int32_t dx_[MAXN], dy_[MAXN];     // accumulated displacement (Q8)
static int32_t sx_[MAXN], sy_[MAXN];     // velocity smoothing accumulators

static int16_t pairI_[MAXPAIRS], pairJ_[MAXPAIRS];
static int16_t pairDx_[MAXPAIRS], pairDy_[MAXPAIRS];
static int16_t pairR_[MAXPAIRS];
static uint16_t pairW_[MAXPAIRS];        // (1 - q) in Q8
static int pairCount = 0;

static const int GCX = (W * 256) / RAD + 2;   // grid columns for neighbour search
static const int GCY = (H * 256) / RAD + 2;
static uint8_t cellOrder[MAXN];
static uint16_t cellStart[(((W * 256) / RAD + 2) * ((H * 256) / RAD + 2)) + 1];

static uint32_t rng = 0x1234567u;
static inline uint32_t rnd() {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return rng;
}

static inline uint32_t isqrt32(uint32_t n) {
  uint32_t r = 0, bit = 1u << 30;
  while (bit > n) bit >>= 2;
  while (bit) {
    if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return r;
}

// Signed right shift that rounds toward zero. A plain ">>" rounds toward minus infinity, which adds a
// tiny constant bias to every pair and made the fluid drift toward the top-left corner.
static inline int32_t rz(int32_t v, int n) { return v >= 0 ? (v >> n) : -((-v) >> n); }

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

const char *presetName(int preset) { return PRESETS[clampi(preset, 0, PRESET_COUNT - 1)].name; }
int particleCount() { return N; }
void setPreset(int preset) { curPreset = clampi(preset, 0, PRESET_COUNT - 1); }

void init(int preset) {
  setPreset(preset);
  N = N_PARTICLES;
  // Hex-ish lattice filling the bottom of the container.
  const int32_t spacing = (46 * 256) / 10;   // 4.6 px
  const int32_t rowH = (spacing * 7) / 8;
  int cols = (int)(((W * 256) - 2 * PAD) / spacing);
  int i = 0;
  for (int row = 0; i < N; ++row) {
    for (int c = 0; c < cols && i < N; ++c, ++i) {
      int32_t x = PAD + c * spacing + ((row & 1) ? spacing / 2 : 0) + (int32_t)(rnd() % 40) - 20;
      int32_t y = H * 256 - PAD - row * rowH + (int32_t)(rnd() % 40) - 20;
      px_[i] = clampi(x, PAD, W * 256 - PAD);
      py_[i] = y;
      ox_[i] = px_[i]; oy_[i] = py_[i];
      vx_[i] = vy_[i] = 0;
    }
  }
}

static void buildNeighbours() {
  const int cells = GCX * GCY;
  for (int c = 0; c <= cells; ++c) cellStart[c] = 0;
  static uint8_t cellOf[MAXN];
  for (int i = 0; i < N; ++i) {
    int cx = clampi(px_[i] / RAD, 0, GCX - 1);
    int cy = clampi(py_[i] / RAD, 0, GCY - 1);
    cellOf[i] = (uint8_t)(cy * GCX + cx);
    cellStart[cellOf[i] + 1]++;
  }
  for (int c = 0; c < cells; ++c) cellStart[c + 1] += cellStart[c];
  static uint16_t fill[(((W * 256) / RAD + 2) * ((H * 256) / RAD + 2))];
  for (int c = 0; c < cells; ++c) fill[c] = cellStart[c];
  for (int i = 0; i < N; ++i) cellOrder[fill[cellOf[i]]++] = (uint8_t)i;

  pairCount = 0;
  for (int i = 0; i < N; ++i) { rho_[i] = 0; near_[i] = 0; }

  for (int i = 0; i < N; ++i) {
    int cx = clampi(px_[i] / RAD, 0, GCX - 1);
    int cy = clampi(py_[i] / RAD, 0, GCY - 1);
    for (int ny = cy - 1; ny <= cy + 1; ++ny) {
      if (ny < 0 || ny >= GCY) continue;
      for (int nx = cx - 1; nx <= cx + 1; ++nx) {
        if (nx < 0 || nx >= GCX) continue;
        int cell = ny * GCX + nx;
        for (int k = cellStart[cell]; k < cellStart[cell + 1]; ++k) {
          int j = cellOrder[k];
          if (j <= i) continue;
          int32_t ddx = px_[j] - px_[i];
          int32_t ddy = py_[j] - py_[i];
          if (ddx >= RAD || ddx <= -RAD || ddy >= RAD || ddy <= -RAD) continue;
          int32_t d2 = ddx * ddx + ddy * ddy;
          if (d2 >= RAD2) continue;
          int32_t r = (int32_t)isqrt32((uint32_t)d2);
          int32_t q = (r * 455) >> 12;                 // r / 9 px in Q8 (0..255)
          int32_t w = 256 - q;                          // (1 - q)
          int32_t w2 = (w * w) >> 8;
          int32_t w3 = (w2 * w) >> 8;
          rho_[i] += w2; rho_[j] += w2;
          near_[i] += w3; near_[j] += w3;
          if (pairCount < MAXPAIRS) {
            pairI_[pairCount] = (int16_t)i; pairJ_[pairCount] = (int16_t)j;
            pairDx_[pairCount] = (int16_t)ddx; pairDy_[pairCount] = (int16_t)ddy;
            pairR_[pairCount] = (int16_t)(r < 1 ? 1 : r);
            pairW_[pairCount] = (uint16_t)w;
            ++pairCount;
          }
        }
      }
    }
  }
}

static void relax(const PresetParams &pp) {
  // Pressure per particle.
  static int32_t pr[MAXN], pn[MAXN];
  for (int i = 0; i < N; ++i) {
    int32_t p = rz(pp.k * (rho_[i] - pp.rho0), 8);
    if (p < 0) p = rz(p * pp.cohesion, 8);     // attraction is weaker than repulsion
    pr[i] = p;
    pn[i] = rz(pp.kNear * near_[i], 8);
    dx_[i] = 0; dy_[i] = 0;
  }
  for (int n = 0; n < pairCount; ++n) {
    int i = pairI_[n], j = pairJ_[n];
    int32_t w = pairW_[n];
    int32_t w2 = (w * w) >> 8;
    int32_t p = rz(pr[i] + pr[j], 1);
    int32_t pnear = rz(pn[i] + pn[j], 1);
    int32_t D = rz(p * w, 8) + rz(pnear * w2, 8);   // displacement along the pair axis (Q8 px)
    D = rz(D, 1);                                     // split between the two particles
    int32_t s = (D * 256) / pairR_[n];
    int32_t ax = rz(s * pairDx_[n], 8);
    int32_t ay = rz(s * pairDy_[n], 8);
    dx_[j] += ax; dy_[j] += ay;
    dx_[i] -= ax; dy_[i] -= ay;
  }
  for (int i = 0; i < N; ++i) {
    px_[i] += clampi(dx_[i], -MAX_PUSH, MAX_PUSH);
    py_[i] += clampi(dy_[i], -MAX_PUSH, MAX_PUSH);
  }
}

static void substep(int32_t gx, int32_t gy) {
  const PresetParams &pp = PRESETS[curPreset];

  for (int i = 0; i < N; ++i) {
    vx_[i] = clampi(rz(vx_[i] * pp.damp, 8) + gx, -MAX_SPEED, MAX_SPEED);
    vy_[i] = clampi(rz(vy_[i] * pp.damp, 8) + gy, -MAX_SPEED, MAX_SPEED);
    ox_[i] = px_[i]; oy_[i] = py_[i];
    px_[i] += vx_[i]; py_[i] += vy_[i];
    px_[i] = clampi(px_[i], PAD, W * 256 - PAD);
    py_[i] = clampi(py_[i], PAD, H * 256 - PAD);
  }

  buildNeighbours();
  relax(pp);

  for (int i = 0; i < N; ++i) {
    px_[i] = clampi(px_[i], PAD, W * 256 - PAD);
    py_[i] = clampi(py_[i], PAD, H * 256 - PAD);
    vx_[i] = px_[i] - ox_[i];
    vy_[i] = py_[i] - oy_[i];
    sx_[i] = 0; sy_[i] = 0;
  }

  // Viscosity: pull each particle's velocity toward its neighbours' average.
  if (pp.visc > 0) {
    for (int n = 0; n < pairCount; ++n) {
      int i = pairI_[n], j = pairJ_[n];
      int32_t w = pairW_[n];
      int32_t ddx = rz((vx_[j] - vx_[i]) * w, 8);
      int32_t ddy = rz((vy_[j] - vy_[i]) * w, 8);
      sx_[i] += ddx; sy_[i] += ddy;
      sx_[j] -= ddx; sy_[j] -= ddy;
    }
    for (int i = 0; i < N; ++i) {
      vx_[i] += rz(sx_[i] * pp.visc, 8);
      vy_[i] += rz(sy_[i] * pp.visc, 8);
    }
  }
}

void step(int32_t gx_q8, int32_t gy_q8) {
  gx_q8 = clampi(gx_q8, -400, 400);
  gy_q8 = clampi(gy_q8, -400, 400);
  const int32_t gs = PRESETS[curPreset].gscale;
  int32_t gx = rz(rz(gx_q8 * GRAV_STEP_Q8, 8) * gs, 8);
  int32_t gy = rz(rz(gy_q8 * GRAV_STEP_Q8, 8) * gs, 8);
  for (int s = 0; s < SUBSTEPS; ++s) substep(gx, gy);
}

// Rotation kick: the container turned, the liquid lags behind and swirls the other way.
// dw_q8 = change of angular speed this frame (+ = clockwise on screen), Q8, about 256 = a strong twist.
void spin(int32_t dw_q8) {
  const int32_t cx = (W * 256) / 2, cy = (H * 256) / 2;
  for (int i = 0; i < N; ++i) {
    int32_t rx = px_[i] - cx, ry = py_[i] - cy;
    vx_[i] = clampi(vx_[i] + rz(-ry * dw_q8, 16), -MAX_SPEED, MAX_SPEED);
    vy_[i] = clampi(vy_[i] + rz(rx * dw_q8, 16), -MAX_SPEED, MAX_SPEED);
  }
}

// ---------------- Rendering ----------------
// Metaball field on a coarse grid (one node every 4 px), bilinearly upsampled and thresholded.
static const int NODE = 4;
static const int GW = W / NODE + 1;   // 33
static const int GH = H / NODE + 1;   // 17
static int16_t field[GH][GW];

static const int32_t KR2 = 8 * 256 * 8 * 256;   // kernel radius 8 px, squared (Q8^2)
static const int32_t T_EDGE = 150;               // surface threshold (Q8)
static const int32_t T_DEEP = 560;               // interior (dithered) threshold (Q8)

static inline void setPixel(uint8_t *buf, int x, int y) { buf[(y >> 3) * W + x] |= (uint8_t)(1u << (y & 7)); }

void render(uint8_t *buf) {
  memset(field, 0, sizeof(field));

  for (int i = 0; i < N; ++i) {
    int32_t x = px_[i], y = py_[i];
    int bx = x >> 10, by = y >> 10;                 // node left/above the particle (1024 = 4 px)
    for (int gy = by - 1; gy <= by + 2; ++gy) {
      if (gy < 0 || gy >= GH) continue;
      int32_t dy = gy * 1024 - y;
      int32_t dy2 = dy * dy;
      if (dy2 >= KR2) continue;
      for (int gx = bx - 1; gx <= bx + 2; ++gx) {
        if (gx < 0 || gx >= GW) continue;
        int32_t dx = gx * 1024 - x;
        int32_t d2 = dx * dx + dy2;
        if (d2 >= KR2) continue;
        int32_t u = (KR2 - d2) >> 14;               // 0..256, (1 - d^2/R^2) in Q8
        field[gy][gx] += (int16_t)((u * u) >> 8);
      }
    }
  }

  static const int8_t wt[NODE] = {1, 3, 5, 7};      // sample at pixel centres, in eighths
  for (int cy = 0; cy < GH - 1; ++cy) {
    for (int cx = 0; cx < GW - 1; ++cx) {
      int32_t v00 = field[cy][cx], v10 = field[cy][cx + 1];
      int32_t v01 = field[cy + 1][cx], v11 = field[cy + 1][cx + 1];
      int32_t vmax = v00 > v10 ? v00 : v10;
      if (v01 > vmax) vmax = v01;
      if (v11 > vmax) vmax = v11;
      if (vmax < T_EDGE) continue;
      for (int fy = 0; fy < NODE; ++fy) {
        int32_t wy = wt[fy];
        int32_t top = v00 * (8 - wy) + v01 * wy;      // blend down the left edge
        int32_t bot = v10 * (8 - wy) + v11 * wy;      // ...and the right edge
        int y = cy * NODE + fy;
        for (int fx = 0; fx < NODE; ++fx) {
          int32_t wx = wt[fx];
          int32_t v = (top * (8 - wx) + bot * wx) >> 6;
          if (v < T_EDGE) continue;
          int x = cx * NODE + fx;
          if (v >= T_DEEP && ((x + y) & 1)) continue;   // dither the deep interior, solid rim
          setPixel(buf, x, y);
        }
      }
    }
  }
}

}  // namespace fluid
