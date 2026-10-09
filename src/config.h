#pragma once

// ---- MPU6050 hardware I2C ----
#define PIN_SDA 5
#define PIN_SCL 6

// ---- OLED software I2C ----
// OLED is on a separate pair because the ESP32-C3 has one hardware I2C controller.
#define PIN_SDA_OLED 8
#define PIN_SCL_OLED 9

// ---- Buttons (wired to GND, internal pull-up used) ----
#define PIN_BTN_A 0   // up / previous
#define PIN_BTN_B 1   // select / back
#define PIN_BTN_C 2   // down / next

// ---- Touch pad (TTP223-style, HIGH when touched) ----
// Not confirmed yet: change this to whatever GPIO the touch pad OUT wire is on.
#define PIN_TOUCH 3

// ---- I2C addresses ----
#define MPU_ADDR 0x68
#define OLED_ADDR 0x3C
