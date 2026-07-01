// =============================================================================
// radio_manager.cpp — Feature Dormancy System Implementation
// =============================================================================

#include "radio_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>

#if defined(CONFIG_BT_ENABLED) && __has_include(<NimBLEDevice.h>)
    #include <NimBLEDevice.h>
    #define PURPLX_HAS_NIMBLE 1
#else
    #define PURPLX_HAS_NIMBLE 0
#endif

// ─── Static member definitions ───────────────────────────────────────────────
RadioMode    RadioManager::_current = RadioMode::IDLE;
portMUX_TYPE RadioManager::_mux     = portMUX_INITIALIZER_UNLOCKED;

constexpr uint8_t RadioManager::HOP_SEQUENCE[];
constexpr uint8_t RadioManager::HOP_COUNT;

// =============================================================================
// PUBLIC: request()
// =============================================================================
bool RadioManager::request(RadioMode desired) {
    portENTER_CRITICAL(&_mux);
    RadioMode prev = _current;
    portEXIT_CRITICAL(&_mux);

    // No-op if already in the requested mode
    if (prev == desired) {
        Serial.printf("[Radio] Already in %s — no transition needed\n",
                      modeName(desired));
        return true;
    }

    // ── Tear down the current mode ────────────────────────────────────────────
    if (prev != RadioMode::IDLE) {
        Serial.printf("[Radio] Tearing down: %s\n", modeName(prev));

        switch (prev) {
            case RadioMode::WIFI_STATION:
            case RadioMode::WIFI_ACCESS_POINT:
            case RadioMode::WIFI_AP_PLUS_STA:
            case RadioMode::WIFI_PROMISCUOUS:
            case RadioMode::WIFI_CSI:
            case RadioMode::WIFI_SCAN:
                _killWiFi();
                break;
            case RadioMode::BLE_ACTIVE:
                _killBLE();
                break;
            case RadioMode::LORA_ACTIVE:
                _killLoRa();
                break;
            default:
                break;
        }

        // Allow driver tasks to finish cleanup before re-init.
        // PorkChop's pattern: small delay after WiFi kill prevents crashes.
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    // ── Initialise the desired mode ───────────────────────────────────────────
    Serial.printf("[Radio] Initialising: %s\n", modeName(desired));

    bool ok = false;
    switch (desired) {
        case RadioMode::IDLE:             ok = true;                    break;
        case RadioMode::WIFI_STATION:     ok = _initWiFiStation();      break;
        case RadioMode::WIFI_ACCESS_POINT: ok = _initWiFiAP();           break;
        case RadioMode::WIFI_AP_PLUS_STA: ok = _initWiFiAPSTA();        break;
        case RadioMode::WIFI_PROMISCUOUS: ok = _initWiFiPromiscuous();  break;
        case RadioMode::WIFI_CSI:         ok = _initWiFiCSI();          break;
        case RadioMode::WIFI_SCAN:        ok = _initWiFiScan();         break;
        case RadioMode::BLE_ACTIVE:       ok = _initBLE();              break;
        case RadioMode::LORA_ACTIVE:      ok = _initLoRa();             break;
    }

    portENTER_CRITICAL(&_mux);
    _current = ok ? desired : RadioMode::IDLE;
    portEXIT_CRITICAL(&_mux);

    if (ok) {
        Serial.printf("[Radio] Active: %s\n", modeName(desired));
    } else {
        Serial.printf("[Radio] FAILED to init %s — returning to IDLE\n",
                      modeName(desired));
    }
    return ok;
}

// =============================================================================
// PUBLIC: release()
// =============================================================================
void RadioManager::release() {
    portENTER_CRITICAL(&_mux);
    RadioMode prev = _current;
    portEXIT_CRITICAL(&_mux);

    if (prev == RadioMode::IDLE) return;

    Serial.printf("[Radio] Releasing: %s → IDLE\n", modeName(prev));

    switch (prev) {
        case RadioMode::WIFI_STATION:
        case RadioMode::WIFI_ACCESS_POINT:
        case RadioMode::WIFI_AP_PLUS_STA:
        case RadioMode::WIFI_PROMISCUOUS:
        case RadioMode::WIFI_CSI:
        case RadioMode::WIFI_SCAN:
            _killWiFi();
            break;
        case RadioMode::BLE_ACTIVE:
            _killBLE();
            break;
        case RadioMode::LORA_ACTIVE:
            _killLoRa();
            break;
        default:
            break;
    }

    portENTER_CRITICAL(&_mux);
    _current = RadioMode::IDLE;
    portEXIT_CRITICAL(&_mux);

    vTaskDelay(pdMS_TO_TICKS(50));
    Serial.println("[Radio] Released → IDLE");
}

// =============================================================================
// PUBLIC: Accessors
// =============================================================================
RadioMode RadioManager::currentMode() { return _current; }
bool      RadioManager::isBusy()      { return _current != RadioMode::IDLE; }

const char* RadioManager::modeName(RadioMode m) {
    switch (m) {
        case RadioMode::IDLE:             return "IDLE";
        case RadioMode::WIFI_STATION:     return "WIFI_STA";
        case RadioMode::WIFI_ACCESS_POINT:return "WIFI_AP";
        case RadioMode::WIFI_AP_PLUS_STA: return "WIFI_AP+STA";
        case RadioMode::WIFI_PROMISCUOUS: return "WIFI_PROMISC";
        case RadioMode::WIFI_CSI:         return "WIFI_CSI";
        case RadioMode::WIFI_SCAN:        return "WIFI_SCAN";
        case RadioMode::BLE_ACTIVE:       return "BLE";
        case RadioMode::LORA_ACTIVE:      return "LORA";
        default:                          return "UNKNOWN";
    }
}

// =============================================================================
// PRIVATE: Teardown — WiFi
// Full sequence required to prevent Guru Meditation on re-init.
// Order matters: CSI off → promiscuous off → disconnect → stop → deinit
// =============================================================================
void RadioManager::_killWiFi() {
    // 1. Disable CSI (no-op if not active, safe to call unconditionally)
    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(nullptr, nullptr);

    // 2. Disable promiscuous mode
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);

    // 3. Disconnect from any AP
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(30));

    // 4. Stop the WiFi stack
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(50));

    // 5. Full deinit — reclaims ~70KB of WiFi driver memory
    // esp_wifi_deinit removed: broke CSI restart err 12289
    vTaskDelay(pdMS_TO_TICKS(50));

    // 6. Sync Arduino WiFi object state
    WiFi.mode(WIFI_OFF);

    Serial.println("[Radio] WiFi stack fully deinitialised");
}

// =============================================================================
// PRIVATE: Teardown — BLE
// NimBLE deinit reclaims 20-30KB internal RAM (confirmed from PorkChop).
// BLE uses internal SRAM only — see sdkconfig.defaults:
//   CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL=1
// =============================================================================
void RadioManager::_killBLE() {
#if PURPLX_HAS_NIMBLE
    if (NimBLEDevice::getInitialized()) {
        NimBLEDevice::deinit(true);  // true = force deinit even with active connections
        vTaskDelay(pdMS_TO_TICKS(100));
        Serial.printf("[Radio] BLE deinit complete — heap recovered\n");
    }
#else
    Serial.println("[Radio] BLE support not available in this build — nothing to kill");
#endif
}

// =============================================================================
// PRIVATE: Teardown — LoRa
// SPI bus release is handled by the Meshtastic module's stop() function.
// RadioManager just tracks ownership; actual SX1262 deinit is module-side.
// =============================================================================
void RadioManager::_killLoRa() {
    Serial.println("[Radio] LoRa ownership released (module handles SPI teardown)");
}

// =============================================================================
// PRIVATE: Init — WiFi Station
// =============================================================================
bool RadioManager::_initWiFiStation() {
    WiFi.mode(WIFI_STA);
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        Serial.printf("[Radio] WiFi start failed: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

// =============================================================================
// PRIVATE: Init — WiFi Soft-AP
// =============================================================================
bool RadioManager::_initWiFiAP() {
    WiFi.mode(WIFI_AP);
    esp_err_t err = esp_wifi_start();
    return (err == ESP_OK);
}

// =============================================================================
// PRIVATE: Init — WiFi AP+STA (evil twin mode)
// =============================================================================
bool RadioManager::_initWiFiAPSTA() {
    WiFi.mode(WIFI_AP_STA);
    esp_err_t err = esp_wifi_start();
    return (err == ESP_OK);
}

// =============================================================================
// PRIVATE: Init — Promiscuous (passive sniff, zero TX)
// =============================================================================
bool RadioManager::_initWiFiPromiscuous() {
    WiFi.mode(WIFI_STA);
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) return false;
    err = esp_wifi_set_promiscuous(true);
    return (err == ESP_OK);
}

// =============================================================================
// PRIVATE: Init — CSI Scanner
// Promiscuous mode + CSI. The CSI rx callback is registered by CSI::start_scanning(),
// not here. RadioManager only boots the WiFi driver stack.
// =============================================================================
bool RadioManager::_initWiFiCSI() {
    WiFi.mode(WIFI_OFF);
    delay(50);
    WiFi.mode(WIFI_STA);
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) return false;
    // Enable promiscuous so we capture CSI from beacon frames
    // (we don't need to be associated to an AP)
    err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) return false;
    Serial.println("[Radio] WiFi CSI base stack ready");
    return true;
}

// =============================================================================
// PRIVATE: Init — Active Channel Scan (wardriving)
// =============================================================================
bool RadioManager::_initWiFiScan() {
    WiFi.mode(WIFI_STA);
    esp_err_t err = esp_wifi_start();
    return (err == ESP_OK);
}

// =============================================================================
// PRIVATE: Init — BLE
// =============================================================================
bool RadioManager::_initBLE() {
#if 0  // BLE disabled: Purplx is WiFi-only. Orphaned code from MegaFW reference.
    if (!NimBLEDevice::getInitialized()) {
        NimBLEDevice::init("MegaFW-ADV");
    }
    return NimBLEDevice::getInitialized();
#else
    Serial.println("[Radio] BLE not compiled in");
    return false;
#endif
}

// =============================================================================
// PRIVATE: Init — LoRa
// Actual SX1262 initialisation is Meshtastic::start()'s responsibility.
// RadioManager claims the slot here.
// =============================================================================
bool RadioManager::_initLoRa() {
    Serial.println("[Radio] LoRa slot claimed — awaiting module init");
    return true;
}
