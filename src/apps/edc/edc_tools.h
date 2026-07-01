#pragma once
// =============================================================================
// edc_tools.h — Everyday Carry utilities
// =============================================================================
// Small self-contained tools: clock/stopwatch/timer, flashlight, dice/coin,
// notes, calculator, unit converter. Each has start/handleKey/tick/stop.
// =============================================================================

#include <Arduino.h>

namespace EDC {

namespace Clock {
    void start(); void handleKey(char key); void tick(uint32_t now);
    bool isRunning(); void stop();
}

namespace Flashlight {
    void start(); void handleKey(char key); void tick(uint32_t now);
    bool isRunning(); void stop();
}

namespace Dice {
    void start(); void handleKey(char key); bool isRunning(); void stop();
}

namespace Notes {
    void start(); void handleKey(char key); bool isRunning(); void stop();
}

namespace Calc {
    void start(); void handleKey(char key); bool isRunning(); void stop();
}

namespace Convert {
    void start(); void handleKey(char key); bool isRunning(); void stop();
}

} // namespace EDC
