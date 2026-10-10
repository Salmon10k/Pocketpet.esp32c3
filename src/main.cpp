#include <Arduino.h>
#include <Wire.h>
#include <SoftWire.h>
#include <U8g2lib.h>
#include <Preferences.h>
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

// ---------------- Other screens ----------------
enum Mode { MODE_MENU, MODE_PET, MODE_STATS, MODE_MOTION };
Mode mode = MODE_MENU;
int menuIndex = 0;
const char *menuItems[] = {"Pet", "Stats", "Motion test"};
const int MENU_COUNT = 3;

void drawMenu() {
  display.setFont(u8g2_font_7x13B_tf);
  display.drawStr(0, 12, "POCKETPET");
  display.drawHLine(0, 15, 128);

  display.setFont(u8g2_font_7x13_tf);
  for (int i = 0; i < MENU_COUNT; i++) {
    int y = 30 + i * 14;
    if (i == menuIndex) display.drawStr(0, y, ">");
    display.drawStr(14, y, menuItems[i]);
  }

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 63, "A up  B ok  C down");
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

void enterMode(Mode m) {
  if (mode == MODE_PET && m != MODE_PET) saveStats(true);
  mode = m;
  if (m == MODE_PET) {
    uint32_t now = millis();
    lastActivity = now;
    lastInteraction = now;
    overlay = OV_NONE;
  }
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
    if (a) menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
    if (c) menuIndex = (menuIndex + 1) % MENU_COUNT;
    if (b) {
      if (menuIndex == 0) enterMode(MODE_PET);
      else if (menuIndex == 1) enterMode(MODE_STATS);
      else enterMode(MODE_MOTION);
    }
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
    } else {
      drawMotion();
    }

    display.sendBuffer();
  }
}
