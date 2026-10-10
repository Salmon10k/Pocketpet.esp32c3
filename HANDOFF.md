# HANDOFF: read this first if you are an agent (or human) continuing the project

Owner: Salman (GitHub: Salmon10k). Student, new to soldering, builds on a protoboard, flashes with
PlatformIO in VS Code on Windows. He tests on the real device and reports back, so keep instructions short
and concrete. Casual tone is fine and preferred. He is on a free Claude plan with limited usage, which is
why this file exists.

## What this is
Pocketpet: a keychain-sized ESP32-C3 Super Mini gadget with a 1.3" SH1106 128x64 mono OLED, MPU6050
(accel/gyro/temp), a TTP223 touch pad, 3 buttons and a battery. Main feature is an animated pet; more
"modes" (tools, games, WiFi clock) are being added around it.

## Hardware map (source of truth: `src/config.h`)
| Part | GPIO | Notes |
|------|------|-------|
| OLED SDA/SCL | 8 / 9 | HARDWARE I2C (`Wire`), 400 kHz, U8g2 `..._F_HW_I2C` |
| MPU6050 SDA/SCL | 5 / 6 | SOFTWARE I2C (SoftWire), raw register reads |
| Button A / B / C | 0 / 1 / 2 | to GND, internal pull-up, active LOW |
| Touch pad OUT | 4 | HIGH when touched |
| Free ADC pin | 3 | reserved for the battery voltage divider |

Do NOT change the bus layout or pins without asking Salman: the board is soldered, rewiring is painful.
GPIO 8/9 are strapping pins, GPIO 0-5 are the only deep-sleep wake pins on the C3.

## Source layout
- `src/config.h`: pins, addresses, timing and tuning macros.
- `src/main.cpp`: current firmware, hardware init, input handling, MPU, stats, pet animation/drawing, menus and mini-games.
- `src/config.h`: hardware pins and tuning.
- The firmware is currently monolithic in `main.cpp`; do not assume the planned screen-registry/multi-file architecture exists yet.
- `README.md`: user-facing docs. Keep it in sync when behaviour changes.

## Architecture: screens
Current implementation uses a `Mode` enum and dispatches input/drawing in `main.cpp`. The menu tree has root, Tools, Games and Online. Rules:
- Modes draw into the shared `display` buffer; the main loop does `clearBuffer()` / `sendBuffer()`.
- Convention: **B = back/exit**, A and C are actions. The touch pad acts as Home outside Pet mode.
- Anything that must keep running outside its screen (Pomodoro timer, step counting, WiFi state machine)
  has a `xxxTick(now)` function that the main loop calls every iteration.
- Use `notify("text", ms)` for a banner shown over any screen (e.g. "Pomodoro done").
- Never block the loop for long. The only accepted blocking call is the weather HTTP GET (short timeout).
- Pet stats are changed through `petAddHappy()` / `petAddEnergy()`, never directly from other files.

## Conventions
- Companion pet: nothing can die, stats are floored. Do not add punishing mechanics.
- Flash saves go through `Preferences` ("pocketpet" namespace), throttled. Do not write to flash in a loop.
- No secrets in the repo (the repo is public). WiFi credentials and the user's coordinates live in
  `src/secrets.h`, which is gitignored; `src/secrets.example.h` is the template. Never commit `secrets.h`.
- Commit messages in this repo end with the Co-Authored-By / session trailers the harness asks for.
- Memory numbers (build of the first pet version): RAM 4.9%, flash 23.1% of the 1.3 MB app partition.

## Testing (important)
- In the agent sandbox PlatformIO cannot download the ESP32 toolchain, so NOTHING can be compiled for the
  chip there. Run `tools/hostcheck/check.sh` instead: it syntax-checks every `src/*.cpp` against mock Arduino
  headers plus the real U8g2 / SoftWire / ArduinoJson headers. Passing means "no typos / API mismatches",
  not "works on the board".
- Salman builds with PlatformIO (check mark), flashes (arrow) and watches Serial Monitor at 115200.
  Ask him for build output / serial log when something fails. Each batch of work should end with a short
  "what to test on the device" list for him.
- Serial prints a status line every second (buttons, touch, oled, mpu, happy, energy, pets).

## Status
Update this section as you work. Mark things DONE only after the host check passes; mark TESTED only after
Salman confirms on the device.

| Item | State |
|------|-------|
| Pet mode (moods, touch, shake, sleep), Stats, Motion test | DONE and TESTED (Salman: "looks good") |
| I2C swap (OLED hw, MPU soft), touch on GPIO4 | DONE and TESTED |
| Expanded root/Tools/Games/Online menus | DONE (monolithic implementation) |
| Temperature, Magic 8-ball, Dice | DONE and TESTED (Salman confirmed) |
| Games: Snake, Flappy, procedural Tilt Maze, Dino Runner with crouch/obstacle variety | DONE in code, needs Salman hardware confirmation |
| Pomodoro + Stopwatch | TODO (batch 3) |
| Wi-Fi NTP clock | DONE in code, needs local credentials and hardware confirmation |\n| Weather (Open-Meteo), pet sleeps at night | TODO |
| Spirit level, Step counter | TODO (batch 5) |
| BLE shutter remote | SKIPPED by Salman for now (do not build unless asked) |
| Battery monitoring (GPIO3 divider), deep sleep + wake on touch | NOT STARTED, needs hardware first |

## Currently working on
Batch 1 (Temperature, Magic 8-ball, Dice) is confirmed working on hardware. Touch Home and Snake / Flappy / Tilt Maze / Dino Runner were added earlier and still need hardware testing. The Tilt Maze now generates a new solvable maze on entry, uses a 45-degree forward-pitch baseline and reversed left/right movement. Dino Runner now supports jump and crouch with cactus clusters and flying birds. Online now has an optional Wi-Fi NTP clock for Pakistan time; credentials are blank by default in `src/config.h`, so Wi-Fi stays disabled until configured. The host syntax check has not been run for this batch.

## Known caveats / things that will bite you
- Keep the pet animation code unchanged unless Salman explicitly asks. Salman has confirmed the existing pet animations look good.
- MPU6050 temperature is the chip's own die temperature: it reads a few degrees above room temperature.
  `TEMP_OFFSET_C` in `config.h` exists to correct it.
- Tilt direction depends on how the MPU is mounted; `TILT_X_SIGN` / `TILT_Y_SIGN` flip it.
- WiFi adds roughly 300-400 KB flash and 40-60 KB RAM once linked. There is plenty of room (about 1 MB free).
- The weather API (Open-Meteo) needs no key but needs HTTPS; the code uses `setInsecure()` (no cert pinning)
  on purpose to keep it small.
- Windows line endings: git may show CRLF noise. Ignore it, do not "fix" files for it.

## Ideas not yet approved
Reaction test, Simon, Dino runner, Breakout, pet reacting to weather, morning greeting, deep sleep.
