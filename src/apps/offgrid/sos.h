#pragma once
// sos.h — Emergency SOS beacon (screen strobe + speaker morse SOS)
#include <Arduino.h>
namespace OffGrid { namespace SOS {
void start(); void handleKey(char key); void tick(uint32_t now);
bool isRunning(); void stop();
} }
