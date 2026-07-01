// =============================================================================
// csi.cpp — WiFi CSI Human Presence Scanner
// =============================================================================
// Algorithm ported from skizzophrenic/Cardputer-CSI-Human-Detector (RADAR_CSI mode).
//
// KEY FIXES vs initial implementation:
//   1. channel_filter_en = TRUE  (was false — raw data was too noisy)
//   2. Phase variance tracked alongside amplitude (60% amp / 40% phase blend)
//   3. Asymmetric EMA normalization — no calibration phase, continuous self-adapt
//   4. Presence hold/coast — 10s persistence after last detection, graceful fade
//   5. PPI radar scope on ILI9341 instead of waterfall
//
// CSI data format (ESP32-S3):
//   buf[] = interleaved int8_t pairs [real0, imag0, real1, imag1, ...]
//   amplitude  = sqrt(r² + im²)
//   sin(phase) = im / amplitude   (mean across subcarriers per frame)
//
// Display layout:
//   ILI9341 (320×240): PPI radar scope — rendered at 240×180, pushRotateZoom 1.333x
//   ST7789  (240×135): PRESENCE/CLEAR banner + 240-sample motion graph + stats
// =============================================================================

#include "csi.h"
#include "../../../include/themes.h"
#include "../../core/radio_manager.h"
#include <math.h>

namespace CSI {

// ─── Public state ─────────────────────────────────────────────────────────────
Config           g_config;
volatile Metrics g_metrics = {};

// ─── Private module state ─────────────────────────────────────────────────────
static QueueHandle_t   _queue       = nullptr;
static TaskHandle_t    _task_handle = nullptr;
static volatile bool   _running     = false;

// ─── CSI algorithm state (in PSRAM) ──────────────────────────────────────────
// Circular amplitude buffer (window for variance computation)
static float*   _amp_buf    = nullptr;  // [window_size]
static float*   _pha_buf    = nullptr;  // [window_size] — mean sin(phase) per frame
static int      _buf_idx    = 0;
static int      _buf_filled = 0;

// EMA-based adaptive normalisation (no calibration phase needed)
// Asymmetric: floor tracks slowly upward, drops quickly; max tracks peak
static float _ampVarMin  = 0.0f, _ampVarMax  = 0.001f;
static float _phaVarMin  = 0.0f, _phaVarMax  = 0.001f;

// Presence hold/coast — 10s at ~15Hz
static const int   kHoldFrames = 150;
static int         _holdCnt    = 0;
static float       _heldMotion = 0.0f;

// Motion history ring buffer (240 samples for graph)
static float*   _hist      = nullptr;  // [HIST_DEPTH]
static int      _hist_head = 0;
static const int HIST_DEPTH = 240;

// Volatile outputs read by render functions
static volatile float _motion    = 0.0f;
static volatile bool  _present   = false;
static volatile int8_t _rssi     = -80;
static volatile uint32_t _count  = 0;

// Smoothed motion (EMA on the output) — reduces jitter for steadier reading
static volatile float _motionSmooth = 0.0f;
// Intensity level 0..4 (STILL/FAINT/LIGHT/MODERATE/STRONG)
static volatile int   _intensity    = 0;
// Peak motion in last ~2s (for hunt mode "warmest" tracking)
static volatile float _motionPeak   = 0.0f;
static uint32_t       _peakTime     = 0;

enum CsiMode { MODE_RADAR = 0, MODE_HUNT = 1 };
static int _mode = MODE_RADAR;

// ─── Render sprites (PSRAM) ───────────────────────────────────────────────────
static LGFX_Sprite* _scope_sprite = nullptr;  // 240×180 PPI scope canvas
static bool         _sprites_ready = false;

// Radar scope state
struct Blip {
    float    ang;
    float    rad;
    float    strength;
    uint32_t birth;
    bool     active;
};
static Blip     _blips[12]     = {};
static uint32_t _lastSpawn     = 0;
static float    _lastSpawnAng  = 0.0f;

struct Ripple {
    float    ang;
    float    rad;
    uint32_t birth;
    bool     active;
};
static Ripple   _ripples[6]    = {};
static float    _prevSweep     = 0.0f;

static const uint32_t BLIP_LIFE = 15000;   // ms a contact persists
static const float    TAU       = 6.2831853f;

// Matrix rain (side strips of scope)
static const int RAIN_COLS = 14;
struct RainDrop { int16_t y; uint8_t speed; uint8_t tick; };
static RainDrop _rain[RAIN_COLS] = {};
static bool     _rainReady = false;

// =============================================================================
// PRIVATE: Helpers
// =============================================================================

static inline uint16_t _rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

static float _pickBlipAngle() {
    // Avoid ±30° around 6 and 12 o'clock (overlap ghost avatar)
    float r = ((float)random(0, 10000) / 10000.0f) * (TAU * 2.0f / 3.0f);
    const float s1 = TAU / 6.0f, s2 = TAU / 3.0f;
    if      (r < s1)       return r;
    else if (r < s1 + s2)  return r - s1 + TAU / 3.0f;
    else                   return r - s1 - s2 + TAU * 5.0f / 6.0f;
}

static float _variance(const float* buf, int n) {
    if (n < 2) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += buf[i];
    const float mean = sum / n;
    float var = 0.0f;
    for (int i = 0; i < n; i++) { float d = buf[i] - mean; var += d * d; }
    return var / n;
}

static const char* _intensityLabel(int lvl){
    switch(lvl){
        case 0: return "STILL";
        case 1: return "FAINT";
        case 2: return "LIGHT";
        case 3: return "MODERATE";
        default:return "STRONG";
    }
}
static uint16_t _intensityColor(int lvl){
    switch(lvl){
        case 0: return _rgb565(0,120,160);     // dim cyan
        case 1: return _rgb565(0,200,200);     // cyan
        case 2: return _rgb565(180,200,0);     // yellow-green
        case 3: return _rgb565(255,160,0);     // orange
        default:return _rgb565(255,40,80);     // hot red/pink
    }
}

// =============================================================================
// PRIVATE: WiFi CSI callback — Core 0, WiFi driver context
// RULES: no malloc, no Serial, no blocking. Volatile flags + queue only.
// =============================================================================
static void IRAM_ATTR _csi_rx_callback(void* ctx, wifi_csi_info_t* info) {
    if (!_running || !_queue || !info || !info->buf || info->len < 4) return;

    CSIRawPacket pkt;
    pkt.len = (info->len < MAX_CSI_BUF_LEN) ? info->len : MAX_CSI_BUF_LEN;
    pkt.rssi         = info->rx_ctrl.rssi;
    pkt.channel      = info->rx_ctrl.channel;
    pkt.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    memcpy(pkt.buf, info->buf, pkt.len);
    memcpy(pkt.mac, info->mac, 6);

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(_queue, &pkt, &woken);
    if (woken) portYIELD_FROM_ISR();
}

// =============================================================================
// PRIVATE: FreeRTOS Processing Task — Core 1
// Ported from Cardputer-CSI-Human-Detector RADAR_CSI algorithm
// =============================================================================
void _processing_task(void* pvParam) {
    CSIRawPacket pkt;
    Serial.println("[CSI] Processing task running on Core 1");

    while (_running) {
        if (xQueueReceive(_queue, &pkt, pdMS_TO_TICKS(200)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // ── Single pass: amplitude + mean sin(phase) ──────────────────────────
        // Phase tracks slower/smaller motion that amplitude variance misses.
        // sin(phase) = im / amplitude (no atan2 needed)
        const int8_t* b      = pkt.buf;
        const int     nPairs = pkt.len / 2;

        float ampSum = 0.0f, sinSum = 0.0f;
        int   validPairs = 0;
        for (int i = 0; i < nPairs; i++) {
            const float r   = (float)b[i * 2];
            const float im  = (float)b[i * 2 + 1];
            const float amp = sqrtf(r * r + im * im);
            ampSum += amp;
            if (amp > 1e-4f) { sinSum += im / amp; validPairs++; }
        }
        const float meanAmp = ampSum / (float)nPairs;
        const float meanSin = validPairs > 0 ? sinSum / (float)validPairs : 0.0f;

        // Push into circular window buffers
        _amp_buf[_buf_idx] = meanAmp;
        _pha_buf[_buf_idx] = meanSin;
        _buf_idx = (_buf_idx + 1) % g_config.window_size;
        if (_buf_filled < g_config.window_size) _buf_filled++;

        _rssi = pkt.rssi;
        _count++;

        // ── Variance over window ───────────────────────────────────────────────
        const float ampVar = _variance(_amp_buf, _buf_filled);
        const float phaVar = _variance(_pha_buf, _buf_filled);

        // ── Asymmetric EMA normalisation (no calibration phase!) ──────────────
        // Floor: rises slowly when quiet (0.002), drops quickly when spikes (0.1)
        // Max:   rises immediately at spikes, decays very slowly (0.005)
        // Source: Cardputer-CSI-Human-Detector RADAR_CSI mode
        if (_ampVarMin < 0.0001f) _ampVarMin = ampVar;
        else _ampVarMin += (ampVar - _ampVarMin) * ((ampVar < _ampVarMin) ? 0.1f : 0.002f);
        if (ampVar > _ampVarMax) _ampVarMax = ampVar;
        else _ampVarMax += (ampVar - _ampVarMax) * 0.005f;
        const float ampRange  = _ampVarMax - _ampVarMin;
        float ampMotion = (ampRange > 0.0001f) ? ((ampVar - _ampVarMin) / ampRange) : 0.0f;
        if (ampMotion < 0.0f) ampMotion = 0.0f;
        if (ampMotion > 1.0f) ampMotion = 1.0f;

        if (_phaVarMin < 0.0001f) _phaVarMin = phaVar;
        else _phaVarMin += (phaVar - _phaVarMin) * ((phaVar < _phaVarMin) ? 0.1f : 0.002f);
        if (phaVar > _phaVarMax) _phaVarMax = phaVar;
        else _phaVarMax += (phaVar - _phaVarMax) * 0.005f;
        const float phaRange  = _phaVarMax - _phaVarMin;
        float phaMotion = (phaRange > 0.0001f) ? ((phaVar - _phaVarMin) / phaRange) : 0.0f;
        if (phaMotion < 0.0f) phaMotion = 0.0f;
        if (phaMotion > 1.0f) phaMotion = 1.0f;

        // ── Blend: 60% amplitude + 40% phase (RuView-inspired weighting) ──────
        float rawMotion = 0.6f * ampMotion + 0.4f * phaMotion;

        // ── Hold/coast: presence persists 10s after last detection ────────────
        // Prevents "flickering" when a person stands still.
        // Source: Cardputer-CSI-Human-Detector serviceCsi()
        bool  presence;
        float displayMotion;
        if (rawMotion > g_config.motion_threshold) {
            _holdCnt    = kHoldFrames;
            _heldMotion = rawMotion;
            presence    = true;
            displayMotion = rawMotion;
        } else if (_holdCnt > 0) {
            _holdCnt--;
            const float fade = (float)_holdCnt / (float)kHoldFrames;
            displayMotion = _heldMotion * (0.10f + 0.90f * fade);  // graceful fade
            presence      = true;
        } else {
            presence      = false;
            displayMotion = 0.0f;
        }

        // ── Smoothing: EMA on output reduces jitter (rise fast, fall slow) ────
        float sm = _motionSmooth;
        float a  = (displayMotion > sm) ? 0.35f : 0.08f;   // attack faster than release
        sm += (displayMotion - sm) * a;
        // dead-zone: clamp tiny baseline noise to zero
        if (sm < 0.05f) sm = 0.0f;
        _motionSmooth = sm;

        // ── Intensity grading (0..4) from smoothed value ─────────────────────
        int lvl;
        if      (sm < 0.06f) lvl = 0;   // STILL
        else if (sm < 0.20f) lvl = 1;   // FAINT
        else if (sm < 0.40f) lvl = 2;   // LIGHT
        else if (sm < 0.65f) lvl = 3;   // MODERATE
        else                 lvl = 4;   // STRONG
        _intensity = lvl;

        // ── Peak tracking for hunt mode (decays over ~2s) ────────────────────
        uint32_t nowMs = pkt.timestamp_ms;
        if (sm >= _motionPeak) { _motionPeak = sm; _peakTime = nowMs; }
        else if (nowMs - _peakTime > 2000) {
            _motionPeak += (sm - _motionPeak) * 0.05f;  // slow decay toward current
        }

        // ── Update motion history ring buffer ─────────────────────────────────
        _hist[_hist_head] = sm;
        _hist_head = (_hist_head + 1) % HIST_DEPTH;

        // ── Publish to volatile metrics ───────────────────────────────────────
        _motion  = sm;
        _present = presence;
        memcpy(const_cast<uint8_t*>(g_metrics.last_mac), pkt.mac, 6);

        g_metrics.activity_score     = displayMotion;
        g_metrics.motion_detected    = presence;
        g_metrics.packet_count       = _count;
        g_metrics.active_subcarriers = (uint8_t)validPairs;
        g_metrics.last_rssi          = pkt.rssi;
        g_metrics.last_ts_ms         = pkt.timestamp_ms;
    }

    Serial.println("[CSI] Processing task exiting");
    vTaskDelete(nullptr);
}

// =============================================================================
// PUBLIC: start_scanning()
// =============================================================================
void start_scanning() {
    if (_running) return;
    Serial.println("[CSI] Starting scanner (amp+phase EMA variant)...");

    if (!RadioManager::request(RadioMode::WIFI_CSI)) {
        Serial.println("[CSI] RadioManager denied WIFI_CSI");
        return;
    }

    // ── Allocate PSRAM buffers ────────────────────────────────────────────────
    _amp_buf = (float*)malloc(g_config.window_size * sizeof(float));
    _pha_buf = (float*)malloc(g_config.window_size * sizeof(float));
    _hist    = (float*)malloc(HIST_DEPTH * sizeof(float));
    if (!_amp_buf || !_pha_buf || !_hist) {
        Serial.println("[CSI] FATAL: PSRAM alloc failed");
        if (_amp_buf) { free(_amp_buf); _amp_buf = nullptr; }
        if (_pha_buf) { free(_pha_buf); _pha_buf = nullptr; }
        if (_hist)    { free(_hist);    _hist    = nullptr; }
        RadioManager::release();
        return;
    }
    memset(_amp_buf, 0, g_config.window_size * sizeof(float));
    memset(_pha_buf, 0, g_config.window_size * sizeof(float));
    memset(_hist,    0, HIST_DEPTH * sizeof(float));

    // ── Allocate scope sprite in PSRAM (240×180) ──────────────────────────────
    // Rendered at 240×180 then scaled 1.333× → fills 320×240 ILI9341
    // (saves ~100KB vs a full 320×240 sprite — technique from CSI detector)
    if (!_sprites_ready) {
        _scope_sprite = new LGFX_Sprite(&g_lcd_secondary);
        _scope_sprite->setColorDepth(8);  // 8-bit: 43KB not 86KB (no PSRAM, must fit regular RAM)
        if (!_scope_sprite->createSprite(240, 180)) {
            Serial.println("[CSI] FATAL: scope sprite alloc failed");
            delete _scope_sprite; _scope_sprite = nullptr;
            free(_amp_buf); free(_pha_buf); free(_hist);
            _amp_buf = _pha_buf = _hist = nullptr;
            RadioManager::release();
            return;
        }
        _scope_sprite->fillScreen(TFT_BLACK);
        _sprites_ready = true;
    }

    // ── Reset state ───────────────────────────────────────────────────────────
    _buf_idx = _buf_filled = 0;
    _hist_head = 0;
    _holdCnt = 0; _heldMotion = 0.0f;
    _ampVarMin = _phaVarMin = 0.0f;
    _ampVarMax = _phaVarMax = 0.001f;
    _motion = 0.0f; _present = false; _count = 0;
    _motionSmooth = 0.0f; _intensity = 0; _motionPeak = 0.0f; _peakTime = 0;
    _rainReady = false;
    memset(_blips,   0, sizeof(_blips));
    memset(_ripples, 0, sizeof(_ripples));
    memset((void*)&g_metrics, 0, sizeof(g_metrics));

    // ── Create queue ──────────────────────────────────────────────────────────
    _queue = xQueueCreate(CSI_QUEUE_DEPTH, sizeof(CSIRawPacket));
    if (!_queue) {
        Serial.println("[CSI] FATAL: queue create failed");
        RadioManager::release();
        return;
    }

    // ── Lock WiFi channel ──────────────────────────────────────────────────────
    esp_wifi_set_channel(g_config.fixed_channel, WIFI_SECOND_CHAN_NONE);

    // ── CSI config — channel_filter_en=TRUE is critical for noise reduction ───
    wifi_csi_config_t csi_cfg = {};
    csi_cfg.lltf_en           = true;
    csi_cfg.htltf_en          = true;
    csi_cfg.stbc_htltf2_en    = true;
    csi_cfg.ltf_merge_en      = true;
    csi_cfg.channel_filter_en = true;   // KEY FIX: was false — reduces noise significantly
    csi_cfg.manu_scale        = false;
    csi_cfg.shift             = 0;
    esp_wifi_set_csi_config(&csi_cfg);

    // ── Promiscuous filter: MGMT + DATA only (reduces callback rate) ──────────
    wifi_promiscuous_filter_t pf = {};
    pf.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&pf);

    esp_wifi_set_csi_rx_cb(_csi_rx_callback, nullptr);
    esp_err_t err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        Serial.printf("[CSI] ERROR: set_csi: %s\n", esp_err_to_name(err));
        RadioManager::release();
        return;
    }

    // ── Spawn processing task ─────────────────────────────────────────────────
    _running = true;
    xTaskCreatePinnedToCore(_processing_task, "CSI_proc",
                            CSI_TASK_STACK, nullptr,
                            CSI_TASK_PRI, &_task_handle, CSI_TASK_CORE);

    Serial.printf("[CSI] Active on channel %d — EMA self-calibrating\n",
                  g_config.fixed_channel);
}

// =============================================================================
// PUBLIC: stop_scanning()
// =============================================================================
void stop_scanning() {
    if (!_running) return;
    _running = false;

    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(nullptr, nullptr);

    if (_task_handle) { vTaskDelete(_task_handle); _task_handle = nullptr; }
    if (_queue)       { vQueueDelete(_queue);       _queue       = nullptr; }

    RadioManager::release();

    if (_amp_buf) { free(_amp_buf); _amp_buf = nullptr; }
    if (_pha_buf) { free(_pha_buf); _pha_buf = nullptr; }
    if (_hist)    { free(_hist);    _hist    = nullptr; }
    if (_sprites_ready) {
        delete _scope_sprite; _scope_sprite = nullptr;
        _sprites_ready = false;
    }
    Serial.println("[CSI] Stopped");
}

bool isRunning() { return _running; }
void toggleMode(){ _mode = (_mode == MODE_RADAR) ? MODE_HUNT : MODE_RADAR; }
int  getMode(){ return _mode; }

// =============================================================================
// PRIVATE: Matrix rain for scope side strips
// =============================================================================
static void _drawRain(uint32_t now) {
    static const char GL[] = "0123456789ABCDEF!:;@#%";
    static const int  GLN  = 22, TRAIL = 7;
    static const uint8_t RAIN_X[14] = { 3,9,15,21,27,33,39, 201,207,213,219,225,231,237 };

    if (!_rainReady) {
        for (int i = 0; i < 14; i++) {
            _rain[i].y     = (int16_t)random(-60, 160);
            _rain[i].speed = 2 + (uint8_t)random(0, 3);
            _rain[i].tick  = (uint8_t)random(0, 4);
        }
        _rainReady = true;
    }
    _scope_sprite->setTextSize(1);
    for (int i = 0; i < 14; i++) {
        if (++_rain[i].tick >= _rain[i].speed) {
            _rain[i].tick = 0;
            _rain[i].y   += 8;
            if (_rain[i].y > 180 + TRAIL * 8) {
                _rain[i].y     = -(int16_t)random(0, 60);
                _rain[i].speed = 2 + (uint8_t)random(0, 3);
            }
        }
        for (int j = TRAIL - 1; j >= 0; j--) {
            int16_t ry = _rain[i].y - j * 8;
            if (ry < 0 || ry >= 180) continue;
            char buf[2] = { GL[((uint32_t)(i * 13 + j * 7) + now / 350) % GLN], 0 };
            const bool isCyan = (i % 2) == 0;
            if (j == 0) {
                _scope_sprite->setTextColor(
                    isCyan ? _rgb565(120,255,255) : _rgb565(220,200,255), TFT_BLACK);
            } else {
                const uint8_t b = (uint8_t)(210 * (TRAIL - j) / TRAIL);
                _scope_sprite->setTextColor(
                    isCyan ? _rgb565(0, b/2, b) : _rgb565(b/2, 0, b), TFT_BLACK);
            }
            _scope_sprite->drawString(buf, RAIN_X[i], ry);
        }
    }
}

// Forward declaration for HUNT mode renderer
static void renderHunt(LGFX_Secondary& lcd);

// HUNT mode: big "warmer/colder" hide-and-seek display on the 320x240 screen.
static void renderHunt(LGFX_Secondary& lcd) {
    const float    m       = _motionSmooth;      // 0..1 current
    const float    peak    = _motionPeak;        // recent peak
    const int      lvl     = _intensity;
    const uint32_t now     = millis();
    const bool     blink   = (now % 500) < 250;

    _scope_sprite->fillSprite(TFT_BLACK);
    const int W=240, H=180, cx=120, cy=92;
    uint16_t hot = _intensityColor(lvl);

    // title
    _scope_sprite->fillRect(0,0,W,14,_rgb565(20,0,35));
    _scope_sprite->setTextSize(1);
    _scope_sprite->setTextColor(g_theme->primary,_rgb565(20,0,35));
    _scope_sprite->drawString("[ HUNT MODE ]", 64, 3);

    // concentric "heat rings" — more rings light up with stronger motion
    int rings = lvl + 1;                  // 1..5
    for (int r=0; r<5; r++){
        int rad = 18 + r*15;
        bool on = (r < rings);
        uint16_t c = on ? hot : _rgb565(25,25,30);
        // pulse the outermost active ring
        if (on && r==rings-1 && blink) c = _rgb565(255,255,255);
        _scope_sprite->drawCircle(cx,cy,rad,c);
        _scope_sprite->drawCircle(cx,cy,rad-1,c);
    }
    // center dot
    _scope_sprite->fillCircle(cx,cy,10,hot);

    // big intensity word
    _scope_sprite->setTextSize(2);
    _scope_sprite->setTextColor(hot,TFT_BLACK);
    const char* word;
    if      (lvl==0) word="COLD";
    else if (lvl==1) word="COOL";
    else if (lvl==2) word="WARM";
    else if (lvl==3) word="HOT";
    else             word="BURNING";
    int ww = _scope_sprite->textWidth(word);
    _scope_sprite->drawString(word, cx-ww/2, H-40);

    // intensity bar at bottom
    int bx=20, bw=W-40, by=H-18, bh=8;
    _scope_sprite->drawRect(bx,by,bw,bh,g_theme->border);
    int fw=(int)((bw-2)*m);
    _scope_sprite->fillRect(bx+1,by+1,fw,bh-2,hot);

    // numeric readout
    _scope_sprite->setTextSize(1);
    char buf[24];
    snprintf(buf,sizeof(buf),"LVL %d  %d%%  peak %d%%", lvl,(int)(m*100),(int)(peak*100));
    _scope_sprite->setTextColor(g_theme->secondary,TFT_BLACK);
    _scope_sprite->drawString(buf, 6, 18);

    // push scaled
    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(15)) == pdTRUE) {
        _scope_sprite->pushRotateZoom(160, 120, 0.0f, 320.0f/240.0f, 320.0f/240.0f);
        xSemaphoreGive(g_display_mutex);
    }
}

// =============================================================================
// PUBLIC: render_waterfall() — PPI radar scope on ILI9341 (320×240)
// Name kept for API compatibility; renders as radar scope (better for cyberdeck).
// Sprite is 240×180; pushRotateZoom at 1.333× fills the 320×240 panel.
// =============================================================================
void render_waterfall(LGFX_Secondary& lcd) {
    if (!_sprites_ready || !_scope_sprite) return;
    if (_mode == MODE_HUNT) { renderHunt(lcd); return; }

    const bool     present = _present;
    const float    motion  = _motion;
    const uint32_t now     = millis();

    const int W = 240, H = 180;
    const int cx = 120, cy = 90, R = 74;

    const uint16_t cMag    = g_theme->primary;
    const uint16_t cCyan   = g_theme->secondary;
    const uint16_t cBar    = _rgb565( 20,   0,  35);
    const uint16_t cBorder = g_theme->border;
    const uint16_t scopeBg = _rgb565(  4,   0,   8);
    const bool     blink   = (now % 600) < 300;

    // ── Sweep angle ───────────────────────────────────────────────────────────
    const float sweep = (float)(now % 6000) / 6000.0f * TAU;

    // ── Spawn / update contact blips ──────────────────────────────────────────
    if (present) {
        float t = ((float)(int)_rssi + 45.0f) / (-33.0f);
        if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
        float targetRad  = R * (0.25f + t * 0.65f);
        if (targetRad < R * 0.30f) targetRad = R * 0.30f;
        const float mergeThresh = R * 0.20f;

        int   closestIdx  = -1;
        float closestDist = (float)R;
        for (int i = 0; i < 12; i++) {
            if (!_blips[i].active || _blips[i].birth + BLIP_LIFE <= now) continue;
            _blips[i].strength += (motion - _blips[i].strength) * 0.12f;
            float d = fabsf(_blips[i].rad - targetRad);
            if (d < closestDist) { closestDist = d; closestIdx = i; }
        }
        if (closestIdx >= 0 && closestDist <= mergeThresh) {
            _blips[closestIdx].birth  = now;
            _blips[closestIdx].rad   += (targetRad - _blips[closestIdx].rad) * 0.05f;
        } else if (now - _lastSpawn > 800) {
            int slot = -1;
            for (int i = 0; i < 12; i++) if (!_blips[i].active) { slot = i; break; }
            if (slot < 0) {
                uint32_t oldest = UINT32_MAX;
                for (int i = 0; i < 12; i++) if (_blips[i].birth < oldest) { oldest = _blips[i].birth; slot = i; }
            }
            if (slot >= 0) {
                _lastSpawnAng         = _pickBlipAngle();
                _blips[slot].ang      = _lastSpawnAng;
                _blips[slot].rad      = targetRad;
                _blips[slot].strength = motion;
                _blips[slot].birth    = now;
                _blips[slot].active   = true;
                _lastSpawn            = now;
                for (int r = 0; r < 6; r++) {
                    if (!_ripples[r].active) {
                        _ripples[r] = { _lastSpawnAng, targetRad, now, true };
                        break;
                    }
                }
            }
        }
    }

    // ── Sonar ping: ripple when sweep crosses a blip ──────────────────────────
    {
        const float ps = fmodf(_prevSweep, TAU), cs = fmodf(sweep, TAU);
        for (int i = 0; i < 12; i++) {
            if (!_blips[i].active) continue;
            float ba = fmodf(_blips[i].ang, TAU);
            if (ba < 0) ba += TAU;
            bool crossed = (cs >= ps) ? (ba >= ps && ba < cs) : (ba >= ps || ba < cs);
            if (crossed) {
                for (int r = 0; r < 6; r++) {
                    if (!_ripples[r].active) {
                        _ripples[r] = { _blips[i].ang, _blips[i].rad, now, true };
                        break;
                    }
                }
            }
        }
    }
    _prevSweep = sweep;

    // ── Render ────────────────────────────────────────────────────────────────

    // Layer 1: background + matrix rain
    _scope_sprite->fillSprite(TFT_BLACK);
    _drawRain(now);

    // Layer 2: scope disc + grid
    _scope_sprite->fillCircle(cx, cy, R, scopeBg);
    _scope_sprite->drawCircle(cx, cy, R,          _rgb565(  0, 80, 100));
    _scope_sprite->drawCircle(cx, cy, R * 2 / 3, _rgb565( 80,  0, 100));
    _scope_sprite->drawCircle(cx, cy, R / 3,     _rgb565(  0, 50,  80));
    const uint16_t cXhair = _rgb565(35, 0, 35);
    _scope_sprite->drawLine(cx, cy - R, cx, cy + R, cXhair);
    _scope_sprite->drawLine(cx - R, cy, cx + R, cy, cXhair);

    // Tick marks
    for (int d = 0; d < 12; d++) {
        const float a = d * (TAU / 12.0f);
        _scope_sprite->drawLine(
            cx + (int)((R-5) * cosf(a)), cy + (int)((R-5) * sinf(a)),
            cx + (int)(R     * cosf(a)), cy + (int)(R     * sinf(a)),
            _rgb565(70, 0, 70));
    }

    // Cardinal labels
    _scope_sprite->setTextSize(1);
    const char*  cLbl[4]  = { "", "", "", "" };  // no compass: CSI has no bearing
    const float  cBase[4] = { -TAU/4, 0.0f, TAU/4, TAU/2 };
    for (int ci = 0; ci < 4; ci++) {
        _scope_sprite->setTextColor(ci == 0 ? TFT_WHITE : cCyan, scopeBg);
        _scope_sprite->drawString(cLbl[ci],
            cx + (int)((R-11) * cosf(cBase[ci])) - 2,
            cy + (int)((R-11) * sinf(cBase[ci])) - 4);
    }

    // Layer 3: phosphor sweep trail (dark → magenta head)
    const int TRAIL = 22;
    for (int k = TRAIL; k >= 1; k--) {
        const float a  = sweep - k * 0.040f;
        const float t  = 1.0f - (float)k / TRAIL;
        const float b2 = t * t;
        _scope_sprite->drawLine(cx, cy,
            cx + (int)(R * cosf(a)), cy + (int)(R * sinf(a)),
            _rgb565((uint8_t)(220*b2), 0, (uint8_t)(40+160*b2)));
    }

    // Layer 4: contact blips
    int contacts = 0;
    for (int i = 0; i < 12; i++) {
        if (!_blips[i].active) continue;
        const uint32_t age = now - _blips[i].birth;
        if (age > BLIP_LIFE) { _blips[i].active = false; continue; }
        contacts++;
        const float fade = 1.0f - (float)age / BLIP_LIFE;
        const int bx = cx + (int)(_blips[i].rad * cosf(_blips[i].ang));
        const int by = cy + (int)(_blips[i].rad * sinf(_blips[i].ang));
        const int sz = 2 + (int)(_blips[i].strength * 4);
        const float str = _blips[i].strength;
        uint16_t col;
        if      (str > 0.85f) col = _rgb565((uint8_t)(255*fade),(uint8_t)(180*fade),(uint8_t)(255*fade));
        else if (str > 0.70f) col = _rgb565((uint8_t)(255*fade), 0, (uint8_t)(200*fade));
        else if (str > 0.50f) col = _rgb565((uint8_t)(140*fade), 0, (uint8_t)(255*fade));
        else                  col = _rgb565(0, (uint8_t)(200*fade), (uint8_t)(255*fade));
        _scope_sprite->fillCircle(bx, by, sz, col);
        if (fade > 0.6f) _scope_sprite->drawCircle(bx, by, sz+2, col);
    }

    // Layer 4.5: ripples
    for (int r = 0; r < 6; r++) {
        if (!_ripples[r].active) continue;
        const uint32_t age = now - _ripples[r].birth;
        if (age > 700) { _ripples[r].active = false; continue; }
        const float prog = (float)age / 700.0f;
        const int bx  = cx + (int)(_ripples[r].rad * cosf(_ripples[r].ang));
        const int by  = cy + (int)(_ripples[r].rad * sinf(_ripples[r].ang));
        const int sz  = 4 + (int)(prog * 22.0f);
        const uint8_t fade = (uint8_t)(255 * (1.0f - prog));
        const uint8_t rc   = (uint8_t)(fade * (1.0f - prog));
        _scope_sprite->drawCircle(bx, by, sz,   _rgb565(rc, fade, fade));
        _scope_sprite->drawCircle(bx, by, sz+3, _rgb565(rc/3, fade/3, fade/3));
    }

    // Layer 5: sweep leading edge
    _scope_sprite->drawLine(cx, cy,
        cx + (int)(R * cosf(sweep)), cy + (int)(R * sinf(sweep)),
        _rgb565(255, 80, 255));

    // Layer 6: title bar
    _scope_sprite->fillRect(0, 0, W, 14, cBar);
    _scope_sprite->drawFastHLine(0, 13, W, cBorder);
    _scope_sprite->setTextSize(1);
    _scope_sprite->setTextColor(cMag, cBar);
    _scope_sprite->drawString("[ WiFi-CSI RADAR ]", 46, 3);

    // Layer 7: status bar
    _scope_sprite->fillRect(0, H-14, W, 14, cBar);
    _scope_sprite->drawFastHLine(0, H-14, W, cBorder);
    const char* ctlbl = present ? ">>CONTACT<<" : " scanning.. ";
    uint16_t ctCol = present
        ? (blink ? _rgb565(255,60,255) : _rgb565(110,0,100))
        : _rgb565(55, 0, 70);
    _scope_sprite->setTextColor(ctCol, cBar);
    _scope_sprite->drawString(ctlbl, 14, H-11);
    char stats[28];
    snprintf(stats, sizeof(stats), "C:%d M:%d%% %ddBm",
             contacts, (int)(motion*100), (int)_rssi);
    _scope_sprite->setTextColor(cCyan, cBar);
    _scope_sprite->drawString(stats, W - _scope_sprite->textWidth(stats) - 4, H-11);

    // Layer 8: frame corners
    _scope_sprite->drawFastHLine(0,   0,   W, cBorder);
    _scope_sprite->drawFastHLine(0,   H-1, W, cBorder);
    _scope_sprite->drawFastVLine(0,   0,   H, cBorder);
    _scope_sprite->drawFastVLine(W-1, 0,   H, cBorder);
    const uint16_t cCorner = _rgb565(255, 120, 255);
    _scope_sprite->fillRect(0,   0,   2, 2, cCorner);
    _scope_sprite->fillRect(W-2, 0,   2, 2, cCorner);
    _scope_sprite->fillRect(0,   H-2, 2, 2, cCorner);
    _scope_sprite->fillRect(W-2, H-2, 2, 2, cCorner);

    // ── Push to display: scale 240×180 → 320×240 (1.333×) ────────────────────
    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(15)) == pdTRUE) {
        _scope_sprite->pushRotateZoom(160, 120, 0.0f, 320.0f/240.0f, 320.0f/240.0f);
        xSemaphoreGive(g_display_mutex);
    }
}

// =============================================================================
// PUBLIC: render_hud() — ST7789 primary display (240×135)
// PRESENCE/CLEAR banner + scrolling motion graph + stats
// =============================================================================
void render_hud(M5GFX& lcd) {
    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    const bool    present = _present;
    const float   motion  = _motion;
    const int     W = 240, H = 135;

    lcd.fillScreen(TFT_BLACK);

    // Title bar
    lcd.fillRect(0, 0, W, 14, _rgb565(20, 0, 35));
    lcd.drawFastHLine(0, 13, W, _rgb565(160, 0, 160));
    lcd.setTextSize(1);
    lcd.setTextColor(_rgb565(220, 0, 200), _rgb565(20, 0, 35));
    lcd.setCursor(4, 3);
    lcd.printf("CSI  CH:%d  PKT:%lu", g_config.fixed_channel,
               (unsigned long)_count);

    // Presence / clear banner
    const uint16_t bg     = present ? _rgb565(60, 0, 60) : _rgb565(0, 0, 25);
    const uint16_t border = present ? _rgb565(220, 0, 200) : _rgb565(70, 0, 70);
    lcd.fillRoundRect(4, 17, W-8, 28, 4, bg);
    lcd.drawRoundRect(4, 17, W-8, 28, 4, border);
    lcd.setTextSize(2);
    const char* lbl = present ? ">> CONTACT <<" : "~~ CLEAR ~~";
    lcd.setTextColor(present ? _rgb565(255,100,255) : _rgb565(0,200,220), bg);
    lcd.setCursor((W - lcd.textWidth(lbl)) / 2, 22);
    lcd.print(lbl);

    // Motion graph (240 samples, gx=4..gx+gw, gy=48..gy+gh)
    const int gx = 4, gy = 48, gw = W-8, gh = 50;
    lcd.drawRect(gx, gy, gw, gh, _rgb565(160, 0, 160));

    // Threshold line
    const int ty = gy + gh - 1 - (int)(g_config.motion_threshold * (gh-2));
    lcd.drawFastHLine(gx+1, ty, gw-2, _rgb565(180, 0, 180));

    // History bars
    if (_hist) {
        const int cols = gw - 2;
        for (int i = 0; i < cols; i++) {
            const int   hi  = (_hist_head - cols + i + HIST_DEPTH) % HIST_DEPTH;
            const float v   = _hist[hi];
            const int   bh  = (int)(v * (gh-2));
            if (bh <= 0) continue;
            const uint16_t col = (v > g_config.motion_threshold)
                ? _rgb565(220, 0, 200)
                : _rgb565(0, 160, 200);
            lcd.drawFastVLine(gx+1+i, gy+gh-1-bh, bh, col);
        }
    }

    // Footer stats
    lcd.setTextSize(1);
    lcd.setTextColor(_rgb565(0, 200, 220), TFT_BLACK);
    lcd.setCursor(4, gy+gh+4);
    lcd.printf("mot %3d%%  rssi %ddBm  thr %2d%%",
               (int)(motion*100), (int)_rssi,
               (int)(g_config.motion_threshold*100));
    lcd.setCursor(4, gy+gh+16);
    lcd.setTextColor(_intensityColor(_intensity), TFT_BLACK);
    lcd.printf("INTENSITY: %s", _intensityLabel(_intensity));
    // mode hint
    lcd.setTextColor(_rgb565(120,120,140), TFT_BLACK);
    lcd.setCursor(150, gy+gh+16);
    lcd.printf("[M]%s", _mode==0?"hunt":"radar");

    xSemaphoreGive(g_display_mutex);
}

} // namespace CSI
