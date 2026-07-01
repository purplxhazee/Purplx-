#pragma once
// =============================================================================
// display.h — Core display manager (init + globals + mutexes)
// =============================================================================
#include "../../include/main.h"

namespace Display {
    bool init();
    void setPrimaryBrightness(uint8_t v);
    void setSecondaryBrightness(uint8_t v);
    void clearAll();
}
