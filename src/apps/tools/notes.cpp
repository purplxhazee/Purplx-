// =============================================================================
// notes.cpp — SD Notes / Journal for Purplx Tools
// Lists .txt files under /notes on SD. Open to read, create new notes.
// =============================================================================
#include "notes.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <SD.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/spi_periph.h>

namespace Tools { namespace Notes {

static constexpr int MAX_FILES = 64;
static constexpr int MAX_LINES = 500;
static constexpr int VISIBLE_F = 10;
static constexpr int VISIBLE_L = 12;
static constexpr int BODY_MAX  = 4096;
static constexpr int LINE_W    = 52;

enum class Screen : uint8_t { LIST, READ, NEW_NAME, NEW_BODY };

static bool   _running = false;
static Screen _screen  = Screen::LIST;
static bool   _sd_ok   = false;

static char _fnames[MAX_FILES][48];
static int  _fcount = 0, _fcursor = 0, _fscroll = 0;

static char _lines[MAX_LINES][LINE_W + 2];
static int  _lcount = 0, _lscroll = 0;
static char _open_name[48] = {};

static char _new_name[40] = {};
static int  _new_name_len = 0;
static char _new_body[BODY_MAX] = {};
static int  _new_body_len = 0;

static const ColorTheme* T() { return g_theme; }

// ── SD init (same MISO re-wire pattern as sd_launcher) ────────────────────────
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

// ── HUD ───────────────────────────────────────────────────────────────────────
static void _drawHUD(const char* title,
                     const char* sub1 = nullptr,
                     const char* sub2 = nullptr) {
    const ColorTheme* t = T();
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.fillRect(0, 0, 240, 4, t->primary);
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(t->primary, t->hud_bg);
    g_lcd_primary.setCursor(4, 10); g_lcd_primary.print(title);
    if (sub1) {
        g_lcd_primary.setTextColor(t->text_dim, t->hud_bg);
        g_lcd_primary.setCursor(4, 28); g_lcd_primary.print(sub1);
    }
    if (sub2) {
        g_lcd_primary.setTextColor(t->secondary, t->hud_bg);
        g_lcd_primary.setCursor(4, 46); g_lcd_primary.print(sub2);
    }
}

// ── Draw helpers ──────────────────────────────────────────────────────────────
static void _drawList() {
    const ColorTheme* t = T();
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0, 0, 320, 24, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 23, 320, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary, t->title_bg);
    g_lcd_secondary.setCursor(8, 8); g_lcd_secondary.print("[ NOTES ]");

    if (!_sd_ok) {
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(t->alert, t->bg);
        g_lcd_secondary.setCursor(60, 90); g_lcd_secondary.print("NO SD CARD");
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(t->text_dim, t->bg);
        g_lcd_secondary.setCursor(20, 130); g_lcd_secondary.print("Insert SD and reopen.");
        g_lcd_secondary.fillRect(0, 224, 320, 16, t->title_bg);
        g_lcd_secondary.drawFastHLine(0, 224, 320, t->border);
        g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
        g_lcd_secondary.setCursor(4, 228); g_lcd_secondary.print("[ESC] back");
        _drawHUD("NOTES", "No SD card");
        return;
    }
    if (_fcount == 0) {
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(t->text_dim, t->bg);
        g_lcd_secondary.setCursor(12, 60); g_lcd_secondary.print("No notes found.");
        g_lcd_secondary.setCursor(12, 78);
        g_lcd_secondary.print("Press [A] to create a new note.");
    } else {
        int end = (_fscroll + VISIBLE_F < _fcount) ? _fscroll + VISIBLE_F : _fcount;
        for (int i = _fscroll; i < end; i++) {
            int y   = 28 + (i - _fscroll) * 19;
            bool sel = (i == _fcursor);
            uint16_t rb = (i & 1) ? t->row_b : t->row_a;
            g_lcd_secondary.fillRect(0, y, 320, 18, sel ? t->highlight_bg : rb);
            g_lcd_secondary.setTextColor(sel ? t->highlight_text : t->text,
                                          sel ? t->highlight_bg  : rb);
            g_lcd_secondary.setCursor(8, y + 5);
            g_lcd_secondary.print(sel ? "> " : "  ");
            g_lcd_secondary.print(_fnames[i]);
        }
    }
    g_lcd_secondary.fillRect(0, 224, 320, 16, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 224, 320, t->border);
    g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
    g_lcd_secondary.setCursor(4, 228);
    g_lcd_secondary.print("[W/S]move [ENTER]open [A]new [ESC]exit");

    char cnt[32]; snprintf(cnt, sizeof(cnt), "%d note(s)", _fcount);
    _drawHUD("NOTES", cnt, _fcount > 0 ? "ENTER open  A=new" : "A = new note");
}

static void _drawRead() {
    const ColorTheme* t = T();
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0, 0, 320, 24, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 23, 320, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary, t->title_bg);
    g_lcd_secondary.setCursor(8, 8); g_lcd_secondary.print(_open_name);

    int end = (_lscroll + VISIBLE_L < _lcount) ? _lscroll + VISIBLE_L : _lcount;
    for (int i = _lscroll; i < end; i++) {
        int y = 28 + (i - _lscroll) * 17;
        g_lcd_secondary.setTextColor(t->text, t->bg);
        g_lcd_secondary.setCursor(4, y); g_lcd_secondary.print(_lines[i]);
    }
    if (_lcount == 0) {
        g_lcd_secondary.setTextColor(t->text_dim, t->bg);
        g_lcd_secondary.setCursor(8, 60); g_lcd_secondary.print("(empty file)");
    }
    g_lcd_secondary.fillRect(0, 224, 320, 16, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 224, 320, t->border);
    g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
    g_lcd_secondary.setCursor(4, 228); g_lcd_secondary.print("[W/S]scroll [ESC]back");

    char info[40]; snprintf(info, sizeof(info), "%d lines", _lcount);
    int denom = (_lcount > 0) ? _lcount : 1;
    char scr[24]; snprintf(scr, sizeof(scr), "ln %d/%d", _lscroll + 1, denom);
    _drawHUD(_open_name, info, scr);
}

static void _drawNewName() {
    const ColorTheme* t = T();
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0, 0, 320, 24, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 23, 320, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary, t->title_bg);
    g_lcd_secondary.setCursor(8, 8); g_lcd_secondary.print("[ NEW NOTE - FILENAME ]");

    g_lcd_secondary.setTextColor(t->text_dim, t->bg);
    g_lcd_secondary.setCursor(8, 40);
    g_lcd_secondary.print("Filename (no spaces, .txt added auto):");
    g_lcd_secondary.drawRect(8, 58, 304, 22, t->border);
    g_lcd_secondary.setTextColor(t->text, t->bg);
    g_lcd_secondary.setCursor(12, 64); g_lcd_secondary.print(_new_name);
    int cx = 12 + (int)strlen(_new_name) * 6;
    g_lcd_secondary.fillRect(cx, 64, 5, 9, t->secondary);

    g_lcd_secondary.fillRect(0, 224, 320, 16, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 224, 320, t->border);
    g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
    g_lcd_secondary.setCursor(4, 228);
    g_lcd_secondary.print("[ENTER]confirm [DEL]delete [ESC]cancel");
    _drawHUD("NEW NOTE", "Type filename", "ENTER to confirm");
}

static void _drawNewBody() {
    const ColorTheme* t = T();
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0, 0, 320, 24, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 23, 320, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary, t->title_bg);
    char hdr[52]; snprintf(hdr, sizeof(hdr), "EDIT: %s.txt", _new_name);
    g_lcd_secondary.setCursor(8, 8); g_lcd_secondary.print(hdr);

    // Circular line buffer: keep last 17 display lines
    static char dlines[18][LINE_W + 2];
    int dcount = 0;
    int li = 0;
    memset(dlines, 0, sizeof(dlines));
    for (int i = 0; i < _new_body_len; i++) {
        char c = _new_body[i];
        if (c == '\r') continue;
        if (c == '\n' || li >= LINE_W) {
            dlines[dcount % 18][li] = 0;
            dcount++;
            li = 0;
            if (c != '\n' && c >= 32 && c < 127) dlines[dcount % 18][li++] = c;
            continue;
        }
        if (c >= 32 && c < 127) dlines[dcount % 18][li++] = c;
    }
    dlines[dcount % 18][li] = 0; // current partial line

    int total   = dcount + 1;
    int maxVis  = 17;
    int start   = (total > maxVis) ? dcount - (maxVis - 1) : 0;
    int dy      = 28;
    for (int i = start; i <= dcount && dy < 220; i++) {
        const char* line = dlines[i % 18];
        g_lcd_secondary.setTextColor(t->text, t->bg);
        g_lcd_secondary.setCursor(4, dy); g_lcd_secondary.print(line);
        if (i == dcount) { // cursor
            int cx = 4 + (int)strlen(line) * 6;
            if (cx < 315) g_lcd_secondary.fillRect(cx, dy, 5, 9, t->secondary);
        }
        dy += 11;
    }
    g_lcd_secondary.fillRect(0, 224, 320, 16, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 224, 320, t->border);
    g_lcd_secondary.setTextColor(t->text_dim, t->title_bg);
    g_lcd_secondary.setCursor(4, 228);
    g_lcd_secondary.print("[TAB]save [ENTER]newline [ESC]discard");

    char lc[24]; snprintf(lc, sizeof(lc), "%d chars", _new_body_len);
    _drawHUD(hdr, lc, "TAB to save");
}

// ── SD helpers ────────────────────────────────────────────────────────────────
static void _scanNotes() {
    _fcount = 0;
    if (!_sd_ok) return;
    if (!SD.exists("/notes")) SD.mkdir("/notes");
    File dir = SD.open("/notes");
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    while (_fcount < MAX_FILES) {
        File f = dir.openNextFile();
        if (!f) break;
        if (!f.isDirectory()) {
            const char* name = f.name();
            const char* base = strrchr(name, '/');
            base = base ? base + 1 : name;
            int len = (int)strlen(base);
            if (len > 4 && strcasecmp(base + len - 4, ".txt") == 0) {
                strncpy(_fnames[_fcount], base, sizeof(_fnames[0]) - 1);
                _fnames[_fcount][sizeof(_fnames[0]) - 1] = 0;
                _fcount++;
            }
        }
        f.close();
    }
    dir.close();
}

static void _loadFile(const char* fname) {
    _lcount = 0; _lscroll = 0;
    char path[64]; snprintf(path, sizeof(path), "/notes/%s", fname);
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    int li = 0;
    char line[LINE_W + 2] = {};
    while (f.available() && _lcount < MAX_LINES) {
        char c = (char)f.read();
        if (c == '\r') continue;
        if (c == '\n' || li >= LINE_W) {
            line[li] = 0;
            strncpy(_lines[_lcount], line, sizeof(_lines[0]) - 1);
            _lines[_lcount][sizeof(_lines[0]) - 1] = 0;
            _lcount++;
            li = 0;
            if (c != '\n' && c >= 32 && c < 127) line[li++] = c;
            continue;
        }
        if (c >= 32 && c < 127) line[li++] = c;
    }
    if ((li > 0 || _lcount == 0) && _lcount < MAX_LINES) {
        line[li] = 0;
        strncpy(_lines[_lcount], line, sizeof(_lines[0]) - 1);
        _lcount++;
    }
    f.close();
}

static bool _saveFile() {
    if (_new_name_len == 0) return false;
    char fname[48]; strncpy(fname, _new_name, sizeof(fname) - 1); fname[sizeof(fname) - 1] = 0;
    int len = (int)strlen(fname);
    if (len < 4 || strcasecmp(fname + len - 4, ".txt") != 0) {
        if (len + 4 < (int)sizeof(fname)) strcat(fname, ".txt");
    }
    char path[64]; snprintf(path, sizeof(path), "/notes/%s", fname);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.write((const uint8_t*)_new_body, (size_t)_new_body_len);
    f.close();
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────
void start() {
    _running = true; _screen = Screen::LIST;
    _fcursor = 0; _fscroll = 0;
    _sd_ok = _sdInit();
    if (_sd_ok) _scanNotes();
    _drawList();
}

void handleKey(char key) {
    if (!_running) return;
    switch (_screen) {
        case Screen::LIST:
            if (key == 27 || key == '`') { OS_SetState(AppState::HOME); return; }
            if (key == 'w' || key == 'W') {
                if (_fcursor > 0) _fcursor--;
                if (_fcursor < _fscroll) _fscroll = _fcursor;
                _drawList();
            } else if (key == 's' || key == 'S') {
                if (_fcursor < _fcount - 1) _fcursor++;
                if (_fcursor >= _fscroll + VISIBLE_F) _fscroll = _fcursor - VISIBLE_F + 1;
                _drawList();
            } else if ((key == '\n' || key == '\r') && _fcount > 0) {
                strncpy(_open_name, _fnames[_fcursor], sizeof(_open_name) - 1);
                _loadFile(_open_name);
                _screen = Screen::READ; _drawRead();
            } else if (key == 'a' || key == 'A') {
                memset(_new_name, 0, sizeof(_new_name)); _new_name_len = 0;
                _screen = Screen::NEW_NAME; _drawNewName();
            }
            break;

        case Screen::READ:
            if (key == 27 || key == '`') { _screen = Screen::LIST; _drawList(); return; }
            if (key == 'w' || key == 'W') {
                if (_lscroll > 0) { _lscroll--; _drawRead(); }
            } else if (key == 's' || key == 'S') {
                if (_lscroll + VISIBLE_L < _lcount) { _lscroll++; _drawRead(); }
            }
            break;

        case Screen::NEW_NAME:
            if (key == 27 || key == '`') { _screen = Screen::LIST; _drawList(); return; }
            if (key == '\n' || key == '\r') {
                if (_new_name_len > 0) {
                    memset(_new_body, 0, sizeof(_new_body)); _new_body_len = 0;
                    _screen = Screen::NEW_BODY; _drawNewBody();
                }
            } else if (key == 8 || key == 127) {
                if (_new_name_len > 0) { _new_name[--_new_name_len] = 0; _drawNewName(); }
            } else if (key > 32 && key < 127 && key != '/' && _new_name_len < 35) {
                _new_name[_new_name_len++] = key; _new_name[_new_name_len] = 0;
                _drawNewName();
            }
            break;

        case Screen::NEW_BODY:
            if (key == 27 || key == '`') { _screen = Screen::LIST; _drawList(); return; }
            if (key == '\t') {
                if (_saveFile()) {
                    const ColorTheme* t = T();
                    g_lcd_primary.fillScreen(t->hud_bg);
                    g_lcd_primary.fillRect(0, 0, 240, 4, t->success);
                    g_lcd_primary.setTextSize(2);
                    g_lcd_primary.setTextColor(t->success, t->hud_bg);
                    g_lcd_primary.setCursor(4, 20); g_lcd_primary.print("SAVED!");
                    g_lcd_primary.setTextSize(1);
                    g_lcd_primary.setTextColor(t->text_dim, t->hud_bg);
                    g_lcd_primary.setCursor(4, 50); g_lcd_primary.print(_new_name);
                    delay(700);
                    _scanNotes();
                }
                _screen = Screen::LIST; _drawList();
            } else if (key == '\n' || key == '\r') {
                if (_new_body_len < BODY_MAX - 2) {
                    _new_body[_new_body_len++] = '\n'; _drawNewBody();
                }
            } else if (key == 8 || key == 127) {
                if (_new_body_len > 0) { _new_body[--_new_body_len] = 0; _drawNewBody(); }
            } else if (key >= 32 && key < 127 && _new_body_len < BODY_MAX - 1) {
                _new_body[_new_body_len++] = key; _new_body[_new_body_len] = 0;
                _drawNewBody();
            }
            break;
    }
}

bool isRunning() { return _running; }
void stop()      { SD.end(); _running = false; }

}} // namespace Tools::Notes
