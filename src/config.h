#pragma once

// ---- OLED: hardware I2C (Wire) ----
// The OLED moves the most data (a full frame every refresh), so it gets the
// real hardware I2C controller at 400 kHz. 8/9 are also the C3's default I2C pins.
#define PIN_SDA_OLED 8
#define PIN_SCL_OLED 9

// ---- MPU6050: software I2C (SoftWire) ----
// The MPU only reads 14 bytes at a time, so bit-banged I2C is plenty.
#define PIN_SDA_MPU 5
#define PIN_SCL_MPU 6

// ---- Buttons (wired to GND, internal pull-up used) ----
#define PIN_BTN_A 0   // up / previous / tickle (in Pet mode)
#define PIN_BTN_B 1   // select / back
#define PIN_BTN_C 2   // down / next / boop (in Pet mode)

// ---- Touch pad (TTP223-style, HIGH when touched) ----
// Confirmed on the final board: the pin next to 3V3 on the Super Mini.
#define PIN_TOUCH 4

// ---- I2C addresses ----
#define MPU_ADDR 0x68
#define OLED_ADDR 0x3C

// ---- Pet tuning ----
#define FRAME_MS        40      // ~25 FPS target
#define IDLE_SLEEPY_MS  30000   // no activity this long -> sleepy
#define IDLE_SLEEP_MS   60000   // no activity this long -> asleep
#define PURR_HOLD_MS    600     // hold the touch pad this long to purr

// If the eyes look the wrong way when you tilt the keychain, flip these (1 or -1).
#define TILT_X_SIGN  1
#define TILT_Y_SIGN  1
