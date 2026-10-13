#include "living_oled.h"
#include <math.h>

namespace living_oled {
namespace {
enum Scene : uint8_t { FLOW, ORBIT, FIELD, RAIN, MESH, TERRAIN, SCENE_COUNT };
const char *SCENE_NAMES[] = {"FLOW", "ORBIT", "FIELD", "RAIN", "MESH", "TERRAIN"};
Scene scene = FLOW;
bool paused = false;
uint32_t enteredAt = 0, pausedAt = 0, pausedTotal = 0;
uint32_t lastFrame = 0;

float wave(float x, float t, float phase, float freq, float amp) {
  return 33.0f + sinf(x * freq + t * 0.0014f + phase) * amp
              + cosf(x * freq * 0.43f - t * 0.0009f + phase) * amp * 0.42f;
}
uint32_t animTime(uint32_t now) {
  uint32_t end = paused ? pausedAt : now;
  return end - enteredAt - pausedTotal;
}
void header(U8G2 &d) {
  d.setFont(u8g2_font_5x7_tf);
  d.drawStr(2, 7, "LIVING OLED");
  d.drawHLine(0, 10, 128);
  d.drawStr(2, 63, SCENE_NAMES[scene]);
  d.drawStr(76, 63, paused ? "B: PLAY" : "A/C: NEXT");
}
void drawFlow(U8G2 &d, uint32_t t) {
  for (int layer = 0; layer < 4; ++layer) {
    int px = 0, py = (int)wave(0, t, layer * 1.1f, 0.085f, 7.0f + layer * 2.0f);
    for (int x = 2; x < 128; x += 2) {
      int y = (int)wave((float)x, t, layer * 1.1f, 0.085f, 7.0f + layer * 2.0f);
      d.drawLine(px, py, x, y);
      if (((x + layer * 9 + t / 90) % 29) < 2) d.drawPixel(x, y - 2);
      px = x; py = y;
    }
  }
}
void drawOrbit(U8G2 &d, uint32_t t) {
  const int cx = 64, cy = 36;
  for (int r = 8; r <= 28; r += 5) {
    d.drawCircle(cx, cy, r);
    float a = t * (0.001f + r * 0.000035f) + r;
    int x = cx + (int)(cosf(a) * r), y = cy + (int)(sinf(a) * r * 0.55f);
    d.drawDisc(x, y, (r % 3) ? 1 : 2);
  }
  d.drawDisc(cx + (int)(sinf(t * 0.001f) * 4), cy, 2);
}
void drawField(U8G2 &d, uint32_t t) {
  for (int row = 0; row < 7; ++row) {
    int px = 0, py = 13 + row * 7;
    for (int x = 0; x <= 128; x += 3) {
      float dx = x - (64.0f + sinf(t * 0.0007f) * 22.0f);
      float dy = py - (36.0f + cosf(t * 0.0008f) * 9.0f);
      float bend = 5.0f * sinf(dx * 0.045f + row * 0.5f + t * 0.001f);
      int y = 13 + row * 7 + (int)(bend + 5.0f * sinf((dx * dx + dy * dy) * 0.0015f - t * 0.001f));
      if (x) d.drawLine(px, py, x, y);
      px = x; py = y;
    }
  }
}
void drawRain(U8G2 &d, uint32_t t) {
  for (int col = 0; col < 16; ++col) {
    int x = col * 8 + 2;
    int speed = 2 + ((col * 7) % 5);
    int head = (int)((t / (30 + col * 2) + col * 11) % 54);
    for (int j = 0; j < 5; ++j) {
      int y = (head - j * 7 + 54) % 54 + 10;
      if (y > 11 && y < 61) {
        if (j == 0) d.drawBox(x, y, 3, 4);
        else if (((col * 13 + j * 7 + t / 180) % 4) != 0) d.drawVLine(x + (j % 3), y, 3);
      }
    }
    if ((col + t / (90 * speed)) % 5 == 0) d.drawPixel(x + 2, 13 + ((col * 9 + t / 40) % 44));
  }
}
void drawMesh(U8G2 &d, uint32_t t) {
  const int rows = 5, cols = 9;
  int px[rows][cols], py[rows][cols];
  for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
    float x = 4.0f + c * 15.0f + sinf(t * 0.001f + r * 1.2f + c * 0.6f) * 4.0f;
    float y = 15.0f + r * 10.0f + cosf(t * 0.0012f + c * 0.7f + r) * 5.0f;
    px[r][c] = (int)x; py[r][c] = (int)y;
  }
  for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
    if (c + 1 < cols) d.drawLine(px[r][c], py[r][c], px[r][c+1], py[r][c+1]);
    if (r + 1 < rows) d.drawLine(px[r][c], py[r][c], px[r+1][c], py[r+1][c]);
    if (r + 1 < rows && c + 1 < cols && ((r + c) % 2 == 0))
      d.drawLine(px[r][c], py[r][c], px[r+1][c+1], py[r+1][c+1]);
    if ((r + c + t / 100) % 4 == 0) d.drawPixel(px[r][c], py[r][c]);
  }
}
void drawTerrain(U8G2 &d, uint32_t t) {
  int lastY[128];
  for (int x = 0; x < 128; ++x) {
    float xx = x + t * 0.035f;
    int y = 43 + (int)(sinf(xx * 0.055f) * 7 + sinf(xx * 0.021f + 1.7f) * 9
                       + cosf(xx * 0.11f) * 2);
    lastY[x] = constrain(y, 16, 57);
  }
  for (int x = 1; x < 128; ++x) {
    d.drawLine(x - 1, lastY[x - 1], x, lastY[x]);
    d.drawLine(x - 1, min(61, lastY[x - 1] + 5), x, min(61, lastY[x] + 5));
  }
  for (int i = 0; i < 12; ++i) {
    int x = (i * 37 + (t / 80) % 128) % 128;
    int y = (lastY[x] + 8 + i * 3) % 60;
    d.drawPixel(x, y);
  }
}
} // namespace

void enter() {
  scene = FLOW;
  paused = false;
  enteredAt = millis();
  pausedAt = pausedTotal = 0;
  lastFrame = 0;
}
void leave() { paused = false; }
void input(bool a, bool b, bool c, uint32_t now) {
  if (a || c) {
    scene = (Scene)((scene + 1) % SCENE_COUNT);
  }
  if (b) {
    if (!paused) { paused = true; pausedAt = now; }
    else { pausedTotal += now - pausedAt; paused = false; }
  }
}
void draw(U8G2 &d, uint32_t now) {
  if (now - lastFrame < 25) return;
  lastFrame = now;
  uint32_t t = animTime(now);
  switch (scene) {
    case FLOW: drawFlow(d, t); break;
    case ORBIT: drawOrbit(d, t); break;
    case FIELD: drawField(d, t); break;
    case RAIN: drawRain(d, t); break;
    case MESH: drawMesh(d, t); break;
    case TERRAIN: drawTerrain(d, t); break;
    default: drawFlow(d, t); break;
  }
  header(d);
}
} // namespace living_oled
