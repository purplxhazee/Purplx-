#pragma once
// =============================================================================
// cyberfx.h — Animated Cyberdeck Background Engine
// =============================================================================
// Reusable animated backdrops drawn into any LovyanGFX sprite/canvas.
// Used by the home menu, splash, and idle screens to give the cyberdeck
// its "alive" feel. All effects are theme-aware (use g_theme colors).
//
// Effects:
//   MATRIX  — falling code rain (Ghost in the Shell style)
//   GRID    — scrolling perspective grid (Tron style)
//   STARS   — warp-speed starfield
//   PULSE   — concentric radar pulses from center
//
// Each effect is cheap enough to run at ~20-30fps behind a menu on the
// ILI9341. They render to a provided sprite so the caller controls
// compositing (draw background, then draw menu on top, then push).
// =============================================================================

#include <Arduino.h>
#include <M5GFX.h>

namespace CyberFX {

enum class Effect : uint8_t {
    MATRIX = 0,
    GRID   = 1,
    STARS  = 2,
    PULSE  = 3,
    COUNT  = 4
};

// Initialise effect state (call once when switching effects or on boot).
// w/h are the dimensions of the target sprite.
void begin(Effect fx, int w, int h);

// Advance + draw the current effect into the given sprite.
// Does NOT push the sprite — caller composites menu on top then pushes.
// 'now' is millis(). Pass a dim factor 0.0-1.0 to fade the effect
// (useful so menu text stays readable on top).
void draw(LGFX_Sprite& spr, uint32_t now, float dim = 1.0f);

// Cycle to the next effect (wraps).
void next(int w, int h);

// Current effect name for display.
const char* name();
Effect      current();

} // namespace CyberFX
