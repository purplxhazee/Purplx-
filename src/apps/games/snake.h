#pragma once
// =============================================================================
// snake.h — Snake Game (built-in, dual-screen)
// =============================================================================
// Classic snake on the ILI9341. Score + controls shown on the ST7789 HUD.
// Controls: WASD or arrow keys to turn, ESC to quit, ENTER to restart on
// game-over. Speed ramps up as you eat. High score persists to NVS.
// =============================================================================

#include <Arduino.h>
#include "../../core/display.h"

namespace Games { namespace Snake {

void start();                       // Init game state, draw board
void handleKey(char key);           // Feed a keypress
void tick(uint32_t now);            // Advance one frame (call every loop)
bool isRunning();
void stop();

} } // namespace Games::Snake
