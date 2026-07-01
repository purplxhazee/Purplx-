#pragma once
// =============================================================================
// tetris.h — Tetris Game (built-in, dual-screen)
// =============================================================================
// Classic Tetris on the ILI9341. Next-piece + score on the ST7789 HUD.
// Controls: A/D move, W rotate, S soft-drop, SPACE hard-drop, ESC quit.
// High score persists to NVS.
// =============================================================================

#include <Arduino.h>

namespace Games { namespace Tetris {

void start();
void handleKey(char key);
void tick(uint32_t now);
bool isRunning();
void stop();

} } // namespace Games::Tetris
