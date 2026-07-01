// =============================================================================
// display.cpp — Core display manager: global instances, mutexes, init
// =============================================================================
#include "display.h"
#include <M5Cardputer.h>
#include "../../include/themes.h"

// ─── Global instances (declared extern in main.h) ────────────────────────────
M5GFX&         g_lcd_primary = M5.Display;
LGFX_Secondary g_lcd_secondary;

volatile AppState g_app_state         = AppState::BOOT;
volatile AppState g_help_return_state = AppState::HOME;

bool g_dual_screen = true;

SemaphoreHandle_t g_display_mutex = nullptr;
SemaphoreHandle_t g_sd_mutex      = nullptr;

namespace Display {

bool init() {
    Serial.println("DISPLAY0");
    g_display_mutex = xSemaphoreCreateMutex();
    g_sd_mutex      = xSemaphoreCreateMutex();
    if (!g_display_mutex || !g_sd_mutex) {
        Serial.println("[Display] mutex alloc failed");
        return false;
    }
    Serial.println("DISPLAY1");

    // g_lcd_primary IS M5.Display (already initialized by M5Cardputer.begin); do NOT call .init()
    Serial.println("DISPLAY2");
    g_lcd_primary.setRotation(1);
    g_lcd_primary.setBrightness(180);
    g_lcd_primary.fillScreen(TFT_BLACK);
    Serial.println("[Display] ST7789 primary OK");

    Serial.println("DISPLAY3");
    const bool secondary_ok = g_lcd_secondary.init();
    if (secondary_ok) {
        Serial.println("DISPLAY4");
        g_lcd_secondary.setRotation(7);   // confirmed from skizzophrenic source
        g_lcd_secondary.setBrightness(220);
        g_lcd_secondary.fillScreen(TFT_BLACK);
        Serial.println("[Display] ILI9341 secondary OK");
        g_dual_screen = true;
    } else {
        g_dual_screen = false;
        Serial.println("[Display] ILI9341 secondary init failed; using primary screen only");
    }

    Serial.println("DISPLAY5");
    return true;
}

void setPrimaryBrightness(uint8_t v)   { g_lcd_primary.setBrightness(v); }
void setSecondaryBrightness(uint8_t v) { g_lcd_secondary.setBrightness(v); }

void clearAll() {
    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_lcd_primary.fillScreen(TFT_BLACK);
        g_lcd_secondary.fillScreen(TFT_BLACK);
        xSemaphoreGive(g_display_mutex);
    }
}

} // namespace Display
