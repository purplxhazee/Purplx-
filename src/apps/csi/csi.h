#pragma once
// =============================================================================
// csi.h — WiFi CSI Human Presence Scanner
// =============================================================================
// Uses ESP32-S3 Channel State Information API (esp_wifi_set_csi_rx_cb) to
// detect physical disruptions in ambient WiFi propagation caused by human
// movement. Based on the RuView / esp-csi methodology from Espressif.
//
// Architecture:
//
//   Core 0 (WiFi task):
//     [_csi_rx_callback] ── non-blocking ──► [g_csi_queue]
//       ↑ fires per packet. ZERO malloc, ZERO Serial. volatile + static only.
//
//   Core 1 (App CPU):
//     [_processing_task] ──dequeues──► parse amplitudes
//                                    ► ring buffer write
//                                    ► calibration (first 80 samples)
//                                    ► sliding window variance
//                                    ► update g_metrics (volatile)
//
//   Main loop (Core 1):
//     Display::renderFrame() ──► CSI::render_waterfall()  [ILI9341 320×240]
//                           ──► CSI::render_hud()         [ST7789  240×135]
//
// Display layout (ILI9341):
//   ┌────────────────────────────────────────┐
//   │  AMPLITUDE LINE GRAPH  (y=0..39)       │  40px
//   ├────────────────────────────────────────┤
//   │                                        │
//   │  WATERFALL MATRIX  (y=40..239)         │  200px
//   │  X = subcarrier index → 0..319 px      │
//   │  Y = time (newest row at bottom)       │
//   │  Colour = heatmap (blue→cyan→green→    │
//   │           yellow→red by amplitude)     │
//   │                                        │
//   └────────────────────────────────────────┘
//
// Display layout (ST7789 HUD):
//   MOTION BANNER | activity_score | active_sc | pkt_count | RSSI | MAC
// =============================================================================

#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <M5GFX.h>
#include "../../../include/main.h"

namespace CSI {

// ─── Module Constants ─────────────────────────────────────────────────────────
static constexpr uint16_t MAX_CSI_BUF_LEN  = 384;  // Max bytes per raw CSI frame
static constexpr uint8_t  MAX_SUBCARRIERS  = 64;   // LLTF=52 + HT-LTF padding
static constexpr uint16_t HISTORY_DEPTH    = 200;  // Waterfall rows in ring buffer
static constexpr uint16_t BASELINE_SAMPLES = 80;   // Calibration frame count
static constexpr int      CSI_QUEUE_DEPTH  = 8;    // FreeRTOS queue slots
static constexpr int      CSI_TASK_STACK   = 8192; // Processing task stack (bytes)
static constexpr int      CSI_TASK_PRI     = 5;
static constexpr int      CSI_TASK_CORE    = 1;    // App CPU (opposite of WiFi task)

static constexpr int      WATERFALL_Y      = 40;   // Y offset on ILI9341
static constexpr int      WATERFALL_H      = 200;
static constexpr int      GRAPH_H          = 40;
static constexpr int      DISP_W           = 320;

// ─── Data Structures ─────────────────────────────────────────────────────────

// Raw snapshot queued from WiFi callback to processing task.
// All fields copied atomically — no pointers.
struct CSIRawPacket {
    int8_t   buf[MAX_CSI_BUF_LEN];
    uint16_t len;
    uint8_t  mac[6];
    int8_t   rssi;
    uint8_t  channel;
    uint32_t timestamp_ms;
};

// Processed amplitude row written into the ring buffer
struct CSIAmplitudeRow {
    float   amp[MAX_SUBCARRIERS];
    uint8_t num_sc;
    uint32_t ts_ms;
};

// ─── Internal function declarations ──────────────────────────────────────────

// FreeRTOS processing task (Core 1)
void _processing_task(void* pvParam);

// WiFi driver callback (Core 0) — IRAM, no heap, no Serial
static void IRAM_ATTR _csi_rx_callback(void* ctx, wifi_csi_info_t* data);

// Parse interleaved int8_t [imag,real,...] buffer → amplitude array
// Returns number of populated subcarriers
uint8_t _parse_amplitudes(const int8_t* buf, uint16_t len, float* out_amp);

// Population variance of a float array
float _variance(const float* arr, uint16_t n);

// Normalised amplitude [0,1] → RGB565 heat-map colour
// 0.0 = blue, 0.25 = cyan, 0.5 = green, 0.75 = yellow, 1.0 = red
uint16_t _amp_to_rgb565(float normalised);

} // namespace CSI
