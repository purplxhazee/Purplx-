// =============================================================================
// wifi_ota.h — WiFi Firmware Downloader for Purplx
// =============================================================================
#pragma once

namespace WiFiOTA {
    void start();
    void stop();
    void handleKey(char key);
}
