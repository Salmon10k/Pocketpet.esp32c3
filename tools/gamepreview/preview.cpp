// PC preview + self-test for the launcher menus and the new games.
// Includes games.cpp directly so the tests can look at the games' internals (AI players, invariants).
#include <Arduino.h>
#include <U8g2lib.h>
#include <functional>
#include <vector>
#include <string>
#include <cstdio>
#include "../../src/games.cpp"
#include "../../src/launcher.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

static std::string OUTDIR = "/tmp/pocketpet-preview";
static void dump(FILE *f) { fwrite(display.getBufferPtr(), 1, 1024, f); }
static FILE *openOut(const char *name) { std::string p = OUTDIR + "/" + name; FILE *f = fopen(p.c_str(), "wb"); if (!f) { perror(p.c_str()); exit(1); } return f; }

// ------------------------------------------------------------------------------------------ menus
using launcher::Item;
static const Item GAMES[] = {{"Star Blaster", launcher::ICON_STAR}, {"Sniper", launcher::ICON_SNIPER}, {"Road Rush", launcher::ICON_ROAD},
                             {"Pinball", launcher::ICON_PINBALL}, {"Breakout", launcher::ICON_BREAKOUT}, {"Asteroids", launcher::ICON_ASTEROID},
                             {"Stack", launcher::ICON_STACK}, {"Quick Draw", launcher::ICON_DUEL}, {"Flappy", launcher::ICON_FLAPPY}, {"Memory", launcher::ICON_MEMORY}, {"Tilt Maze", launcher::ICON_MAZE}, {"Snake", launcher::ICON_SNAKE}, {"Back", launcher::ICON_BACK}};
static const Item TOOLS[] = {{"Temperature", launcher::ICON_TOOLS}, {"Timer", launcher::ICON_TIMER}, {"Pomodoro", launcher::ICON_POMO}, {"Stopwatch", launcher::ICON_STOPWATCH}, {"Magic 8-ball", launcher::ICON_8BALL}, {"Dice", launcher::ICON_DICE}, {"Back", launcher::ICON_BACK}};
static const Item SETTINGS[] = {{"Brightness", launcher::ICON_BRIGHT}, {"Screen off", launcher::ICON_SCREEN_OFF}, {"Flip screen", launcher::ICON_FLIP},
                                {"Menu style", launcher::ICON_STYLE}, {"Lyrics sync", launcher::ICON_SYNC}, {"Music pet", launcher::ICON_SPOTIFY},
                                {"Step goal", launcher::ICON_STEPS}, {"Reset pet", launcher::ICON_RESET}, {"Back", launcher::ICON_BACK}};
static const Item ONLINE[] = {{"Clock", launcher::ICON_CLOCK}, {"Weather", launcher::ICON_WEATHER}, {"Forecast", launcher::ICON_FORECAST}, {"Air quality", launcher::ICON_AQI},
                              {"Currency", launcher::ICON_CURRENCY}, {"World clocks", launcher::ICON_WORLD}, {"Fact", launcher::ICON_FACT}, {"Advice", launcher::ICON_ADVICE},
                              {"Moon", launcher::ICON_MOON}, {"Net status", launcher::ICON_NET}, {"ISS", launcher::ICON_ISS}, {"GitHub", launcher::ICON_GITHUB},
                              {"Sync now", launcher::ICON_SYNC}, {"Back", launcher::ICON_BACK}};
static const char *settingsVal(int i) {
  static const char *v[] = {"Max", "1 min", "Off", "Carousel", "+0ms", "On", "6000", "", ""};
  return v[i];
}
static const char *onlineVal(int) { return "online"; }

static void menuSheet(const char *file, const Item *items, int n, int style, const char *(*val)(int), std::vector<int> sels) {
  FILE *f = openOut(file);
  for (int s : sels) {
    launcher::enterSub(items, n, s, style, 0, val);
    uint32_t t = 0;
    for (; t <= 1600; t += 40) { display.clearBuffer(); launcher::draw(display, t); }
    dump(f);
  }
  // a scroll: press C and capture while it glides
  launcher::enterSub(items, n, sels[0], style, 0, val);
  uint32_t t = 0;
  for (; t <= 1600; t += 40) { display.clearBuffer(); launcher::draw(display, t); }
  launcher::input(false, false, true, t, 0, 0, false);
  for (int k = 0; k < 3; ++k) { t += 40; display.clearBuffer(); launcher::draw(display, t); dump(f); }
  fclose(f);
}

static void introSheet(const char *file) {                 // the glide-in animation of the home launcher
  FILE *f = openOut(file);
  for (int st = 0; st < 2; ++st) {
    launcher::enter(4, st, 0);
    for (uint32_t t = 0; t <= 480; t += 40) { display.clearBuffer(); launcher::draw(display, t); if (t == 120 || t == 280 || t == 480) dump(f); }
  }
  fclose(f);
}

static void iconSheet(const char *file) {
  FILE *f = openOut(file);
  const int total = launcher::ICON_MEMORY + 1;
  for (int base = 0; base < total; base += 15) {
    display.clearBuffer();
    for (int k = 0; k < 15 && base + k < total; ++k) {
      int cx = 14 + (k % 5) * 25, cy = 11 + (k / 5) * 21;
      launcher::drawIcon(display, base + k, cx, cy, 8, 900 + 37 * k);
    }
    dump(f);
  }
  fclose(f);
}

// ------------------------------------------------------------------------------------------ games
using namespace games;
typedef std::function<void(float, uint32_t, Input &)> Script;

static void runGame(const char *file, Id id, float secs, std::vector<float> capAt, Script script, std::function<void()> report = nullptr) {
  begin(id);
  FILE *f = openOut(file);
  size_t ci = 0;
  for (uint32_t now = 0; now <= (uint32_t)(secs * 1000); now += 20) {
    float t = now / 1000.0f;
    Input i = {};
    i.tiltOk = true;
    script(t, now, i);
    update(now, i);
    if (ci < capAt.size() && t >= capAt[ci]) { display.clearBuffer(); draw(display, now); dump(f); ++ci; }
  }
  fclose(f);
  if (report) report();
}

static float tiltFor(float want, float cur, float k, float lim) { return clampf((want - cur) * k, -lim, lim); }

// Random-input torture test: every game gets garbage tilt / shakes / button mashing for a long time.
// Run it from a build with -fsanitize=address,undefined to catch out-of-range indexes, NaNs, overflows.
static void fuzz() {
  for (int g = 0; g < games::COUNT; ++g) {
    for (int round = 0; round < 6; ++round) {
      begin((Id)g);
      uint32_t now = 0;
      bool hA = false, hC = false;
      for (int step = 0; step < 15000; ++step) {                         // 300 s
        now += 20;
        if (rand() % 25 == 0) hA = !hA;
        if (rand() % 25 == 0) hC = !hC;
        Input i = {};
        i.tiltOk = (round != 5);                                          // the last round: no MPU at all
        i.calibrating = rand() % 400 == 0;
        i.calib = (rand() % 100) / 100.0f;
        i.a = rand() % 40 == 0; i.c = rand() % 60 == 0;
        i.aHeld = hA; i.cHeld = hC;
        float big = (round % 2) ? 1.5f : 0.5f;
        i.tx = ((rand() % 2001) - 1000) / 1000.0f * big; i.ty = ((rand() % 2001) - 1000) / 1000.0f * big;
        if (rand() % 80 == 0) { i.shx = ((rand() % 2001) - 1000) / 400.0f; i.shy = ((rand() % 2001) - 1000) / 400.0f; }
        update(now, i);
        if (step % 5 == 0) { display.clearBuffer(); draw(display, now); }
        if (g == PINBALL && (!std::isfinite(pin::bx) || !std::isfinite(pin::by))) { printf("FUZZ FAIL: pinball ball is NaN\n"); exit(1); }
        if (g == ROAD && (!std::isfinite(road::ppx) || !std::isfinite(road::dist))) { printf("FUZZ FAIL: road NaN\n"); exit(1); }
      }
    }
    printf("fuzz: %s ok (best %lu)\n", name((Id)g), (unsigned long)best[g]);
  }
  // menus: mash the launcher with random inputs in both styles
  for (int style = 0; style < 2; ++style) {
    launcher::enterSub(ONLINE, 14, 0, style, 0, onlineVal);
    for (uint32_t t = 0; t < 120000; t += 40) {
      launcher::input(rand() % 20 == 0, rand() % 90 == 0, rand() % 20 == 0, t, ((rand() % 2001) - 1000) / 1000.0f, ((rand() % 2001) - 1000) / 1000.0f, true);
      display.clearBuffer(); launcher::draw(display, t);
    }
  }
  printf("fuzz: menus ok\n");
}

int main(int argc, char **argv) {
  if (argc > 1) OUTDIR = argv[1];
  display.begin();
  randomSeed(7);
  srand(7);
  if (argc > 2 && std::string(argv[2]) == "fuzz") { fuzz(); return 0; }

  introSheet("intro.bin");
  {                                                       // the home launcher, both styles
    FILE *f = openOut("m_home.bin");
    for (int st = 0; st < 2; ++st) for (int sel : {0, 4, 9}) {
      launcher::enter(sel, st, 0);
      for (uint32_t t = 0; t <= 1600; t += 40) { display.clearBuffer(); launcher::draw(display, t); }
      dump(f);
    }
    fclose(f);
  }
  menuSheet("m_games_car.bin", GAMES, 13, 0, nullptr, {0, 4, 7, 8, 9, 12});
  menuSheet("m_games_ring.bin", GAMES, 13, 1, nullptr, {0, 5});
  menuSheet("m_tools_car.bin", TOOLS, 7, 0, nullptr, {0, 1, 2, 3, 6});
  menuSheet("m_settings_car.bin", SETTINGS, 9, 0, settingsVal, {0, 6, 8});
  menuSheet("m_settings_ring.bin", SETTINGS, 9, 1, settingsVal, {0, 6});
  menuSheet("m_online_car.bin", ONLINE, 14, 0, onlineVal, {0, 1, 13});
  menuSheet("m_online_ring.bin", ONLINE, 14, 1, onlineVal, {0, 7});
  iconSheet("icons.bin");

  // ---------------- Star Blaster: a scripted pilot that chases the nearest enemy
  runGame("star.bin", STAR, 60, {1.0f, 3.0f, 6.0f, 9.0f, 13.0f, 18.0f, 25.0f, 32.0f, 45.0f, 59.0f},
    [](float t, uint32_t now, Input &i) {
      float wantY = 36, wantX = 22;
      float bestD = 1e9f;
      for (auto &e : star::en) if (e.on && e.x > star::sx && e.x < 126) { float d = e.x - star::sx; if (d < bestD) { bestD = d; wantY = e.y; } }
      for (auto &b : star::eb) if (b.on && fabsf(b.y - star::sy) < 7 && b.x > star::sx && b.x - star::sx < 40) wantY = star::sy + (b.y > star::sy ? -16 : 16);
      i.tx = clampf((wantX - 38) / 30.0f * 0.45f, -0.45f, 0.45f);
      i.ty = clampf((wantY - 36) / 22.0f * 0.45f, -0.45f, 0.45f);
      i.aHeld = true;
      i.a = false;
      static int lastBombs = 0; (void)lastBombs;
      i.c = star::bombs > 0 && fmodf(t, 17.0f) < 0.02f;
    },
    []() { printf("star: score %lu kills %d lives %d over %d\n", (unsigned long)star::score, star::kills, star::lives, (int)star::over); });

  // ---------------- Sniper: aim at the nearest live target, zoom, fire when the crosshair sits on the head
  runGame("sniper.bin", SNIPER, 90, {1.5f, 4.0f, 8.0f, 12.0f, 16.0f, 20.0f, 30.0f, 45.0f, 60.0f, 89.0f},
    [](float t, uint32_t now, Input &i) {
      const sniper::Tgt *tt = nullptr;
      for (auto &g : sniper::tg) if (g.on && (!tt || g.life < tt->life)) tt = &g;
      if (tt) {
        float wx, wy; sniper::winPos(tt->b, tt->c, tt->r, wx, wy);
        float ax, ay; sniper::headPos(tt->b, tt->c, tt->r, ax, ay);
        float cx = sniper::camX + sniper::swx, cy = sniper::camY + sniper::swy;
        float dx = ax - cx, dy = ay - cy, d = sqrtf(dx * dx + dy * dy);
        auto drive = [](float e, float k) { return fabsf(e) < 0.5f ? 0.0f : (e > 0 ? 1 : -1) * clampf(0.07f + fabsf(e) * k, 0, 0.5f); };   // beats the 0.05 dead zone
        i.tx = drive(ax - cx, 0.02f);
        i.ty = drive(ay - cy, 0.04f);
        i.cHeld = d < 14;
        i.a = d < 1.6f && sniper::bolt <= 0 && sniper::zoom > 1.6f;
      }
    },
    []() { printf("sniper: score %lu kills %d lives %d over %d\n", (unsigned long)sniper::score, sniper::kills, sniper::lives, (int)sniper::over); });

  // ---------------- Road Rush: stay on the road, dodge cars, nitro when the road ahead is clear
  runGame("road.bin", ROAD, 80, {0.6f, 3.0f, 6.0f, 10.0f, 14.0f, 20.0f, 28.0f, 36.0f, 50.0f, 79.0f},
    [](float t, uint32_t now, Input &i) {
      float want = 0;
      float bestRel = 1e9f; bool clear = true;
      for (auto &c : road::cars) if (c.on) {
        float rel = c.z - road::dist;
        if (rel > 0 && rel < 45) { clear = false; if (rel < bestRel) { bestRel = rel; want = c.x > 0.2f ? -0.66f : (c.x < -0.2f ? 0.66f : (road::ppx > 0 ? -0.66f : 0.66f)); } }
      }
      float cv = road::curveAt(road::dist) * road::spd * road::spd * 0.95f / (4.0f * 1.8f * (0.4f + road::spd));
      i.tx = clampf((want - road::ppx) * 0.35f + cv, -0.5f, 0.5f);
      i.aHeld = clear && road::nitro > 0.2f;
    },
    []() { printf("road: dist %d m, time left %.1f, over %d, ppx %.2f\n", (int)road::dist, road::timeLeft, (int)road::over, road::ppx); });

  // ---------------- Pinball: flippers follow the ball; also checks the ball never leaves the table
  static int violations = 0, drains = 0, lastBalls = 3;
  runGame("pinball.bin", PINBALL, 240, {0.3f, 1.0f, 3.0f, 6.0f, 9.0f, 15.0f, 25.0f, 40.0f, 60.0f, 239.0f},
    [](float t, uint32_t now, Input &i) {
      if (pin::state == pin::SERVE) i.a = ((int)(t * 50) % 40) == 0;
      bool near = pin::state == pin::PLAY && pin::bx > 82;
      i.aHeld = near && pin::by < 34 && pin::bvx > -30;
      i.cHeld = near && pin::by >= 30 && pin::bvx > -30;
      if (!std::isfinite(pin::bx) || !std::isfinite(pin::by) || pin::bx < -3 || pin::bx > 135 || pin::by < -3 || pin::by > 67) ++violations;
      if (pin::balls != lastBalls) { drains++; lastBalls = pin::balls; }
      i.tiltOk = true;
    },
    []() { printf("pinball: score %lu balls %d state %d drains %d out-of-table samples %d\n", (unsigned long)pin::score, pin::balls, (int)pin::state, drains, violations); });

  // ---------------- Breakout: follow the ball with the paddle, launch when it sits on the paddle
  runGame("breakout.bin", BREAKOUT, 90, {0.5f, 5.0f, 12.0f, 25.0f, 40.0f, 55.0f, 70.0f, 89.0f},
    [](float t, uint32_t now, Input &i) {
      float want = brk::bx + (brk::bx - brk::padX) * 0.0f;
      i.tx = clampf((want - 64.0f) / 106.0f, -0.5f, 0.5f);
      if (brk::state == brk::SERVE) i.a = ((int)(t * 50) % 25) == 0;
    },
    []() { printf("breakout: score %lu level %d lives %d left %d state %d\n", (unsigned long)brk::score, brk::level, brk::lives, brk::left, (int)brk::state); });

  // ---------------- Asteroids: turn toward the nearest rock, fire when roughly aimed, thrust now and then
  runGame("asteroids.bin", ASTEROIDS, 90, {1.0f, 6.0f, 12.0f, 20.0f, 30.0f, 45.0f, 60.0f, 89.0f},
    [](float t, uint32_t now, Input &i) {
      const ast::Rock *nr = nullptr; float bd = 1e9f;
      for (auto &r : ast::rk) if (r.on) { float dx = r.x - ast::sx, dy = r.y - ast::sy, d2 = dx * dx + dy * dy; if (d2 < bd) { bd = d2; nr = &r; } }
      if (nr) {
        float want = atan2f(nr->y - ast::sy, nr->x - ast::sx) / 6.2831853f; if (want < 0) want += 1;
        float diff = want - ast::ang; while (diff > 0.5f) diff -= 1; while (diff < -0.5f) diff += 1;
        i.tx = clampf(diff * 6.0f, -0.5f, 0.5f);
        i.cHeld = fabsf(diff) < 0.06f;
        i.aHeld = bd > 900 && fmodf(t, 3.0f) < 0.4f;
      }
    },
    []() { printf("asteroids: score %lu wave %d lives %d over %d\n", (unsigned long)ast::score, ast::wave, ast::lives, (int)ast::over); });

  // ---------------- Stack: drop when the block is close to lined up (imperfect on purpose)
  runGame("stack.bin", STACK, 80, {0.5f, 8.0f, 16.0f, 28.0f, 40.0f, 55.0f, 70.0f, 79.0f},
    [](float t, uint32_t now, Input &i) {
      int top = (stk::cnt - 1) & 7;
      float err = stk::mx - stk::lx[top];
      static uint32_t lastDrop = 0;
      if (!stk::over && fabsf(err) < 3.0f + (float)((now / 1000) % 4) && now - lastDrop > 400) { i.a = true; lastDrop = now; }
    },
    []() { printf("stack: score %lu layers %d width %.1f over %d\n", (unsigned long)stk::score, stk::cnt, stk::mw, (int)stk::over); });

  // ---------------- Quick Draw: a player with a 300 ms reaction time
  runGame("duel.bin", DUEL, 40, {0.5f, 2.5f, 5.0f, 6.0f, 8.0f, 12.0f, 20.0f, 39.0f},
    [](float t, uint32_t now, Input &i) {
      static float seenAt = -1; static int lastSt = -1;
      if ((int)duel::st != lastSt) { lastSt = (int)duel::st; seenAt = duel::st == duel::DRAW ? t : -1; }
      if (duel::st == duel::DRAW && seenAt >= 0 && t - seenAt >= 0.30f) i.a = true;
      if (duel::over && duel::t > 0.8f) i.c = true;
    },
    []() { printf("duel: wins %d best %d ms over %d\n", duel::wins, (int)duel::bestMs, (int)duel::over); });

  // ---------------- Flappy: flap when below the middle of the next gap (a decent but imperfect player)
  runGame("flappy.bin", FLAPPY, 90, {0.3f, 2.0f, 5.0f, 9.0f, 14.0f, 20.0f, 30.0f, 45.0f, 60.0f, 89.0f},
    [](float t, uint32_t now, Input &i) {
      float gy = 30; float bestX = 1e9f;
      for (auto &p : flp::pp) if (p.x + flp::PW > flp::BX - 6 && p.x < bestX) { bestX = p.x; gy = p.gy; }
      static uint32_t lastFlap = 0;
      if (flp::over) { if (flp::deadT > 0.6f) i.a = true; return; }
      if (flp::by > gy + 5.0f && flp::bvy > -30 && now - lastFlap > 120) { i.a = true; lastFlap = now; }
      if (!flp::started && t > 0.5f) i.c = true;
    },
    []() { printf("flappy: score %d over %d speed %.0f gap %.0f\n", flp::score, (int)flp::over, flp::speed, flp::gap); });

  // ---------------- Memory: repeat the pattern by tilting to each pad and back to centre
  runGame("memory.bin", MEMORY, 80, {0.5f, 2.0f, 5.0f, 9.0f, 14.0f, 22.0f, 35.0f, 50.0f, 79.0f},
    [](float t, uint32_t now, Input &i) {
      static int lastIdx = -1; static uint32_t pressedAt = 0; static bool inDir = false;
      if (mem::over) { if (mem::t > 0.8f) i.a = true; return; }
      if (mem::st == mem::TURN) {
        static const float dx[4] = {0, 0.7f, 0, -0.7f}, dy[4] = {-0.7f, 0, 0.7f, 0};
        if (!inDir && now - pressedAt > 180) { int k = mem::seq[mem::idx]; i.tx = dx[k]; i.ty = dy[k]; inDir = true; pressedAt = now; }
        else if (inDir && now - pressedAt > 120) { inDir = false; pressedAt = now; }
        else if (inDir) { int k = mem::seq[mem::idx]; i.tx = dx[k]; i.ty = dy[k]; }
        (void)lastIdx;
      } else inDir = false;
    },
    []() { printf("memory: round %d score %d over %d\n", mem::len, mem::score, (int)mem::over); });

  // ---------------- Road Rush traffic close up: sedans and trucks at several distances (must not look like blobs)
  {
    FILE *f = openOut("road_cars.bin");
    begin(ROAD);
    Input i0 = {}; i0.tiltOk = true;
    update(20, i0);
    struct P { float rel, x; uint8_t type; };
    const std::vector<std::vector<P>> sets = {
      {{9, -0.66f, 0}, {18, 0.0f, 1}, {34, 0.66f, 0}, {60, -0.66f, 0}, {110, 0.0f, 0}},
      {{6, 0.0f, 1}, {13, 0.66f, 0}, {26, -0.66f, 1}, {45, 0.0f, 0}, {80, 0.66f, 1}},
      {{4, 0.66f, 0}, {11, -0.66f, 0}, {22, 0.0f, 0}, {40, 0.66f, 1}, {70, -0.66f, 0}},
    };
    for (auto &set : sets) {
      memset(road::cars, 0, sizeof(road::cars));
      int k = 0;
      for (auto &p : set) { road::Car &c = road::cars[k++]; c.on = true; c.z = road::dist + p.rel; c.x = p.x; c.v = 0; c.type = p.type; }
      display.clearBuffer(); draw(display, 1000); dump(f);
    }
    fclose(f);
  }
  return 0;
}
