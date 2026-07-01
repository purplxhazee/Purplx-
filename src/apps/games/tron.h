#pragma once
// =============================================================================
// tron.h — Tron Light Cycles (built-in, dual-screen)
// =============================================================================
// Two light cycles leave trails. Crash into any trail or wall and you lose.
// Modes: 2-player (P1 = WASD, P2 = arrow-ish keys) or 1-player vs AI.
// The AI avoids walls/trails and turns to survive. Last cycle riding wins.
// =============================================================================

#include <Arduino.h>

namespace Games { namespace Tron {

void start();              // shows mode select (2P / vs AI)
void handleKey(char key);
void tick(uint32_t now);
bool isRunning();
void stop();

} } // namespace Games::Tron
