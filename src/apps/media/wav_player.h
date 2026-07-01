#pragma once
// =============================================================================
// wav_player.h — WAV Music Player (built-in, dual-screen)
// =============================================================================
// Plays .wav files from /purplx/music/ on the SD card through the ADV speaker
// via M5.Speaker (I2S). Now-playing UI on ILI9341 with an animated visualizer;
// transport controls on the ST7789 HUD.
//
// Supported: PCM WAV, 8/16-bit, mono/stereo, up to 44.1kHz.
// (MP3 intentionally not supported — WAV is far more reliable on ESP32.)
//
// Controls: SPACE play/pause, A/D prev/next, W/S volume, ESC quit.
// =============================================================================

#include <Arduino.h>

namespace Music {

void start();              // Scan /purplx/music, draw browser
void handleKey(char key);
void tick(uint32_t now);   // Feed PCM to speaker + animate visualizer
bool isRunning();
void stop();

} // namespace Music
