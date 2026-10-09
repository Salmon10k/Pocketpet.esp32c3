#pragma once

// ---- I2C (shared by OLED + MPU6050) ----
#define PIN_SDA 5
#define PIN_SCL 6

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
