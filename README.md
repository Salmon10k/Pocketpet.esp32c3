# Pocketpet.esp32c3

Keychain-sized digital pet gadget built around an ESP32-C3 Super Mini with a 1.3" SH1106 OLED, MPU6050 motion sensor, touch input and three buttons.

## Current hardware wiring

**Important:** the OLED and MPU6050 are on **separate I2C buses**, and the roles are:

- **OLED = hardware I2C (`Wire`)**, because it moves a full frame every refresh and needs the speed.
- **MPU6050 = software I2C (SoftWire)**, because it only reads 14 bytes at a time.

| Part | Pin | ESP32-C3 GPIO | Notes |
|------|-----|---------------|-------|
| OLED SH1106 | SDA | **8** | Hardware I2C, 400 kHz |
| OLED SH1106 | SCL | **9** | Hardware I2C, 400 kHz |
| MPU6050 | SDA | **5** | Software I2C (SoftWire) |
| MPU6050 | SCL | **6** | Software I2C (SoftWire) |
| Button A | one leg to GND | **0** | Up / previous / tickle |
| Button B | one leg to GND | **1** | Select / back |
| Button C | one leg to GND | **2** | Down / next / boop |
| Touch pad | OUT | **4** | TTP223-style, HIGH when touched (the pin next to 3V3) |

The physical wiring did not change when the I2C roles were swapped. Only the drivers did.

### I2C addresses
- OLED: `0x3C`
- MPU6050: `0x68`

### GPIO notes
- GPIO 8 and GPIO 9 are ESP32-C3 strapping pins (they are also the C3's default I2C pins). The OLED has been tested on them. If the board ever becomes unreliable during boot, investigate these pins first.
- GPIO3 is the only free ADC1 pin left. Keep it for the battery voltage divider.
- GPIO 0-5 can wake the C3 from deep sleep, so the touch pad (GPIO4) and buttons (0, 1, 2) all work for wake-up later.

## Current firmware

### Menu
Pet, Stats, Motion test, Tools, Games, Online. Tools contains Temperature. Games contains Magic 8-ball, Dice, Snake, Flappy, procedurally generated Tilt Maze maps, and an upgraded Dino Runner with jumping, crouching, cactus clusters and flying birds. Online is a Wi-Fi NTP clock (copy `src/secrets.example.h` to `src/secrets.h` and set `WIFI_SSID` / `WIFI_PASSWORD` locally first). It displays local Pakistan time after syncing. Wi-Fi stays off until you enter Online. Outside Pet mode, touching the pad returns to the main menu.

### Pet mode (companion style, nothing can die)
- Eyes ease smoothly between moods, blink on their own, sometimes double-blink or wink, and wander around when idle.
- Pupils follow tilt from the MPU6050.
- **Moods:** neutral, happy, sad (when ignored), sleepy (low energy or no activity for 30 s), asleep (no activity for 60 s, with floating Zzz).
- **Touch:** tap = love face and hearts. Hold the pad for 0.6 s = purr, which keeps raising happiness.
- **Motion:** shake = dizzy spiral eyes (and a small happiness drop). A sharp pickup, bump or drop = surprised.
- **Buttons in Pet mode:** A = tickle (laughs), C = boop (surprised), B = back to the menu.
- Any touch, button press or movement wakes it up.

### Stats
Happiness and energy are saved to flash (about every 2 minutes when they changed, and when leaving Pet mode), so the pet remembers across reboots. The Stats screen shows both bars and a lifetime pet count.

### Other
- Button debouncing with press events.
- MPU6050 and OLED are re-checked every 2 seconds and recover if they drop out.
- Serial diagnostics once a second (buttons, touch, OLED, MPU, happy, energy, pets).

## Build

1. Install PlatformIO in VS Code.
2. Open this folder (it will download U8g2, SoftWire and AsyncDelay).
3. Plug the ESP32-C3 Super Mini in with a data-capable USB cable.
4. Build and upload with PlatformIO.
5. Board environment: `esp32-c3-supermini`.
6. USB CDC on boot is enabled in `platformio.ini`.

## Files
- `src/main.cpp` -> main firmware and pet logic
- `src/config.h` -> all GPIO assignments, I2C addresses and pet tuning
- `platformio.ini` -> PlatformIO build configuration

When changing wiring, update `src/config.h` first.

## First-flash checklist
- [ ] Builds and uploads
- [ ] OLED shows the menu (serial boot log says "OLED found")
- [ ] Serial shows `mpu=1`
- [ ] `touch=` flips to 1 when the pad is touched
- [ ] Eyes follow tilt the right way (if not, flip `TILT_X_SIGN` / `TILT_Y_SIGN` in `config.h`)
- [ ] Shake gives dizzy eyes, sharp pickup gives surprised eyes

## Status
- [x] Repo + starter project
- [x] OLED confirmed working (SH1106, GPIO 8/9)
- [x] Touch pad soldered on, signal on GPIO4
- [x] I2C roles swapped: OLED on hardware I2C, MPU6050 on SoftWire
- [x] Improved pet mode: moods, eased eyes, touch petting, motion reactions, sleep, hearts, Zzz
- [x] Stats screen with flash-saved happiness and energy
- [x] Verify current firmware on hardware
- [ ] Try the RoboEyes library for the eyes (optional, only if the current look is not liked)
- [x] Temperature mode
- [x] Game mode (Magic 8-ball + Dice)\n- [x] Mini-games (Snake, Flappy, generated Tilt Maze, Dino Runner with jump/crouch and varied obstacles)\n- [x] Touch pad as Home control outside Pet mode\n- [x] Optional Wi-Fi NTP clock (credentials must be configured locally in ignored `src/secrets.h`)
- [x] Wi-Fi NTP clock\n- [ ] Pet sleeps at night
- [ ] Battery monitoring (GPIO3 divider)
- [ ] Deep sleep / wake on touch

## Notes for future development

> **OLED = hardware I2C on GPIO 8/9. MPU6050 = software I2C (SoftWire) on GPIO 5/6.**

If another developer or AI agent continues this project, read `src/config.h` and this README before changing pin assignments or the bus setup. The pet logic is separated from the drawing: `currentFace()` decides the mood, `faceParams()` maps a mood to eye/mouth targets, and `drawPet()` eases toward them.
