#pragma once
// =============================================================================
// netrun.h — NETRUN: a cyberpunk roguelike (built-in, dual-screen)
// =============================================================================
// Dive through procedurally generated network floors. Bump cyber-enemies to
// attack, grab loot, upgrade gear, use stims, and descend as deep as you can.
// Big screen (ILI9341) = the grid map. Internal (ST7789) = stats + combat log.
// Controls: arrows/WASD to move, E to use a stim, ESC to quit, ENTER to restart
// on game-over. Permadeath. Depth + best depth persist to NVS.
// =============================================================================

#include <Arduino.h>
#include "../../core/display.h"

namespace Games { namespace Netrun {

void start();
void handleKey(char key);
void tick(uint32_t now);
bool isRunning();
void stop();

} }  // namespace Games::Netrun
