#pragma once
// life.h — Conway's Game of Life (animated cyberdeck screensaver-game)
#include <Arduino.h>
namespace Games { namespace Life {
void start(); void handleKey(char key); void tick(uint32_t now);
bool isRunning(); void stop();
} }
