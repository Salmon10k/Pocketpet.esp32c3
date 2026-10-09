# Pocketpet.esp32c3

Keychain-sized digital pet gadget built around an ESP32-C3 Super Mini with a 1.3" SH1106 OLED, MPU6050 motion sensor, touch input and three buttons.

## Current hardware wiring

**Important:** The OLED and MPU6050 are **not on the same I2C bus anymore**.

The ESP32-C3 has one hardware I2C controller, so the current firmware uses:
- **Hardware I2C (`Wire`) for the MPU6050**
- **Software I2C through U8g2 for the OLED**

| Part | Pin | ESP32-C3 GPIO | Notes |
|------|-----|---------------|-------|
| MPU6050 | SDA | **5** | Hardware I2C |
| MPU6050 | SCL | **6** | Hardware I2C |
| OLED SH1106 | SDA | **8** | Software I2C |
| OLED SH1106 | SCL | **9** | Software I2C |
| Button A | one leg to GND | **0** | Up / previous |
| Button B | one leg to GND | **1** | Select / back |
| Button C | one leg to GND | **2** | Down / next |
| Touch pad | OUT | **3** | TTP223-style, HIGH when touched |

### I2C addresses
- OLED: `0x3C`
- MPU6050: `0x68`

### OLED

The current OLED is confirmed working as a **1.3" SH1106 128x64 I2C display**.

A standalone Arduino test on GPIO 8/9 successfully displayed text, confirming the OLED and wiring work.

The actual Pocketpet firmware does **not** use `Wire.begin(8, 9)` for the OLED. It uses U8g2 software I2C because `Wire` is reserved for the MPU6050 on GPIO 5/6.

### Important GPIO note

GPIO 8 and GPIO 9 are ESP32-C3 strapping pins. The OLED has been tested successfully on these pins, but if the board ever becomes unreliable during boot, investigate these pins first.

## Current firmware

The current `src/main.cpp` contains:
- Menu system
- Pet mode
- Motion test mode
- SH1106 OLED graphics
- Animated blinking eyes
- Eyes follow MPU6050 tilt
- Touch makes the pet happy
- Shaking makes the pet dizzy
- MPU6050 acceleration, gyro and temperature readings
- Button debouncing
- MPU6050 failure/recovery handling
- Serial diagnostics

### Current architecture

```text
ESP32-C3
│
├── Hardware I2C / Wire
│   └── MPU6050
│       ├── SDA → GPIO 5
│       └── SCL → GPIO 6
│
└── U8g2 Software I2C
    └── SH1106 OLED
        ├── SDA → GPIO 8
        └── SCL → GPIO 9
```

**Do not change the OLED back to hardware I2C on `Wire` unless the bus architecture is changed deliberately.** The current setup depends on the MPU6050 owning the hardware `Wire` bus.

## Build

1. Install PlatformIO in VS Code.
2. Open this folder.
3. Plug the ESP32-C3 Super Mini in with a data-capable USB cable.
4. Build and upload with PlatformIO.
5. Board environment: `esp32-c3-supermini`.
6. USB CDC on boot is enabled in `platformio.ini`.

## Controls
- Menu: **A = up**, **C = down**, **B = select**
- Pet / Motion test: **B = back**
- Pet reacts to:
  - Tilt → eyes follow movement
  - Touch → happy face
  - Shake → dizzy face

## Files
- `src/main.cpp` → main firmware and pet logic
- `src/config.h` → all GPIO assignments and I2C addresses
- `platformio.ini` → PlatformIO build configuration

When changing wiring, update `src/config.h` first.

## Status
- [x] Repo + starter project
- [x] OLED confirmed working
- [x] OLED confirmed as SH1106
- [x] OLED moved to GPIO 8/9
- [x] MPU6050 kept on GPIO 5/6 hardware I2C
- [x] OLED switched to U8g2 software I2C
- [x] Basic pet UI and motion test
- [ ] Confirm touch pin on final hardware
- [ ] Temperature mode
- [ ] Game mode
- [ ] WiFi features
- [ ] Battery monitoring
- [ ] Deep sleep / wake on touch

## Notes for future development

Before changing the I2C setup, remember:

> **MPU6050 = hardware I2C on GPIO 5/6. OLED = software I2C on GPIO 8/9.**

The OLED originally failed when it shared GPIO 5/6 with the MPU6050 in the physical build. A standalone OLED test on GPIO 8/9 worked, so the firmware was changed to use separate buses.

If another developer or AI agent continues this project, read `src/config.h` and this README before changing pin assignments or the U8g2 constructor.
