// =============================================================================
// sd_launcher.cpp — SD Firmware Launcher for Purplx
// =============================================================================
// Architecture:
//   1. start()     → init SD, scan for .bin files, render list
//   2. handleKey() → navigate list → confirm screen → flashAndBoot()
//   3. flashAndBoot():
//        a. Open .bin from SD
//        b. esp_ota_begin(ota_0_partition, ...)
//        c. Stream SD → ota_0 in 4KB chunks with progress bar
//        d. esp_ota_end()        — verifies SHA-256 in image
//        e. esp_ota_set_boot_partition(ota_0)  — sets next boot
//        f. Do NOT call esp_ota_mark_app_valid_cancel_rollback()
//           → partition stays PENDING_VERIFY; bootloader auto-rolls back
//           to factory (Purplx) on next reset if guest never confirms.
//        g. esp_restart()
//
// Rollback guarantee (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y required):
//   • Factory partition is the permanent fallback — cannot be OTA-overwritten.
//   • Guest binary goes to ota_0 only.
//   • Physical RESET after guest boot → bootloader sees PENDING_VERIFY → ABORTED
//     → boots factory (Purplx). Even a crash triggers this path.
// =============================================================================

#include "sd_launcher.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <SD.h>
#include <esp_ota_ops.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/spi_periph.h>

namespace SDLauncher {

// ─── Constants ────────────────────────────────────────────────────────────────
static constexpr int MAX_BINS  = 32;    // max firmware files listed
static constexpr int VISIBLE   = 10;    // rows visible on ILI9341 at once
static constexpr int ROW_H     = 19;    // pixels per file row

// ─── Module state ─────────────────────────────────────────────────────────────
static char _paths[MAX_BINS][64];       // full SD paths e.g. "/firmware/Bruce.bin"
static int  _count  = 0;               // number of .bin files found
static int  _cursor = 0;               // currently highlighted row
static int  _scroll = 0;               // first visible row index
static bool _sd_ok  = false;           // SD successfully mounted

// Internal display state machine
enum class Screen : uint8_t { LIST, CONFIRM, ERROR_VIEW };
static Screen _screen = Screen::LIST;
static char   _errMsg[64] = {};        // last error detail for ERROR_VIEW

// ─── SD scanning ──────────────────────────────────────────────────────────────

// Scan one directory on the SD card; append matching .bin paths to _paths[].
static void _scanDir(const char* dirPath) {
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory()) { dir.close(); return; }

    while (_count < MAX_BINS) {
        File f = dir.openNextFile();
        if (!f) break;                  // no more entries

        if (!f.isDirectory()) {
            // f.name() may return full path or just basename depending on SD lib version.
            // Use strrchr to get basename reliably.
            const char* full = f.name();
            const char* base = strrchr(full, '/');
            base = base ? base + 1 : full;

            int len = (int)strlen(base);
            if (len > 4 && strcasecmp(base + len - 4, ".bin") == 0) {
                // Build guaranteed-absolute path
                bool isRoot = (strcmp(dirPath, "/") == 0);
                if (isRoot) {
                    snprintf(_paths[_count], sizeof(_paths[0]), "/%s", base);
                } else {
                    snprintf(_paths[_count], sizeof(_paths[0]), "%s/%s", dirPath, base);
                }
                _count++;
            }
        }
        f.close();
    }
    dir.close();
}

// Attempt to mount SD and scan root + /firmware directory.
static void _initAndScan() {
    _count  = 0;
    _sd_ok  = false;

    // LovyanGFX initializes SPI2 with miso_io_num=-1 for the write-only
    // displays, which calls gpio_matrix_in(GPIO_FUNC_IN_HIGH, SPIQ_IN_IDX)
    // — disconnecting GPIO39 from SPI2 MISO in the hardware GPIO matrix.
    // esp_rom_gpio_connect_in_signal() directly re-wires the path before
    // every SD.begin() attempt, bypassing Arduino and LovyanGFX entirely.
    gpio_set_direction((gpio_num_t)PIN_SD_MISO, GPIO_MODE_INPUT);
    esp_rom_gpio_connect_in_signal(PIN_SD_MISO,
                                   spi_periph_signal[SPI2_HOST].spiq_in,
                                   false);
    Serial.printf("[SDLauncher] GPIO%d re-wired -> SPI2 MISO (signal %u)\n",
                  PIN_SD_MISO, spi_periph_signal[SPI2_HOST].spiq_in);

    // Retry up to 3 times; some cards need a brief settle after power-on.
    for (int attempt = 0; attempt < 3; attempt++) {
        if (SD.begin(PIN_SD_CS, SPI, 25000000)) {
            _sd_ok = true;
            break;
        }
        Serial.printf("[SDLauncher] SD.begin() attempt %d failed\n", attempt + 1);
        SD.end();
        delay(80);
    }

    if (!_sd_ok) {
        Serial.println("[SDLauncher] SD mount failed after 3 attempts");
        return;
    }
    _sd_ok = true;
    Serial.println("[SDLauncher] SD mounted");

    _scanDir("/");            // binaries in SD root
    _scanDir("/firmware");   // or in a /firmware subfolder

    Serial.printf("[SDLauncher] Found %d .bin file(s)\n", _count);
}

// ─── Draw helpers ─────────────────────────────────────────────────────────────

// Shared header/footer drawing for secondary (ILI9341 320×240).
static void _drawSecondaryChrome(const char* title, bool warn = false) {
    const ColorTheme* t = g_theme;
    const int W = 320, H = 240;
    g_lcd_secondary.fillScreen(t->bg);
    // Title bar
    g_lcd_secondary.fillRect(0, 0, W, 24, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 23, W, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(warn ? t->alert : t->primary, t->title_bg);
    g_lcd_secondary.setCursor(8, 8);
    g_lcd_secondary.print(title);
    // Corner accents (Purplx style)
    g_lcd_secondary.fillRect(0,   0,   2, 2, t->primary);
    g_lcd_secondary.fillRect(W-2, 0,   2, 2, t->primary);
    // Footer bar
    g_lcd_secondary.fillRect(0, H-16, W, 16, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, H-16, W, t->border);
}

static void _drawHUD(const char* title, const char* line2 = nullptr,
                     const char* line3 = nullptr, bool alertTitle = false) {
    const ColorTheme* t = g_theme;
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.fillRect(0, 0, 240, 4, alertTitle ? t->alert : t->primary);
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(alertTitle ? t->alert : t->primary, t->hud_bg);
    g_lcd_primary.setCursor(4, 10);
    g_lcd_primary.print(title);
    if (line2) {
        g_lcd_primary.setTextColor(t->text_dim, t->hud_bg);
        g_lcd_primary.setCursor(4, 28); g_lcd_primary.print(line2);
    }
    if (line3) {
        g_lcd_primary.setTextColor(t->secondary, t->hud_bg);
        g_lcd_primary.setCursor(4, 46); g_lcd_primary.print(line3);
    }
}

// Render the file list screen.
static void _drawList() {
    const ColorTheme* t = g_theme;
    const int W = 320, H = 240;

    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(40)) != pdTRUE) return;

    _drawSecondaryChrome("[ SD FIRMWARE LAUNCHER ]");

    if (!_sd_ok) {
        // ── No SD card ────────────────────────────────────────────────────────
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(t->alert, t->bg);
        int tw = g_lcd_secondary.textWidth("SD NOT FOUND");
        g_lcd_secondary.setCursor((W - tw) / 2, 70);
        g_lcd_secondary.print("SD NOT FOUND");
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(t->text_dim, t->bg);
        g_lcd_secondary.setCursor(24, 112); g_lcd_secondary.print("Insert SD card with .bin files,");
        g_lcd_secondary.setCursor(24, 128); g_lcd_secondary.print("then press [R] to retry.");
        g_lcd_secondary.setCursor(24, 150); g_lcd_secondary.print("Files can be in SD root or /firmware/");

        g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
        g_lcd_secondary.setCursor(6, H - 11);
        g_lcd_secondary.print("[R] retry SD   [ESC] back");

        _drawHUD("SD LAUNCHER", "SD not mounted", nullptr, true);

    } else if (_count == 0) {
        // ── SD mounted but no .bin files ──────────────────────────────────────
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(t->alert, t->bg);
        int tw = g_lcd_secondary.textWidth("No .bin files found");
        g_lcd_secondary.setCursor((W - tw) / 2, 80);
        g_lcd_secondary.print("No .bin files found");
        g_lcd_secondary.setTextColor(t->text_dim, t->bg);
        g_lcd_secondary.setCursor(32, 106); g_lcd_secondary.print("Copy firmware .bin files to:");
        g_lcd_secondary.setTextColor(t->secondary, t->bg);
        g_lcd_secondary.setCursor(32, 122); g_lcd_secondary.print("  SD root   (e.g. /Bruce.bin)");
        g_lcd_secondary.setCursor(32, 138); g_lcd_secondary.print("  /firmware/  subfolder");

        g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
        g_lcd_secondary.setCursor(6, H - 11);
        g_lcd_secondary.print("[R] rescan   [ESC] back");

        char buf[32]; snprintf(buf, sizeof(buf), "SD ok, 0 bins");
        _drawHUD("SD LAUNCHER", buf);

    } else {
        // ── File list ─────────────────────────────────────────────────────────
        // Item counter (top-right of title bar)
        char cnt[12]; snprintf(cnt, sizeof(cnt), "%d/%d", _cursor + 1, _count);
        g_lcd_secondary.setTextColor(t->secondary, t->title_bg);
        g_lcd_secondary.setCursor(W - (int)g_lcd_secondary.textWidth(cnt) - 8, 8);
        g_lcd_secondary.print(cnt);

        for (int i = 0; i < VISIBLE; i++) {
            int idx = _scroll + i;
            if (idx >= _count) break;
            bool sel = (idx == _cursor);
            int  y   = 26 + i * ROW_H;

            uint16_t rowBg = sel ? t->highlight_bg : (i % 2 == 0 ? t->row_a : t->row_b);
            uint16_t rowFg = sel ? t->highlight_text : t->text;

            g_lcd_secondary.fillRect(0, y, W, ROW_H - 1, rowBg);

            // Selection bar on left edge
            if (sel) {
                g_lcd_secondary.fillRect(0, y, 3, ROW_H - 1, t->primary);
            }

            // Filename (basename only, truncated to fit)
            const char* nm = strrchr(_paths[idx], '/');
            nm = nm ? nm + 1 : _paths[idx];
            char label[42]; snprintf(label, sizeof(label), "%s", nm);

            g_lcd_secondary.setTextColor(rowFg, rowBg);
            g_lcd_secondary.setTextSize(1);
            g_lcd_secondary.setCursor(8, y + 5);
            g_lcd_secondary.print(label);
        }

        // Scroll indicator lines (right gutter)
        if (_count > VISIBLE) {
            int gutX = W - 6;
            float top    = (float)_scroll / _count;
            float bottom = (float)(_scroll + VISIBLE) / _count;
            int   barTop = 26 + (int)(top    * (VISIBLE * ROW_H));
            int   barH   = (int)((bottom - top) * (VISIBLE * ROW_H));
            g_lcd_secondary.drawFastVLine(gutX, 26, VISIBLE * ROW_H, t->text_dim);
            g_lcd_secondary.fillRect(gutX - 1, barTop, 3, barH, t->primary);
        }

        // Footer hint
        g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
        g_lcd_secondary.setCursor(6, H - 11);
        g_lcd_secondary.print("[W/S] select   [ENTER] flash+boot   [ESC] back");

        // HUD: show selected file info
        const char* nm = strrchr(_paths[_cursor], '/');
        nm = nm ? nm + 1 : _paths[_cursor];
        char hudName[26]; snprintf(hudName, sizeof(hudName), "%s", nm);
        _drawHUD("SD LAUNCHER", hudName, "ENTER to flash+boot");
        g_lcd_primary.setTextColor(t->text_dim, t->hud_bg);
        g_lcd_primary.setCursor(4, 64);
        g_lcd_primary.print("Reset = back to Purplx");
    }

    xSemaphoreGive(g_display_mutex);
}

// Render the confirm-before-flash screen.
static void _drawConfirm() {
    const ColorTheme* t = g_theme;
    const int W = 320, H = 240;

    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(40)) != pdTRUE) return;

    _drawSecondaryChrome("[ CONFIRM FLASH ]", /*warn=*/true);

    const char* nm = strrchr(_paths[_cursor], '/');
    nm = nm ? nm + 1 : _paths[_cursor];

    // Firmware name
    g_lcd_secondary.setTextColor(t->text_dim, t->bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setCursor(20, 36); g_lcd_secondary.print("About to flash and boot:");

    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(t->primary, t->bg);
    int tw = g_lcd_secondary.textWidth(nm);
    if (tw > W - 16) { g_lcd_secondary.setTextSize(1); tw = g_lcd_secondary.textWidth(nm); }
    g_lcd_secondary.setCursor((W - tw) / 2, 58);
    g_lcd_secondary.print(nm);

    // File size
    File f = SD.open(_paths[_cursor], FILE_READ);
    if (f) {
        uint32_t sz = f.size(); f.close();
        char szbuf[20];
        if (sz >= 1024 * 1024) snprintf(szbuf, sizeof(szbuf), "%.2f MB", sz / 1048576.0f);
        else                    snprintf(szbuf, sizeof(szbuf), "%u KB",   sz / 1024);
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(t->secondary, t->bg);
        int sw = g_lcd_secondary.textWidth(szbuf);
        g_lcd_secondary.setCursor((W - sw) / 2, 95);
        g_lcd_secondary.print(szbuf);
    }

    // Explanation
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->text, t->bg);
    g_lcd_secondary.setCursor(18, 122); g_lcd_secondary.print("The device will boot this firmware once.");
    g_lcd_secondary.setCursor(18, 139); g_lcd_secondary.print("Press physical RESET (or let it crash)");
    g_lcd_secondary.setCursor(18, 154); g_lcd_secondary.print("to automatically return to Purplx.");
    g_lcd_secondary.setTextColor(t->text_dim, t->bg);
    g_lcd_secondary.setCursor(18, 174); g_lcd_secondary.print("Purplx in factory partition is safe —");
    g_lcd_secondary.setCursor(18, 188); g_lcd_secondary.print("it cannot be overwritten by this flash.");

    // Footer: confirm / cancel
    g_lcd_secondary.setTextColor(t->success, t->title_bg);
    g_lcd_secondary.setCursor(6, H - 11);
    g_lcd_secondary.print("[ENTER] FLASH NOW");
    g_lcd_secondary.setTextColor(t->alert, t->title_bg);
    g_lcd_secondary.setCursor(192, H - 11);
    g_lcd_secondary.print("[ESC] cancel");

    _drawHUD("CONFIRM FLASH", "ENTER = flash+boot", "ESC   = cancel", /*alert=*/true);

    xSemaphoreGive(g_display_mutex);
}

// Render the flash-progress screen (owns the display — no mutex taken here,
// caller must ensure no other renderer is active).
static void _drawProgress(int pct, const char* status) {
    const ColorTheme* t = g_theme;
    const int W = 320, H = 240;

    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0, 0, W, 24, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 23, W, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary, t->title_bg);
    g_lcd_secondary.setCursor(8, 8);
    g_lcd_secondary.print("[ WRITING FIRMWARE ]");

    // Status text
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->secondary, t->bg);
    int sw = g_lcd_secondary.textWidth(status);
    g_lcd_secondary.setCursor((W - sw) / 2, 66);
    g_lcd_secondary.print(status);

    // Progress bar  (bx=20, by=96, bw=280, bh=20)
    const int bx = 20, by = 96, bw = W - 40, bh = 20;
    g_lcd_secondary.drawRect(bx, by, bw, bh, t->border);
    int fw = (pct > 0) ? (int)((bw - 2) * pct / 100) : 0;
    if (fw > 0) g_lcd_secondary.fillRect(bx + 1, by + 1, fw, bh - 2, t->primary);

    // Percentage number
    char pctbuf[8]; snprintf(pctbuf, sizeof(pctbuf), "%d%%", pct);
    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(t->text, t->bg);
    int pw = g_lcd_secondary.textWidth(pctbuf);
    g_lcd_secondary.setCursor((W - pw) / 2, by + 28);
    g_lcd_secondary.print(pctbuf);

    if (pct >= 100) {
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(t->success, t->bg);
        int lw = g_lcd_secondary.textWidth("Launching guest firmware...");
        g_lcd_secondary.setCursor((W - lw) / 2, by + 60);
        g_lcd_secondary.print("Launching guest firmware...");
    }

    // Footer warning
    g_lcd_secondary.fillRect(0, H - 16, W, 16, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, H - 16, W, t->border);
    g_lcd_secondary.setTextSize(1);   // reset from textSize(2) above
    g_lcd_secondary.setTextColor(t->alert, t->title_bg);
    g_lcd_secondary.setCursor(6, H - 11);
    g_lcd_secondary.print("DO NOT REMOVE SD OR RESET");

    // HUD
    char hudLine[32]; snprintf(hudLine, sizeof(hudLine), "WRITING %d%%", pct);
    _drawHUD(hudLine, "DO NOT RESET", nullptr, /*alert=*/true);
}

// Render the error screen and save message for key dismissal.
static void _drawError(const char* detail) {
    snprintf(_errMsg, sizeof(_errMsg), "%s", detail);
    _screen = Screen::ERROR_VIEW;

    const ColorTheme* t = g_theme;
    const int W = 320, H = 240;

    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    _drawSecondaryChrome("[ FLASH ERROR ]", /*warn=*/true);

    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(t->alert, t->bg);
    int tw = g_lcd_secondary.textWidth("FAILED");
    g_lcd_secondary.setCursor((W - tw) / 2, 58);
    g_lcd_secondary.print("FAILED");

    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->text, t->bg);
    g_lcd_secondary.setCursor(14, 105);
    g_lcd_secondary.print(_errMsg);

    g_lcd_secondary.setTextColor(t->text_dim, t->bg);
    g_lcd_secondary.setCursor(14, 124);
    g_lcd_secondary.print("Partition state is unchanged.");
    g_lcd_secondary.setCursor(14, 140);
    g_lcd_secondary.print("Purplx will boot normally on reset.");

    g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
    g_lcd_secondary.setCursor(6, H - 11);
    g_lcd_secondary.print("[ANY KEY] back to list");

    _drawHUD("FLASH FAILED", _errMsg, nullptr, /*alert=*/true);

    xSemaphoreGive(g_display_mutex);
}

// ─── Core flash + boot logic ──────────────────────────────────────────────────

// Flash the selected .bin from SD to the ota_0 partition, then reboot.
// The partition is intentionally left UNCONFIRMED (PENDING_VERIFY).
// If the guest app never calls esp_ota_mark_app_valid_cancel_rollback(),
// the ESP32-S3 bootloader will roll back to factory (Purplx) on next reset.
static void _flashAndBoot(const char* filepath) {
    Serial.printf("[SDLauncher] Starting flash: %s\n", filepath);

    // ── 1. Open source file from SD ───────────────────────────────────────────
    File f = SD.open(filepath, FILE_READ);
    if (!f) {
        _drawError("Cannot open file from SD");
        return;
    }

    // f.size() can return 0 on some ESP32 SD library versions.
    // Seek to end for a guaranteed measurement, then return to start.
    f.seek(0, SeekEnd);
    size_t fileSize = (size_t)f.position();
    f.seek(0, SeekSet);
    Serial.printf("[SDLauncher] File size: %u bytes\n", fileSize);

    if (fileSize < 256) {
        f.close();
        char msg[56];
        snprintf(msg, sizeof(msg), "File too small / unreadable (%u B)", (unsigned)fileSize);
        _drawError(msg);
        return;
    }

    // ── 2. Locate the ota_0 guest partition ───────────────────────────────────
    const esp_partition_t* guest = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    if (!guest) {
        f.close();
        _drawError("ota_0 partition not found in table");
        return;
    }
    Serial.printf("[SDLauncher] ota_0 at 0x%x, size 0x%x\n",
                  guest->address, guest->size);

    if (fileSize > guest->size) {
        f.close();
        char msg[64];
        snprintf(msg, sizeof(msg), "File %uKB > ota_0 %uKB",
                 (unsigned)(fileSize / 1024), (unsigned)(guest->size / 1024));
        _drawError(msg);
        return;
    }

    // ── 3. Begin OTA write into ota_0 (erases partition automatically) ────────
    esp_ota_handle_t handle = 0;
    _drawProgress(0, "Erasing OTA partition...");

    esp_err_t err = esp_ota_begin(guest, fileSize, &handle);
    if (err != ESP_OK) {
        f.close();
        char msg[56]; snprintf(msg, sizeof(msg), "esp_ota_begin: %s", esp_err_to_name(err));
        _drawError(msg);
        return;
    }

    // ── 4. Stream file → ota_0 in 4KB chunks ─────────────────────────────────
    static uint8_t buf[4096];   // static: avoids 4KB stack hit per call
    size_t written  = 0;
    int    lastPct  = -1;

    while (f.available()) {
        size_t rd = f.read(buf, sizeof(buf));
        if (rd == 0) break;

        err = esp_ota_write(handle, buf, rd);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            f.close();
            char msg[56]; snprintf(msg, sizeof(msg), "esp_ota_write: %s", esp_err_to_name(err));
            _drawError(msg);
            return;
        }

        written += rd;
        int pct = (fileSize > 0) ? (int)(written * 100UL / fileSize) : 50;
        if (pct != lastPct) {
            char status[56];
            snprintf(status, sizeof(status), "Writing %u / %u KB",
                     (unsigned)(written / 1024), (unsigned)(fileSize / 1024));
            _drawProgress(pct, status);
            lastPct = pct;
        }
    }
    f.close();
    Serial.printf("[SDLauncher] Wrote %u bytes\n", (unsigned)written);

    // ── 5. Finalise — verifies SHA-256 hash embedded by esptool ──────────────
    _drawProgress(99, "Verifying image integrity...");
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        char msg[56]; snprintf(msg, sizeof(msg), "esp_ota_end: %s", esp_err_to_name(err));
        _drawError(msg);
        return;
    }
    Serial.println("[SDLauncher] Image verified OK");

    // ── 6. Set ota_0 as next boot — DELIBERATELY leave PENDING_VERIFY ─────────
    //
    // We do NOT call esp_ota_mark_app_valid_cancel_rollback().
    // Effect: ota_0 state = ESP_OTA_IMG_NEW → bootloader promotes to PENDING_VERIFY.
    // The guest app will not know to confirm itself.
    // When the user presses physical RESET (or the guest crashes), the bootloader
    // sees PENDING_VERIFY still set, marks ota_0 ABORTED, and falls back to
    // the factory partition (Purplx). This is the "one-time boot" guarantee.
    //
    // Requires: CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y in sdkconfig.defaults
    err = esp_ota_set_boot_partition(guest);
    if (err != ESP_OK) {
        char msg[56]; snprintf(msg, sizeof(msg), "esp_ota_set_boot: %s", esp_err_to_name(err));
        _drawError(msg);
        return;
    }
    Serial.println("[SDLauncher] Boot partition set to ota_0");

    // ── 7. Final launch screen ─────────────────────────────────────────────────
    _drawProgress(100, "Flash complete — launching...");
    delay(700);     // give the user time to read the "Launching..." message

    // Blank both displays before guest firmware takes over SPI.
    // Note: PIN_EXT_BL is hardwired to 3.3V on this hardware (no SW backlight
    // control). We blank screen content instead — the guest SPI init will
    // briefly glitch the external display anyway.
    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_lcd_secondary.fillScreen(TFT_BLACK);
        g_lcd_primary.fillScreen(TFT_BLACK);
        xSemaphoreGive(g_display_mutex);
    }
    delay(150);

    Serial.println("[SDLauncher] Rebooting into guest firmware. "
                   "Physical RESET = return to Purplx.");
    Serial.flush();
    esp_restart();  // ← never returns
}

// ─── Public API ───────────────────────────────────────────────────────────────

void start() {
    _cursor = 0;
    _scroll = 0;
    _screen = Screen::LIST;
    _initAndScan();
    _drawList();
}

void stop() {
    // Release SD bus so display has exclusive SPI2 access again.
    if (_sd_ok) {
        SD.end();
        _sd_ok = false;
        Serial.println("[SDLauncher] SD released");
    }
}

void handleKey(char key) {
    // ── Error dismiss ─────────────────────────────────────────────────────────
    if (_screen == Screen::ERROR_VIEW) {
        _screen = Screen::LIST;
        _drawList();
        return;
    }

    // ── Confirm screen ────────────────────────────────────────────────────────
    if (_screen == Screen::CONFIRM) {
        if (key == '\n' || key == '\r') {
            // Flash! (blocking — ESP.restart() at end, never returns on success)
            _flashAndBoot(_paths[_cursor]);
            // If we reach here, _flashAndBoot showed an error screen.
            // The error screen handler above will send user back to list on next key.
        } else {
            // Any other key (including ESC, though ESC is caught by main.cpp first)
            _screen = Screen::LIST;
            _drawList();
        }
        return;
    }

    // ── List screen ───────────────────────────────────────────────────────────
    if (key == 'r' || key == 'R') {
        // Retry: re-init SD and rescan
        if (_sd_ok) SD.end();
        start();
        return;
    }

    if (!_sd_ok || _count == 0) return;  // no files to navigate

    if (key == 'w' || key == 'W') {
        if (_cursor > 0) {
            _cursor--;
            if (_cursor < _scroll) _scroll = _cursor;
            _drawList();
        }
    } else if (key == 's' || key == 'S') {
        if (_cursor < _count - 1) {
            _cursor++;
            if (_cursor >= _scroll + VISIBLE) _scroll = _cursor - VISIBLE + 1;
            _drawList();
        }
    } else if (key == '\n' || key == '\r') {
        // First Enter → confirmation screen (prevents accidental flash)
        _screen = Screen::CONFIRM;
        _drawConfirm();
    }
}

} // namespace SDLauncher
