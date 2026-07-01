#pragma once
// =============================================================================
// main.h — Purplx Global Header (pure cyberdeck)
// =============================================================================

#include <Arduino.h>
#include <M5GFX.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "pin_config.h"
#include "display_config.h"
#include "themes.h"

#ifndef PURPLX_VERSION
#define PURPLX_VERSION "1.0.0-dev"
#endif

// ─── Global display instances ────────────────────────────────────────────────
extern LGFX_Primary   g_lcd_primary;     // ST7789 240x135 (internal)
extern LGFX_Secondary g_lcd_secondary;   // ILI9341 320x240 (external)

// ─── Dual-screen flag ─────────────────────────────────────────────────────────
extern bool g_dual_screen;   // true = ILI9341 present, false = internal only

// ─── App states ───────────────────────────────────────────────────────────────
enum class AppState : uint8_t {
    BOOT          = 0,
    SCREEN_SELECT = 1,    // first-boot screen chooser
    HOME          = 2,

    // Games
    G_SNAKE       = 10,
    G_TETRIS      = 11,
    G_WORDLE      = 12,
    G_CHESS       = 13,
    G_CHECKERS    = 14,
    G_TRON        = 15,
    G_PONG        = 16,
    G_2048        = 17,
    G_LIFE        = 18,
    G_NETRUN      = 19,

    // Off-grid
    OG_SURVIVAL   = 30,
    OG_MORSE      = 31,
    OG_SOS        = 32,
    OG_GPS        = 33,    // slot — needs CapLoRa
    OG_LORA       = 34,    // slot — needs CapLoRa
    OG_COMPASS    = 35,    // slot — needs CapLoRa

    // EDC
    EDC_CLOCK     = 50,
    EDC_TIMER     = 51,
    EDC_FLASHLIGHT= 52,
    EDC_NOTES     = 53,
    EDC_CONVERT   = 54,
    EDC_DICE      = 55,
    EDC_CALC      = 56,

    // Media
    MEDIA_MUSIC   = 70,

    // Learn
    LEARN         = 80,

    // Tools
    TOOL_CSI      = 60,   // passive WiFi human radar
    TOOL_PET      = 61,   // tamagotchi virtual pet

    // System
    SYS_SETTINGS  = 90,
    SYS_INFO      = 91,
    SYS_CHARGING  = 92,
    SYS_FIRMWARES = 93,   // "Other Firmwares" info screen
    SYS_HELP      = 94,

    SHUTDOWN      = 255
};

extern volatile AppState g_app_state;
extern volatile AppState g_help_return_state;

bool OS_SetState(AppState next);

// ─── Global mutexes ───────────────────────────────────────────────────────────
extern SemaphoreHandle_t g_display_mutex;
extern SemaphoreHandle_t g_sd_mutex;

// =============================================================================
// CSI Radar API (passive WiFi human-presence sensing)
// =============================================================================
namespace CSI {
    struct Config {
        uint8_t  fixed_channel    = 6;
        uint16_t window_size      = 50;
        float    motion_threshold = 0.35f;
        bool     render_waterfall = true;
        bool     render_hud       = true;
    };
    extern Config g_config;

    struct Metrics {
        float    activity_score;
        bool     motion_detected;
        uint32_t packet_count;
        uint8_t  active_subcarriers;
        uint8_t  last_mac[6];
        int8_t   last_rssi;
        uint32_t last_ts_ms;
    };
    extern volatile Metrics g_metrics;

    void start_scanning(); void stop_scanning(); bool isRunning();
    void render_waterfall(LGFX_Secondary& lcd);
    void render_hud(LGFX_Primary& lcd);
}

// ─── OS utilities ─────────────────────────────────────────────────────────────
namespace OS {
    struct KeyEvent { char key; bool shift; bool fn; bool pressed; };
    bool Keyboard_poll(KeyEvent& out);
    void Keyboard_begin();
    bool SD_init(); bool SD_ready(); void SD_ensureDirectories();
    bool SD_mount(); void SD_unmount();
    float getBatteryVoltage();
    void reboot();
    void deepSleep(uint32_t wake_after_ms);
}
