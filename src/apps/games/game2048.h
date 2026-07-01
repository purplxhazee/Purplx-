#pragma once
// game2048.h
#include <Arduino.h>
namespace Games { namespace G2048 {
void start(); void handleKey(char key); bool isRunning(); void stop();
} }
