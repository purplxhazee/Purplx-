#pragma once
// =============================================================================
// radio_manager.h — Feature Dormancy System
// =============================================================================
// The ESP32-S3 has ONE 2.4GHz radio shared between WiFi and BLE.
// LoRa (SX1262) is a separate SPI peripheral but its SPI bus may conflict.
//
// EVERY module MUST call RadioManager::request() before touching any radio
// hardware, and RadioManager::release() inside its stop() function.
//
// The manager explicitly tears down the previous stack before initialising
// the new one. This prevents:
//   - Guru Meditation crashes from double-init
//   - Memory leaks from orphaned WiFi/BLE tasks
//   - Conflicts from concurrent promiscuous + AP modes
//
// Architecture validated against M5PorkChop's dual-core pattern:
//   "WiFi task on Core 0; callbacks set volatile flags; no malloc in ISR"
//   Channel hop order: 1→6→11→2-5→7-10→12-13 (confirmed from PorkChop)
//
// Usage:
//   if (!RadioManager::request(RadioMode::WIFI_CSI)) return; // Radio busy
//   // ... safe to initialise CSI ...
//   // In stop(): RadioManager::release();
// =============================================================================

#include <Arduino.h>

enum class RadioMode : uint8_t {
    IDLE              = 0,
    WIFI_STATION      = 1,   // STA connected to AP (file transfer, cloud uploads)
    WIFI_ACCESS_POINT = 2,   // Soft-AP only (captive portal)
    WIFI_AP_PLUS_STA  = 3,   // AP+STA (Evil-M5 evil twin)
    WIFI_PROMISCUOUS  = 4,   // Passive sniff, no TX (DO NO HAM style)
    WIFI_CSI          = 5,   // CSI scanner (promiscuous + csi_rx_cb)
    WIFI_SCAN         = 6,   // Active scan (wardriving channel hop)
    BLE_ACTIVE        = 7,   // NimBLE stack (BLE spam, BLE wardrive)
    LORA_ACTIVE       = 8    // SX1262 active (WiFi/BLE must be off)
};

class RadioManager {
public:
    // Request a radio mode transition.
    // If current mode != desired: tears down current, then inits desired.
    // Blocks up to ~500ms during teardown. Call from application task only.
    // Returns true on success.
    static bool      request(RadioMode desired);

    // Release current mode → IDLE. Call from every module's stop().
    static void      release();

    // Current mode query (safe to call from any context).
    static RadioMode currentMode();
    static bool      isBusy();      // true if any radio stack is active

    // Canonical channel hop sequence (matches PorkChop / Marauder)
    // Primary channels first (highest AP density), then fill-in.
    static constexpr uint8_t HOP_SEQUENCE[] = {
        1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13
    };
    static constexpr uint8_t HOP_COUNT = 13;

    // Human-readable mode name for Serial logging
    static const char* modeName(RadioMode m);

private:
    static RadioMode     _current;
    static portMUX_TYPE  _mux;

    // Teardown functions — ensure complete stack shutdown
    static void _killWiFi();
    static void _killBLE();
    static void _killLoRa();

    // Init functions — return true on success
    static bool _initWiFiStation();
    static bool _initWiFiAP();
    static bool _initWiFiAPSTA();
    static bool _initWiFiPromiscuous();
    static bool _initWiFiCSI();
    static bool _initWiFiScan();
    static bool _initBLE();
    static bool _initLoRa();
};
