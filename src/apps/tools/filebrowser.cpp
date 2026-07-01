// =============================================================================
// filebrowser.cpp — SD File Browser + Hex Viewer for Purplx Tools
// Browse files/folders. Text view scrollable; any file shows hex view.
// =============================================================================
#include "filebrowser.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <SD.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/spi_periph.h>

namespace Tools { namespace FileBrowser {

static constexpr int MAX_ENTRIES = 64;
static constexpr int VISIBLE     = 11;
static constexpr int ROW_H       = 19;
static constexpr int TEXT_LINES  = 12;
static constexpr int TEXT_COLS   = 52;
static constexpr int HEX_ROWS    = 10;   // hex rows visible at once

enum class Screen : uint8_t { BROWSE, TEXT_VIEW, HEX_VIEW };

static bool   _running = false;
static Screen _screen  = Screen::BROWSE;
static bool   _sd_ok   = false;

// Directory stack
static constexpr int MAX_DEPTH = 8;
static char   _pathStack[MAX_DEPTH][64];
static int    _depth = 0;

// Current directory entries
struct Entry { char name[48]; bool isDir; uint32_t size; };
static Entry _entries[MAX_ENTRIES];
static int   _ecount = 0, _ecursor = 0, _escroll = 0;

// Text view
static char   _tlines[200][TEXT_COLS + 2];
static int    _tcount = 0, _tscroll = 0;

// Hex view
static uint8_t _hexBuf[16 * 200];   // 200 rows × 16 bytes = 3200 bytes
static int     _hexRows = 0, _hexScroll = 0;

static char _viewName[48] = {};
static uint32_t _viewSize = 0;

static const ColorTheme* T() { return g_theme; }

// ── SD init ───────────────────────────────────────────────────────────────────
static bool _sdInit() {
    gpio_set_direction((gpio_num_t)PIN_SD_MISO, GPIO_MODE_INPUT);
    esp_rom_gpio_connect_in_signal(PIN_SD_MISO,
        spi_periph_signal[SPI2_HOST].spiq_in, false);
    SD.end();
    for (int i = 0; i < 3; i++) {
        if (SD.begin(PIN_SD_CS, SPI, 25000000)) return true;
        SD.end(); delay(80);
    }
    return false;
}

// ── Path helpers ──────────────────────────────────────────────────────────────
static const char* _curPath() { return _depth > 0 ? _pathStack[_depth - 1] : "/"; }

static void _buildPath(char* out, size_t outLen, const char* dirPath, const char* name) {
    if (strcmp(dirPath, "/") == 0)
        snprintf(out, outLen, "/%s", name);
    else
        snprintf(out, outLen, "%s/%s", dirPath, name);
}

// ── HUD ───────────────────────────────────────────────────────────────────────
static void _drawHUD(const char* line1, const char* line2 = nullptr,
                     const char* line3 = nullptr) {
    const ColorTheme* t = T();
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.fillRect(0, 0, 240, 4, t->primary);
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(t->primary, t->hud_bg);
    g_lcd_primary.setCursor(4, 10); g_lcd_primary.print(line1);
    if (line2) {
        g_lcd_primary.setTextColor(t->text_dim, t->hud_bg);
        g_lcd_primary.setCursor(4, 28); g_lcd_primary.print(line2);
    }
    if (line3) {
        g_lcd_primary.setTextColor(t->secondary, t->hud_bg);
        g_lcd_primary.setCursor(4, 46); g_lcd_primary.print(line3);
    }
}

// ── Draw browse screen ────────────────────────────────────────────────────────
static void _drawBrowse() {
    const ColorTheme* t = T();
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0, 0, 320, 24, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 23, 320, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary, t->title_bg);
    // Show truncated path
    const char* cp = _curPath();
    char pathDisp[36]; snprintf(pathDisp, sizeof(pathDisp), "%s", cp);
    g_lcd_secondary.setCursor(8, 8); g_lcd_secondary.print(pathDisp);

    if (!_sd_ok) {
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(t->alert, t->bg);
        g_lcd_secondary.setCursor(60, 90); g_lcd_secondary.print("NO SD CARD");
        g_lcd_secondary.fillRect(0, 224, 320, 16, t->title_bg);
        g_lcd_secondary.drawFastHLine(0, 224, 320, t->border);
        g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
        g_lcd_secondary.setCursor(4, 228); g_lcd_secondary.print("[ESC] back");
        _drawHUD("FILE BROWSER", "No SD card");
        return;
    }
    int end = (_escroll + VISIBLE < _ecount) ? _escroll + VISIBLE : _ecount;
    for (int i = _escroll; i < end; i++) {
        int y   = 28 + (i - _escroll) * ROW_H;
        bool sel = (i == _ecursor);
        uint16_t rb = (i & 1) ? t->row_b : t->row_a;
        g_lcd_secondary.fillRect(0, y, 320, ROW_H - 1, sel ? t->highlight_bg : rb);
        g_lcd_secondary.setTextColor(sel ? t->highlight_text : t->text,
                                      sel ? t->highlight_bg  : rb);
        g_lcd_secondary.setCursor(6, y + 5);
        if (_entries[i].isDir) {
            g_lcd_secondary.setTextColor(sel ? t->highlight_text : t->primary,
                                          sel ? t->highlight_bg : rb);
            g_lcd_secondary.print("[+] ");
        } else {
            g_lcd_secondary.print("    ");
        }
        g_lcd_secondary.setTextColor(sel ? t->highlight_text : t->text,
                                      sel ? t->highlight_bg  : rb);
        g_lcd_secondary.print(_entries[i].name);
        if (!_entries[i].isDir) {
            char sz[16];
            if (_entries[i].size >= 1024)
                snprintf(sz, sizeof(sz), " %uK", (unsigned)(_entries[i].size / 1024));
            else
                snprintf(sz, sizeof(sz), " %uB", (unsigned)_entries[i].size);
            int tw = (int)strlen(sz) * 6;
            g_lcd_secondary.setTextColor(sel ? t->highlight_text : t->text_dim,
                                          sel ? t->highlight_bg : rb);
            g_lcd_secondary.setCursor(312 - tw, y + 5);
            g_lcd_secondary.print(sz);
        }
    }
    if (_ecount == 0) {
        g_lcd_secondary.setTextColor(t->text_dim, t->bg);
        g_lcd_secondary.setCursor(8, 60); g_lcd_secondary.print("(empty directory)");
    }
    g_lcd_secondary.fillRect(0, 224, 320, 16, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 224, 320, t->border);
    g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
    g_lcd_secondary.setCursor(4, 228);
    g_lcd_secondary.print("[W/S]move [ENTER]open [ESC]up/exit");

    char info[48]; snprintf(info, sizeof(info), "%-24s", pathDisp);
    char cnt[24]; snprintf(cnt, sizeof(cnt), "%d item(s)", _ecount);
    _drawHUD("FILES", info, cnt);
}

static void _drawText() {
    const ColorTheme* t = T();
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0, 0, 320, 24, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 23, 320, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary, t->title_bg);
    g_lcd_secondary.setCursor(8, 8); g_lcd_secondary.print(_viewName);

    int end = (_tscroll + TEXT_LINES < _tcount) ? _tscroll + TEXT_LINES : _tcount;
    for (int i = _tscroll; i < end; i++) {
        int y = 28 + (i - _tscroll) * 17;
        g_lcd_secondary.setTextColor(t->text, t->bg);
        g_lcd_secondary.setCursor(4, y); g_lcd_secondary.print(_tlines[i]);
    }
    if (_tcount == 0) {
        g_lcd_secondary.setTextColor(t->text_dim, t->bg);
        g_lcd_secondary.setCursor(8, 60); g_lcd_secondary.print("(empty or binary file)");
    }
    g_lcd_secondary.fillRect(0, 224, 320, 16, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 224, 320, t->border);
    g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
    g_lcd_secondary.setCursor(4, 228);
    g_lcd_secondary.print("[W/S]scroll [A]hex view [ESC]back");

    char sz[16];
    if (_viewSize >= 1024) snprintf(sz, sizeof(sz), "%uK", (unsigned)(_viewSize / 1024));
    else                   snprintf(sz, sizeof(sz), "%uB", (unsigned)_viewSize);
    char ln[24]; snprintf(ln, sizeof(ln), "%d lines", _tcount);
    _drawHUD(_viewName, sz, ln);
}

static void _drawHex() {
    const ColorTheme* t = T();
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0, 0, 320, 24, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 23, 320, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary, t->title_bg);
    g_lcd_secondary.setCursor(8, 8); g_lcd_secondary.print(_viewName);
    g_lcd_secondary.setTextColor(t->secondary, t->title_bg);
    g_lcd_secondary.setCursor(200, 8); g_lcd_secondary.print("HEX VIEW");

    int end = (_hexScroll + HEX_ROWS < _hexRows) ? _hexScroll + HEX_ROWS : _hexRows;
    for (int row = _hexScroll; row < end; row++) {
        int y = 28 + (row - _hexScroll) * 20;
        const uint8_t* bytes = _hexBuf + row * 16;
        char line[80];
        // offset
        snprintf(line, sizeof(line), "%04X: ", (unsigned)(row * 16));
        g_lcd_secondary.setTextColor(t->text_dim, t->bg);
        g_lcd_secondary.setCursor(4, y); g_lcd_secondary.print(line);
        // hex bytes
        char hx[4]; int x = 46;
        for (int b = 0; b < 16; b++) {
            snprintf(hx, sizeof(hx), "%02X", bytes[b]);
            g_lcd_secondary.setTextColor(bytes[b] ? t->text : t->text_dim, t->bg);
            g_lcd_secondary.setCursor(x + b * 18, y); g_lcd_secondary.print(hx);
        }
        // ASCII
        g_lcd_secondary.setCursor(4, y + 10);
        for (int b = 0; b < 16; b++) {
            char ac = (bytes[b] >= 32 && bytes[b] < 127) ? (char)bytes[b] : '.';
            g_lcd_secondary.setTextColor(ac != '.' ? t->secondary : t->text_dim, t->bg);
            g_lcd_secondary.setCursor(4 + b * 12, y + 10); g_lcd_secondary.print(ac);
        }
    }
    g_lcd_secondary.fillRect(0, 224, 320, 16, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 224, 320, t->border);
    g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
    g_lcd_secondary.setCursor(4, 228);
    g_lcd_secondary.print("[W/S]scroll [A]text view [ESC]back");

    char sz[16];
    if (_viewSize >= 1024) snprintf(sz, sizeof(sz), "%uK", (unsigned)(_viewSize / 1024));
    else                   snprintf(sz, sizeof(sz), "%uB", (unsigned)_viewSize);
    char rows[24]; snprintf(rows, sizeof(rows), "%d rows", _hexRows);
    _drawHUD(_viewName, sz, rows);
}

// ── Directory scanner ─────────────────────────────────────────────────────────
static void _scanDir(const char* path) {
    _ecount = 0; _ecursor = 0; _escroll = 0;
    if (!_sd_ok) return;
    // First entry: back/up
    if (strcmp(path, "/") != 0) {
        strncpy(_entries[0].name, ".. (back)", sizeof(_entries[0].name) - 1);
        _entries[0].isDir = true; _entries[0].size = 0;
        _ecount = 1;
    }
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    // Dirs first
    for (int pass = 0; pass < 2 && _ecount < MAX_ENTRIES; pass++) {
        dir.rewindDirectory();
        while (_ecount < MAX_ENTRIES) {
            File f = dir.openNextFile();
            if (!f) break;
            bool isDir = f.isDirectory();
            if ((pass == 0) != isDir) { f.close(); continue; }
            const char* name = f.name();
            const char* base = strrchr(name, '/');
            base = base ? base + 1 : name;
            if (base[0] == '.') { f.close(); continue; }  // skip hidden
            strncpy(_entries[_ecount].name, base, sizeof(_entries[0].name) - 1);
            _entries[_ecount].name[sizeof(_entries[0].name) - 1] = 0;
            _entries[_ecount].isDir = isDir;
            _entries[_ecount].size  = isDir ? 0 : (uint32_t)f.size();
            _ecount++;
            f.close();
        }
    }
    dir.close();
}

// ── File loaders ──────────────────────────────────────────────────────────────
static void _loadText(const char* path) {
    _tcount = 0; _tscroll = 0;
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    int li = 0;
    char line[TEXT_COLS + 2] = {};
    while (f.available() && _tcount < 200) {
        char c = (char)f.read();
        if (c == '\r') continue;
        if (c == '\n' || li >= TEXT_COLS) {
            line[li] = 0;
            strncpy(_tlines[_tcount], line, sizeof(_tlines[0]) - 1);
            _tlines[_tcount][sizeof(_tlines[0]) - 1] = 0;
            _tcount++;
            li = 0;
            if (c != '\n' && c >= 32 && c < 127) line[li++] = c;
            continue;
        }
        if (c >= 32 && c < 127) line[li++] = c;
    }
    if (li > 0 && _tcount < 200) {
        line[li] = 0;
        strncpy(_tlines[_tcount], line, sizeof(_tlines[0]) - 1);
        _tcount++;
    }
    f.close();
}

static void _loadHex(const char* path) {
    _hexRows = 0; _hexScroll = 0;
    memset(_hexBuf, 0, sizeof(_hexBuf));
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    int maxBytes = 200 * 16;
    int read = 0;
    while (f.available() && read < maxBytes) {
        _hexBuf[read++] = (uint8_t)f.read();
    }
    _hexRows = (read + 15) / 16;
    f.close();
}

// ── Public API ────────────────────────────────────────────────────────────────
void start() {
    _running = true; _screen = Screen::BROWSE;
    _depth = 0; _sd_ok = false;
    _sd_ok = _sdInit();
    _scanDir("/");
    _drawBrowse();
}

void handleKey(char key) {
    if (!_running) return;

    switch (_screen) {
        case Screen::BROWSE: {
            if (key == 27 || key == '`') {
                if (_depth > 0) {
                    _depth--;
                    _scanDir(_curPath());
                    _drawBrowse();
                } else {
                    OS_SetState(AppState::HOME);
                }
                return;
            }
            if (key == 'w' || key == 'W') {
                if (_ecursor > 0) _ecursor--;
                if (_ecursor < _escroll) _escroll = _ecursor;
                _drawBrowse();
            } else if (key == 's' || key == 'S') {
                if (_ecursor < _ecount - 1) _ecursor++;
                if (_ecursor >= _escroll + VISIBLE) _escroll = _ecursor - VISIBLE + 1;
                _drawBrowse();
            } else if (key == '\n' || key == '\r') {
                if (_ecount == 0) break;
                Entry& e = _entries[_ecursor];
                if (e.isDir) {
                    if (strcmp(e.name, ".. (back)") == 0) {
                        if (_depth > 0) { _depth--; _scanDir(_curPath()); _drawBrowse(); }
                    } else {
                        if (_depth < MAX_DEPTH - 1) {
                            _buildPath(_pathStack[_depth], sizeof(_pathStack[0]),
                                       _curPath(), e.name);
                            _depth++;
                            _scanDir(_curPath());
                            _drawBrowse();
                        }
                    }
                } else {
                    // Open file as text
                    strncpy(_viewName, e.name, sizeof(_viewName) - 1);
                    _viewSize = e.size;
                    char fullPath[80]; _buildPath(fullPath, sizeof(fullPath), _curPath(), e.name);
                    _loadText(fullPath);
                    _screen = Screen::TEXT_VIEW; _drawText();
                }
            }
            break;
        }
        case Screen::TEXT_VIEW:
            if (key == 27 || key == '`') { _screen = Screen::BROWSE; _drawBrowse(); return; }
            if (key == 'w' || key == 'W') {
                if (_tscroll > 0) { _tscroll--; _drawText(); }
            } else if (key == 's' || key == 'S') {
                if (_tscroll + TEXT_LINES < _tcount) { _tscroll++; _drawText(); }
            } else if (key == 'a' || key == 'A') {
                // Toggle to hex view
                char fullPath[80]; _buildPath(fullPath, sizeof(fullPath), _curPath(), _viewName);
                _loadHex(fullPath);
                _screen = Screen::HEX_VIEW; _drawHex();
            }
            break;

        case Screen::HEX_VIEW:
            if (key == 27 || key == '`') { _screen = Screen::BROWSE; _drawBrowse(); return; }
            if (key == 'w' || key == 'W') {
                if (_hexScroll > 0) { _hexScroll--; _drawHex(); }
            } else if (key == 's' || key == 'S') {
                if (_hexScroll + HEX_ROWS < _hexRows) { _hexScroll++; _drawHex(); }
            } else if (key == 'a' || key == 'A') {
                _screen = Screen::TEXT_VIEW; _drawText();
            }
            break;
    }
}

bool isRunning() { return _running; }
void stop()      { SD.end(); _running = false; }

}} // namespace Tools::FileBrowser
