#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <esp_system.h>
#include "config.h"
#include "fluid.h"
#include "online.h"
#include "spotify.h"
#include "lyrics_visual.h"
#include "living_oled.h"
#include "launcher.h"
#include "games.h"

// OLED: 1.3" SH1106 and MPU6050 share one hardware I2C bus (Wire: SDA = GPIO 8, SCL = GPIO 9).
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ---------------- Boot log ----------------
String bootLog;
bool bootLogPrinted = false;
void stamp(const char *what) {
  bootLog += String(millis());
  bootLog += " ms  ";
  bootLog += what;
  bootLog += "\n";
}

bool oledUp = false;

bool oledPresent() {
  Wire.beginTransmission(OLED_ADDR);
  return Wire.endTransmission() == 0;
}

// ---------------- MPU6050 (shared hardware I2C, raw registers) ----------------
struct Motion {
  float ax, ay, az;   // g
  float gx, gy, gz;   // deg/s
  float tempC;
} motion = {0, 0, 1, 0, 0, 0, 0};
bool mpuOk = false;
uint8_t mpuFails = 0;
float gyroBias[3] = {0, 0, 0};          // deg/s, measured at boot while the device sits still

bool mpuPresent() {
  Wire.beginTransmission(MPU_ADDR);
  return Wire.endTransmission() == 0;
}

bool mpuWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

void mpuInit() {
  bool ok = mpuWriteReg(0x6B, 0x00);   // PWR_MGMT_1: wake up
  if (ok) mpuWriteReg(0x1A, 0x03);     // CONFIG: ~44 Hz low-pass, calmer readings
  mpuOk = ok;
}

bool mpuRead() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return false;

  int16_t r[7];
  for (int i = 0; i < 7; i++) {
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    r[i] = (int16_t)((hi << 8) | lo);
  }

  motion.ax = r[0] / 16384.0f;
  motion.ay = r[1] / 16384.0f;
  motion.az = r[2] / 16384.0f;
  motion.tempC = r[3] / 340.0f + 36.53f;
  motion.gx = r[4] / 131.0f - gyroBias[0];
  motion.gy = r[5] / 131.0f - gyroBias[1];
  motion.gz = r[6] / 131.0f - gyroBias[2];
  return true;
}

// ---------------- Buttons ----------------
struct Button {
  uint8_t pin;
  bool stable;
  bool lastRaw;
  uint32_t changedAt;
  bool pressEvent;
};

Button btnA = {PIN_BTN_A, HIGH, HIGH, 0, false};
Button btnB = {PIN_BTN_B, HIGH, HIGH, 0, false};
Button btnC = {PIN_BTN_C, HIGH, HIGH, 0, false};

void updateButton(Button &b) {
  bool raw = digitalRead(b.pin);
  uint32_t now = millis();

  // Start/restart debounce whenever the electrical state changes.
  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.changedAt = now;
  }

  // Once stable for 30 ms, accept the new state.
  if ((now - b.changedAt) >= 30 && raw != b.stable) {
    b.stable = raw;

    // Buttons are wired to GND, so LOW is a press.
    if (b.stable == LOW) {
      b.pressEvent = true;
    }
  }
}

bool pressed(Button &b) {
  bool event = b.pressEvent;
  b.pressEvent = false;
  return event;
}

// ---------------- Touch pad ----------------
bool touchRaw = false;
bool touchStable = false;
uint32_t touchChangedAt = 0;
uint32_t touchDownAt = 0;
bool touchDownEvent = false;

void updateTouch(uint32_t now) {
  bool raw = digitalRead(PIN_TOUCH) == HIGH;
  if (raw != touchRaw) {
    touchRaw = raw;
    touchChangedAt = now;
  }
  if ((now - touchChangedAt) >= 20 && touchStable != touchRaw) {
    touchStable = touchRaw;
    if (touchStable) {
      touchDownAt = now;
      touchDownEvent = true;
    }
  }
}

// ---------------- Pet stats (saved to flash) ----------------
// Companion style: nothing can die. Energy and happiness only change how the
// pet looks and acts.
Preferences prefs;
struct PetStats {
  float happy;     // 0..100
  float energy;    // 0..100
  uint32_t pets;   // lifetime touches
} pet = {60, 80, 0};
uint8_t savedHappy = 0, savedEnergy = 0;
uint32_t savedPets = 0;
uint32_t lastSave = 0;

void loadStats() {
  prefs.begin("pocketpet", false);
  pet.happy = prefs.getUChar("happy", 60);
  pet.energy = prefs.getUChar("energy", 80);
  pet.pets = prefs.getUInt("pets", 0);
  savedHappy = (uint8_t)pet.happy;
  savedEnergy = (uint8_t)pet.energy;
  savedPets = pet.pets;
}

void saveStats(bool force) {
  uint32_t now = millis();
  if (!force && now - lastSave < 120000) return;

  uint8_t h = (uint8_t)pet.happy;
  uint8_t e = (uint8_t)pet.energy;
  bool changed = (abs((int)h - (int)savedHappy) >= 2) ||
                 (abs((int)e - (int)savedEnergy) >= 2) ||
                 (pet.pets != savedPets);
  if (!changed) return;

  prefs.putUChar("happy", h);
  prefs.putUChar("energy", e);
  prefs.putUInt("pets", pet.pets);
  savedHappy = h;
  savedEnergy = e;
  savedPets = pet.pets;
  lastSave = now;
}

uint32_t lastActivity = 0;      // any touch, button or motion
uint32_t lastInteraction = 0;   // touch or button only

// ---------------- Settings (saved to flash) ----------------
const uint8_t BRIGHT_LEVELS[] = {8, 60, 150, 255};
const char *BRIGHT_NAMES[] = {"Low", "Medium", "High", "Max"};
const uint32_t SLEEP_MS[] = {0, 30000, 60000, 300000};
const char *SLEEP_NAMES[] = {"Off", "30 s", "1 min", "5 min"};
uint8_t setBright = 2, setSleep = 0, setFlip = 0, setLyric = 3, setMenu = 0;   // setMenu: 0 carousel, 1 tilt ring
const int LYRIC_STEPS[] = {-500, -300, -150, 0, 150, 300, 500, 800};   // ms, + = lyrics earlier
const int LYRIC_STEP_COUNT = 8;
bool screenOff = false;

void applyScreen() {
  display.setContrast(BRIGHT_LEVELS[setBright]);
  display.setFlipMode(setFlip ? 1 : 0);
}

const char *const BEST_KEYS[games::COUNT] = {"bestStar", "bestSnip", "bestRoad", "bestPin"};   // best scores of the new games

void loadSettings() {
  setBright = constrain(prefs.getUChar("bright", 2), 0, 3);
  setSleep = constrain(prefs.getUChar("sleep", 0), 0, 3);
  setFlip = prefs.getUChar("flip", 0) ? 1 : 0;
  setLyric = constrain(prefs.getUChar("lyoff", 3), 0, LYRIC_STEP_COUNT - 1);
  setMenu = prefs.getUChar("menu", 0) ? 1 : 0;
  for (int i = 0; i < games::COUNT; ++i) games::best[i] = prefs.getUInt(BEST_KEYS[i], 0);
  spotify::setLyricOffset(LYRIC_STEPS[setLyric]);
}

void saveSettings() {
  prefs.putUChar("bright", setBright);
  prefs.putUChar("sleep", setSleep);
  prefs.putUChar("flip", setFlip);
  prefs.putUChar("lyoff", setLyric);
  prefs.putUChar("menu", setMenu);
}

void saveBest() {
  for (int i = 0; i < games::COUNT; ++i) prefs.putUInt(BEST_KEYS[i], games::best[i]);
}

void resetPet() {
  pet.happy = 60; pet.energy = 80; pet.pets = 0;
  prefs.putUChar("happy", 60); prefs.putUChar("energy", 80); prefs.putUInt("pets", 0);
  savedHappy = 60; savedEnergy = 80; savedPets = 0;
}

void addHappy(float v) { pet.happy = constrain(pet.happy + v, 0.0f, 100.0f); }

// Called once per second.
void statsTick(uint32_t now) {
  bool sleeping = (now - lastActivity) > IDLE_SLEEP_MS;
  if (sleeping) {
    pet.energy = min(100.0f, pet.energy + 0.35f);   // about +21 per minute asleep
  } else {
    pet.energy = max(10.0f, pet.energy - 0.025f);   // about -1.5 per minute awake
  }
  if ((now - lastInteraction) > 20000) {
    pet.happy = max(15.0f, pet.happy - 0.05f);      // about -3 per minute of neglect
  }
}

// ---------------- Pet events and faces ----------------
enum Overlay { OV_NONE, OV_DIZZY, OV_SURPRISED, OV_LAUGH, OV_LOVE, OV_PURR };
Overlay overlay = OV_NONE;
uint32_t overlayUntil = 0;

void trigger(Overlay o, uint32_t ms) {
  uint32_t now = millis();
  bool dizzyBusy = (overlay == OV_DIZZY && now < overlayUntil);
  if (dizzyBusy && o != OV_DIZZY) return;   // dizziness wins over everything
  overlay = o;
  overlayUntil = now + ms;
}

enum Face {
  F_NEUTRAL, F_HAPPY, F_SLEEPY, F_SLEEPING, F_SAD,
  F_SURPRISED, F_DIZZY, F_LOVE, F_PURR, F_LAUGH
};

Face currentFace(uint32_t now) {
  if (now < overlayUntil) {
    switch (overlay) {
      case OV_DIZZY:     return F_DIZZY;
      case OV_SURPRISED: return F_SURPRISED;
      case OV_LAUGH:     return F_LAUGH;
      case OV_LOVE:      return F_LOVE;
      case OV_PURR:      return F_PURR;
      default: break;
    }
  }
  uint32_t idle = now - lastActivity;
  if (idle > IDLE_SLEEP_MS) return F_SLEEPING;
  if (idle > IDLE_SLEEPY_MS || pet.energy < 25) return F_SLEEPY;
  if (pet.happy < 30) return F_SAD;
  if (pet.happy > 70) return F_HAPPY;
  return F_NEUTRAL;
}

enum EyeStyle { EY_NORMAL, EY_ARC, EY_SPIRAL, EY_CLOSED };
enum MouthType { M_FLAT, M_SMILE, M_CAT, M_BIG, M_O, M_FROWN, M_WAVY, M_TINY, M_YAWN };

struct FaceParams {
  EyeStyle style;
  float open;       // 0..1.2 eye height
  float pupil;      // pupil radius
  float lid;        // upper lid droop 0..1
  float slant;      // + angry, - sad
  float lower;      // lower lid raise (cheeks) 0..1
  float lookBiasY;  // pupils drift up/down
  MouthType mouth;
};

FaceParams faceParams(Face f, uint32_t now) {
  switch (f) {
    case F_HAPPY:     return {EY_NORMAL, 1.0f, 8, 0.00f, 0.0f, 0.22f, 0, M_CAT};
    case F_SLEEPY: {
      MouthType m = ((now / 1000) % 9 < 2) ? M_YAWN : M_FLAT;
      return {EY_NORMAL, 0.85f, 6, 0.45f, 0.0f, 0.0f, 2, m};
    }
    case F_SLEEPING:  return {EY_CLOSED, 0.1f, 6, 0.0f, 0.0f, 0.0f, 0, M_TINY};
    case F_SAD:       return {EY_NORMAL, 0.95f, 7, 0.12f, -0.9f, 0.0f, 3, M_FROWN};
    case F_SURPRISED: return {EY_NORMAL, 1.18f, 3, 0.00f, 0.0f, 0.0f, 0, M_O};
    case F_DIZZY:     return {EY_SPIRAL, 1.0f, 7, 0.0f, 0.0f, 0.0f, 0, M_WAVY};
    case F_LOVE:      return {EY_ARC, 1.0f, 7, 0.0f, 0.0f, 0.0f, 0, M_CAT};
    case F_PURR:      return {EY_ARC, 1.0f, 7, 0.0f, 0.0f, 0.0f, 0, M_CAT};
    case F_LAUGH:     return {EY_ARC, 1.0f, 7, 0.0f, 0.0f, 0.0f, 0, M_BIG};
    case F_NEUTRAL:
    default:          return {EY_NORMAL, 1.0f, 7, 0.0f, 0.0f, 0.0f, 0, M_SMILE};
  }
}

// ---------------- Hearts ----------------
struct Heart {
  int16_t x;
  int16_t y0;
  uint32_t born;
  bool alive;
};
Heart hearts[6];

void spawnHeart(uint32_t now) {
  for (auto &h : hearts) {
    if (!h.alive) {
      h.alive = true;
      h.born = now;
      h.x = random(0, 2) ? random(4, 22) : random(106, 124);
      h.y0 = random(44, 56);
      return;
    }
  }
}

void drawHeart(int x, int y) {
  display.drawDisc(x - 2, y, 2);
  display.drawDisc(x + 2, y, 2);
  display.drawTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 6);
}

void drawHearts(uint32_t now) {
  display.setDrawColor(1);
  for (auto &h : hearts) {
    if (!h.alive) continue;
    uint32_t age = now - h.born;
    if (age > 1700) {
      h.alive = false;
      continue;
    }
    int y = h.y0 - (int)(age * 28 / 1000);
    if (y >= 3) drawHeart(h.x, y);
  }
}

// ---------------- Eye animation state ----------------
float approach(float cur, float tgt, float k) { return cur + (tgt - cur) * k; }

float eOpen = 1, ePupil = 7, eLid = 0, eSlant = 0, eLower = 0;
float eLookX = 0, eLookY = 0, eBlink = 1, eWink = 1;

uint32_t nextBlinkAt = 0, blinkUntil = 0;
uint32_t nextWinkAt = 0, winkUntil = 0;
uint32_t nextWanderAt = 0;
int wanderX = 0, wanderY = 0;
uint32_t nextHeartAt = 0;

const int EYE_W = 30;
const int EYE_H = 30;
const int EYE_L_X = 40;
const int EYE_R_X = 88;
const int EYE_Y = 24;
const int MOUTH_Y = 52;

void drawEye(int cx, int cy, int side, EyeStyle style, float open, float pupil,
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

void drawMouth(MouthType m, int y, uint32_t now) {
  display.setDrawColor(1);
  switch (m) {
    case M_FLAT:
      display.drawHLine(58, y, 12);
      break;
    case M_SMILE:
      display.drawLine(56, y - 2, 60, y + 1);
      display.drawHLine(60, y + 1, 8);
      display.drawLine(68, y + 1, 72, y - 2);
      break;
    case M_CAT:
      display.drawLine(56, y - 1, 60, y + 2);
      display.drawLine(60, y + 2, 64, y - 1);
      display.drawLine(64, y - 1, 68, y + 2);
      display.drawLine(68, y + 2, 72, y - 1);
      break;
    case M_BIG:
      display.drawDisc(64, y - 3, 7, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
      break;
    case M_O:
      display.drawCircle(64, y, 3);
      break;
    case M_FROWN:
      display.drawLine(57, y + 2, 61, y - 1);
      display.drawHLine(61, y - 1, 6);
      display.drawLine(67, y - 1, 71, y + 2);
      break;
    case M_WAVY:
      for (int i = 0; i < 6; i++) {
        int y0 = y + ((i % 2) ? 2 : -2);
        int y1 = y + ((i % 2) ? -2 : 2);
        display.drawLine(52 + i * 4, y0, 56 + i * 4, y1);
      }
      break;
    case M_TINY:
      display.drawHLine(61, y, 6);
      break;
    case M_YAWN: {
      int r = 2 + (int)((sinf(now / 300.0f) + 1.0f) * 2.5f);
      display.drawDisc(64, y, r);
      break;
    }
  }
}

void drawZzz(uint32_t now) {
  display.setFont(u8g2_font_6x10_tf);
  int n = (now / 500) % 4;
  if (n >= 1) display.drawStr(104, 20, "z");
  if (n >= 2) display.drawStr(111, 13, "z");
  if (n >= 3) display.drawStr(118, 6, "Z");
}

void petTilt(float &tx, float &ty);   // defined with the tilt helper below

void drawPet(uint32_t now) {
  Face face = currentFace(now);
  FaceParams p = faceParams(face, now);

  // Blink and wink (only when the eyes are open and normal).
  bool normalEyes = (p.style == EY_NORMAL);
  if (normalEyes && now >= nextBlinkAt) {
    blinkUntil = now + 130;
    nextBlinkAt = now + random(2500, 5500);
    if (random(6) == 0) nextBlinkAt = now + 260;   // sometimes a quick double blink
  }
  if (normalEyes && (face == F_NEUTRAL || face == F_HAPPY) && now >= nextWinkAt) {
    winkUntil = now + 300;
    nextWinkAt = now + random(12000, 30000);
  }
  eBlink = approach(eBlink, now < blinkUntil ? 0.05f : 1.0f, 0.6f);
  eWink = approach(eWink, now < winkUntil ? 0.05f : 1.0f, 0.6f);

  // Eye shape eases toward the mood's target so changes feel smooth.
  eOpen = approach(eOpen, p.open, 0.22f);
  ePupil = approach(ePupil, p.pupil, 0.22f);
  eLid = approach(eLid, p.lid, 0.22f);
  eSlant = approach(eSlant, p.slant, 0.22f);
  eLower = approach(eLower, p.lower, 0.22f);

  // Where the pupils look: tilt wins, otherwise wander around now and then.
  float tx, ty;
  petTilt(tx, ty);
  bool tilted = (fabsf(tx) + fabsf(ty)) > 2.0f;
  if (!tilted) {
    if (now >= nextWanderAt) {
      wanderX = random(-6, 7);
      wanderY = random(-4, 5);
      if (random(3) == 0) { wanderX = 0; wanderY = 0; }
      nextWanderAt = now + random(900, 2600);
    }
    tx = wanderX;
    ty = wanderY;
  }
  eLookX = approach(eLookX, tx, 0.3f);
  eLookY = approach(eLookY, ty + p.lookBiasY, 0.3f);

  // Gentle breathing bob, jitter while laughing.
  int bob = (int)roundf(sinf(now / (face == F_SLEEPING ? 900.0f : 650.0f)) * 1.3f);
  if (face == F_LAUGH) bob += ((now / 60) % 2) ? 1 : -1;
  int cy = EYE_Y + bob;

  // Hearts while purring.
  if (face == F_PURR && now >= nextHeartAt) {
    spawnHeart(now);
    nextHeartAt = now + 320;
  }

  int lx = (int)roundf(eLookX);
  int ly = (int)roundf(eLookY);
  drawEye(EYE_L_X, cy, -1, p.style, eOpen * eBlink, ePupil, eLid, eSlant, eLower, lx, ly, now);
  drawEye(EYE_R_X, cy, +1, p.style, eOpen * eBlink * eWink, ePupil, eLid, eSlant, eLower, lx, ly, now);
  drawMouth(p.mouth, MOUTH_Y + bob, now);
  drawHearts(now);
  if (face == F_SLEEPING) drawZzz(now);
}

// ---------------- Screens and menus ----------------
enum Mode {
  MODE_MENU,
  MODE_PET,
  MODE_FLUID,
  MODE_STATS,
  MODE_MOTION,
  MODE_TOOLS,
  MODE_GAMES,
  MODE_ONLINE,
  MODE_SPOTIFY,
  MODE_LYRICS_VISUAL,
  MODE_LIVING_OLED,
  MODE_SETTINGS,
  MODE_TEMP,
  MODE_8BALL,
  MODE_DICE,
  MODE_SNAKE,
  MODE_TILT_MAZE,
  MODE_GAME          // one of the new games in games.cpp (curGame says which)
};

Mode mode = MODE_PET;          // the pet is the home / idle screen; B opens the launcher

const char *rootItems[] = {"Pet", "Fluid", "Stats", "Motion test", "Tools", "Games", "Online", "Spotify", "Lyrics visual", "Living OLED", "Settings"};
const int ROOT_COUNT = 11;
// Tools, Games and Settings reuse the launcher (carousel / tilt ring), so every menu looks like the home screen.
const launcher::Item toolsMenu[] = {
  {"Temperature", launcher::ICON_TOOLS}, {"Magic 8-ball", launcher::ICON_8BALL}, {"Dice", launcher::ICON_DICE}, {"Back", launcher::ICON_BACK}};
const int TOOLS_COUNT = 4;
const launcher::Item gamesMenu[] = {
  {"Star Blaster", launcher::ICON_STAR}, {"Sniper", launcher::ICON_SNIPER}, {"Road Rush", launcher::ICON_ROAD}, {"Pinball", launcher::ICON_PINBALL},
  {"Tilt Maze", launcher::ICON_MAZE}, {"Snake", launcher::ICON_SNAKE}, {"Back", launcher::ICON_BACK}};
const int GAMES_COUNT = 7;
games::Id curGame = games::STAR;

int menuIndex = 0;
int submenuIndex = 0;          // item that was highlighted in Tools / Games / Settings (restored when coming back)

const char *eightBallAnswers[] = {
  "Yes.", "No.", "Maybe.", "Definitely!",
  "Ask again.", "Looks good.", "Not likely.", "Absolutely."
};
const int EIGHT_BALL_COUNT = 8;
int eightBallIndex = 0;
int diceValue = 1;
bool shaken = false;           // set by the MPU code: the keychain was shaken (rolls the dice / asks the 8-ball)
uint32_t shakeAt = 0;

// ---------------- Mini-game state ----------------
// All games are original, compact monochrome adaptations for the 128x64 OLED.
// A/C are game actions; B returns to Games. The touch pad is ignored outside Pet.
uint32_t gameLastTick = 0;
bool gameOver = false;
uint32_t gameScore = 0;

// ---------------- Tilt helper (shared by Tilt Maze and Fluid) ----------------
// Hold the device the way you normally hold a phone (tilted toward your face). The pose you are in
// while calibrating is "level"; tilts are measured from there. The screen axes are worked out from
// the gravity direction itself, so it does not matter how the MPU6050 is mounted on the board.
uint32_t motionSeq = 0;          // incremented on every fresh MPU sample
uint32_t tiltSeen = 0;
struct TiltState {
  bool calibrating;
  uint8_t samples;
  float sx, sy, sz;              // calibration sums
  bool autoAxes;                 // false = held too flat to tell, fall back to TILT_X/Y_SIGN
  float dX, dY, rX, rY;          // unit vectors (device frame): screen-down, screen-right
  float down0;                   // in-plane gravity along screen-down in the neutral pose (sin of tilt)
  float scale;                   // 1 / cos(neutral tilt): left/right and up/down tilt feel the same
  float fx0, fy0;                // fallback neutral reading
  float tx, ty;                  // smoothed relative tilt, angle-like, +x right, +y down
  float gx, gy;                  // smoothed in-plane gravity, neutral pose = (0, 1)
  float rgx, rgy;                // unsmoothed in-plane gravity (includes the push from moving the device)
  float sgx, sgy;                // very slow gravity estimate, so rgx-sgx is the shake / slosh part
  float zSign;                   // +1/-1: direction of the screen-normal gyro axis
  float lastWz;                  // previous gyro spin, for the angular acceleration kick
} tilt = {false, 0, 0, 0, 0, false, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 0};

// Works out the screen axes from a gravity reading (g, device frame) taken while the device is held still.
// Called by tiltBegin()'s calibration, by the launcher when it opens and by the background auto-level.
void tiltApplyPose(float mx, float my, float mz) {
  float inplane = sqrtf(mx * mx + my * my);
  if (inplane >= 0.18f) {
    tilt.autoAxes = true;
    tilt.dX = -mx / inplane;       // gravity points toward the bottom of the screen
    tilt.dY = -my / inplane;
    if (mz >= 0) { tilt.rX = -tilt.dY; tilt.rY = tilt.dX; }   // screen faces the user
    else         { tilt.rX = tilt.dY;  tilt.rY = -tilt.dX; }  // sensor mounted facing away
    tilt.down0 = inplane;
    tilt.zSign = (mz >= 0) ? 1.0f : -1.0f;
    tilt.scale = 1.0f / fmaxf(0.35f, sqrtf(fmaxf(0.0f, 1.0f - inplane * inplane)));
  } else {
    tilt.autoAxes = false;         // held nearly flat: use the fixed axis mapping from config.h
    tilt.fx0 = mx; tilt.fy0 = my;
    tilt.scale = 1.0f;
    tilt.down0 = 0.6f;
  }
}

void tiltBegin() {
  tilt.calibrating = true;
  tilt.samples = 0;
  tilt.sx = tilt.sy = tilt.sz = 0;
  tilt.tx = tilt.ty = 0;
  tilt.gx = 0;
  tilt.gy = 1;
  tilt.rgx = tilt.sgx = 0;
  tilt.rgy = tilt.sgy = 1;
  tilt.lastWz = 0;
  tiltSeen = motionSeq;
}

void tiltUpdate() {
  if (motionSeq == tiltSeen) return;   // wait for a fresh sample
  tiltSeen = motionSeq;
  if (!mpuOk) return;

  if (tilt.calibrating) {
    tilt.sx += motion.ax; tilt.sy += motion.ay; tilt.sz += motion.az;
    if (++tilt.samples >= 25) {        // 0.5 s of readings
      tiltApplyPose(tilt.sx / tilt.samples, tilt.sy / tilt.samples, tilt.sz / tilt.samples);
      tilt.calibrating = false;
    }
    return;
  }

  float rawx, rawy, rawgx, rawgy;
  if (tilt.autoAxes) {
    float gR = -(motion.ax * tilt.rX + motion.ay * tilt.rY);   // gravity toward screen-right
    float gD = -(motion.ax * tilt.dX + motion.ay * tilt.dY);   // gravity toward screen-down
    rawx = gR * tilt.scale;
    rawy = (gD - tilt.down0) * tilt.scale;
    float nd = fmaxf(tilt.down0, 0.25f);
    rawgx = gR / nd;
    rawgy = gD / nd;
  } else {
    rawx = TILT_X_SIGN * (motion.ay - tilt.fy0);
    rawy = TILT_Y_SIGN * (motion.ax - tilt.fx0);
    rawgx = rawx * 1.5f;
    rawgy = 1.0f + rawy * 1.5f;
  }
  const float k = 0.4f;
  tilt.tx += (constrain(rawx, -1.5f, 1.5f) - tilt.tx) * k;
  tilt.ty += (constrain(rawy, -1.5f, 1.5f) - tilt.ty) * k;
  tilt.gx += (constrain(rawgx, -2.0f, 2.0f) - tilt.gx) * k;
  tilt.gy += (constrain(rawgy, -2.0f, 2.0f) - tilt.gy) * k;
  tilt.rgx = constrain(rawgx, -3.0f, 3.0f);
  tilt.rgy = constrain(rawgy, -3.0f, 3.0f);
  tilt.sgx += (tilt.rgx - tilt.sgx) * 0.06f;
  tilt.sgy += (tilt.rgy - tilt.sgy) * 0.06f;
}

// ---------------- Background calibration (runs in every mode) ----------------
// 1. Gyro bias: for the first second the gyro offsets are averaged while the device is still and subtracted afterwards.
// 2. Auto-level: a slow average of the gravity reading is kept while the device is calm. Once it has been calm for
//    0.5 s the screen axes ("which way is down") are worked out from it; in Pet mode they are worked out again
//    whenever the device has been held still for LEVEL_HOLD samples in a clearly different pose, so the pet's eyes
//    and the tilt menu always measure from how the device is being held, however the MPU6050 sits on the board.
const int LEVEL_HOLD = 75;                // samples at 50 Hz (1.5 s) before a new pose replaces the old one in Pet mode
struct LevelState {
  float x, y, z;                          // calm-gravity average
  bool have, applied;
  uint16_t calm;                          // consecutive calm samples
  float px, py, pz;                       // the pose the tilt axes were last built from
  uint8_t gyN;                            // gyro-bias samples collected (0..50, 50 = done)
  float gsx, gsy, gsz;
} lvl = {0, 0, 1, false, false, 0, 0, 0, 1, 0, 0, 0, 0};

void motionBackground() {
  float amag = sqrtf(motion.ax * motion.ax + motion.ay * motion.ay + motion.az * motion.az);
  float dev = fabsf(amag - 1.0f);
  float spin = fabsf(motion.gx) + fabsf(motion.gy) + fabsf(motion.gz);
  bool calm = dev < 0.08f && spin < 40.0f;

  if (lvl.gyN < 50) {                                      // gyro bias (motion.g* is still unbiased here)
    if (calm) {
      lvl.gsx += motion.gx; lvl.gsy += motion.gy; lvl.gsz += motion.gz;
      if (++lvl.gyN == 50) { gyroBias[0] = lvl.gsx / 50; gyroBias[1] = lvl.gsy / 50; gyroBias[2] = lvl.gsz / 50; }
    } else { lvl.gyN = 0; lvl.gsx = lvl.gsy = lvl.gsz = 0; }
  }

  if (!calm) { lvl.calm = 0; return; }
  if (!lvl.have) { lvl.x = motion.ax; lvl.y = motion.ay; lvl.z = motion.az; lvl.have = true; }
  else { lvl.x += (motion.ax - lvl.x) * 0.12f; lvl.y += (motion.ay - lvl.y) * 0.12f; lvl.z += (motion.az - lvl.z) * 0.12f; }
  if (lvl.calm < 60000) lvl.calm++;
  if (tilt.calibrating) return;                            // a game is calibrating on its own

  bool apply = false;
  if (!lvl.applied) apply = lvl.calm >= 25;                // first pose after boot
  else if (mode == MODE_PET && lvl.calm >= LEVEL_HOLD) {   // idle: follow how the device is being held
    float dot = lvl.x * lvl.px + lvl.y * lvl.py + lvl.z * lvl.pz;   // both are ~1 g long, so dot ~ cos(angle)
    apply = dot < 0.985f;                                  // more than ~10 degrees away from the pose in use
  }
  if (apply) {
    tiltApplyPose(lvl.x, lvl.y, lvl.z);
    lvl.px = lvl.x; lvl.py = lvl.y; lvl.pz = lvl.z;
    lvl.applied = true;
  }
}

// Where the pet looks: tilt measured from the calibrated pose, or the raw axes until a pose exists.
void petTilt(float &tx, float &ty) {
  if (lvl.applied && tilt.autoAxes && !tilt.calibrating) {
    tx = constrain(tilt.tx * 10.0f, -7.0f, 7.0f);
    ty = constrain(tilt.ty * 10.0f, -7.0f, 7.0f);
  } else {
    tx = TILT_X_SIGN * constrain((int)(motion.ay * 10), -7, 7);
    ty = TILT_Y_SIGN * constrain((int)(motion.ax * 10), -7, 7);
  }
}

// The launcher measures tilt from the pose the device was in just before the button press.
void levelForLauncher() {
  if (lvl.have) tiltApplyPose(lvl.x, lvl.y, lvl.z);
  else tiltBegin();
  tilt.tx = tilt.ty = 0;
}

// Small banner (black box, white text) drawn over whatever is on screen.
void drawBanner(const char *text, int y) {
  display.setFont(u8g2_font_6x10_tf);
  int w = display.getStrWidth(text);
  int x = (128 - w) / 2;
  display.setDrawColor(0);
  display.drawBox(x - 3, y - 9, w + 6, 12);
  display.setDrawColor(1);
  display.drawFrame(x - 3, y - 9, w + 6, 12);
  display.drawStr(x, y, text);
}

// ---------------- Tilt Maze ----------------
// Procedural perfect maze (DFS), 12 x 4 cells of 10 px. Walls are N/E/S/W bit flags per cell.
// Physics runs on a fixed 20 ms step (independent of the main loop speed), the ball is a circle that
// collides with every wall segment, and the neutral pose is calibrated on entry (A = re-zero).
const int MAZE_COLS = 12, MAZE_ROWS = 4, MAZE_CELL = 10;
const int MAZE_X0 = 4, MAZE_Y0 = 18;
const uint8_t MW_N = 1, MW_E = 2, MW_S = 4, MW_W = 8;
const float MAZE_R = 2.5f;            // ball radius
const float MAZE_ACCEL = 420.0f;      // px/s^2 per unit of tilt
const float MAZE_DRAG = 2.2f;         // 1/s
const float MAZE_VMAX = 75.0f;        // px/s
uint8_t mazeWalls[MAZE_ROWS][MAZE_COLS];
bool mazeVisited[MAZE_ROWS][MAZE_COLS];
float mazeX = 9.5f, mazeY = 23.5f, mazeVX = 0, mazeVY = 0;
bool mazeWon = false, mazeStarted = false;
uint32_t mazeStart = 0, mazeEnd = 0, mazeLast = 0;

void generateMaze() {
  for (int r = 0; r < MAZE_ROWS; ++r)
    for (int c = 0; c < MAZE_COLS; ++c) { mazeWalls[r][c] = 15; mazeVisited[r][c] = false; }
  int stackC[MAZE_COLS * MAZE_ROWS], stackR[MAZE_COLS * MAZE_ROWS], top = 0;
  stackC[0] = 0; stackR[0] = 0; mazeVisited[0][0] = true;
  while (top >= 0) {
    int c = stackC[top], r = stackR[top];
    int nc[4], nr[4], dir[4], count = 0;
    if (r > 0 && !mazeVisited[r-1][c]) { nc[count]=c; nr[count]=r-1; dir[count++]=MW_N; }
    if (c < MAZE_COLS-1 && !mazeVisited[r][c+1]) { nc[count]=c+1; nr[count]=r; dir[count++]=MW_E; }
    if (r < MAZE_ROWS-1 && !mazeVisited[r+1][c]) { nc[count]=c; nr[count]=r+1; dir[count++]=MW_S; }
    if (c > 0 && !mazeVisited[r][c-1]) { nc[count]=c-1; nr[count]=r; dir[count++]=MW_W; }
    if (!count) { --top; continue; }
    int pick = random(count), d = dir[pick], x = nc[pick], y = nr[pick];
    mazeWalls[r][c] &= (uint8_t)~d;
    uint8_t opposite = d == MW_N ? MW_S : d == MW_E ? MW_W : d == MW_S ? MW_N : MW_E;
    mazeWalls[y][x] &= (uint8_t)~opposite;
    mazeVisited[y][x] = true;
    ++top; stackC[top] = x; stackR[top] = y;
  }
}

void resetMaze() {
  generateMaze();
  mazeX = MAZE_X0 + MAZE_CELL / 2.0f + 0.5f;
  mazeY = MAZE_Y0 + MAZE_CELL / 2.0f + 0.5f;
  mazeVX = mazeVY = 0;
  mazeWon = false;
  mazeStarted = false;
  tiltBegin();
}

// Push the ball out of one wall rectangle [x0,x1] x [y0,y1] (continuous pixel coordinates).
void mazePushOut(float x0, float y0, float x1, float y1) {
  float cx = constrain(mazeX, x0, x1), cy = constrain(mazeY, y0, y1);
  float dx = mazeX - cx, dy = mazeY - cy;
  float d2 = dx * dx + dy * dy;
  if (d2 >= MAZE_R * MAZE_R) return;
  float nx, ny, push;
  if (d2 > 1e-6f) {
    float d = sqrtf(d2);
    nx = dx / d; ny = dy / d; push = MAZE_R - d;
  } else {                                   // centre is inside the wall: leave by the shortest way
    float l = mazeX - x0, r = x1 - mazeX, t = mazeY - y0, b = y1 - mazeY;
    float m = fminf(fminf(l, r), fminf(t, b));
    nx = ny = 0;
    if (m == l) nx = -1; else if (m == r) nx = 1; else if (m == t) ny = -1; else ny = 1;
    push = m + MAZE_R;
  }
  mazeX += nx * push;
  mazeY += ny * push;
  float vn = mazeVX * nx + mazeVY * ny;      // remove the velocity going into the wall, with a little bounce
  if (vn < 0) { mazeVX -= vn * nx * 1.15f; mazeVY -= vn * ny * 1.15f; }
}

void mazeCollide() {
  int col = constrain((int)((mazeX - MAZE_X0) / MAZE_CELL), 0, MAZE_COLS - 1);
  int row = constrain((int)((mazeY - MAZE_Y0) / MAZE_CELL), 0, MAZE_ROWS - 1);
  for (int r = row - 1; r <= row + 1; ++r) {
    for (int c = col - 1; c <= col + 1; ++c) {
      if (r < 0 || r >= MAZE_ROWS || c < 0 || c >= MAZE_COLS) continue;
      float x0 = MAZE_X0 + c * MAZE_CELL, y0 = MAZE_Y0 + r * MAZE_CELL;
      uint8_t w = mazeWalls[r][c];
      if (w & MW_N) mazePushOut(x0, y0, x0 + MAZE_CELL + 1, y0 + 1);
      if (w & MW_W) mazePushOut(x0, y0, x0 + 1, y0 + MAZE_CELL + 1);
      if (c == MAZE_COLS - 1 && (w & MW_E)) mazePushOut(x0 + MAZE_CELL, y0, x0 + MAZE_CELL + 1, y0 + MAZE_CELL + 1);
      if (r == MAZE_ROWS - 1 && (w & MW_S)) mazePushOut(x0, y0 + MAZE_CELL, x0 + MAZE_CELL + 1, y0 + MAZE_CELL + 1);
    }
  }
}

float tiltDeadzone(float v) {
  const float d = 0.04f;
  if (v > d) return v - d;
  if (v < -d) return v + d;
  return 0;
}

void mazeStep(float dt) {
  float ax = tiltDeadzone(constrain(tilt.tx, -0.7f, 0.7f)) * MAZE_ACCEL;
  float ay = tiltDeadzone(constrain(tilt.ty, -0.7f, 0.7f)) * MAZE_ACCEL;
  mazeVX += ax * dt;
  mazeVY += ay * dt;
  float drag = fmaxf(0.0f, 1.0f - MAZE_DRAG * dt);
  mazeVX *= drag;
  mazeVY *= drag;
  mazeVX = constrain(mazeVX, -MAZE_VMAX, MAZE_VMAX);
  mazeVY = constrain(mazeVY, -MAZE_VMAX, MAZE_VMAX);
  for (int i = 0; i < 2; ++i) {              // two sub-steps keep fast balls from tunnelling through walls
    mazeX += mazeVX * dt * 0.5f;
    mazeY += mazeVY * dt * 0.5f;
    mazeCollide();
  }
  mazeX = constrain(mazeX, MAZE_X0 + MAZE_R, MAZE_X0 + MAZE_COLS * MAZE_CELL - MAZE_R);
  mazeY = constrain(mazeY, MAZE_Y0 + MAZE_R, MAZE_Y0 + MAZE_ROWS * MAZE_CELL - MAZE_R);
}

void mazeTick(uint32_t now) {
  tiltUpdate();
  if (tilt.calibrating || mazeWon || !mpuOk) { mazeLast = now; return; }
  if (!mazeStarted) { mazeStarted = true; mazeStart = now; mazeLast = now; }
  int steps = 0;
  while (now - mazeLast >= 20 && steps < 3) { mazeStep(0.02f); mazeLast += 20; ++steps; }
  if (now - mazeLast > 60) mazeLast = now;   // fell behind (slow frame): do not try to catch up
  int col = (int)((mazeX - MAZE_X0) / MAZE_CELL), row = (int)((mazeY - MAZE_Y0) / MAZE_CELL);
  if (col == MAZE_COLS - 1 && row == MAZE_ROWS - 1) { mazeWon = true; mazeEnd = now; }
}

void drawMaze(uint32_t now) {
  display.setFont(u8g2_font_5x7_tf);
  char label[28];
  if (!mpuOk) {
    display.drawStr(0, 7, "TILT MAZE");
    drawBanner("MPU6050 not found", 36);
    return;
  }
  uint32_t t = !mazeStarted ? 0 : (mazeWon ? mazeEnd : now) - mazeStart;
  if (mazeWon) snprintf(label, sizeof(label), "CLEAR %lu.%lus  A/C new", (unsigned long)(t / 1000), (unsigned long)((t / 100) % 10));
  else snprintf(label, sizeof(label), "TILT MAZE  %lu.%lus", (unsigned long)(t / 1000), (unsigned long)((t / 100) % 10));
  display.drawStr(0, 7, label);

  for (int r = 0; r < MAZE_ROWS; ++r) for (int c = 0; c < MAZE_COLS; ++c) {
    int x = MAZE_X0 + c * MAZE_CELL, y = MAZE_Y0 + r * MAZE_CELL;
    uint8_t w = mazeWalls[r][c];
    if (w & MW_N) display.drawHLine(x, y, MAZE_CELL + 1);
    if (w & MW_W) display.drawVLine(x, y, MAZE_CELL + 1);
    if (c == MAZE_COLS - 1 && (w & MW_E)) display.drawVLine(x + MAZE_CELL, y, MAZE_CELL + 1);
    if (r == MAZE_ROWS - 1 && (w & MW_S)) display.drawHLine(x, y + MAZE_CELL, MAZE_CELL + 1);
  }
  // Goal: a small hollow square in the bottom-right cell.
  display.drawFrame(MAZE_X0 + (MAZE_COLS - 1) * MAZE_CELL + 3, MAZE_Y0 + (MAZE_ROWS - 1) * MAZE_CELL + 3, 5, 5);
  display.drawDisc((int)mazeX, (int)mazeY, 2);

  if (tilt.calibrating) {
    drawBanner("Hold like a phone...", 38);
    display.drawFrame(34, 43, 60, 5);
    display.drawBox(35, 44, (int)(58.0f * tilt.samples / 25.0f), 3);
  }
}

// ---------------- Snake ----------------
// 8 px grid, 16 x 6 cells. Tilt the keychain to steer (a firm tilt, measured from the pose you start in), or use
// A = turn left, C = turn right. The pose is calibrated every time a round starts.
int snakeX[96], snakeY[96];
int snakeLength = 4, snakeDir = 0, snakeMoveDir = 0, foodX = 10, foodY = 3;
uint32_t snakeLastStep = 0;

void snakeTurn(int d) { if (d != (snakeMoveDir + 2) % 4) snakeDir = d; }   // never an instant U-turn into the neck

void resetSnake() {
  snakeLength = 4; snakeDir = snakeMoveDir = 0; gameOver = false; gameScore = 0;
  for (int i = 0; i < snakeLength; ++i) {
    snakeX[i] = 6 - i; snakeY[i] = 3;
  }
  foodX = random(0, 16); foodY = random(0, 6);
  snakeLastStep = millis();
  tiltBegin();
}

void snakeTick(uint32_t now) {
  tiltUpdate();
  if (tilt.calibrating) { snakeLastStep = now; return; }
  if (!gameOver && mpuOk) {
    float ax = fabsf(tilt.tx), ay = fabsf(tilt.ty);
    if (ax > 0.22f || ay > 0.22f) snakeTurn(ax > ay ? (tilt.tx > 0 ? 0 : 2) : (tilt.ty > 0 ? 1 : 3));
  }
  if (gameOver || now - snakeLastStep < 170) return;
  snakeLastStep = now;
  snakeMoveDir = snakeDir;
  int nx = snakeX[0], ny = snakeY[0];
  if (snakeDir == 0) nx++;
  else if (snakeDir == 1) ny++;
  else if (snakeDir == 2) nx--;
  else ny--;
  if (nx < 0 || nx >= 16 || ny < 0 || ny >= 6) { gameOver = true; return; }
  bool eating = nx == foodX && ny == foodY;
  int limit = snakeLength - (eating ? 0 : 1);
  for (int i = 0; i < limit; ++i) {
    if (snakeX[i] == nx && snakeY[i] == ny) { gameOver = true; return; }
  }
  if (eating && snakeLength < 96) {
    snakeLength++;
    gameScore++;
    foodX = random(0, 16); foodY = random(0, 6);
  }
  for (int i = snakeLength - 1; i > 0; --i) {
    snakeX[i] = snakeX[i - 1]; snakeY[i] = snakeY[i - 1];
  }
  snakeX[0] = nx; snakeY[0] = ny;
}

void drawSnake() {
  display.setFont(u8g2_font_5x7_tf);
  char score[16]; snprintf(score, sizeof(score), "SNAKE %lu", (unsigned long)gameScore);
  display.drawStr(0, 7, score);
  display.drawHLine(0, 10, 128);
  display.drawDisc(foodX * 8 + 4, 16 + foodY * 8 + 4, 3);
  for (int i = 0; i < snakeLength; ++i) {
    if (i == 0) display.drawBox(snakeX[i] * 8 + 1, 17 + snakeY[i] * 8, 6, 6);
    else display.drawFrame(snakeX[i] * 8 + 1, 17 + snakeY[i] * 8, 6, 6);
  }
  if (gameOver) {
    display.drawBox(24, 25, 80, 18);
    display.setDrawColor(0); display.drawStr(34, 37, "GAME OVER"); display.setDrawColor(1);
    display.setFont(u8g2_font_5x7_tf); display.drawStr(18, 61, "A/C restart  B back");
  }
  if (tilt.calibrating) {
    drawBanner("Hold like a phone...", 38);
    display.drawFrame(34, 43, 60, 5);
    display.drawBox(35, 44, (int)(58.0f * tilt.samples / 25.0f), 3);
  }
}

// ---------------- Fluid ----------------
int fluidPreset = 0;
uint32_t fluidLabelUntil = 0;

void enterFluid() {
  fluidPreset = 0;
  fluid::init(fluidPreset);
  tiltBegin();
  fluidLabelUntil = 0;
}

void drawFluid(uint32_t now) {
  if (!mpuOk) {
    display.setFont(u8g2_font_6x10_tf);
    drawBanner("MPU6050 not found", 36);
    return;
  }
  // The gravity vector is already in screen space with the calibrated pose = straight down.
  fluid::step((int32_t)(tilt.gx * 256.0f), (int32_t)(tilt.gy * 256.0f));
  fluid::render(display.getBufferPtr());

  if (tilt.calibrating) {
    drawBanner("Hold like a phone...", 16);
  } else if (now < fluidLabelUntil) {
    drawBanner(fluid::presetName(fluidPreset), 16);
  }
}

void gameTick(uint32_t now) {
  if (mode == MODE_SNAKE) snakeTick(now);
  else if (mode == MODE_TILT_MAZE) mazeTick(now);
  else if (mode == MODE_FLUID) tiltUpdate();
}

void drawBar(int y, const char *label, float value) {
  char num[8];
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, y + 8, label);
  display.drawFrame(44, y, 60, 9);
  display.drawBox(46, y + 2, (int)(56.0f * value / 100.0f), 5);
  snprintf(num, sizeof(num), "%d", (int)value);
  display.drawStr(108, y + 8, num);
}

// Why the chip last restarted. BROWN = the supply dipped (usually the battery cannot feed the Wi-Fi radio).
const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "CRASH";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "sleep";
    case ESP_RST_EXT: return "reset pin";
    default: return "other";
  }
}

void drawStats() {
  char line[32];
  display.setFont(u8g2_font_7x13B_tf);
  display.drawStr(0, 12, "STATS");
  display.drawHLine(0, 15, 128);
  drawBar(22, "Happy", pet.happy);
  drawBar(36, "Energy", pet.energy);
  display.setFont(u8g2_font_6x10_tf);
  snprintf(line, sizeof(line), "Pets: %lu", (unsigned long)pet.pets);
  display.drawStr(0, 60, line);
  char rr[24];
  snprintf(rr, sizeof(rr), "boot: %s", resetReasonText());
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(128 - display.getStrWidth(rr), 60, rr);
}

bool resetArmed = false;
uint32_t settingsMsgUntil = 0;
const int SETTINGS_ROWS = 7;
const launcher::Item settingsMenu[SETTINGS_ROWS] = {
  {"Brightness", launcher::ICON_BRIGHT}, {"Screen off", launcher::ICON_SCREEN_OFF}, {"Flip screen", launcher::ICON_FLIP},
  {"Menu style", launcher::ICON_STYLE}, {"Lyrics sync", launcher::ICON_SYNC}, {"Reset pet", launcher::ICON_RESET}, {"Back", launcher::ICON_BACK}};

const char *settingsValue(int i) {                  // the little pill under the name
  static char buf[12];
  switch (i) {
    case 0: return BRIGHT_NAMES[setBright];
    case 1: return SLEEP_NAMES[setSleep];
    case 2: return setFlip ? "On" : "Off";
    case 3: return setMenu ? "Tilt ring" : "Carousel";
    case 4: snprintf(buf, sizeof(buf), LYRIC_STEPS[setLyric] > 0 ? "+%dms" : "%dms", LYRIC_STEPS[setLyric]); return buf;
    case 5: return resetArmed ? "Sure? B" : "";
    default: return "";
  }
}

const char *gamesValue(int i) {                     // best score under the game name
  static char buf[16];
  if (i < games::COUNT && games::best[i] > 0) { snprintf(buf, sizeof(buf), "best %lu", (unsigned long)games::best[i]); return buf; }
  return "";
}

void drawMotion() {
  char line[32];
  display.setFont(u8g2_font_6x10_tf);

  if (!mpuOk) {
    display.drawStr(0, 12, "MPU6050 not found");
    display.drawStr(0, 24, "check SDA8 / SCL9");
    return;
  }

  snprintf(line, sizeof(line), "ax %6.2f gx %6.0f", motion.ax, motion.gx);
  display.drawStr(0, 12, line);

  snprintf(line, sizeof(line), "ay %6.2f gy %6.0f", motion.ay, motion.gy);
  display.drawStr(0, 26, line);

  snprintf(line, sizeof(line), "az %6.2f gz %6.0f", motion.az, motion.gz);
  display.drawStr(0, 40, line);

  snprintf(line, sizeof(line), "%.1fC  touch %d", motion.tempC, touchStable ? 1 : 0);
  display.drawStr(0, 54, line);
}

void drawTemperature() {
  display.setFont(u8g2_font_7x13B_tf);
  display.drawStr(0, 12, "TEMPERATURE");
  display.drawHLine(0, 15, 128);

  display.setFont(u8g2_font_10x20_tf);
  if (mpuOk) {
    char temp[16];
    snprintf(temp, sizeof(temp), "%.1f C", motion.tempC);
    display.drawStr(27, 40, temp);
  } else {
    display.drawStr(18, 40, "MPU offline");
  }

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 62, "MPU6050 die temperature");
}

void draw8Ball() {
  display.setFont(u8g2_font_7x13B_tf);
  display.drawStr(0, 12, "MAGIC 8-BALL");
  display.drawHLine(0, 15, 128);

  display.setFont(u8g2_font_7x13_tf);
  display.drawStr(8, 35, eightBallAnswers[eightBallIndex]);

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 63, "shake or A/C  B back");
}

void drawDice() {
  display.setFont(u8g2_font_7x13B_tf);
  display.drawStr(0, 12, "DICE");
  display.drawHLine(0, 15, 128);

  display.drawRFrame(48, 21, 32, 32, 5);
  int x = 64;
  int y = 37;
  int p = 7;

  if (diceValue == 1 || diceValue == 3 || diceValue == 5) display.drawDisc(x, y, 3);
  if (diceValue >= 2) {
    display.drawDisc(x - p, y - p, 3);
    display.drawDisc(x + p, y + p, 3);
  }
  if (diceValue >= 4) {
    display.drawDisc(x + p, y - p, 3);
    display.drawDisc(x - p, y + p, 3);
  }
  if (diceValue == 6) {
    display.drawDisc(x - p, y, 3);
    display.drawDisc(x + p, y, 3);
  }

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 63, "shake or A/C  B back");
}

// Shared B handling for the home launcher and its sub-menus (Tools, Games, Settings): a short press (on release) opens
// the highlighted item, holding B for 0.7 s goes back. Returns the picked index or -1; longB is set when the hold fired.
bool menuHeld = false, menuLong = false;
int menuPick(uint32_t now, bool a, bool b, bool c, bool &longB) {
  bool bShort = false;
  longB = false;
  if (b) { menuHeld = true; menuLong = false; }
  if (menuHeld) {
    if (btnB.stable == LOW) {
      if (!menuLong && now - btnB.changedAt >= 700) { menuLong = true; longB = true; }
    } else {
      if (!menuLong) bShort = true;
      menuHeld = false;
    }
  }
  if (longB) return -1;
  bool tiltOk = mpuOk && !tilt.calibrating && tilt.autoAxes;
  return launcher::input(a, bShort, c, now, tilt.tx, tilt.ty, tiltOk);
}

void openSubMenu(Mode m) {
  levelForLauncher();                          // tilt (ring style) is measured from the pose the device was just in
  const launcher::Item *items = m == MODE_TOOLS ? toolsMenu : (m == MODE_GAMES ? gamesMenu : settingsMenu);
  int n = m == MODE_TOOLS ? TOOLS_COUNT : (m == MODE_GAMES ? GAMES_COUNT : SETTINGS_ROWS);
  const char *(*val)(int) = m == MODE_SETTINGS ? settingsValue : (m == MODE_GAMES ? gamesValue : nullptr);
  launcher::enterSub(items, n, constrain(submenuIndex, 0, n - 1), setMenu, millis(), val);
}

void enterMode(Mode m) {
  if (mode == MODE_PET && m != MODE_PET) saveStats(true);
  if (mode == MODE_ONLINE && m != MODE_ONLINE) online::leave();   // radio off, cancel requests
  if (mode == MODE_SPOTIFY && m != MODE_SPOTIFY) spotify::leave();
  if (mode == MODE_LYRICS_VISUAL && m != MODE_LYRICS_VISUAL) lyrics_visual::leave();
  if (mode == MODE_LIVING_OLED && m != MODE_LIVING_OLED) living_oled::leave();
  mode = m;

  if (m == MODE_PET) {
    uint32_t now = millis();
    lastActivity = now;
    lastInteraction = now;
    overlay = OV_NONE;
  }
  if (m == MODE_MENU) {                       // launcher: tilt is measured from the pose the device was in a moment ago
    levelForLauncher();
    launcher::enter(constrain(menuIndex - 1, 0, launcher::APP_COUNT - 1), setMenu, millis());
  }
  if (m == MODE_TOOLS || m == MODE_GAMES || m == MODE_SETTINGS) {
    if (m == MODE_SETTINGS) resetArmed = false;
    openSubMenu(m);
  }
  if (m == MODE_ONLINE) { levelForLauncher(); online::enter(setMenu); }
  if (m == MODE_SPOTIFY) spotify::enter();
  if (m == MODE_LYRICS_VISUAL) lyrics_visual::enter();
  if (m == MODE_LIVING_OLED) living_oled::enter();
  if (m == MODE_8BALL) eightBallIndex = random(EIGHT_BALL_COUNT);
  if (m == MODE_DICE) diceValue = random(1, 7);
  if (m == MODE_SNAKE) resetSnake();
  if (m == MODE_TILT_MAZE) resetMaze();
  if (m == MODE_FLUID) enterFluid();
  if (m == MODE_GAME) { games::begin(curGame); tiltBegin(); }
}

// ---------------- Arduino ----------------
#if defined(SET_LOOP_TASK_STACK_SIZE)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);   // TLS handshakes (Online, Spotify) run on the loop task and want more than the default 8 KB
#endif

void setup() {
  stamp("setup start");
  stamp(resetReasonText());                                  // also printed with the boot log

  Serial.begin(115200);

  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  pinMode(PIN_BTN_C, INPUT_PULLUP);
  pinMode(PIN_TOUCH, INPUT);

  // Buttons are active-low: unpressed = HIGH, pressed = LOW.
  // Read once after pull-ups are enabled so startup state is correct.
  btnA.lastRaw = btnA.stable = digitalRead(PIN_BTN_A);
  btnB.lastRaw = btnB.stable = digitalRead(PIN_BTN_B);
  btnC.lastRaw = btnC.stable = digitalRead(PIN_BTN_C);
  stamp("pins ready");

  // One shared hardware I2C bus on GPIO 8/9 for the OLED and the MPU6050.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setTimeOut(20);
  Wire.setClock(400000);
  display.setBusClock(400000);
  oledUp = oledPresent();
  if (oledUp) display.begin();
  stamp(oledUp ? "OLED found (I2C 0x3C)" : "OLED NOT found");

  mpuInit();
  stamp(mpuOk ? "mpu found (I2C 0x68)" : "mpu NOT found");

  loadStats();
  loadSettings();
  if (oledUp) applyScreen();
  stamp("stats loaded");

  randomSeed(micros());
  uint32_t now = millis();
  nextBlinkAt = now + 3000;
  nextWinkAt = now + 15000;
  lastActivity = lastInteraction = now;
  {
    char mem[96];
    snprintf(mem, sizeof(mem), "flash: app %lu KB, free app space %lu KB; heap free %lu KB (min %lu KB)",
             (unsigned long)(ESP.getSketchSize() / 1024), (unsigned long)(ESP.getFreeSketchSpace() / 1024),
             (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getMinFreeHeap() / 1024));
    stamp(mem);
  }
  stamp("setup done");
}

void loop() {
  static uint32_t lastFrame = 0;
  static uint32_t lastCheck = 0;
  static uint32_t lastSensor = 0;
  static uint32_t lastTick = 0;
  static uint32_t lastStatus = 0;
  static uint32_t lastLoop = millis();
  static bool purring = false;

  uint32_t now = millis();
  float dt = (now - lastLoop) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastLoop = now;

  updateButton(btnA);
  updateButton(btnB);
  updateButton(btnC);
  updateTouch(now);

  bool a = pressed(btnA);
  bool b = pressed(btnB);
  bool c = pressed(btnC);
  bool touchDown = touchDownEvent;
  touchDownEvent = false;
  if (mode != MODE_PET) touchDown = false;   // the pad is ignored outside Pet mode (it misfired)

  if (Serial && (a || b || c)) {
    Serial.printf("BUTTON: A=%d B=%d C=%d mode=%d menu=%d\n",
                  a, b, c, (int)mode, menuIndex);
  }
  // Screen auto-off: any button / touch / movement wakes it, and that first press is not passed on.
  if (screenOff) {
    if (a || b || c || touchDown || now - lastActivity < 300) {
      screenOff = false;
      display.setPowerSave(0);
      a = b = c = touchDown = false;
      lastInteraction = lastActivity = now;
    }
  } else if (setSleep && oledUp && SLEEP_MS[setSleep] && now - lastActivity > SLEEP_MS[setSleep] &&
             mode != MODE_FLUID && mode != MODE_TILT_MAZE && mode != MODE_GAME && mode != MODE_SNAKE && mode != MODE_SPOTIFY && mode != MODE_LYRICS_VISUAL && mode != MODE_LIVING_OLED) {
    screenOff = true;
    display.setPowerSave(1);
  }
  if (a || b || c || touchDown) lastInteraction = lastActivity = now;

  // The touch pad only works in Pet mode (petting). It used to double as Home, but it misfired too often.

  gameTick(now);
  online::tick(now);
  spotify::tick(now);
  for (int t = online::takeTreats(); t > 0; t--) addHappy(1);   // each successful fetch is a small treat

  // Every 2 seconds: recover the MPU and the OLED if either one dropped out.
  if (now - lastCheck >= 2000) {
    lastCheck = now;

    if (!mpuOk) {
      mpuInit();
    } else if (!mpuPresent()) {
      mpuOk = false;
    }

    bool present = oledPresent();
    if (present && !oledUp) {
      display.begin();
      applyScreen();
      screenOff = false;
      oledUp = true;
    } else if (!present) {
      oledUp = false;
    }
  }

  // Motion sensor at 50 Hz.
  if (mpuOk && now - lastSensor >= 20) {
    lastSensor = now;
    if (mpuRead()) {
      mpuFails = 0;
      motionSeq++;
      motionBackground();
      tiltUpdate();

      float amag = sqrtf(motion.ax * motion.ax + motion.ay * motion.ay + motion.az * motion.az);
      float dev = fabsf(amag - 1.0f);
      float spin = fabsf(motion.gx) + fabsf(motion.gy) + fabsf(motion.gz);

      if (dev > 0.12f || spin > 45.0f) lastActivity = now;
      if ((mode == MODE_8BALL || mode == MODE_DICE) && (dev > 0.55f || spin > 260.0f) && now - shakeAt > 700) { shakeAt = now; shaken = true; }

      if (mode == MODE_PET) {
        if (spin > 400.0f) {
          bool alreadyDizzy = (overlay == OV_DIZZY && now < overlayUntil);
          if (!alreadyDizzy) addHappy(-3);   // being shaken is not fun
          trigger(OV_DIZZY, 1800);
        } else if (dev > 0.6f) {
          // Picked up fast, bumped, or dropped.
          bool busy = (now < overlayUntil && overlay == OV_SURPRISED);
          if (!busy) trigger(OV_SURPRISED, 1000);
        }
      }
    } else if (++mpuFails > 10) {
      mpuOk = false;
      mpuFails = 0;
    }
  }

  // Touch petting: tap = love, hold = purr.
  if (mode == MODE_PET) {
    if (touchDown) {
      trigger(OV_LOVE, 1200);
      addHappy(4);
      pet.pets++;
      spawnHeart(now);
      spawnHeart(now);
    }
    if (touchStable && (now - touchDownAt) > PURR_HOLD_MS) {
      if (!purring) {
        purring = true;
        pet.pets++;
      }
      addHappy(10.0f * dt);   // about +10 happiness per second of purring
      trigger(OV_PURR, 600);
    } else {
      purring = false;
    }
  } else {
    purring = false;
  }

  // Slow stats update once a second, flash save every couple of minutes.
  if (now - lastTick >= 1000) {
    lastTick = now;
    statsTick(now);
    saveStats(false);
    if (games::takeBestDirty()) saveBest();
  }

  // Serial diagnostics.
  if (Serial) {
    if (!bootLogPrinted) {
      Serial.print("--- boot log ---\n");
      Serial.print(bootLog);
      bootLogPrinted = true;
    }

    if (now - lastStatus >= 1000) {
      lastStatus = now;
      Serial.printf(
        "btnA=%d btnB=%d btnC=%d touch=%d oled=%d mpu=%d happy=%d energy=%d pets=%lu\n",
        digitalRead(PIN_BTN_A),
        digitalRead(PIN_BTN_B),
        digitalRead(PIN_BTN_C),
        digitalRead(PIN_TOUCH),
        oledUp,
        mpuOk,
        (int)pet.happy,
        (int)pet.energy,
        (unsigned long)pet.pets
      );
    }
  }

  // Input handling.
  if ((mode == MODE_MENU || mode == MODE_TOOLS || mode == MODE_GAMES || mode == MODE_SETTINGS) && now - launcher::lastInputAt() > 20000) {
    menuIndex = 0;                                                     // nobody touches the menu: back to the pet
    enterMode(MODE_PET);
  }
  if (mode == MODE_MENU) {
    bool longB;
    int app = menuPick(now, a, b, c, longB);                           // short B opens the app, hold 0.7 s = back to the pet
    if (longB) {
      menuIndex = 0;
      enterMode(MODE_PET);
    } else if (app >= 0) {
      menuIndex = app + 1;                                             // rootItems index: 1 = Fluid ... 10 = Settings
      submenuIndex = 0;
      if (menuIndex == 1) enterMode(MODE_FLUID);
      else if (menuIndex == 2) enterMode(MODE_STATS);
      else if (menuIndex == 3) enterMode(MODE_MOTION);
      else if (menuIndex == 4) enterMode(MODE_TOOLS);
      else if (menuIndex == 5) enterMode(MODE_GAMES);
      else if (menuIndex == 6) enterMode(MODE_ONLINE);
      else if (menuIndex == 7) enterMode(MODE_SPOTIFY);
      else if (menuIndex == 8) enterMode(MODE_LYRICS_VISUAL);
      else if (menuIndex == 9) enterMode(MODE_LIVING_OLED);
      else enterMode(MODE_SETTINGS);
    }
  } else if (mode == MODE_TOOLS || mode == MODE_GAMES || mode == MODE_SETTINGS) {
    bool longB;
    int pick = menuPick(now, a, b, c, longB);                          // same controls as the home menu; hold B = up one level
    if (mode == MODE_SETTINGS && launcher::selected() != 5) resetArmed = false;
    if (longB) {
      enterMode(MODE_MENU);
    } else if (pick >= 0 && mode == MODE_TOOLS) {
      submenuIndex = pick;
      if (pick == 0) enterMode(MODE_TEMP);
      else if (pick == 1) enterMode(MODE_8BALL);
      else if (pick == 2) enterMode(MODE_DICE);
      else enterMode(MODE_MENU);
    } else if (pick >= 0 && mode == MODE_GAMES) {
      submenuIndex = pick;
      if (pick < games::COUNT) { curGame = (games::Id)pick; enterMode(MODE_GAME); }
      else if (pick == 4) enterMode(MODE_TILT_MAZE);
      else if (pick == 5) enterMode(MODE_SNAKE);
      else enterMode(MODE_MENU);
    } else if (pick >= 0 && mode == MODE_SETTINGS) {
      if (pick == 0) { setBright = (setBright + 1) % 4; applyScreen(); saveSettings(); }
      else if (pick == 1) { setSleep = (setSleep + 1) % 4; saveSettings(); }
      else if (pick == 2) { setFlip ^= 1; applyScreen(); saveSettings(); }
      else if (pick == 3) { setMenu ^= 1; launcher::restyle(setMenu, now); saveSettings(); }
      else if (pick == 4) { setLyric = (setLyric + 1) % LYRIC_STEP_COUNT; spotify::setLyricOffset(LYRIC_STEPS[setLyric]); saveSettings(); }
      else if (pick == 5) {
        if (resetArmed) { resetPet(); resetArmed = false; settingsMsgUntil = millis() + 1500; }
        else resetArmed = true;
      } else enterMode(MODE_MENU);
    }
  } else if (mode == MODE_ONLINE) {
    bool tiltOk = mpuOk && !tilt.calibrating && tilt.autoAxes;
    if (online::input(a, b, c, now, tilt.tx, tilt.ty, tiltOk)) enterMode(MODE_MENU);
  } else if (mode == MODE_SPOTIFY || mode == MODE_LYRICS_VISUAL) {
    // B: short press = play/pause (on release), hold 0.7 s = back to the menu.
    static bool spHeld = false, spLong = false;
    bool bShort = false;
    if (b) { spHeld = true; spLong = false; }
    if (spHeld) {
      if (btnB.stable == LOW) {
        if (!spLong && now - btnB.changedAt >= 700) { spLong = true; menuIndex = (mode == MODE_LYRICS_VISUAL) ? 8 : 7; enterMode(MODE_MENU); }
      } else {
        if (!spLong) bShort = true;
        spHeld = false;
      }
    }
    if (mode == MODE_SPOTIFY || mode == MODE_LYRICS_VISUAL) spotify::input(a, bShort, c, now);
  } else if (mode == MODE_LIVING_OLED) {
    static bool livingHeld = false, livingLong = false;
    bool livingShortB = false;
    if (b) { livingHeld = true; livingLong = false; }
    if (livingHeld) {
      if (btnB.stable == LOW) {
        if (!livingLong && now - btnB.changedAt >= 700) {
          livingLong = true;
          menuIndex = 9;
          enterMode(MODE_MENU);
        }
      } else {
        if (!livingLong) livingShortB = true;
        livingHeld = false;
      }
    }
    if (mode == MODE_LIVING_OLED) living_oled::input(a, livingShortB, c, now);
  } else if (mode == MODE_PET) {
    if (b) enterMode(MODE_MENU);
    if (a) {            // tickle
      trigger(OV_LAUGH, 1600);
      addHappy(3);
    }
    if (c) {            // boop
      trigger(OV_SURPRISED, 800);
      addHappy(1);
    }
  } else if (mode == MODE_TEMP) {
    if (b) enterMode(MODE_TOOLS);
  } else if (mode == MODE_8BALL) {
    if (a || c || shaken) eightBallIndex = random(EIGHT_BALL_COUNT);
    if (b) enterMode(MODE_TOOLS);
  } else if (mode == MODE_DICE) {
    if (a || c || shaken) diceValue = random(1, 7);
    if (b) enterMode(MODE_TOOLS);
  } else if (mode == MODE_SNAKE) {
    if (a) snakeTurn((snakeDir + 3) % 4);
    if (c) snakeTurn((snakeDir + 1) % 4);
    if (b) enterMode(MODE_GAMES);
    if (gameOver && (a || c)) resetSnake();
  } else if (mode == MODE_TILT_MAZE) {
    if (b) enterMode(MODE_GAMES);
    else if (c || (a && mazeWon)) resetMaze();          // new maze
    else if (a) tiltBegin();                             // re-zero: hold the way you like and press A
  } else if (mode == MODE_GAME) {
    if (b) enterMode(MODE_GAMES);
    else {
      games::Input gi = {};
      gi.a = a; gi.c = c;
      gi.aHeld = btnA.stable == LOW; gi.cHeld = btnC.stable == LOW;
      gi.tiltOk = mpuOk;
      gi.calibrating = tilt.calibrating; gi.calib = tilt.samples / 25.0f;
      gi.tx = tilt.tx; gi.ty = tilt.ty;
      gi.shx = tilt.rgx - tilt.sgx; gi.shy = tilt.rgy - tilt.sgy;   // sudden push = a shake
      if (games::update(now, gi) & games::FLAG_REZERO) tiltBegin();  // a new round: calibrate the pose again
    }
  } else if (mode == MODE_FLUID) {
    if (b) enterMode(MODE_MENU);
    else if (a) tiltBegin();                             // re-zero the "level" pose
    else if (c) {                                        // next fluid
      fluidPreset = (fluidPreset + 1) % fluid::PRESET_COUNT;
      fluid::setPreset(fluidPreset);
      fluidLabelUntil = millis() + 1500;
    }
  } else if (b) {
    enterMode(MODE_MENU);
  }
  shaken = false;

  // Draw.
  if (oledUp && !screenOff && now - lastFrame >= FRAME_MS) {
    lastFrame = now;

    display.clearBuffer();

    if (mode == MODE_MENU) {
      launcher::draw(display, now);
    } else if (mode == MODE_PET) {
      drawPet(now);
      online::drawPetBadge(display);   // tiny weather/air/night icons from cached Online data
    } else if (mode == MODE_STATS) {
      drawStats();
    } else if (mode == MODE_MOTION) {
      drawMotion();
    } else if (mode == MODE_TOOLS || mode == MODE_GAMES) {
      launcher::draw(display, now);
    } else if (mode == MODE_SETTINGS) {
      launcher::draw(display, now);
      if (now < settingsMsgUntil) drawBanner("Pet reset", 40);
    } else if (mode == MODE_GAME) {
      games::draw(display, now);
    } else if (mode == MODE_ONLINE) {
      online::draw(display, now);
    } else if (mode == MODE_SPOTIFY) {
      spotify::draw(display, now);
    } else if (mode == MODE_LYRICS_VISUAL) {
      lyrics_visual::draw(display, now);
    } else if (mode == MODE_LIVING_OLED) {
      living_oled::draw(display, now);
    } else if (mode == MODE_TEMP) {
      drawTemperature();
    } else if (mode == MODE_8BALL) {
      draw8Ball();
    } else if (mode == MODE_DICE) {
      drawDice();
    } else if (mode == MODE_SNAKE) {
      drawSnake();
    } else if (mode == MODE_FLUID) {
      drawFluid(now);
    } else if (mode == MODE_TILT_MAZE) {
      drawMaze(now);
    }

    display.sendBuffer();
  }
  delay(1);   // yield the CPU: loop() used to spin flat out and starved the Spotify network task (same priority)
}
