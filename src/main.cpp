#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "config.h"

// 1.3" I2C OLED. Most 1.3" modules are SH1106. If yours is SSD1306 (or the
// picture looks shifted by 2 pixels), swap to the SSD1306 line below.
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);

// ---------------- MPU6050 (raw registers, no extra library) ----------------
struct Motion {
  float ax, ay, az;   // g
  float gx, gy, gz;   // deg/s
  float tempC;
} motion;
bool mpuOk = false;

void mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  // PWR_MGMT_1
  Wire.write(0x00);  // wake up
  mpuOk = (Wire.endTransmission() == 0);
}

bool mpuRead() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU_ADDR, 14) != 14) return false;
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
  motion.gx = r[4] / 131.0f;
  motion.gy = r[5] / 131.0f;
  motion.gz = r[6] / 131.0f;
  return true;
}

// ---------------- Buttons ----------------
struct Button {
  uint8_t pin;
  bool lastRaw;
  bool stable;
  uint32_t changedAt;
};
Button btnA = {PIN_BTN_A, true, true, 0};
Button btnB = {PIN_BTN_B, true, true, 0};
Button btnC = {PIN_BTN_C, true, true, 0};

// returns true once on each press (active low)
bool pressed(Button &b) {
  bool raw = digitalRead(b.pin);
  uint32_t now = millis();
  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.changedAt = now;
  }
  if (now - b.changedAt > 25 && raw != b.stable) {
    b.stable = raw;
    if (!b.stable) return true;
  }
  return false;
}

// ---------------- App state ----------------
enum Mode { MODE_MENU, MODE_PET, MODE_MOTION };
Mode mode = MODE_MENU;
int menuIndex = 0;
const char *menuItems[] = {"Pet", "Motion test"};
const int MENU_COUNT = 2;

uint32_t happyUntil = 0, dizzyUntil = 0;
uint32_t nextBlinkAt = 0, blinkUntil = 0;

// ---------------- Drawing ----------------
void drawMenu() {
  display.setFont(u8g2_font_7x13B_tf);
  display.drawStr(0, 12, "POCKETPET");
  display.drawHLine(0, 15, 128);
  display.setFont(u8g2_font_7x13_tf);
  for (int i = 0; i < MENU_COUNT; i++) {
    int y = 32 + i * 16;
    if (i == menuIndex) display.drawStr(0, y, ">");
    display.drawStr(14, y, menuItems[i]);
  }
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 63, "A up  B ok  C down");
}

void drawEye(int cx, int cy, int px, int py, bool blink, bool happy, bool dizzy) {
  if (dizzy) {
    display.drawLine(cx - 8, cy - 8, cx + 8, cy + 8);
    display.drawLine(cx - 8, cy + 8, cx + 8, cy - 8);
    return;
  }
  if (happy) {
    display.drawLine(cx - 10, cy + 4, cx, cy - 6);
    display.drawLine(cx, cy - 6, cx + 10, cy + 4);
    return;
  }
  if (blink) {
    display.drawHLine(cx - 12, cy, 24);
    return;
  }
  display.setDrawColor(1);
  display.drawDisc(cx, cy, 13);
  display.setDrawColor(0);
  display.drawDisc(cx + px, cy + py, 6);
  display.setDrawColor(1);
}

void drawPet() {
  uint32_t now = millis();
  bool happy = now < happyUntil;
  bool dizzy = now < dizzyUntil;

  // blink timing
  if (now >= nextBlinkAt) {
    blinkUntil = now + 150;
    nextBlinkAt = now + random(2500, 5500);
  }
  bool blink = now < blinkUntil;

  // pupils follow tilt
  int px = constrain((int)(motion.ay * 10), -6, 6);
  int py = constrain((int)(motion.ax * 10), -6, 6);

  drawEye(40, 26, px, py, blink, happy, dizzy);
  drawEye(88, 26, px, py, blink, happy, dizzy);

  // mouth
  if (dizzy) {
    display.drawCircle(64, 52, 4);
  } else if (happy) {
    display.drawLine(52, 46, 64, 56);
    display.drawLine(64, 56, 76, 46);
  } else {
    display.drawHLine(54, 52, 20);
  }
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
  snprintf(line, sizeof(line), "temp %.1f C", motion.tempC);
  display.drawStr(0, 54, line);
}

// ---------------- Arduino ----------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  pinMode(PIN_BTN_C, INPUT_PULLUP);
  pinMode(PIN_TOUCH, INPUT);

  display.begin();          // also starts Wire on SDA/SCL from the constructor
  Wire.setClock(400000);
  mpuInit();
  randomSeed(micros());
  nextBlinkAt = millis() + 3000;

  Serial.printf("MPU6050: %s\n", mpuOk ? "ok" : "NOT FOUND");
}

void loop() {
  static uint32_t lastFrame = 0;
  static bool lastTouch = false;
  uint32_t now = millis();

  bool a = pressed(btnA), b = pressed(btnB), c = pressed(btnC);

  if (mpuOk) mpuRead();

  // touch -> happy
  bool touch = digitalRead(PIN_TOUCH) == HIGH;
  if (touch && !lastTouch && mode == MODE_PET) happyUntil = now + 1500;
  lastTouch = touch;

  // shake -> dizzy
  float spin = fabsf(motion.gx) + fabsf(motion.gy) + fabsf(motion.gz);
  if (mode == MODE_PET && spin > 400) dizzyUntil = now + 1500;

  // input handling
  if (mode == MODE_MENU) {
    if (a) menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
    if (c) menuIndex = (menuIndex + 1) % MENU_COUNT;
    if (b) mode = (menuIndex == 0) ? MODE_PET : MODE_MOTION;
  } else if (b) {
    mode = MODE_MENU;  // B = back
  }

  // draw ~30 fps
  if (now - lastFrame >= 33) {
    lastFrame = now;
    display.clearBuffer();
    if (mode == MODE_MENU) drawMenu();
    else if (mode == MODE_PET) drawPet();
    else drawMotion();
    display.sendBuffer();
  }
}
