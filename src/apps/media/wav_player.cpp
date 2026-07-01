// =============================================================================
// wav_player.cpp — WAV Music Player Implementation
// =============================================================================
// Streams PCM WAV from SD to M5.Speaker in chunks. Non-blocking: each tick()
// pushes one buffer if the speaker queue has room, so the UI stays responsive.
// =============================================================================

#include "wav_player.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <M5Unified.h>
#include <SD.h>

namespace Music {

// ─── WAV header ───────────────────────────────────────────────────────────────
struct WavHeader {
    char     riff[4];
    uint32_t fileSize;
    char     wave[4];
    char     fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t channels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};

// ─── Track list ───────────────────────────────────────────────────────────────
static const int MAX_TRACKS = 64;
static char     _tracks[MAX_TRACKS][64];
static int      _track_count = 0;
static int      _sel = 0;          // browser cursor
static int      _now_playing = -1; // currently playing index

// ─── Playback state ───────────────────────────────────────────────────────────
static File     _file;
static bool     _running   = false;
static bool     _playing   = false;
static bool     _paused    = false;
static uint16_t _channels  = 2;
static uint16_t _bits      = 16;
static uint32_t _rate      = 44100;
static uint32_t _data_start= 0;
static uint32_t _data_len  = 0;
static uint32_t _data_pos  = 0;
static uint8_t  _volume    = 6;    // 0..10

// Streaming buffer
static const size_t BUF_SAMPLES = 512;
static int16_t _buf[BUF_SAMPLES];

// Visualizer bars
static const int NBARS = 16;
static float _bars[NBARS] = {0};

// =============================================================================
static void scanTracks() {
    _track_count = 0;
    File dir = SD.open("/purplx/music");
    if (!dir) return;
    File f;
    while ((f = dir.openNextFile()) && _track_count < MAX_TRACKS) {
        if (!f.isDirectory()) {
            const char* n = f.name();
            const char* dot = strrchr(n, '.');
            if (dot && (strcasecmp(dot, ".wav") == 0)) {
                // store basename only
                const char* slash = strrchr(n, '/');
                const char* base = slash ? slash+1 : n;
                strncpy(_tracks[_track_count], base, 63);
                _tracks[_track_count][63] = 0;
                _track_count++;
            }
        }
        f.close();
    }
    dir.close();
}

static void drawBrowser() {
    const ColorTheme* t = g_theme;
    g_lcd_secondary.fillScreen(t->bg);
    // Title
    g_lcd_secondary.fillRect(0,0,320,22,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,21,320,t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(8,7);
    g_lcd_secondary.print("MUSIC  //  /purplx/music/");

    if (_track_count == 0) {
        g_lcd_secondary.setTextColor(t->text,t->bg);
        g_lcd_secondary.setCursor(20,60);
        g_lcd_secondary.print("No .wav files found.");
        g_lcd_secondary.setTextColor(t->text_dim,t->bg);
        g_lcd_secondary.setCursor(20,80);
        g_lcd_secondary.print("Put WAV files in /purplx/music/");
        g_lcd_secondary.setCursor(20,94);
        g_lcd_secondary.print("on your SD card, then reopen.");
        return;
    }

    // Track list (show ~14)
    const int top = (_sel < 7) ? 0 : _sel - 7;
    for (int i = 0; i < 14 && (top+i) < _track_count; i++) {
        const int idx = top+i;
        const int y = 26 + i*13;
        const bool sel = (idx == _sel);
        if (sel) {
            g_lcd_secondary.fillRect(0,y,320,13,t->highlight_bg);
            g_lcd_secondary.setTextColor(t->highlight_text,t->highlight_bg);
        } else {
            g_lcd_secondary.setTextColor(
                idx==_now_playing ? t->secondary : t->text, t->bg);
        }
        g_lcd_secondary.setCursor(6,y+3);
        g_lcd_secondary.print(idx==_now_playing ? ">" : " ");
        g_lcd_secondary.setCursor(16,y+3);
        char nm[40]; strncpy(nm,_tracks[idx],39); nm[39]=0;
        g_lcd_secondary.print(nm);
    }
    // Footer
    g_lcd_secondary.fillRect(0,222,320,18,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,222,320,t->border);
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,227);
    g_lcd_secondary.print("[W/S] pick [ENTER] play [SPACE] pause [ESC] back");
}

static void drawNowPlaying() {
    const ColorTheme* t = g_theme;
    // bottom area visualizer is drawn in tick(); here just the header strip
    g_lcd_secondary.fillRect(0,160,320,40,t->bg);
}

static void drawHUD() {
    const ColorTheme* t = g_theme;
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.setTextColor(t->primary,t->hud_bg);
    g_lcd_primary.setTextSize(2);
    g_lcd_primary.setCursor(4,4);
    g_lcd_primary.print("MUSIC");
    g_lcd_primary.setTextSize(1);
    if (_now_playing >= 0) {
        g_lcd_primary.setTextColor(t->text,t->hud_bg);
        g_lcd_primary.setCursor(4,30);
        char nm[28]; strncpy(nm,_tracks[_now_playing],27); nm[27]=0;
        g_lcd_primary.print(nm);
        g_lcd_primary.setCursor(4,46);
        g_lcd_primary.setTextColor(_playing && !_paused ? t->success : t->alert, t->hud_bg);
        g_lcd_primary.print(_paused ? "[ PAUSED ]" : (_playing ? "[ PLAYING ]":"[ STOPPED ]"));
        // progress
        if (_data_len > 0) {
            int pct = (int)((uint64_t)_data_pos*100/_data_len);
            g_lcd_primary.setTextColor(t->secondary,t->hud_bg);
            g_lcd_primary.setCursor(4,60);
            g_lcd_primary.printf("%d%%  %luHz", pct, (unsigned long)_rate);
        }
    } else {
        g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);
        g_lcd_primary.setCursor(4,30);
        g_lcd_primary.print("Pick a track");
    }
    // volume bar
    g_lcd_primary.setTextColor(t->text,t->hud_bg);
    g_lcd_primary.setCursor(4,80);
    g_lcd_primary.print("VOL ");
    for (int i=0;i<10;i++) {
        g_lcd_primary.fillRect(34+i*9, 80, 7, 8,
            i < _volume ? t->primary : t->row_a);
    }
    g_lcd_primary.setCursor(4,98);
    g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);
    g_lcd_primary.print("SPC play A/D trk W/S vol");
}

static void stopPlayback() {
    if (_file) _file.close();
    M5.Speaker.stop();
    _playing = false; _paused = false;
}

static bool openTrack(int idx) {
    if (idx < 0 || idx >= _track_count) return false;
    stopPlayback();

    char path[96];
    snprintf(path, sizeof(path), "/purplx/music/%s", _tracks[idx]);
    _file = SD.open(path);
    if (!_file) return false;

    WavHeader h;
    if (_file.read((uint8_t*)&h, sizeof(h)) != sizeof(h)) { _file.close(); return false; }
    if (strncmp(h.riff,"RIFF",4)!=0 || strncmp(h.wave,"WAVE",4)!=0) {
        _file.close(); return false;
    }
    _channels = h.channels ? h.channels : 1;
    _bits     = h.bitsPerSample ? h.bitsPerSample : 16;
    _rate     = h.sampleRate ? h.sampleRate : 44100;

    // find 'data' chunk (it may not immediately follow fmt)
    // We already read a standard 44-byte header; seek to find "data".
    _file.seek(12); // after RIFF/size/WAVE
    char cid[4]; uint32_t csz;
    _data_start = 0; _data_len = 0;
    while (_file.available()) {
        if (_file.read((uint8_t*)cid,4)!=4) break;
        if (_file.read((uint8_t*)&csz,4)!=4) break;
        if (strncmp(cid,"data",4)==0) {
            _data_start = _file.position();
            _data_len   = csz;
            break;
        }
        _file.seek(_file.position() + csz); // skip chunk
    }
    if (_data_start == 0) { _file.close(); return false; }

    _file.seek(_data_start);
    _data_pos = 0;
    _now_playing = idx;
    _playing = true; _paused = false;

    M5.Speaker.setVolume(_volume * 25);  // 0..250
    return true;
}

// =============================================================================
void start() {
    _running = true;
    M5.Speaker.begin();
    M5.Speaker.setVolume(_volume * 25);
    scanTracks();
    _sel = 0;
    drawBrowser();
    drawHUD();
}

void handleKey(char key) {
    if (key == 'w' || key == 'W') {
        if (_now_playing < 0) {           // browsing: move cursor
            if (_sel > 0) _sel--; drawBrowser();
        } else {                          // playing: volume up
            if (_volume < 10) _volume++;
            M5.Speaker.setVolume(_volume*25); drawHUD();
        }
    }
    else if (key == 's' || key == 'S') {
        if (_now_playing < 0) {
            if (_sel < _track_count-1) _sel++; drawBrowser();
        } else {
            if (_volume > 0) _volume--;
            M5.Speaker.setVolume(_volume*25); drawHUD();
        }
    }
    else if (key == '\n' || key == '\r') {
        if (openTrack(_sel)) { drawBrowser(); drawHUD(); }
    }
    else if (key == ' ') {
        if (_playing) {
            _paused = !_paused;
            if (_paused) M5.Speaker.stop();
            drawHUD();
        } else if (_track_count > 0) {
            if (openTrack(_sel)) { drawBrowser(); drawHUD(); }
        }
    }
    else if (key == 'd' || key == 'D') {
        int n = (_now_playing < 0 ? _sel : _now_playing) + 1;
        if (n < _track_count && openTrack(n)) { _sel=n; drawBrowser(); drawHUD(); }
    }
    else if (key == 'a' || key == 'A') {
        int n = (_now_playing < 0 ? _sel : _now_playing) - 1;
        if (n >= 0 && openTrack(n)) { _sel=n; drawBrowser(); drawHUD(); }
    }
}

void tick(uint32_t now) {
    if (!_running || !_playing || _paused || !_file) return;

    // Push one buffer if the speaker can accept it
    if (M5.Speaker.isPlaying() < 2) {  // queue depth < 2 → room for more
        size_t want = BUF_SAMPLES;
        size_t bytesPerSample = (_bits/8) * _channels;
        size_t remaining = (_data_len > _data_pos) ? (_data_len - _data_pos) : 0;
        size_t toRead = want * (_bits/8);  // we read into int16 buffer
        if (toRead > remaining) toRead = remaining;

        if (toRead == 0) {
            // track ended → auto-advance
            int n = _now_playing + 1;
            stopPlayback();
            if (n < _track_count) { openTrack(n); _sel=n; drawBrowser(); }
            else { _now_playing=-1; }
            drawHUD();
            return;
        }

        int n = _file.read((uint8_t*)_buf, toRead);
        if (n <= 0) return;
        _data_pos += n;

        const int samples = n / (_bits/8);
        // Feed to speaker (M5.Speaker handles rate). For 16-bit:
        M5.Speaker.playRaw(_buf, samples, _rate, _channels > 1);

        // ── Visualizer: cheap amplitude → bars ───────────────────────────────
        // Compute a few band energies from the buffer for animation only.
        float energy[NBARS] = {0};
        int per = samples / NBARS; if (per < 1) per = 1;
        for (int b = 0; b < NBARS; b++) {
            long acc = 0;
            for (int i = 0; i < per; i++) {
                int idx = b*per + i;
                if (idx < samples) acc += abs(_buf[idx]);
            }
            energy[b] = (float)acc / per / 32768.0f;
        }
        const ColorTheme* t = g_theme;
        const int vis_y = 160, vis_h = 56, base = vis_y + vis_h;
        g_lcd_secondary.fillRect(0, vis_y, 320, vis_h+2, t->bg);
        const int bw = 320 / NBARS;
        for (int b = 0; b < NBARS; b++) {
            _bars[b] += (energy[b]*vis_h - _bars[b]) * 0.4f;  // smooth
            int h = (int)_bars[b]; if (h > vis_h) h = vis_h;
            uint16_t col = (b % 2) ? t->primary : t->secondary;
            g_lcd_secondary.fillRect(b*bw+2, base-h, bw-3, h, col);
        }
    }
}

bool isRunning() { return _running; }
void stop() {
    stopPlayback();
    _running = false;
    _now_playing = -1;
}

} // namespace Music
