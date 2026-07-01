#pragma once
// =============================================================================
// sd_launcher.h — SD Firmware Launcher for Purplx
// =============================================================================
// Scans the SD card for .bin firmware files, presents a selection menu,
// flashes the chosen binary to the ota_0 partition, and executes a
// one-time boot into the guest firmware.
//
// On any physical RESET or crash of the guest app (since it never calls
// esp_ota_mark_app_valid_cancel_rollback()), the ESP32-S3 bootloader
// automatically rolls back to the factory partition (Purplx).
//
// Requires in sdkconfig.defaults:
//   CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
//
// Partition layout (partitions_8mb_launcher.csv):
//   factory  @ 0x010000  1.5MB  — Purplx permanent home
//   ota_0    @ 0x190000  5.4MB  — guest firmware slot (this module writes here)
//
// Usage:
//   SDLauncher::start()      — call from OS_SetState(SYS_FIRMWARES)
//   SDLauncher::stop()       — call from OS_SetState (leaving SYS_FIRMWARES)
//   SDLauncher::handleKey()  — call from loop() key handler
// =============================================================================

#include <Arduino.h>

namespace SDLauncher {

// Scan SD card, render file list on both displays.
// Initialises SD internally (SD.begin). Safe to call multiple times.
void start();

// Release SD bus (SD.end). Call when navigating away from this screen.
void stop();

// Route keyboard input. Keys:
//   W / S       — navigate list (also arrow emulation via Cardputer firmware)
//   ENTER       — first press: show confirm screen; second press: flash + boot
//   R           — retry SD init (useful if card was inserted after entering)
//   ESC / `     — handled by main.cpp (OS_SetState HOME); stop() is called
void handleKey(char key);

} // namespace SDLauncher
