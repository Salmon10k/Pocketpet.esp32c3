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
- `src/online.h` / `src/online.cpp`: the whole Online mode. Own state (Wi-Fi state machine, per-request cache, 12 screens). main.cpp only calls `online::enter/leave/tick/input/draw/drawPetBadge/takeTreats`. See "Online mode" below.
- `src/spotify.h` / `src/spotify.cpp`: Spotify mode (MODE_SPOTIFY, root menu index 7). Borrows the radio from online.cpp through `online::wifiAcquire/wifiRelease/wifiUp/wifiStatusText`; online's tick drives the connection while `wifiHold` is set, but runs no jobs unless `session` (Online mode) is open.
- `tools/spotify_auth.py`: PKCE login helper run on the PC, writes SPOTIFY_CLIENT_ID / SPOTIFY_REFRESH_TOKEN into secrets.h.
- `src/main.cpp`: current firmware, hardware init, input handling, MPU, stats, pet animation/drawing, menus and mini-games, shared tilt helper, Tilt Maze, Fluid glue.
- `src/fluid.h` / `src/fluid.cpp`: fixed-point particle fluid (no Arduino deps, also compiled on PC by `tools/fluidtest/`).
- `tools/fluidtest/`: host harness (`fluidtest.cpp` + `render.py` PNG contact sheets) for tuning the fluid without hardware.
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
- Memory numbers: the first pet version was RAM 4.9%, flash 23.1% of the default 1.3 MB app slot. After Online mode (Wi-Fi + TLS + ArduinoJson) Salman reported about 21% flash left, i.e. roughly 79% used of the 1.3 MB slot. So `platformio.ini` now uses `partitions.csv` (one 3 MB app slot, no OTA, nvs at the same address so saved stats survive). The first upload after that change rewrites the partition table. Serial prints one `flash: app ... heap free ...` line in the boot log and a heap line after each Online sync: ask Salman for those numbers before deciding to cut features.

## Testing (important)
- In the agent sandbox PlatformIO cannot download the ESP32 toolchain, so NOTHING can be compiled for the
  chip there. Run `tools/hostcheck/check.sh` instead (it has mock WiFi.h too): it syntax-checks every `src/*.cpp` against mock Arduino
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
| Fluid main mode (root menu item 2), 3 presets | DONE in code + host-checked + host-rendered; needs Salman hardware confirmation (FPS, tilt feel) |
| Tilt Maze rewrite (fixed 20 ms physics, shared tilt helper) | DONE in code + host-checked; needs hardware confirmation |
| Pomodoro + Stopwatch | TODO (batch 3) |
| Online mode (12 screens: Clock, Weather, Forecast, Air quality, Currency, World clocks, Fact, Advice, Moon, Net status, ISS, GitHub) | DONE in code + host-checked; needs Salman hardware confirmation (never compiled for the chip) |
| Pet corner badges from Online data (hot, rain, smog, night) | DONE in code, needs hardware confirmation |
| Pet sleeps at night | TODO (Online already stores `is_day`, see `online::petHint()`) |
| Spirit level, Step counter | TODO (batch 5) |
| BLE shutter remote | SKIPPED by Salman for now (do not build unless asked) |
| Battery monitoring (GPIO3 divider), deep sleep + wake on touch | NOT STARTED, needs hardware first |

## Currently working on
Online mode just landed (host syntax check passes, nothing compiled for the chip or flashed). Next: Salman flashes it and reports, then Pomodoro/Stopwatch, Spirit level, Step counter.

### Online mode (online.cpp)
- **Items vs jobs.** 12 screens ("items") map to 10 network "jobs" (geo, weather, aqi, currency, fact, advice, net, iss, crew, github). Forecast reuses the weather job; Clock/World/Moon only need NTP time.
- **Radio policy: ONE session per visit.** `enter()` queues every job (`queueSync`) and connects once; `leave()` switches the radio off. Nothing else touches Wi-Fi, and Pet/other modes never turn it on. Networks come from `WIFI_NETWORKS` (several) or `WIFI_SSID/WIFI_PASSWORD` (one) in secrets.h: with several, an async scan runs first (`W_SCAN`) and `buildOrder()` sorts the known ones by signal; each network gets 2 attempts (min 3 total, max 8). Connect attempts are 10 s each with 1 s backoff, `WiFi.setTxPower(WIFI_TX_POWER)` defaults to 8.5 dBm (known fix for flaky C3 Super Mini antennas, overridable in secrets.h). Link drop while in Online reconnects by itself and keeps the queue. `tick()` runs at most one request per call, only after a frame was drawn; transient failures (HTTP <= 0 or >= 500, bad JSON) retry up to 3 times. After the initial sync, `autoRefresh()` re-queues stale jobs every 3 s (Net/ISS only while that screen is open; a failed refresh waits 60 s). Do not add per-screen `WiFi.begin()` calls.
- **Time.** System time is UTC (`configTime(0,0,...)`), then `TZ` is set from `LOCAL_TZ` (default `PKT-5`). Clock keeps ticking with the radio off. World clocks compute offsets with their own DST rules (EU, US, AU). Do not call `configTime` elsewhere with an offset, it resets TZ.
- **Cache.** Each job has `at/has/err/http`. Failed refresh keeps old data and shows `!` in the header tag. Intervals live in `JOB_EVERY[]`.
- **Location.** `LOCATION_LAT/LON/NAME` in secrets.h, else one IP lookup per boot (ip-api.com, HTTP). No city or coordinates are committed.
- **Controls.** List: A/C move, B opens; extra rows "Sync now" (refetch everything) and "Back". Inside a screen: A = refresh that screen's jobs (ignored within 1.5 s of the last one), C = next screen, B = back to list, touch = Home (existing global rule).
- **Pet hook.** `online::petHint()` reads cached data only; `drawPetBadge()` is drawn after `drawPet()` in main.cpp, `drawPet()` itself is untouched. `takeTreats()` gives +1 happiness per completed sync.
- **Known approximations.** Moon phase uses the mean synodic month (can be off by up to about a day). `setInsecure()` TLS. ISS/crew use open-notify (HTTP only, free service, may be down; failure just shows "Couldn't load").

### What Salman should test on the device (Online)
1. Build with PlatformIO (it will download ArduinoJson). Send me the build output if it fails; I could not compile for the chip.
2. Copy `src/secrets.example.h` to `src/secrets.h`, set SSID + password, flash.
3. Online: the list header should go `wifi 1/3` > `sync n/9` > `online`, all with ONE connection. Clock shows PKT time.
4. Weather / Forecast / Air quality: city comes from the IP lookup, values look sane. Press A to refresh.
5. Currency, World clocks (compare with a phone), Moon, Net status (RSSI, IP, ping), ISS dot moves between refreshes, GitHub shows your repo.
6. Fact / Advice: A gives a new one, long ones scroll.
7. With Wi-Fi off or a wrong password: a screen should say "Couldn't load / No Wi-Fi", not freeze.
8. Pet mode after fetching weather: no icon is normal unless it is 35 C+, rainy, AQI 150+ or night.
9. Free heap / flash: check the PlatformIO memory summary after the build and tell the next agent.

### Spotify lyric stutter fix (2026-10-11; not yet hardware-validated)

**Goal:** keep lyric sliding/highlighting animated while Spotify or LRCLIB HTTPS requests are slow. The animation and its drawing algorithm were not changed.

**Exact files changed for this fix**
- `src/spotify.cpp`
  - Added a persistent FreeRTOS task named `spotifyNet` with a 16 KB stack.
  - `spotify::tick()` now queues one of four network jobs: login, now-playing poll, lyric lookup, or playback command. Those HTTPS calls run on `spotifyNet`, not the Arduino loop that draws the OLED.
  - Added a recursive mutex around short shared-state operations and the Spotify draw/input functions. The mutex is not held during HTTP requests.
  - Added a session-generation counter so responses that finish after leaving/re-entering Spotify are discarded.
  - The TLS client is now owned/cleaned up by the worker. `leave()` no longer calls `sec.stop()` while a request could be using the client.
  - Existing lyric animations, timings, fonts, line parsing, poll intervals, credentials, controls, and network endpoints were left as they were.
- `tools/hostcheck/freertos/FreeRTOS.h`, `task.h`, `semphr.h`
  - Added syntax-only mocks for the task and recursive-mutex APIs. They are not real FreeRTOS implementations and do not test scheduling.
- `HANDOFF.md`
  - This rollback/change note only.

**Not changed:** `src/main.cpp`, `src/online.cpp`, `src/spotify.h`, `src/config.h`, README, pin assignments, pet animation code, or secrets.

**Validation status:** source changes are committed, but a PlatformIO ESP32-C3 build and on-device playback test have **not** been run by the agent. Host syntax checks are useful but cannot confirm task stack sizing, heap headroom, Wi-Fi behavior, or race-free playback on hardware. First test: build with PlatformIO, flash, then watch whether lyrics keep animating during a poll and during a track change. If the screen says `Network task unavailable`, the worker task or mutex could not be created.

**Commits in this change**
- Spotify worker/state-locking implementation: `aeea5775ee4005358710d89c6ea9d4e02b13987f`
- Host-check mock headers: `f4208e5f4561731020b57d1535936d9882cb83d3`, `e96ad10dbbe40e473b74e79b94f6b71aff11e7eb`, `0496876a6007b276334382a86e0ff30719d19704`
- Pre-change rollback point: `d94f61cb9bf372f5fc9533e8a86e69632073006f`

**Rollback:** use `git revert --no-edit` on the commits above in reverse order, including the HANDOFF note commit once its SHA is recorded in the commit history. The simplest clean reference for the original animated-lyrics code is `d94f61cb9bf372f5fc9533e8a86e69632073006f`. Do not reset/force-push if there are later commits you want to keep.

### Spotify visual polish V2 (2026-10-11; review on hardware)

**Goal:** give the 128x64 Spotify lyrics screen a calmer, more intentional hierarchy without changing its signature animated transitions.

**Exact change**
- File changed: `src/spotify.cpp` only for the implementation.
- In `drawBig()`, removed the dotted/solid underline drawn under every lyric row. Word-by-word inverted highlighting remains unchanged, reducing visual clutter.
- Added `drawNextPreview()`: upcoming lyric now has a small `NEXT` label, with text centered in a reserved right-hand area.
- In `drawLocked()`, added a thin divider under the header and a divider above the upcoming-line preview.
- Reworked the overall track progress line into a dotted unplayed rail, solid elapsed segment, and small vertical playhead.
- No changes to HTTP/network worker, LRC parsing/timing, animation durations, lyrics fonts, input controls, `main.cpp`, `online.cpp`, hardware configuration, pet animations, or games.

**Commit**
- Visual polish: `55e2b7309434ca76495d61d5789ac503f6253267`
- Pre-polish rollback point: `cb49a9592411ab5c74fb525116a606deed187798`

**Validation:** no PlatformIO build or hardware check has been run for this visual-only change yet. Build and flash, then inspect short lyrics, long lyrics (2-3 rows), instrumental gaps, and the next-line preview. Confirm the new `NEXT` label does not overlap the incoming lyric during slide transitions and the bottom playhead is visible but not distracting.

**Rollback if the appearance is worse:** revert `55e2b7309434ca76495d61d5789ac503f6253267` to restore the prior Spotify visuals while keeping the earlier non-blocking network worker. The following HANDOFF-only commit documents this change and can also be reverted; do not revert the network-worker commits unless intentionally rolling back the stutter fix too.

### Spotify mode (spotify.cpp)
- Auth: refresh token (from secrets.h, rotated copy kept in NVS namespace `spotify`, ignored when secrets.h holds a different token) -> access token (1 h) refreshed quietly. 400/401 on refresh = "Login expired".
- Polls `GET /v1/me/player/currently-playing` every 4 s (8 s paused), interpolates the progress between polls. Commands (play/pause/next/previous) are blocking requests run from tick() after one frame is drawn; the pause button flips the UI optimistically and undoes it on failure. 403 = no Premium, 404 = no active device.
- Lyrics: LRCLIB `/api/get` (exact title+album+duration, then cleaned title without album). Parsed LRC lives in static arrays (160 lines, 7 KB pool), converted from UTF-8 to Latin-1 for the `_tf` fonts. Fetch happens once per track; network errors retry up to 3 times, 404 = "no synced lyrics".
- One reused `WiFiClientSecure`; any non-200 answer closes the connection. Requests are refused below 55 KB free heap.
- Loop task stack raised to 16 KB with `SET_LOOP_TASK_STACK_SIZE` (guarded by `#if defined`).
- Host check covers syntax; the pure helpers (toLatin1, urlEncode, cleanTitle, parseLrc, wrapText) were tested on the PC by extracting them. NOT tested on hardware or against the live Spotify API: HTTPClient keep-alive, the 204 handling and the Content-Length: 0 on PUT/POST are the first things to check if something fails.

### What Salman should test on the device (Spotify)
1. Do the one-time setup in README ("Spotify mode"), build, flash.
2. Menu > Spotify: `wifi` > `logging in` > now playing within a few seconds, with Spotify playing on his phone.
3. B pauses and resumes, A/C change songs, the progress bar follows.
4. Lyrics highlight the right line; compare timing with the Spotify app. Tell me if they are early/late (`LYRIC_LEAD_MS`).
5. Free account or no device: should show "needs Premium" / "open Spotify on a device", not freeze.
6. Send the serial log if a request fails (HTTP codes are not logged yet, add if needed).

### Tilt helper (main.cpp, `tiltBegin()` / `tiltUpdate()`)
On entering Tilt Maze or Fluid it averages 25 MPU samples ("Hold like a phone..." banner) and derives screen-down/right axes from gravity itself, so it works for any MPU mounting. Neutral pose = however a phone is held. Maze uses relative tilt (`tilt.tx/ty`), Fluid uses normalised in-plane gravity (`tilt.gx/gy`, neutral = (0,1)). If the device is held nearly flat it falls back to `TILT_X_SIGN/TILT_Y_SIGN`. Button A in both modes re-zeroes.

### Fluid (fluid.cpp)
Clavet double-density relaxation, all Q8 integer maths (ESP32-C3 has no FPU, avoid floats). Neighbour grid, 3 presets (Water/Honey/Mercury; C cycles). Each preset has a `gscale` (honey 0.35x, mercury 1.25x). Renders a metaball field straight into the U8g2 page buffer. Use `rz()` (round toward zero) for signed shifts, plain `>>` biases and made the surface slope. Tune with `N_PARTICLES` / `SUBSTEPS`; if FPS is low on the C3, lower those first. Host measured about 0.2 ms/frame, C3 cost is UNMEASURED.

### Catch Water (Games menu)
Tilt-controlled glass under a wandering tap, 20 s round, ends with % of drops caught. Same tilt helper/fixed 20 ms step as the maze. A re-zero, A/C again when finished, B back.

### Maze
12x4 cells, fixed 20 ms physics step (was loop-rate dependent), circle-vs-wall-rect collision. B back, A re-zero, C (or A after winning) new maze.

### Other behaviour notes
The touch pad is Pet-mode only (as Home it misfired). Spotify mode: hold B 0.7 s to go back; B short press acts on release. Root menu order: Pet, Fluid, Stats, Motion test, Tools, Games, Online, Settings.

### Settings (MODE_SETTINGS)
Brightness (contrast 8/60/150/255), Screen auto-off (Off/30 s/1 min/5 min; wakes on any button/touch/movement and swallows that first press; disabled in Fluid/Maze/Catch), Flip screen (180 deg, `setFlipMode`), Reset pet (B twice), Back. Stored in NVS keys `bright`/`sleep`/`flip`; `applyScreen()` must be called after every `display.begin()`. Tilt calibration is deliberately NOT here: each tilt mode re-zeroes with A.

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


### Lyrics Visual mode (2026-10-11; standalone Reel-inspired mode)

**Goal:** add a completely separate Spotify lyrics-only visual mode inspired by the uploaded Reel. The existing Spotify mode is frozen and was not modified.

**Exact files changed**
- `src/lyrics_visual.cpp`
  - New standalone Spotify/LRCLIB client and nonblocking FreeRTOS network worker.
  - Reuses the existing Spotify credentials/NVS refresh-token convention, but keeps its own state.
  - Polls the currently playing track, fetches synced LRCLIB lyrics, and estimates word timing by dividing each LRC line duration across its words.
  - Draws only the lyric word plus procedural monochrome geometric art: boxed word, diamonds, rings, perspective lines, crossing geometry and small orbit dots.
  - Four visual patterns rotate by lyric/word index.
  - No title, artist, progress bar, or normal Spotify controls are drawn.
  - B exits the mode through the main menu; the mode itself does not modify playback.
- `src/lyrics_visual.h`
  - New standalone mode API.
- `src/main.cpp`
  - Adds a new root item, `Lyrics Visual`, as its own mode.
  - Calls the new module's enter/tick/draw lifecycle.
  - Keeps screen auto-off disabled while the new mode is open.
  - Existing `MODE_SPOTIFY` dispatch and Spotify source files are unchanged.

**Important boundary:** `src/spotify.cpp` and `src/spotify.h` were not changed for this feature. The existing animated Spotify lyrics screen remains untouched.

**Current implementation status:** code is committed on branch `lyrics-visual-mode`. PlatformIO ESP32-C3 build and hardware validation have not been run by the agent. The host-check script was not executable in this environment, so this needs a PlatformIO build before treating it as DONE.

**First device test**
1. Build/flash from branch `lyrics-visual-mode`.
2. Root menu -> Lyrics Visual.
3. Start a Spotify song with synced LRCLIB lyrics.
4. Confirm the screen shows one large lyric word at a time with changing geometric art and no normal Spotify UI.
5. Confirm the word sequence follows the song reasonably closely.
6. Press B and confirm it returns to the root menu and Wi-Fi is released.
7. Then open the normal Spotify mode and verify its existing animated lyrics screen is unchanged.

**Rollback:** the clean pre-feature point is `09e46003c05f1fab6c246a077956230148d06b3b`. Reverting the three feature commits on `lyrics-visual-mode` restores the previous firmware. Do not revert the earlier Spotify worker/visual commits.

