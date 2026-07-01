#pragma once
// pong.h — Pong (2P + AI)
#include <Arduino.h>
namespace Games { namespace Pong {
void start(); void handleKey(char key); void tick(uint32_t now);
bool isRunning(); void stop();
} }
