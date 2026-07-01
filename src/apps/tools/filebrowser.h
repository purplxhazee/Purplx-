#pragma once
#include <Arduino.h>
namespace Tools { namespace FileBrowser {
    void start(); void handleKey(char key); bool isRunning(); void stop();
}}
