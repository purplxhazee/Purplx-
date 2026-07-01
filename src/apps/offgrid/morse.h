#pragma once
// =============================================================================
// morse.h — Morse Code Trainer & Sender (built-in, dual-screen)
// =============================================================================
// Type text, see it converted to morse, and transmit it as screen flashes +
// speaker beeps. Also shows a reference chart. Useful off-grid signaling skill.
// Controls: type letters to build a message, ENTER to transmit, TAB toggles
// the reference chart, ESC quits.
// =============================================================================

#include <Arduino.h>

namespace OffGrid { namespace Morse {

void start();
void handleKey(char key);
void tick(uint32_t now);   // drives the flash/beep transmission timing
bool isRunning();
void stop();

} } // namespace OffGrid::Morse
