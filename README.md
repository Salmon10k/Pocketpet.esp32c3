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
Pet, Fluid, Stats, Motion test, Tools, Games, Online, Settings (brightness, screen auto-off, flip screen, reset pet). Tools contains Temperature. Games contains Magic 8-ball, Dice, Snake, Flappy, procedurally generated Tilt Maze maps, and an upgraded Dino Runner with jumping, crouching, cactus clusters and flying birds. Online has 12 screens that need the internet (see below). Copy `src/secrets.example.h` to `src/secrets.h` and set `WIFI_SSID` / `WIFI_PASSWORD` locally first. Outside Pet mode, touching the pad returns to the main menu.

### Online mode
Wi-Fi stays **off** everywhere except Online mode. Entering Online connects **once** (up to 3 automatic attempts, then each request is retried up to 3 times for random network hiccups), fetches everything for every screen in one go, and stays connected while you browse. Leaving Online switches the radio off. The list header shows the state: `wifi 2/3`, `sync 4/9`, `online`, `offline`. Every screen shows how old its data is (top right). If a refresh fails you keep the last good data.

Controls: in the list, A/C move and B opens (**Sync now** refetches everything, **Back** leaves). On a screen, **A = refresh this screen** (no reconnect), **C = next screen**, **B = back to the list**, touch = Home.

| Screen | Source (no API keys) | Refreshes |
|--------|----------------------|-----------|
| Clock | NTP (`pool.ntp.org`), keeps ticking offline | synced on connect, resync every 6 h |
| Weather | Open-Meteo | 30 min |
| Forecast | same request as Weather | 30 min |
| Air quality | Open-Meteo air quality (US AQI, PM2.5) | 30 min |
| Currency | open.er-api.com (USD to `CURRENCY_HOME`, EUR, GBP) | 6 h |
| World clocks | computed from NTP time, with DST rules for London, New York, Tokyo, Sydney | no request |
| Random fact | uselessfacts.jsph.pl | once per visit, A = new one |
| Advice | adviceslip.com | once per visit, A = new one |
| Moon phase | calculated from the date (approximate, within about a day) | no request |
| Net status | RSSI, IP, a `generate_204` check with ping time | every 20 s while open |
| ISS tracker | api.open-notify.org (plain HTTP) | every 60 s while open |
| GitHub | api.github.com stars, forks, issues, last push | 10 min |

Location for weather and air quality comes from an IP lookup (ip-api.com) unless you set `LOCATION_LAT` / `LOCATION_LON` in `secrets.h`. See `src/secrets.example.h` for all optional overrides.

**Pet reactions:** when cached data exists, Pet mode shows tiny corner icons: sweat drop (35 C or hotter), umbrella (rain chance 60% or more), cloud (AQI 150 or more), moon (after sunset). Each successful fetch also gives the pet a small happiness bump.

Requests are blocking with a 7 second timeout, so the screen can pause briefly while one runs. A progress frame is drawn first so it never looks frozen. Background refresh while you are in Online: weather and air quality every 30 min, currency 6 h, GitHub 10 min. Net status and ISS only refresh while you are looking at them.

**Connection problems:** many ESP32-C3 Super Mini boards fail to connect at full TX power, so the firmware lowers it to 8.5 dBm by default. If your router is far away and the signal is weak, put `#define WIFI_TX_POWER WIFI_POWER_11dBm` (or `WIFI_POWER_19_5dBm`) in `src/secrets.h`.

### Fluid mode
Particle water simulation that follows tilt. Hold the device like a phone, it calibrates for half a second (that pose counts as level). A = re-zero, C = Water / Honey / Mercury, B = back.

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
2. Open this folder (it will download U8g2, SoftWire, AsyncDelay and ArduinoJson).
3. Plug the ESP32-C3 Super Mini in with a data-capable USB cable.
4. Build and upload with PlatformIO.
5. Board environment: `esp32-c3-supermini`.
6. USB CDC on boot is enabled in `platformio.ini`.
7. `partitions.csv` gives the app a 3 MB slot (no OTA) because Wi-Fi + TLS made the firmware too big for the default 1.3 MB slot.

## Files
- `src/main.cpp` -> main firmware and pet logic
- `src/online.h` / `src/online.cpp` -> Online mode (Wi-Fi state machine, request cache, the 12 screens)
- `src/config.h` -> all GPIO assignments, I2C addresses and pet tuning
- `platformio.ini` -> PlatformIO build configuration
- `partitions.csv` -> flash layout (one 3 MB app slot)

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
- [x] Wi-Fi NTP clock
- [x] Online mode: weather, forecast, AQI, currency, world clocks, fact, advice, moon, net status, ISS, GitHub (in code, host-checked, not yet tested on the device)
- [ ] Pet sleeps at night
- [ ] Battery monitoring (GPIO3 divider)
- [ ] Deep sleep / wake on touch

## Notes for future development

> **OLED = hardware I2C on GPIO 8/9. MPU6050 = software I2C (SoftWire) on GPIO 5/6.**

If another developer or AI agent continues this project, read `src/config.h` and this README before changing pin assignments or the bus setup. The pet logic is separated from the drawing: `currentFace()` decides the mood, `faceParams()` maps a mood to eye/mouth targets, and `drawPet()` eases toward them.


## Work completed today (2026-10-10)

Today’s development session focused on expanding the games and preparing the online clock feature:

- **Tilt Maze:** planned/prototyped a procedural maze system, with randomized maze generation, solvable layouts, reversed left/right tilt behavior, and a 45° forward-facing MPU6050 neutral angle.
- **Dino Runner:** expanded the design with jump/crouch controls, additional obstacle types, flying obstacles, and more polished movement/animation.
- **Online clock:** added the Wi-Fi/NTP clock direction, including local credential handling through the ignored `src/secrets.h` file and an example template at `src/secrets.example.h`.
- **Documentation:** updated the handoff notes and README to record the work from this session.

**Hardware status:** these new changes were not fully hardware-validated today. The existing Batch 1 hardware features remain the known-tested baseline. Do not treat the new maze, Dino upgrades, or Wi-Fi clock as hardware-confirmed until they are built, flashed, and tested on the device.
