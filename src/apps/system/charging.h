#pragma once
// =============================================================================
// charging.h — Charging / Battery Mode (built-in, dual-screen aware)
// =============================================================================
// A low-power-friendly battery screen. Shows battery %, voltage, charging
// status, and an animated charging indicator. Dims screens to save power.
// On single-screen setups, shows everything on the internal display.
// ESC exits back to home.
// =============================================================================

#include <Arduino.h>

namespace Charging {

void start();
void handleKey(char key);
void tick(uint32_t now);
bool isRunning();
void stop();

} // namespace Charging
