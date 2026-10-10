#include <Arduino.h>
#include <Wire.h>
#include <SoftWire.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "fluid.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

// OLED: 1.3" SH1106 on hardware I2C (SDA = GPIO 8, SCL = GPIO 9).
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// MPU6050 on software I2C (SDA = GPIO 5, SCL = GPIO 6).
SoftWire mpuWire(PIN_SDA_MPU, PIN_SCL_MPU);
uint8_t mpuTxBuf[16];
uint8_t mpuRxBuf[32];

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

// ---------------- MPU6050 (software I2C, raw registers) ----------------
struct Motion {
  float ax, ay, az;   // g
  float gx, gy, gz;   // deg/s
  float tempC;
} motion = {0, 0, 1, 0, 0, 0, 0};
bool mpuOk = false;
uint8_t mpuFails = 0;

bool mpuPresent() {
  mpuWire.beginTransmission(MPU_ADDR);
  return mpuWire.endTransmission() == 0;
}

bool mpuWriteReg(uint8_t reg, uint8_t val) {
  mpuWire.beginTransmission(MPU_ADDR);
  mpuWire.write(reg);
  mpuWire.write(val);
  return mpuWire.endTransmission() == 0;
}

void mpuInit() {
  bool ok = mpuWriteReg(0x6B, 0x00);   // PWR_MGMT_1: wake up
  if (ok) mpuWriteReg(0x1A, 0x03);     // CONFIG: ~44 Hz low-pass, calmer readings
  mpuOk = ok;
}

bool mpuRead() {
  mpuWire.beginTransmission(MPU_ADDR);
  mpuWire.write(0x3B);  // ACCEL_XOUT_H
  if (mpuWire.endTransmission(false) != 0) return false;
  if (mpuWire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return false;

  int16_t r[7];
  for (int i = 0; i < 7; i++) {
    uint8_t hi = mpuWire.read();
    uint8_t lo = mpuWire.read();
    r[i] = (int16_t)((hi << 8) | lo);
  }

  motion.ax = r[0] / 16384.0f;
  motion.ay = r[1] / 16384.0f;
  motion.az = r[2] / 16384.0f;
  motion.tempC = r[3] / 340.0f + 36.53f;
  motion.gx = r[4] / 131.0f;
  motion.gy = r[5] / 131.0f;
  motion.gz = r[6] / 131.0f;
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
  float tx = TILT_X_SIGN * constrain((int)(motion.ay * 10), -7, 7);
  float ty = TILT_Y_SIGN * constrain((int)(motion.ax * 10), -7, 7);
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
  MODE_TEMP,
  MODE_8BALL,
  MODE_DICE,
  MODE_SNAKE,
  MODE_FLAPPY,
  MODE_TILT_MAZE,
  MODE_DINO
};

Mode mode = MODE_MENU;

const char *rootItems[] = {"Pet", "Fluid", "Stats", "Motion test", "Tools", "Games", "Online"};
const int ROOT_COUNT = 7;
const char *toolsItems[] = {"Temperature"};
const int TOOLS_COUNT = 1;
const char *gamesItems[] = {"Magic 8-ball", "Dice", "Snake", "Flappy", "Tilt Maze", "Dino Runner"};
const int GAMES_COUNT = 6;
const char *onlineItems[] = {"Wi-Fi clock"};
const int ONLINE_COUNT = 1;

int menuIndex = 0;
int submenuIndex = 0;

const char *eightBallAnswers[] = {
  "Yes.", "No.", "Maybe.", "Definitely!",
  "Ask again.", "Looks good.", "Not likely.", "Absolutely."
};
const int EIGHT_BALL_COUNT = 8;
int eightBallIndex = 0;
int diceValue = 1;

// ---------------- Online clock ----------------
bool clockWifiStarted = false;
uint32_t clockConnectStarted = 0;
void startOnlineClock() {
  if (clockWifiStarted) return;
  clockWifiStarted = true;
  clockConnectStarted = millis();
  if (strlen(WIFI_SSID) == 0) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  setenv("TZ", "PKT-5", 1); tzset();
  configTime(18000, 0, "pool.ntp.org", "time.google.com");
}
void drawOnlineClock() {
  display.setFont(u8g2_font_7x13B_tf);
  display.drawStr(0, 12, "POCKET CLOCK");
  display.drawHLine(0, 15, 128);
  display.setFont(u8g2_font_5x7_tf);
  if (strlen(WIFI_SSID) == 0) {
    display.drawStr(0, 28, "Copy secrets.example.h to secrets.h");
    display.drawStr(0, 40, "SSID + password needed");
    display.drawStr(0, 61, "B back  Touch home");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    display.drawStr(0, 29, "Connecting to Wi-Fi...");
    display.drawStr(0, 42, WiFi.status() == WL_CONNECT_FAILED ? "Connection failed" : "Waiting for network");
    display.drawStr(0, 61, "B back  Touch home");
    return;
  }
  time_t nowTime = time(nullptr);
  if (nowTime < 1700000000) {
    display.drawStr(0, 26, "Wi-Fi connected");
    display.drawStr(0, 38, "Syncing time from NTP...");
  } else {
    struct tm tmNow;
    localtime_r(&nowTime, &tmNow);
    char clockLine[16], dateLine[24];
    strftime(clockLine, sizeof(clockLine), "%H:%M:%S", &tmNow);
    strftime(dateLine, sizeof(dateLine), "%a %d %b %Y", &tmNow);
    display.setFont(u8g2_font_logisoso24_tn);
    display.drawStr(10, 44, clockLine);
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(18, 56, dateLine);
  }
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 63, "NTP  UTC+5  B back");
}

// ---------------- Mini-game state ----------------
// All games are original, compact monochrome adaptations for the 128x64 OLED.
// A/C are game actions; B returns to Games. Touch always returns Home outside Pet.
uint32_t gameLastTick = 0;
bool gameOver = false;
uint32_t gameScore = 0;

// Snake: 8px grid, 16x6 cells.
int snakeX[96], snakeY[96];
int snakeLength = 4, snakeDir = 0, foodX = 10, foodY = 3;
uint32_t snakeLastStep = 0;

void resetSnake() {
  snakeLength = 4; snakeDir = 0; gameOver = false; gameScore = 0;
  for (int i = 0; i < snakeLength; ++i) {
    snakeX[i] = 6 - i; snakeY[i] = 3;
  }
  foodX = random(0, 16); foodY = random(0, 6);
  snakeLastStep = millis();
}

void snakeTick(uint32_t now) {
  if (gameOver || now - snakeLastStep < 170) return;
  snakeLastStep = now;
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
}

// Flappy: tap A or C to flap; pipes scroll automatically.
float birdY = 34, birdV = 0;
int pipeX = 112, pipeGapY = 34;
uint32_t flappyLastTick = 0;

void resetFlappy() {
  birdY = 34; birdV = 0; pipeX = 112; pipeGapY = random(27, 48);
  gameOver = false; gameScore = 0; flappyLastTick = millis();
}
void flappyTick(uint32_t now) {
  if (gameOver || now - flappyLastTick < 35) return;
  flappyLastTick = now;
  birdV += 0.16f; birdY += birdV;
  pipeX -= 2 + (int)(gameScore / 8);
  if (pipeX < -8) { pipeX = 128; pipeGapY = random(25, 48); gameScore++; }
  if (birdY < 13 || birdY > 57) gameOver = true;
  if (pipeX < 34 && pipeX + 8 > 22 &&
      (birdY < pipeGapY - 10 || birdY > pipeGapY + 10)) gameOver = true;
}
void drawFlappy() {
  char score[16]; snprintf(score, sizeof(score), "FLAPPY %lu", (unsigned long)gameScore);
  display.setFont(u8g2_font_5x7_tf); display.drawStr(0, 7, score);
  display.drawHLine(0, 60, 128);
  display.drawBox(pipeX, 12, 8, max(0, pipeGapY - 10 - 12));
  display.drawBox(pipeX, pipeGapY + 10, 8, max(0, 60 - (pipeGapY + 10)));
  display.drawDisc(27, (int)birdY, 4);
  display.drawPixel(29, (int)birdY - 1);
  if (gameOver) {
    display.drawBox(24, 25, 80, 18);
    display.setDrawColor(0); display.drawStr(34, 37, "GAME OVER"); display.setDrawColor(1);
    display.drawStr(18, 61, "A/C restart  B back");
  }
}

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
      float mx = tilt.sx / tilt.samples, my = tilt.sy / tilt.samples, mz = tilt.sz / tilt.samples;
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
  float gx = tilt.gx, gy = tilt.gy;
  if (fluidPreset == fluid::PRESET_SPLASH && !tilt.calibrating) {
    // Splash = water with momentum. Tilting flicks the liquid a bit further than the tilt itself (the change
    // of tilt per frame is added on top), a shake or flick of the gyro stirs it up, and twisting the
    // device in its plane swirls it. Moving the device only adds a small push (rgx - sgx).
    static float lastGx = 0, lastGy = 1;
    float leadX = (tilt.gx - lastGx) * 5.0f, leadY = (tilt.gy - lastGy) * 5.0f;
    lastGx = tilt.gx; lastGy = tilt.gy;
    gx = tilt.gx + constrain(leadX, -1.0f, 1.0f) + 0.5f * (tilt.rgx - tilt.sgx);
    gy = tilt.gy + constrain(leadY, -1.0f, 1.0f) + 0.5f * (tilt.rgy - tilt.sgy);
    float rate = fabsf(motion.gx) + fabsf(motion.gy) + fabsf(motion.gz);   // total rotation speed, deg/s
    if (rate > 120.0f) fluid::agitate((int32_t)constrain((rate - 120.0f) * 0.25f, 0.0f, 60.0f));
    float wz = motion.gz * tilt.zSign;                 // deg/s about the screen normal
    float dw = constrain((wz - tilt.lastWz) * 1.0f, -400.0f, 400.0f);
    tilt.lastWz = wz;
    if (fabsf(dw) > 8.0f) fluid::spin((int32_t)dw);
  }
  gx *= FLUID_FLIP_X;
  gy = (gy - 1.0f) * FLUID_FLIP_Y + 1.0f;   // flip tilt around the neutral pose (straight down)
  fluid::step((int32_t)(gx * 256.0f), (int32_t)(gy * 256.0f));
  fluid::render(display.getBufferPtr());

  if (tilt.calibrating) {
    drawBanner("Hold like a phone...", 16);
  } else if (now < fluidLabelUntil) {
    drawBanner(fluid::presetName(fluidPreset), 16);
  }
}

// Dino Runner: jump, crouch, mixed cactus clusters and low-flying birds.
float dinoY = 49, dinoV = 0;
int cactusX = 120, cactusH = 12, dinoObstacleType = 0;
bool dinoCrouching = false;
uint32_t dinoLastTick = 0;
void resetDino() {
  dinoY = 49; dinoV = 0; cactusX = 120; cactusH = random(9, 17);
  dinoObstacleType = random(0, 3); dinoCrouching = false;
  gameOver = false; gameScore = 0; dinoLastTick = millis();
}
void dinoTick(uint32_t now) {
  if (gameOver || now - dinoLastTick < 32) return;
  dinoLastTick = now; dinoV += 0.24f; dinoY += dinoV;
  if (dinoY > 49) { dinoY = 49; dinoV = 0; }
  int speed = 2 + min(4, (int)(gameScore / 8));
  cactusX -= speed;
  if (cactusX < -14) { cactusX = 128 + random(14, 38); dinoObstacleType = random(0, 3); cactusH = random(9, 18); gameScore++; }
  if (cactusX < 27 && cactusX > 5) {
    if (dinoObstacleType == 2) { if (!dinoCrouching && dinoY > 43) gameOver = true; }
    else if (dinoY > 49 - cactusH + 5) gameOver = true;
    if (dinoObstacleType == 1 && cactusX < 22 && cactusX > 8 && dinoY > 49 - cactusH + 5) gameOver = true;
  }
}
void drawDino() {
  char score[20]; snprintf(score, sizeof(score), "DINO %lu %s", (unsigned long)gameScore, dinoCrouching ? "DUCK" : "");
  display.setFont(u8g2_font_5x7_tf); display.drawStr(0, 7, score); display.drawHLine(0, 55, 128);
  int y = (int)dinoY;
  if (dinoCrouching && y >= 48) {
    display.drawBox(10, 45, 13, 6); display.drawBox(19, 42, 6, 5); display.drawPixel(23, 43);
  } else {
    display.drawBox(12, y - 10, 10, 8); display.drawBox(18, y - 14, 5, 6); display.drawPixel(21, y - 12);
    display.drawBox(9, y - 5, 5, 3); int leg = (millis()/100)%2 ? 1 : -1;
    display.drawLine(15, y-2, 14+leg, y+1); display.drawLine(20, y-2, 21-leg, y+1);
  }
  if (dinoObstacleType == 2) {
    int wing = (millis()/100)%2 ? 2 : -2;
    display.drawLine(cactusX-4, 32, cactusX+7, 32); display.drawLine(cactusX, 32, cactusX+wing, 28);
    display.drawLine(cactusX+2, 32, cactusX+2-wing, 36); display.drawPixel(cactusX+8, 31);
  } else {
    display.drawBox(cactusX, 55-cactusH, 4, cactusH); display.drawBox(cactusX-3, 47-cactusH/2, 3, 4);
    display.drawBox(cactusX+3, 43-cactusH/2, 3, 4);
    if (dinoObstacleType == 1) { display.drawBox(cactusX+6, 55-cactusH+3, 4, cactusH-3); display.drawBox(cactusX+4, 45-cactusH/2, 3, 4); }
  }
  if (gameOver) { display.drawBox(27, 24, 74, 20); display.setDrawColor(0); display.drawStr(37, 36, "GAME OVER"); display.setDrawColor(1); display.setFont(u8g2_font_5x7_tf); display.drawStr(8, 63, "A jump  C duck  B back"); }
}
void gameTick(uint32_t now) {
  if (mode == MODE_SNAKE) snakeTick(now);
  else if (mode == MODE_FLAPPY) flappyTick(now);
  else if (mode == MODE_TILT_MAZE) mazeTick(now);
  else if (mode == MODE_FLUID) tiltUpdate();
  else if (mode == MODE_DINO) dinoTick(now);
}

void drawMenuList(const char *title, const char *items[], int count, int selected) {
  display.setFont(u8g2_font_7x13B_tf);
  display.drawStr(0, 12, title);
  display.drawHLine(0, 15, 128);

  display.setFont(u8g2_font_7x13_tf);

  // Four rows fit comfortably while leaving room for the controls hint.
  const int visible = 3;
  int first = selected - visible + 1;
  if (first < 0) first = 0;
  if (first > count - visible) first = max(0, count - visible);

  for (int row = 0; row < visible && first + row < count; row++) {
    int i = first + row;
    int y = 29 + row * 13;
    if (i == selected) display.drawStr(0, y, ">");
    display.drawStr(14, y, items[i]);
  }

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 63, "A up B ok C dn Touch=Home");
}

void drawMenu() {
  drawMenuList("POCKETPET", rootItems, ROOT_COUNT, menuIndex);
}

void drawToolsMenu() {
  drawMenuList("TOOLS", toolsItems, TOOLS_COUNT, submenuIndex);
}

void drawGamesMenu() {
  drawMenuList("GAMES", gamesItems, GAMES_COUNT, submenuIndex);
}

void drawOnlineMenu() {
  drawMenuList("ONLINE", onlineItems, ONLINE_COUNT, submenuIndex);
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
}

void drawMotion() {
  char line[32];
  display.setFont(u8g2_font_6x10_tf);

  if (!mpuOk) {
    display.drawStr(0, 12, "MPU6050 not found");
    display.drawStr(0, 24, "check SDA5 / SCL6");
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
  display.drawStr(0, 63, "A/C ask again  B back");
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
  display.drawStr(0, 63, "A/C roll  B back");
}

void enterMode(Mode m) {
  if (mode == MODE_PET && m != MODE_PET) saveStats(true);
  mode = m;

  if (m == MODE_PET) {
    uint32_t now = millis();
    lastActivity = now;
    lastInteraction = now;
    overlay = OV_NONE;
  }
  if (m == MODE_ONLINE) startOnlineClock();
  if (m == MODE_8BALL) eightBallIndex = random(EIGHT_BALL_COUNT);
  if (m == MODE_DICE) diceValue = random(1, 7);
  if (m == MODE_SNAKE) resetSnake();
  if (m == MODE_FLAPPY) resetFlappy();
  if (m == MODE_TILT_MAZE) resetMaze();
  if (m == MODE_FLUID) enterFluid();
  if (m == MODE_DINO) resetDino();
}

// ---------------- Arduino ----------------
void setup() {
  stamp("setup start");

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

  // OLED: hardware I2C on GPIO 8/9.
  Wire.begin(PIN_SDA_OLED, PIN_SCL_OLED);
  Wire.setTimeOut(20);
  Wire.setClock(400000);
  display.setBusClock(400000);
  oledUp = oledPresent();
  if (oledUp) display.begin();
  stamp(oledUp ? "OLED found (hardware I2C)" : "OLED NOT found");

  // MPU6050: software I2C on GPIO 5/6.
  mpuWire.setTxBuffer(mpuTxBuf, sizeof(mpuTxBuf));
  mpuWire.setRxBuffer(mpuRxBuf, sizeof(mpuRxBuf));
  mpuWire.setDelay_us(4);
  mpuWire.setTimeout_ms(20);
  mpuWire.begin();
  mpuInit();
  stamp(mpuOk ? "mpu found (software I2C)" : "mpu NOT found");

  loadStats();
  stamp("stats loaded");

  randomSeed(micros());
  uint32_t now = millis();
  nextBlinkAt = now + 3000;
  nextWinkAt = now + 15000;
  lastActivity = lastInteraction = now;
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

  if (Serial && (a || b || c)) {
    Serial.printf("BUTTON: A=%d B=%d C=%d mode=%d menu=%d\n",
                  a, b, c, (int)mode, menuIndex);
  }
  if (a || b || c || touchDown) lastInteraction = lastActivity = now;

  // Touch pad doubles as a Home button outside Pet mode.
  // (Not in Tilt Maze / Fluid: your grip on the back of the device would keep sending you home.)
  if (touchDown && mode != MODE_PET && mode != MODE_MENU && mode != MODE_FLUID && mode != MODE_TILT_MAZE) {
    menuIndex = 0;
    enterMode(MODE_MENU);
  }

  gameTick(now);

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

      float amag = sqrtf(motion.ax * motion.ax + motion.ay * motion.ay + motion.az * motion.az);
      float dev = fabsf(amag - 1.0f);
      float spin = fabsf(motion.gx) + fabsf(motion.gy) + fabsf(motion.gz);

      if (dev > 0.12f || spin > 45.0f) lastActivity = now;

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
  if (mode == MODE_MENU) {
    if (a) menuIndex = (menuIndex + ROOT_COUNT - 1) % ROOT_COUNT;
    if (c) menuIndex = (menuIndex + 1) % ROOT_COUNT;
    if (b) {
      if (menuIndex == 0) enterMode(MODE_PET);
      else if (menuIndex == 1) enterMode(MODE_FLUID);
      else if (menuIndex == 2) enterMode(MODE_STATS);
      else if (menuIndex == 3) enterMode(MODE_MOTION);
      else if (menuIndex == 4) {
        submenuIndex = 0;
        enterMode(MODE_TOOLS);
      } else if (menuIndex == 5) {
        submenuIndex = 0;
        enterMode(MODE_GAMES);
      } else {
        submenuIndex = 0;
        enterMode(MODE_ONLINE);
      }
    }
  } else if (mode == MODE_TOOLS) {
    if (a) submenuIndex = (submenuIndex + TOOLS_COUNT - 1) % TOOLS_COUNT;
    if (c) submenuIndex = (submenuIndex + 1) % TOOLS_COUNT;
    if (b) {
      if (submenuIndex == 0) enterMode(MODE_TEMP);
    }
  } else if (mode == MODE_GAMES) {
    if (a) submenuIndex = (submenuIndex + GAMES_COUNT - 1) % GAMES_COUNT;
    if (c) submenuIndex = (submenuIndex + 1) % GAMES_COUNT;
    if (b) {
      if (submenuIndex == 0) enterMode(MODE_8BALL);
      else if (submenuIndex == 1) enterMode(MODE_DICE);
      else if (submenuIndex == 2) enterMode(MODE_SNAKE);
      else if (submenuIndex == 3) enterMode(MODE_FLAPPY);
      else if (submenuIndex == 4) enterMode(MODE_TILT_MAZE);
      else enterMode(MODE_DINO);
    }
  } else if (mode == MODE_ONLINE) {
    if (b) enterMode(MODE_MENU);
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
  } else if (mode == MODE_8BALL) {
    if (a || c) eightBallIndex = random(EIGHT_BALL_COUNT);
    if (b) enterMode(MODE_GAMES);
  } else if (mode == MODE_DICE) {
    if (a || c) diceValue = random(1, 7);
    if (b) enterMode(MODE_GAMES);
  } else if (mode == MODE_SNAKE) {
    if (a) snakeDir = (snakeDir + 3) % 4;
    if (c) snakeDir = (snakeDir + 1) % 4;
    if (b) enterMode(MODE_GAMES);
    if (gameOver && (a || c)) resetSnake();
  } else if (mode == MODE_FLAPPY) {
    if (b) enterMode(MODE_GAMES);
    else if (a || c) {
      if (gameOver) resetFlappy();
      else birdV = -2.4f;
    }
  } else if (mode == MODE_TILT_MAZE) {
    if (b) enterMode(MODE_GAMES);
    else if (c || (a && mazeWon)) resetMaze();          // new maze
    else if (a) tiltBegin();                             // re-zero: hold the way you like and press A
  } else if (mode == MODE_FLUID) {
    if (b) enterMode(MODE_MENU);
    else if (a) tiltBegin();                             // re-zero the "level" pose
    else if (c) {                                        // next fluid
      fluidPreset = (fluidPreset + 1) % fluid::PRESET_COUNT;
      fluid::setPreset(fluidPreset);
      fluidLabelUntil = millis() + 1500;
    }
  } else if (mode == MODE_DINO) {
    if (b) enterMode(MODE_GAMES);
    else if (gameOver && (a || c)) resetDino();
    else {
      if (a && !dinoCrouching && dinoY >= 48.5f) dinoV = -3.7f;
      if (c && dinoY >= 48.5f) dinoCrouching = !dinoCrouching;
    }
  } else if (b) {
    enterMode(MODE_MENU);
  }

  // Draw.
  if (oledUp && now - lastFrame >= FRAME_MS) {
    lastFrame = now;

    display.clearBuffer();

    if (mode == MODE_MENU) {
      drawMenu();
    } else if (mode == MODE_PET) {
      drawPet(now);
    } else if (mode == MODE_STATS) {
      drawStats();
    } else if (mode == MODE_MOTION) {
      drawMotion();
    } else if (mode == MODE_TOOLS) {
      drawToolsMenu();
    } else if (mode == MODE_GAMES) {
      drawGamesMenu();
    } else if (mode == MODE_ONLINE) {
      drawOnlineClock();
    } else if (mode == MODE_TEMP) {
      drawTemperature();
    } else if (mode == MODE_8BALL) {
      draw8Ball();
    } else if (mode == MODE_DICE) {
      drawDice();
    } else if (mode == MODE_SNAKE) {
      drawSnake();
    } else if (mode == MODE_FLAPPY) {
      drawFlappy();
    } else if (mode == MODE_FLUID) {
      drawFluid(now);
    } else if (mode == MODE_TILT_MAZE) {
      drawMaze(now);
    } else {
      drawDino();
    }

    display.sendBuffer();
  }
}
