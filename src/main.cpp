#include <Arduino.h>
#include <Wire.h>
#include <SoftWire.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"

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

const char *rootItems[] = {"Pet", "Stats", "Motion test", "Tools", "Games", "Online"};
const int ROOT_COUNT = 6;
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
    display.drawStr(0, 28, "Set Wi-Fi in src/config.h");
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

// Tilt Maze: procedural perfect maze (DFS), 12 x 4 cells. Each cell stores N/E/S/W walls.
const int MAZE_COLS = 12, MAZE_ROWS = 4, MAZE_CELL = 10;
const uint8_t MW_N = 1, MW_E = 2, MW_S = 4, MW_W = 8;
uint8_t mazeWalls[MAZE_ROWS][MAZE_COLS];
bool mazeVisited[MAZE_ROWS][MAZE_COLS];
float mazeX = 9, mazeY = 23;
bool mazeWon = false;
uint32_t mazeMoves = 0;
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
void resetMaze() { generateMaze(); mazeX = 9; mazeY = 23; mazeWon = false; mazeMoves = 0; }
bool mazeCanEnter(int col, int row, int ncol, int nrow) {
  if (ncol < 0 || ncol >= MAZE_COLS || nrow < 0 || nrow >= MAZE_ROWS) return false;
  if (ncol > col) return !(mazeWalls[row][col] & MW_E);
  if (ncol < col) return !(mazeWalls[row][col] & MW_W);
  if (nrow > row) return !(mazeWalls[row][col] & MW_S);
  if (nrow < row) return !(mazeWalls[row][col] & MW_N);
  return true;
}
void mazeTick() {
  if (mazeWon || !mpuOk) return;
  float roll = atan2f(motion.ay, motion.az) * 57.29578f;
  float pitch = atan2f(motion.ax, sqrtf(motion.ay*motion.ay + motion.az*motion.az)) * 57.29578f;
  float vx = constrain(-roll * 0.055f, -1.25f, 1.25f);
  float vy = constrain((pitch - 45.0f) * 0.055f, -1.25f, 1.25f);
  float nx = constrain(mazeX + vx, 5.0f, 124.0f);
  float ny = constrain(mazeY + vy, 17.0f, 60.0f);
  int col = constrain((int)((mazeX - 4) / MAZE_CELL), 0, MAZE_COLS-1);
  int row = constrain((int)((mazeY - 18) / MAZE_CELL), 0, MAZE_ROWS-1);
  int ncol = constrain((int)((nx - 4) / MAZE_CELL), 0, MAZE_COLS-1);
  int nrow = constrain((int)((ny - 18) / MAZE_CELL), 0, MAZE_ROWS-1);
  if (ncol != col && !mazeCanEnter(col, row, ncol, row)) { nx = mazeX; ncol = col; }
  if (nrow != row && !mazeCanEnter(col, row, col, nrow)) { ny = mazeY; nrow = row; }
  mazeX = nx; mazeY = ny;
  if (ncol != col || nrow != row) ++mazeMoves;
  if (ncol == MAZE_COLS-1 && nrow == MAZE_ROWS-1) mazeWon = true;
}
void drawMaze() {
  display.setFont(u8g2_font_5x7_tf);
  char label[20]; snprintf(label, sizeof(label), "TILT MAZE  %lu", (unsigned long)mazeMoves);
  display.drawStr(0, 7, label);
  display.drawFrame(3, 17, 122, 42);
  for (int r=0; r<MAZE_ROWS; ++r) for (int c=0; c<MAZE_COLS; ++c) {
    int x=4+c*MAZE_CELL, y=18+r*MAZE_CELL; uint8_t w=mazeWalls[r][c];
    if (w & MW_N) display.drawHLine(x, y, MAZE_CELL+1);
    if (w & MW_W) display.drawVLine(x, y, MAZE_CELL+1);
    if (c==MAZE_COLS-1 && (w & MW_E)) display.drawVLine(x+MAZE_CELL, y, MAZE_CELL+1);
    if (r==MAZE_ROWS-1 && (w & MW_S)) display.drawHLine(x, y+MAZE_CELL, MAZE_CELL+1);
  }
  display.drawFrame(109, 48, 9, 9);
  display.drawDisc((int)mazeX, (int)mazeY, 2);
  if (mazeWon) { display.drawBox(35, 27, 58, 15); display.setDrawColor(0); display.drawStr(43, 37, "MAZE CLEAR"); display.setDrawColor(1); }
}
float mazeX = 12, mazeY = 22;
bool mazeWon = false;
void resetMaze() { mazeX = 12; mazeY = 22; mazeWon = false; }
void mazeTick() {
  if (mazeWon) return;
  float nx = mazeX + constrain(motion.ay * 2.4f, -2.0f, 2.0f);
  float ny = mazeY + constrain(motion.ax * 2.4f, -2.0f, 2.0f);
  // Outer boundary and two simple interior walls with gaps.
  nx = constrain(nx, 5.0f, 122.0f); ny = constrain(ny, 18.0f, 58.0f);
  if (nx > 36 && nx < 42 && ny < 48) nx = mazeX;
  if (ny > 34 && ny < 40 && nx > 48 && nx < 102) ny = mazeY;
  if (nx > 78 && nx < 84 && ny > 27) nx = mazeX;
  mazeX = nx; mazeY = ny;
  if (mazeX > 112 && mazeY < 29) mazeWon = true;
}
void drawMaze() {
  display.setFont(u8g2_font_5x7_tf); display.drawStr(0, 7, "TILT MAZE");
  display.drawFrame(1, 12, 126, 50);
  display.drawBox(38, 12, 4, 28);
  display.drawBox(48, 34, 54, 4);
  display.drawBox(80, 38, 4, 24);
  display.drawFrame(108, 15, 13, 13);
  display.drawDisc((int)mazeX, (int)mazeY, 3);
  if (mazeWon) display.drawStr(45, 60, "GOAL!");
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
float dinoY = 49, dinoV = 0;
int cactusX = 120, cactusH = 12;
uint32_t dinoLastTick = 0;
void resetDino() {
  dinoY = 49; dinoV = 0; cactusX = 120; cactusH = random(9, 17);
  gameOver = false; gameScore = 0; dinoLastTick = millis();
}
void dinoTick(uint32_t now) {
  if (gameOver || now - dinoLastTick < 35) return;
  dinoLastTick = now;
  dinoV += 0.22f; dinoY += dinoV;
  if (dinoY > 49) { dinoY = 49; dinoV = 0; }
  int speed = 2 + min(3, (int)(gameScore / 12));
  cactusX -= speed;
  if (cactusX < -5) { cactusX = 128 + random(15, 45); cactusH = random(9, 17); gameScore++; }
  if (cactusX < 27 && cactusX > 8 && dinoY > 49 - cactusH + 5) gameOver = true;
}
void drawDino() {
  char score[16]; snprintf(score, sizeof(score), "DINO %lu", (unsigned long)gameScore);
  display.setFont(u8g2_font_5x7_tf); display.drawStr(0, 7, score);
  display.drawHLine(0, 55, 128);
  int y = (int)dinoY;
  // Tiny pixel T-Rex silhouette.
  display.drawBox(12, y - 10, 10, 8);
  display.drawBox(18, y - 14, 5, 6);
  display.drawPixel(21, y - 12);
  display.drawBox(9, y - 5, 5, 3);
  display.drawBox(14, y - 2, 3, 2);
  display.drawBox(20, y - 2, 3, 2);
  display.drawBox(cactusX, 55 - cactusH, 4, cactusH);
  display.drawBox(cactusX - 3, 47 - cactusH / 2, 3, 4);
  display.drawBox(cactusX + 3, 43 - cactusH / 2, 3, 4);
  if (gameOver) {
    display.drawBox(27, 25, 74, 18);
    display.setDrawColor(0); display.drawStr(37, 37, "GAME OVER"); display.setDrawColor(1);
    display.drawStr(18, 63, "A/C restart  B back");
  }
}

void gameTick(uint32_t now) {
  if (mode == MODE_SNAKE) snakeTick(now);
  else if (mode == MODE_FLAPPY) flappyTick(now);
  else if (mode == MODE_TILT_MAZE) mazeTick();
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
  if (touchDown && mode != MODE_PET && mode != MODE_MENU) {
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
      else if (menuIndex == 1) enterMode(MODE_STATS);
      else if (menuIndex == 2) enterMode(MODE_MOTION);
      else if (menuIndex == 3) {
        submenuIndex = 0;
        enterMode(MODE_TOOLS);
      } else if (menuIndex == 4) {
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
    } else if (mode == MODE_TILT_MAZE) {
      drawMaze();
    } else {
      drawDino();
    }

    display.sendBuffer();
  }
}
