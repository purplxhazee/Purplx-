#pragma once
// checkers.h — Checkers (2P + basic AI)
#include <Arduino.h>
namespace Games { namespace Checkers {
void start(); void handleKey(char key); bool isRunning(); void stop();
} }
