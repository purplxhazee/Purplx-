#pragma once
#include <Arduino.h>
namespace Tools { namespace Notes {
    void start(); void handleKey(char key); bool isRunning(); void stop();
}}
