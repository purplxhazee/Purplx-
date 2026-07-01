# Purplx — Complete Flashing Guide (Beginner Friendly)

This guide gets Purplx onto your M5Stack Cardputer ADV from zero.
Follow it top to bottom. Total time: ~20 minutes.

---

## WHAT YOU NEED

### Hardware
- M5Stack Cardputer ADV (you have this)
- USB-C cable (the one that came with it works — some cables are charge-only,
  so if the computer doesn't see the device, try a different cable)
- A computer (Mac, Windows, or Linux)
- (Optional) Your ILI9341 second screen, wired up
- (Optional) A microSD card, formatted FAT32, for music/Wordle word list

### Software (free)
- **Visual Studio Code** (the code editor)
- **PlatformIO** (the extension that compiles + flashes — installs inside VS Code)

---

## STEP 1 — Install VS Code

1. Go to https://code.visualstudio.com
2. Download for your operating system
3. Install it (just click through the installer)
4. Open VS Code

---

## STEP 2 — Install PlatformIO

1. In VS Code, click the **Extensions** icon on the left bar
   (it looks like 4 squares, or press Ctrl+Shift+X / Cmd+Shift+X)
2. In the search box, type: **PlatformIO IDE**
3. Click **Install** on the one called "PlatformIO IDE"
4. Wait for it to finish (it downloads a toolchain — can take 5-10 min the
   first time, and it may ask to reload VS Code — let it)

When it's done you'll see a little **alien/ant head icon** on the left bar.
That's PlatformIO.

---

## STEP 3 — Open the Purplx project

1. Unzip **Purplx.zip** somewhere easy (like your Desktop)
2. In VS Code: **File → Open Folder**
3. Select the **Purplx** folder (the one with `platformio.ini`
   inside it — not the zip, the unzipped folder)
4. PlatformIO will detect the project and may show "Configuring project..."
   at the bottom. Wait for it to settle.

> IMPORTANT: open the folder that DIRECTLY contains `platformio.ini`.
> If you opened a folder and don't see `platformio.ini` in the file list on
> the left, you opened the wrong level — go up or down one folder.

---

## STEP 4 — First compile (no device needed yet)

This checks the code builds before we flash anything.

1. Open the PlatformIO terminal: **View → Terminal** (or press Ctrl+` )
2. Type this and press Enter:

   ```
   pio run
   ```

3. Wait. The first build downloads libraries (LovyanGFX, M5 stuff) and can
   take several minutes.

### If it says "SUCCESS"
Great — skip to Step 5.

### If it shows errors (red text)
This is NORMAL for a first build of a big project.
**Copy ALL the red error text and send it back to me.** I'll give you exact
fixes. Don't try to fix them yourself — just paste them and we'll knock them
out together.

---

## STEP 5 — Plug in and flash

1. Connect the Cardputer ADV to your computer with the USB-C cable
2. Turn the Cardputer ON (side power button — press briefly)
3. **First time ever?** Run the erase step first so the new partition layout
   (factory + OTA slot) goes on clean:

   ```
   pio run -t erase && pio run -t upload
   ```

   If you already have Purplx installed and are just updating, skip the erase:

   ```
   pio run -t upload
   ```

4. PlatformIO finds the device automatically and flashes it.

### If it hangs on "Connecting........"
- Hold the **BOOT/G0 button** on the Cardputer while it says Connecting,
  then release once it starts uploading
- Or try a different USB cable (charge-only cables can't flash)
- On Windows you may need the CP210x or CH340 USB driver — PlatformIO usually
  installs this, but if the port never appears, search "CP210x driver" and
  install from Silicon Labs

### Finding the port manually (only if auto-detect fails)
- **Mac:** it's usually `/dev/tty.usbmodem...` — run `ls /dev/tty.*`
- **Windows:** it's a COM port like `COM5` — check Device Manager
- **Linux:** usually `/dev/ttyACM0`

If you need to set it manually, tell me and I'll show you the one line to add.

---

## STEP 6 — First boot

When flashing finishes, the Cardputer restarts automatically. You should see:

1. **"PURPLX"** appear over matrix rain (the boot splash)
2. A **SCREEN SETUP** question on the internal screen:
   - If you wired the ILI9341 and can read text on it → pick **YES**
   - If you only have the built-in screen → pick **NO**
   - Use W/S to choose, ENTER to confirm
3. The **home menu** with Tools / Games / Media / Off-Grid / Learn / System

That's it — you're running Purplx!

---

## STEP 7 — (Optional) Set up the SD card

Format a microSD as **FAT32** (32GB or smaller works best). Put it in the
Cardputer. On boot, Purplx creates these folders automatically:

```
/purplx/music/     <- put .wav music files here
/purplx/games/     <- put words5.txt here for more Wordle words
```

- **Music:** copy any `.wav` files into `/purplx/music/`
- **Wordle words:** make a text file `words5.txt` with one 5-letter word per
  line (uppercase or lowercase) and put it in `/purplx/games/`. Without it,
  Wordle uses a small built-in list.

---

## CONTROLS CHEAT SHEET

```
HOME MENU
  W / S      move up / down
  ENTER      open selected app
  T          change color theme
  B          change background animation
  ?          help

IN ANY APP
  ESC        back to home (top-left ` key also works)
  ?          help for that app

GAMES
  Snake/Tetris/Chess/Checkers: W A S D to move, ENTER to select
  Wordle: type letters, ENTER to guess, BACKSPACE to delete

MUSIC
  W/S volume or pick track, ENTER play, SPACE pause, A/D prev/next

MORSE
  Type a message, ENTER to transmit (flashes + beeps), TAB for chart
```

---

## TROUBLESHOOTING

**"Both screens are black after flashing"**
- The internal screen should always work. If it's black, the flash may have
  failed — re-run `pio run -t upload`.

**"Internal screen works but ILI9341 is black"**
- Check your wiring (see the wiring table in README.md)
- The critical wire is MOSI → GPIO14
- In SCREEN SETUP, you can pick NO to use single-screen mode for now

**"It compiled before but now won't"**
- Run `pio run -t clean` then `pio run` again

**"I want to go back to a different firmware later"**
- Just flash the other firmware. Nothing here is permanent.

---

## FLASHING GUEST FIRMWARE (Marauder, Bruce, etc.)

Purplx has a built-in firmware launcher. You never need M5Burner or a computer
to boot other firmwares — and you automatically come back to Purplx on reset.

### How it works

Purplx lives in the **factory** partition (permanent, can't be overwritten by
OTA). Guest firmware goes into the **ota_0** slot. The ESP32-S3 bootloader
is configured with rollback: if a partition never confirms itself as valid,
the next reset rolls back to factory — that's Purplx.

Marauder, Bruce, and any other guest firmware never call the "I'm valid" API,
so they're always treated as a one-time boot. **Press physical RESET →
back to Purplx. Every time.**

### Option A — WiFi Flash (no SD card needed)

1. From the Purplx home menu, go to **SYSTEM → WiFi Flash**
2. Pick your WiFi network, enter the password
3. Choose **Marauder ADV**, **Bruce**, or paste a custom `.bin` URL
4. Confirm → Purplx downloads and flashes it, then boots into it
5. When you're done, press the physical **RESET** button → Purplx

### Option B — SD Card Flash

1. Put a `.bin` firmware file in the root of your SD card (or in `/firmware/`)
   - e.g. `Marauder.bin`, `Bruce.bin`
2. Insert the SD card into the Cardputer
3. From the Purplx home menu, go to **SYSTEM → SD Launch**
4. Select the file with W/S, press ENTER twice (second press confirms)
5. Purplx flashes it and boots into it
6. When you're done, press the physical **RESET** button → Purplx

### Notes

- The factory partition (Purplx) **cannot** be overwritten through either
  launcher — it only updates when you run `pio run -t upload` from your computer.
- If a guest firmware crashes instead of waiting for reset, rollback still
  triggers automatically.
- **Exception:** if you flash a guest firmware that was itself compiled with
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, it will confirm its own boot and
  you'll need to re-flash Purplx over USB to return. Standard prebuilt binaries
  (the ones in the WiFi catalog) don't do this.
- The second screen only works in firmwares built for it (Purplx and
  the dual-screen forks). Stock Bruce, GhostESP, and Marauder use the
  internal screen only.

---

## STAY LEGAL

The hacking tools (in separate firmwares) are for testing YOUR OWN networks
and devices, or ones you have WRITTEN permission to test. Using them on others
is a crime in nearly every country. Purplx's built-in Learn library explains
the details — start there.

Have fun with your cyberdeck!
