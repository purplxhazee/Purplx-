#pragma once
// =============================================================================
// learn.h — Ethical Hacking Learning Library
// =============================================================================
// Plain-English lessons that teach the concepts behind the tools, written for
// a curious beginner. This is the heart of the "learn as you go" cyberdeck.
// Each lesson explains: what it is, how it works, why it works, and the legal
// reality. Navigable on the ILI9341 with scrolling.
// =============================================================================

#include <Arduino.h>

namespace Learn {

void start();              // Show lesson index
void handleKey(char key);
bool isRunning();
bool atIndex();            // true if at lesson list (ESC exits) vs reading (ESC=back to list)
void stop();

} // namespace Learn
