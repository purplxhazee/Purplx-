#pragma once
// =============================================================================
// themes.h — MegaFirmware Color Theme System
// =============================================================================
// 7 themes. Default is PURPLE. Persists across reboots via NVS.
// Apply via: Themes::set(ThemeID::RED);
// Use colors via: g_theme.primary, g_theme.bg, etc.
// =============================================================================

#include <Arduino.h>

// ─── RGB565 helper (compile-time safe) ───────────────────────────────────────
#define RGB565(r,g,b) ((uint16_t)(((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))

// ─── Theme palette ─────────────────────────────────────────────────────────
struct ColorTheme {
    const char* name;

    uint16_t primary;       // Main accent — titles, selected text, highlights
    uint16_t secondary;     // Secondary accent — values, subheadings
    uint16_t bg;            // Screen background
    uint16_t text;          // Normal body text
    uint16_t text_dim;      // Dimmed/inactive text
    uint16_t title_bg;      // Title bar background
    uint16_t row_a;         // Menu row (even)
    uint16_t row_b;         // Menu row (odd)
    uint16_t highlight_bg;  // Selected row background
    uint16_t highlight_text;// Selected row text
    uint16_t border;        // Box / frame borders
    uint16_t success;       // OK / CLEAR / connected
    uint16_t alert;         // Warning / MOTION / error
    uint16_t hud_bg;        // Primary (ST7789) HUD background
};

// ─── Theme definitions ────────────────────────────────────────────────────────

static constexpr ColorTheme THEME_PURPLE = {
    "PURPLE",
    RGB565(220,  0,220),   // primary    — magenta
    RGB565(  0,220,220),   // secondary  — cyan
    RGB565(  0,  0,  0),   // bg         — black
    RGB565(255,255,255),   // text       — white
    RGB565( 90, 90, 90),   // text_dim   — grey
    RGB565( 20,  0, 35),   // title_bg   — deep purple
    RGB565( 12,  0, 20),   // row_a
    RGB565(  6,  0, 12),   // row_b
    RGB565(  0,200,220),   // highlight_bg — cyan
    RGB565(  0,  0,  0),   // highlight_text — black
    RGB565(160,  0,160),   // border
    RGB565(  0,220, 80),   // success    — green
    RGB565(255, 50, 50),   // alert      — red
    RGB565( 10,  0, 20),   // hud_bg
};

static constexpr ColorTheme THEME_WHITE = {
    "WHITE",
    RGB565(255,255,255),   // primary    — white
    RGB565(180,200,255),   // secondary  — light blue
    RGB565(  8,  8, 20),   // bg         — very dark navy
    RGB565(255,255,255),   // text
    RGB565(110,110,130),   // text_dim
    RGB565( 25, 25, 50),   // title_bg
    RGB565( 16, 16, 35),   // row_a
    RGB565(  8,  8, 20),   // row_b
    RGB565(255,255,255),   // highlight_bg
    RGB565(  0,  0, 20),   // highlight_text
    RGB565(100,100,160),   // border
    RGB565(100,220,100),   // success
    RGB565(255, 80, 80),   // alert
    RGB565( 15, 15, 30),   // hud_bg
};

static constexpr ColorTheme THEME_RED = {
    "RED",
    RGB565(255, 40, 40),   // primary    — red
    RGB565(255,140,  0),   // secondary  — orange
    RGB565(  0,  0,  0),   // bg
    RGB565(255,255,255),   // text
    RGB565( 90, 50, 50),   // text_dim
    RGB565( 30,  0,  0),   // title_bg
    RGB565( 18,  0,  0),   // row_a
    RGB565(  8,  0,  0),   // row_b
    RGB565(255, 40, 40),   // highlight_bg
    RGB565(255,255,255),   // highlight_text
    RGB565(160,  0,  0),   // border
    RGB565(  0,220, 80),   // success
    RGB565(255,200,  0),   // alert      — amber
    RGB565( 15,  0,  0),   // hud_bg
};

static constexpr ColorTheme THEME_GREEN = {
    "GREEN",
    RGB565(  0,255, 60),   // primary    — bright green
    RGB565( 80,255, 80),   // secondary  — lime
    RGB565(  0,  0,  0),   // bg
    RGB565(255,255,255),   // text
    RGB565( 50, 90, 50),   // text_dim
    RGB565(  0, 25,  0),   // title_bg
    RGB565(  0, 14,  0),   // row_a
    RGB565(  0,  7,  0),   // row_b
    RGB565(  0,200, 40),   // highlight_bg
    RGB565(  0,  0,  0),   // highlight_text
    RGB565(  0,140,  0),   // border
    RGB565(  0,255, 60),   // success
    RGB565(255, 80, 80),   // alert
    RGB565(  0, 10,  0),   // hud_bg
};

static constexpr ColorTheme THEME_YELLOW = {
    "YELLOW",
    RGB565(255,240,  0),   // primary    — yellow
    RGB565(255,200,  0),   // secondary  — gold
    RGB565(  0,  0,  0),   // bg
    RGB565(255,255,255),   // text
    RGB565( 90, 80, 40),   // text_dim
    RGB565( 25, 20,  0),   // title_bg
    RGB565( 14, 11,  0),   // row_a
    RGB565(  7,  5,  0),   // row_b
    RGB565(255,230,  0),   // highlight_bg
    RGB565(  0,  0,  0),   // highlight_text
    RGB565(160,130,  0),   // border
    RGB565(  0,220, 80),   // success
    RGB565(255, 60, 60),   // alert
    RGB565( 12,  9,  0),   // hud_bg
};

static constexpr ColorTheme THEME_ORANGE = {
    "ORANGE",
    RGB565(255,120,  0),   // primary    — orange
    RGB565(255,180, 60),   // secondary  — gold-orange
    RGB565(  0,  0,  0),   // bg
    RGB565(255,255,255),   // text
    RGB565( 90, 55, 20),   // text_dim
    RGB565( 25, 10,  0),   // title_bg
    RGB565( 14,  6,  0),   // row_a
    RGB565(  7,  3,  0),   // row_b
    RGB565(255,120,  0),   // highlight_bg
    RGB565(  0,  0,  0),   // highlight_text
    RGB565(160, 60,  0),   // border
    RGB565(  0,220, 80),   // success
    RGB565(255, 50, 50),   // alert
    RGB565( 12,  5,  0),   // hud_bg
};

static constexpr ColorTheme THEME_BLUE = {
    "BLUE",
    RGB565( 40,140,255),   // primary    — blue
    RGB565(  0,220,255),   // secondary  — cyan
    RGB565(  0,  0,  0),   // bg
    RGB565(255,255,255),   // text
    RGB565( 50, 70,100),   // text_dim
    RGB565(  0, 10, 35),   // title_bg
    RGB565(  0,  6, 20),   // row_a
    RGB565(  0,  3, 10),   // row_b
    RGB565( 40,140,255),   // highlight_bg
    RGB565(255,255,255),   // highlight_text
    RGB565(  0, 80,180),   // border
    RGB565(  0,220, 80),   // success
    RGB565(255, 60, 60),   // alert
    RGB565(  0,  5, 18),   // hud_bg
};

// Night-vision: everything red-on-black to preserve dark adaptation.
// Deliberately uses ONLY red channel — no blue/green leak at all.
static constexpr ColorTheme THEME_NIGHT = {
    "NIGHT",
    RGB565(255,  0,  0),   // primary    — pure red
    RGB565(180,  0,  0),   // secondary  — dim red
    RGB565(  0,  0,  0),   // bg         — pure black
    RGB565(255,  0,  0),   // text       — red
    RGB565( 90,  0,  0),   // text_dim   — dark red
    RGB565( 30,  0,  0),   // title_bg
    RGB565( 16,  0,  0),   // row_a
    RGB565(  8,  0,  0),   // row_b
    RGB565(160,  0,  0),   // highlight_bg
    RGB565(  0,  0,  0),   // highlight_text — black on red
    RGB565(120,  0,  0),   // border
    RGB565(255,  0,  0),   // success    — red (no green in night mode)
    RGB565(255,  0,  0),   // alert      — red
    RGB565(  6,  0,  0),   // hud_bg
};

// ─── Theme registry ───────────────────────────────────────────────────────────
enum class ThemeID : uint8_t {
    PURPLE = 0,
    WHITE  = 1,
    RED    = 2,
    GREEN  = 3,
    YELLOW = 4,
    ORANGE = 5,
    BLUE   = 6,
    NIGHT  = 7,
    COUNT  = 8
};

static const ColorTheme* const THEMES[] = {
    &THEME_PURPLE,
    &THEME_WHITE,
    &THEME_RED,
    &THEME_GREEN,
    &THEME_YELLOW,
    &THEME_ORANGE,
    &THEME_BLUE,
    &THEME_NIGHT,
};

// ─── Active theme (global) ────────────────────────────────────────────────────
extern const ColorTheme* g_theme;   // Always valid — default PURPLE at boot
extern ThemeID           g_theme_id;

namespace Themes {
    void          begin();                   // Load saved theme from NVS
    void          set(ThemeID id);           // Change + save to NVS
    void          next();                    // Cycle forward
    void          prev();                    // Cycle backward
    ThemeID       current();
    const char*   currentName();
}
