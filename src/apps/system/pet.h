#pragma once
// =============================================================================
// pet.h — Purplx Pet (tamagotchi-style virtual pet, dual-screen)
// =============================================================================
// A virtual pet that lives on your cyberdeck. It gets hungry, bored, dirty,
// and tired over real time — even while the device is off (state + timestamp
// saved to NVS, aged forward on next boot). Feed, play, clean, and put it to
// sleep to keep it happy. It grows from egg -> baby -> child -> teen -> adult.
//
// Animated pixel creature on the ILI9341; stats + actions on the ST7789.
// Controls: A/D pick action, ENTER do it, ESC back to home.
// =============================================================================

#include <Arduino.h>

namespace Pet {

void start();
void handleKey(char key);
void tick(uint32_t now);
bool isRunning();
void stop();

// Called once at boot (even if not opened) so the pet ages in the background
// and we can show alerts. Loads state + applies elapsed-time decay.
void boot_load();

} // namespace Pet
