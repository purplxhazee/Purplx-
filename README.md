# Purplx — Cyberdeck OS for M5Stack Cardputer ADV

A full-featured cyberdeck firmware for the **M5Stack Cardputer ADV** (ESP32-S3).
Not a hacking tool — a cyberpunk *operating system*: 10 games, off-grid survival
guides, EDC utilities, a WiFi CSI human-presence radar, music player, virtual pet,
an ethical-hacking learn library, and a built-in firmware launcher that lets you
boot Marauder / Bruce / any `.bin` and return to Purplx with one button press.

---

## Hardware

| Required | Optional |
|---|---|
| M5Stack Cardputer ADV (ESP32-S3, 8 MB flash) | ILI9341 320×240 external display (SPI) |
| USB-C cable for flashing | MicroSD card (FAT32, ≤ 32 GB) |

The Cardputer's built-in ST7789 135×240 screen is always used as a HUD / context panel.
The ILI9341 is the "big screen" — most apps render their main view there. Everything
still works without it; the HUD screen just shows whatever fits.

### ILI9341 Wiring

| ILI9341 pin | Cardputer ADV GPIO |
|---|---|
| CLK / SCK | GPIO 40 (shared bus clock) |
| SDI / MOSI | **GPIO 14** (ILI9341-only) |
| DC / RS | GPIO 6 |
| CS | GPIO 5 |
| RST | GPIO 3 |
| LED / BL | 3.3 V direct (always on) |
| MISO / SDO | — leave unconnected — |

> **Note:** GPIO 3 / 5 / 6 conflict with CapLoRa GPS. If you use a Grove GPS
> module it's fine (uses GPIO 1/2). If you use CapLoRa GPS (GPIO 15/13) it's
> also fine. Standard CapLoRa SX1262 radio only conflicts with GPS on the same module.

---

## Features

### Tools

#### CSI Radar — passive WiFi human-presence detector
Uses the ESP32-S3 Channel State Information (CSI) API to detect people by watching
how ambient WiFi signals are disrupted. No camera, no sound, no active transmission.

- **Big screen:** live waterfall heatmap (subcarrier amplitudes over time) + amplitude line graph
- **HUD:** motion banner, activity score, active subcarriers, packet count, RSSI, MAC
- Configurable WiFi channel (1–13) and motion threshold (10–90%) in Settings
- Press `M` to toggle scan mode

Controls: `ESC` back to home

---

#### Notes
Full-featured text editor that saves to `/purplx/notes.txt` on the SD card.
Append or overwrite, scrollable view, full keyboard entry.

Controls: type normally, `ENTER` new line, `DEL`/`BKSP` delete, `ESC` back

---

#### File Browser
Browse the SD card directory tree. Navigate folders, view file names, sizes, and
do a live hex + ASCII dump of any file.

Controls: `W/S` navigate, `ENTER` open file/folder, `ESC` back up / exit

---

#### My Pet — tamagotchi virtual pet
A pixel-art creature that lives on your Cardputer. It gets hungry, bored, dirty,
and tired in **real time** — even while the device is off (state is saved to NVS
and aged forward on next boot).

- Grows through 5 stages: Egg → Baby → Child → Teen → Adult
- Needs: Feed, Play, Clean, Sleep — use `A/D` to pick, `ENTER` to act
- **Big screen:** animated creature with mood/stat bar
- **HUD:** hunger, happiness, cleanliness, energy, age

Controls: `A/D` cycle action, `ENTER` do it, `ESC` back

---

### Games

All games display on the ILI9341 big screen. The HUD shows score / stats.
Every game shows a **How-to screen** when first launched — press any key to start.
`ESC` quits back to the home menu from any game.

| Game | Description | Controls |
|---|---|---|
| **Snake** | Classic snake — eat food, grow, don't hit walls or yourself | `W/A/S/D` steer |
| **Tetris** | Stack and clear rows before you top out | `A/D` move, `W` rotate, `S` soft drop |
| **Wordle** | Guess the 5-letter word in 6 tries. Green = right spot, yellow = wrong spot | Type letters, `ENTER` submit, `DEL` backspace |
| **Chess** | Full chess vs AI or 2-player on one device | `W/A/S/D` cursor, `ENTER` pick/place, `ESC` quit |
| **Checkers** | Jump and capture all enemy pieces vs AI or 2-player | `W/A/S/D` cursor, `ENTER` pick/place |
| **Tron** | Light cycles — outlast your opponent vs AI or 2-player | `W/A/S/D` turn |
| **Pong** | Classic paddle game vs AI or 2-player | `W/S` move paddle |
| **2048** | Slide tiles, combine matching numbers, reach 2048 | `W/A/S/D` slide all tiles |
| **Conway's Life** | Watch patterns evolve — pause, edit cells, resume | `ENTER` pause/play, `W/A/S/D` cursor, `SPACE` toggle cell |
| **Netrun** | Cyberpunk roguelike — procedural floors, enemies, loot, permadeath | `W/A/S/D` move/attack (bump = hack), `E` use stim pack, `>` descend |

Netrun saves your best depth to NVS. All other games reset each run.

---

### Off-Grid

All content is stored in firmware — **no SD card, no internet, no signal needed.**

#### Survival Guide
Offline reference library across 6 topics, scrollable on the big screen:

1. **First Aid** — bleeding control, CPR, burns, fractures
2. **Water** — finding and purifying water in the field
3. **Fire** — friction, spark, fuel, sustaining a fire
4. **Knots** — 6 essential knots with step-by-step instructions
5. **Signaling** — mirror, whistle, ground-to-air signals, attracting rescue
6. **Navigation** — sun/stars, shadow stick, terrain reading without GPS

Controls: `W/S` pick topic, `ENTER` read, `W/S` scroll, `ESC` back

---

#### Morse Trainer & Sender
Learn Morse code and transmit messages using the screen backlight flash and speaker.

- Built-in reference chart (tap `TAB` to view)
- Type any message and transmit — each character flashes and beeps in Morse

Controls: type message, `ENTER` transmit, `TAB` show chart, `ESC` back

---

#### SOS Beacon
Continuously broadcasts the international SOS signal (· · · — — — · · ·) using
both the screen and speaker. Runs until you press `ESC`.

---

#### LoRa Slots (GPS / LoRa Messenger / Compass)
These slots are reserved for the **CapLoRa SX1262** expansion module. The firmware
is ready — plug in the module and a future Purplx update enables them.

---

### EDC Tools

| Tool | Description | Controls |
|---|---|---|
| **Clock** | Digital clock, stopwatch, countdown timer | `W/S` switch mode, `ENTER` start/stop, `R` reset |
| **Flashlight** | Max brightness, SOS strobe, and slow pulse modes | `W/S` or `ENTER` cycle modes |
| **Dice & Coin** | Roll d4/d6/d8/d10/d12/d20 or flip a coin | `W/S` pick die, `ENTER` roll/flip |
| **Notes** | Quick-save text to SD (`/purplx/notes.txt`) | Type, `ENTER` newline, `ESC` save + exit |
| **Calculator** | Basic arithmetic (+ − × ÷), live display | Type expression, `ENTER` evaluate |
| **Unit Converter** | Length, weight, temperature, speed | `W/S` pick category, `A/D` pick unit pair, type value |

---

### Media

#### Music Player
Plays `.wav` files from `/purplx/music/` on the SD card through the Cardputer ADV's
built-in speaker via I2S. Supports PCM WAV, 8 or 16-bit, mono or stereo, up to 44.1 kHz.

- **Big screen:** now-playing info + animated audio visualizer
- **HUD:** track name, volume level, transport status

Controls: `SPACE` play/pause, `A/D` previous/next track, `W/S` volume, `ESC` back

---

### Learn — Ethical Hacking Library

7 lessons stored in firmware (no internet needed). Plain-English explanations
for beginners — what the technique is, how it works mechanically, and the legal reality.

| # | Lesson | Topic |
|---|---|---|
| 1 | **Ethical Hacking 101** | What makes it legal — consent |
| 2 | **How WiFi Works** | Frames, channels, management packets |
| 3 | **Deauthentication Attacks** | How deauth works and why WPA3 fixes it |
| 4 | **Captive Portals & Evil Twins** | Fake networks, credential harvesting |
| 5 | **WiFi CSI Sensing** | Detecting people with radio waves |
| 6 | **Bluetooth & BLE** | BLE advertising, spam floods |
| 7 | **Learn It Right** | Legal paths: CTF, bug bounty, certifications |

Controls: `W/S` pick lesson, `ENTER` read, `W/S` scroll pages, `ESC` back to list

---

### System

#### Themes
8 built-in color themes. Saved to NVS — persists across reboots.

`PURPLE` · `WHITE` · `RED` · `GREEN` · `YELLOW` · `ORANGE` · `BLUE` · `NIGHT`

**NIGHT** mode is a true red-on-black theme designed to preserve dark adaptation.
Press `N` anywhere on the home screen to jump straight to it.

Press `T` on the home screen to cycle through all themes.

---

#### Animated Backgrounds
4 animated backdrops for the home menu. Press `B` on the home screen to cycle.

- **Matrix** — falling green code rain
- **Starfield** — warp-speed stars
- **Grid** — scrolling perspective grid (Tron-style)
- **Pulse** — concentric radar rings from center

---

#### Firmware Launcher — flash guest firmware, return to Purplx on reset

This is the killer feature for multi-firmware setups.

**How it works:** Purplx lives in the `factory` partition (permanent, can never be
overwritten by OTA). Guest firmware (Marauder, Bruce, etc.) goes into the `ota_0`
slot. With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, any partition that doesn't call
the "I'm valid" API is treated as a one-time boot. Guest firmware never calls it.

**Result: press physical RESET → back to Purplx. Every single time. Even if the
guest firmware crashes.**

##### WiFi Flash (`SYSTEM → WiFi Flash`)
Download and boot any firmware directly over WiFi — no SD card, no computer.

Pre-loaded catalog:
- **Bruce** — multi-tool hacking firmware
- **Marauder ADV** — ESP32 Marauder built for Cardputer ADV hardware
- **Marauder** — standard Cardputer build
- **Custom URL** — paste any direct `.bin` link

Flow: pick network → enter password (saved to NVS for next time) → pick firmware → confirm → Purplx downloads, flashes, and boots it → press RESET → Purplx.

##### SD Flash (`SYSTEM → SD Launch`)
Copy `.bin` files to your SD card root or `/firmware/` folder. Purplx scans and
lists them automatically.

Flow: insert SD → navigate to `SD Launch` → `W/S` pick file → `ENTER` → confirm → flashes and boots → press RESET → Purplx.

---

#### Battery / Charging Mode
Shows battery voltage, estimated charge %, and charging status with an animated
indicator. Dims both screens to conserve power while charging.

Controls: `ESC` back

---

#### Deep Sleep
Press `ESC` from Settings and select Sleep, or navigate to Settings → Sleep Now.
Both screens go dark. Press the side power button to wake.

---

#### Device Info
Shows chip, CPU speed, flash size, PSRAM, free heap, SD status, firmware version.

---

#### Settings

| Setting | Keys |
|---|---|
| Theme | `A/D` cycle |
| Background animation | `A/D` cycle |
| CSI WiFi channel | `A/D` adjust (1–13) |
| CSI motion threshold | `A/D` adjust (10–90%) |
| Sleep Now | `A/D` or `,` |

---

## Controls Reference

### Home Menu

| Key | Action |
|---|---|
| `W` / `S` | Move cursor up / down |
| `A` / `D` | Same as W/S (also arrows via Cardputer firmware) |
| `ENTER` | Open selected app |
| `T` | Cycle color theme |
| `N` | Switch to NIGHT theme |
| `B` | Cycle background animation |
| `ESC` / `` ` `` | No-op on home (already home) |

### Universal

| Key | Action |
|---|---|
| `ESC` or `` ` `` | Back to home from any app |
| Arrow keys | Mapped to `W/A/S/D` by the Cardputer keyboard firmware |

---

## Build & Flash

### Requirements
- [VS Code](https://code.visualstudio.com) + [PlatformIO](https://platformio.org/install/ide?install=vscode) extension

### First-time flash (chip erase + upload)

```bash
pio run -t erase && pio run -t upload
```

The erase wipes any stale partition data from a previous firmware so the new
factory + ota_0 layout goes on clean.

### Subsequent updates (Purplx already installed)

```bash
pio run -t upload
```

### If it hangs on "Connecting........"

Hold the **BOOT / G0 button** while it says Connecting, release once uploading starts.
Or try a different USB-C cable — charge-only cables can't flash.

### Build size (v1.0.0)

```
Flash: 1,291,121 bytes / 1,572,864 bytes (82% of factory partition)
RAM:     141,956 bytes /   327,680 bytes (43%)
```

See [FLASHING_GUIDE.md](FLASHING_GUIDE.md) for full beginner step-by-step instructions.

---

## SD Card Setup

Format as **FAT32** (32 GB or smaller). Insert before boot — Purplx mounts it when
entering apps that need it (SD Launch, Notes, Files, Music).

```
/                       ← put guest .bin files here for SD Launch
/firmware/              ← or here (SD Launch scans both)
/purplx/music/          ← .wav files for the music player
/purplx/games/          ← words5.txt (one 5-letter word per line) for Wordle
/purplx/notes.txt       ← created automatically by the Notes app
```

---

## Partition Layout

```
nvs        20 KB   @ 0x009000   key-value store (themes, pet state, WiFi creds)
otadata     8 KB   @ 0x00e000   OTA boot selector
factory   1.5 MB   @ 0x010000   Purplx — permanent home, cannot be OTA-overwritten
ota_0     5.4 MB   @ 0x190000   guest firmware slot (Marauder, Bruce, etc.)
storage   1.0 MB   @ 0x700000   internal FAT (future use)
```

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` is set in `sdkconfig.defaults`.
Purplx calls `esp_ota_mark_app_valid_cancel_rollback()` in `setup()`.
Guest firmware never does — so it's always a one-time boot.

---

## Project Structure

```
Purplx/
├── platformio.ini              build config (ESP32-S3, 8MB, arduino)
├── sdkconfig.defaults          ESP-IDF overrides (OTA rollback, WiFi CSI, BLE)
├── partitions_8mb_launcher.csv partition table
├── include/
│   ├── main.h                  global state, AppState enum, OS API
│   ├── themes.h                8 color themes
│   ├── pin_config.h            all GPIO assignments
│   └── display_config.h        LovyanGFX panel configs
└── src/
    ├── main.cpp                OS loop, home menu, state machine
    ├── core/
    │   ├── display.cpp         dual-screen init + mutex
    │   └── radio_manager.cpp   WiFi/BLE shared radio manager
    ├── ui/
    │   └── cyberfx.cpp         animated backgrounds (Matrix/Stars/Grid/Pulse)
    └── apps/
        ├── csi/                WiFi CSI human radar
        ├── games/              Snake, Tetris, Wordle, Chess, Checkers, Tron,
        │                       Pong, 2048, Life, Netrun
        ├── offgrid/            Survival guide, Morse, SOS
        ├── edc/                Clock, Flashlight, Dice, Notes, Calc, Converter
        ├── media/              WAV music player
        ├── learn/              7-lesson ethical hacking library
        ├── tools/              File browser, SD Notes
        └── system/             Pet, Charging, SD Launcher, WiFi OTA
```

---

## Credits & Acknowledgments

Purplx stands on the shoulders of the M5Stack and Cardputer open-source community.
This project was inspired by, and in places adapts approaches from, the following
works. Huge thanks to everyone who built and shared them:

- **skizzophrenic (TalkingSasquach)** — the CSI human-detection algorithm in Purplx
  is adapted from skizzophrenic's Cardputer CSI Human Detector, and the dual-screen /
  ILI9341 approach draws on their PorkChop dual-screen work.
- **[M5Launcher](https://github.com/bmorcelli/M5Launcher) (bmorcelli)** — inspiration
  for the multi-firmware launcher concept and the approach to booting guest firmware.
- **PorkChop** — dual-screen architecture and ILI9341 pin-mapping reference.
- **[Espressif esp-csi](https://github.com/espressif/esp-csi)** — the reference CSI
  implementation that underlies most ESP32 WiFi-sensing projects, including this one.
- **The broader Cardputer / ESP32 community** — Bruce, Evil-M5, Marauder, GhostESP
  and others, whose work shaped what a Cardputer firmware can be and which Purplx's
  launcher can boot.

If your work is reflected here and you'd like different or additional credit (or a
correction), please open an issue — I'm happy to fix it. Some code origins were
inherited from earlier iterations of this project and I've credited to the best of
my knowledge; if I've missed or misattributed anyone, that's an honest mistake and
I'll make it right.

Built with the help of AI coding assistants.

---

## License

MIT — see [LICENSE](LICENSE).

Upstream works this project adapts from (skizzophrenic/PorkChop, esp-csi) are also
MIT, so the whole project is clean.
