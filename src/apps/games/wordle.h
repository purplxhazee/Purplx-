#pragma once
// =============================================================================
// wordle.h — Wordle Game (built-in, dual-screen)
// =============================================================================
// Guess the 5-letter word in 6 tries. Green = right spot, Yellow = wrong spot,
// Grey = not in word. Type letters on the Cardputer keyboard, ENTER to submit,
// BACKSPACE to delete. Word list loads from /purplx/games/words5.txt if present,
// otherwise falls back to a small built-in list so it works with no SD card.
// =============================================================================

#include <Arduino.h>

namespace Games { namespace Wordle {

void start();
void handleKey(char key);
bool isRunning();
void stop();

} } // namespace Games::Wordle
