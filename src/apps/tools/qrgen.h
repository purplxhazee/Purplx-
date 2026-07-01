#pragma once
#include <Arduino.h>
namespace Tools { namespace QRGen {
    void start(); void handleKey(char key); bool isRunning(); void stop();
}}
