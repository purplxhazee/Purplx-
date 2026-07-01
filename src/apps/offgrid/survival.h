#pragma once
// =============================================================================
// survival.h — Offline Survival Reference (built-in, dual-screen)
// =============================================================================
// A browsable library of survival/off-grid reference material stored in the
// firmware (works with zero SD card, zero signal). Topics: first aid, knots,
// morse code, emergency signals, water, fire, navigation. Scrollable reader.
// =============================================================================

#include <Arduino.h>

namespace OffGrid { namespace Survival {

void start();
void handleKey(char key);
bool isRunning();
bool atIndex();
void stop();

} } // namespace OffGrid::Survival
