// =============================================================================
// charging.cpp — Charging / Battery Mode
// =============================================================================
// Uses M5.Power for battery level + charging state where available.
// Dims both screens to reduce draw. Animated lightning bolt when charging.
// =============================================================================

#include "charging.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include "../../core/display.h"
#include <M5Unified.h>

// Pull in the dual-screen mode flag (defined in main.cpp)
extern bool g_dual_screen;

namespace Charging {

static bool     _running = false;
static uint32_t _last_anim = 0;
static int      _bolt_frame = 0;
static uint8_t  _saved_pri_bright = 180;
static uint8_t  _saved_sec_bright = 220;

static const ColorTheme* T(){ return g_theme; }

// Battery voltage history for charge-rate estimate (simple)
static float _vhist[10] = {0};
static int   _vhist_n = 0;

static int batteryPercent(){
    int p = M5.Power.getBatteryLevel();   // 0-100, -1 if unknown
    if (p < 0) p = 50;
    return p;
}
static bool isCharging(){
    auto c = M5.Power.isCharging();
    return (c == m5::Power_Class::is_charging_t::is_charging);
}
static float batteryVoltage(){
    int mv = M5.Power.getBatteryVoltage();   // millivolts
    if (mv <= 0) return 3.85f;
    return mv / 1000.0f;
}

// =============================================================================
static void drawMain(){
    const int W = g_dual_screen ? 320 : 240;
    lgfx::v1::LovyanGFX* scr = g_dual_screen
        ? (lgfx::v1::LovyanGFX*)&g_lcd_secondary
        : (lgfx::v1::LovyanGFX*)&g_lcd_primary;

    scr->fillScreen(T()->bg);

    const int p   = batteryPercent();
    const bool ch = isCharging();
    const float v = batteryVoltage();

    // Title
    scr->fillRect(0,0,W,22,T()->title_bg);
    scr->setTextSize(1);
    scr->setTextColor(T()->primary,T()->title_bg);
    scr->setCursor(8,7);
    scr->print(ch ? "CHARGING" : "BATTERY");

    // Big battery icon
    const int bw = g_dual_screen ? 180 : 140;
    const int bh = g_dual_screen ? 80 : 60;
    const int bx = (W - bw)/2;
    const int by = g_dual_screen ? 50 : 40;

    // battery body
    scr->drawRect(bx, by, bw, bh, T()->text);
    scr->drawRect(bx+1, by+1, bw-2, bh-2, T()->text);
    // terminal nub
    scr->fillRect(bx+bw, by+bh/3, 6, bh/3, T()->text);

    // fill level
    const int fillW = (bw-8) * p / 100;
    uint16_t fillCol = (p > 50) ? T()->success
                     : (p > 20) ? RGB565(220,170,0)
                     : T()->alert;
    scr->fillRect(bx+4, by+4, fillW, bh-8, fillCol);

    // percent text centered on battery
    scr->setTextSize(g_dual_screen ? 4 : 3);
    scr->setTextColor(TFT_WHITE);
    char pct[8]; snprintf(pct,sizeof(pct),"%d%%",p);
    int tw = scr->textWidth(pct);
    scr->setCursor(bx + (bw-tw)/2, by + bh/2 - (g_dual_screen?14:10));
    scr->print(pct);

    // charging bolt (animated)
    if (ch){
        const int boltX = bx + bw/2;
        const int boltY = by - 8;
        uint16_t bc = (_bolt_frame % 2) ? RGB565(255,230,0) : T()->secondary;
        // simple lightning triangle
        scr->fillTriangle(boltX-6, boltY-12, boltX+4, boltY-12, boltX-2, boltY, bc);
        scr->fillTriangle(boltX+2, boltY, boltX-4, boltY+12, boltX+6, boltY, bc);
    }

    // stats
    int sy = by + bh + 16;
    scr->setTextSize(1);
    scr->setTextColor(T()->text,T()->bg);
    scr->setCursor(bx, sy);
    scr->printf("Voltage: %.2f V", (double)v);
    scr->setCursor(bx, sy+16);
    scr->setTextColor(ch ? T()->success : T()->text_dim, T()->bg);
    scr->print(ch ? "Status:  Charging via USB-C" : "Status:  On battery");

    if (!ch && p <= 20){
        scr->setCursor(bx, sy+32);
        scr->setTextColor(T()->alert,T()->bg);
        scr->print("Low battery - plug in soon");
    }

    // footer
    scr->fillRect(0, (g_dual_screen?222:118), W, (g_dual_screen?18:17), T()->title_bg);
    scr->setTextColor(T()->text_dim,T()->title_bg);
    scr->setCursor(6, g_dual_screen?227:120);
    scr->print("Screens dimmed  [ESC] exit");

    // On dual-screen, show a minimal HUD on the primary too
    if (g_dual_screen){
        g_lcd_primary.fillScreen(T()->hud_bg);
        g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
        g_lcd_primary.setTextSize(2);
        g_lcd_primary.setCursor(4,6);
        g_lcd_primary.print(ch?"CHARGE":"BATT");
        g_lcd_primary.setTextSize(3);
        g_lcd_primary.setTextColor(fillCol,T()->hud_bg);
        g_lcd_primary.setCursor(4,36);
        g_lcd_primary.printf("%d%%",p);
        g_lcd_primary.setTextSize(1);
        g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
        g_lcd_primary.setCursor(4,80);
        g_lcd_primary.printf("%.2fV", (double)v);
    }
}

// =============================================================================
void start(){
    _running = true;
    M5.Power.begin();

    // Save + lower brightness to save power
    _saved_pri_bright = 180;
    _saved_sec_bright = 220;
    g_lcd_primary.setBrightness(40);
    if (g_dual_screen) g_lcd_secondary.setBrightness(40);

    _vhist_n = 0;
    drawMain();
}

void handleKey(char key){
    // ESC handled by main loop
}

void tick(uint32_t now){
    if (!_running) return;
    // Animate bolt + refresh stats once per second
    if (now - _last_anim > 1000){
        _last_anim = now;
        _bolt_frame++;
        drawMain();
    }
}

bool isRunning(){ return _running; }

void stop(){
    _running = false;
    // Restore brightness
    g_lcd_primary.setBrightness(_saved_pri_bright);
    if (g_dual_screen) g_lcd_secondary.setBrightness(_saved_sec_bright);
}

} // namespace Charging
