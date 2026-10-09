# Pocketpet.esp32c3
Keychain-sized pet gadget on ESP32-C3 Super Mini with OLED, MPU6050 and touch.

## Parts
- ESP32-C3 Super Mini
- 1.3" 4-pin I2C OLED (SH1106 / SSD1306, 128x64)
- MPU6050 gyro + accelerometer
- Touch pad (TTP223-style)
- 3 push buttons
- TP4056 charger + Li-ion battery

## Pin table
| Part | Pin | ESP32-C3 GPIO |
|------|-----|---------------|
| OLED + MPU6050 (shared I2C) | SDA | 5 |
| OLED + MPU6050 (shared I2C) | SCL | 6 |
| Button A (up) | one leg to GND | 0 |
| Button B (select / back) | one leg to GND | 1 |
| Button C (down) | one leg to GND | 2 |
| Touch pad | OUT | 3 (change in `src/config.h`) |

I2C addresses: OLED `0x3C`, MPU6050 `0x68`.

## Build
1. Install PlatformIO in VS Code.
2. Open this folder, plug in the board with a data cable.
3. Build and upload with the PlatformIO toolbar (board: `esp32-c3-devkitm-1`, USB CDC on boot is enabled in `platformio.ini`).

## Controls
- Menu: A up, C down, B select
- Pet / Motion test: B goes back to the menu
- Pet reacts to tilt (eyes follow), touch (happy) and shaking (dizzy)

## Status
- [x] Repo + starter project
- [ ] Verify OLED driver (SH1106 vs SSD1306)
- [ ] Touch pin confirmed
- [ ] Temp mode, game mode, WiFi
- [ ] Battery + deep sleep (wake on touch)
