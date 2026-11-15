#pragma once

// ---- One shared I2C bus: OLED (0x3C) + MPU6050 (0x68) ----
// Both devices sit on the same hardware I2C bus (Wire) at 400 kHz. 8/9 are also the C3's default I2C pins.
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

// ---- Hardware variant ----
// 0 = the protoboard build (default, PlatformIO env esp32-c3-supermini)
// 1 = the custom PCB v1 (PlatformIO env esp32-c3-pcb), whose buttons are on other pins. See docs/pcb-v1-notes.md.
#ifndef HW_PCB_V1
#define HW_PCB_V1 0
#endif

// ---- Buttons (wired to GND, internal pull-up used) ----
#if HW_PCB_V1
#define PIN_BTN_A 7   // SW1, left: up / previous / tickle (in Pet mode)
#define PIN_BTN_B 5   // SW2, middle: select / back
#define PIN_BTN_C 6   // SW3, right: down / next / boop (in Pet mode)
#else
#define PIN_BTN_A 2   // up / previous / tickle (in Pet mode)
#define PIN_BTN_B 1   // select / back
#define PIN_BTN_C 0   // down / next / boop (in Pet mode)
#endif

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
