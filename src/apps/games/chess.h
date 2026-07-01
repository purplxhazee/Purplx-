#pragma once
// =============================================================================
// chess.h — Chess (built-in, dual-screen, two-player local)
// =============================================================================
// Full legal-move chess for two players passing the device. Renders an 8x8
// board on the ILI9341 with a cursor you move with WASD; ENTER selects a piece
// then selects a destination. Enforces piece movement rules, check, checkmate,
// castling, en passant, and pawn promotion (auto-queen for simplicity).
// Turn indicator + captured pieces on the ST7789 HUD.
// =============================================================================

#include <Arduino.h>

namespace Games { namespace Chess {

void start();
void handleKey(char key);
bool isRunning();
void stop();

} } // namespace Games::Chess
