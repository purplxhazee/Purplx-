// =============================================================================
// snake.cpp — Snake Game Implementation
// =============================================================================
#include "snake.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <Preferences.h>

namespace Games { namespace Snake {

// ─── Board geometry (ILI9341 is 320x240 after rotation) ──────────────────────
static const int CELL    = 10;
static const int BOARD_X = 10;
static const int BOARD_Y = 28;
static const int COLS    = 30;   // (320-20)/10
static const int ROWS    = 20;   // (240-28-12)/10
static const int MAX_LEN = COLS * ROWS;

// ─── Game state ───────────────────────────────────────────────────────────────
static int      _sx[MAX_LEN], _sy[MAX_LEN];   // snake segments (head = index 0)
static int      _len;
static int      _dx, _dy;          // current direction
static int      _ndx, _ndy;        // queued direction (anti-180 turn)
static int      _fx, _fy;          // food
static int      _score;
static int      _hiscore;
static bool     _running    = false;
static bool     _gameover   = false;
static uint32_t _last_step  = 0;
static uint32_t _step_ms    = 180;  // ramps down as score rises

// =============================================================================
static void loadHiScore() {
    Preferences p; p.begin("purplx", true);
    _hiscore = p.getInt("snake_hi", 0);
    p.end();
}
static void saveHiScore() {
    Preferences p; p.begin("purplx", false);
    p.putInt("snake_hi", _hiscore);
    p.end();
}

static void placeFood() {
    bool ok = false;
    while (!ok) {
        _fx = random(0, COLS);
        _fy = random(0, ROWS);
        ok = true;
        for (int i = 0; i < _len; i++)
            if (_sx[i] == _fx && _sy[i] == _fy) { ok = false; break; }
    }
}

static void drawCell(int cx, int cy, uint16_t col) {
    g_lcd_secondary.fillRect(BOARD_X + cx * CELL, BOARD_Y + cy * CELL,
                             CELL - 1, CELL - 1, col);
}

static void drawHUD() {
    const ColorTheme* t = g_theme;
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.setTextColor(t->primary, t->hud_bg);
    g_lcd_primary.setTextSize(2);
    g_lcd_primary.setCursor(4, 6);
    g_lcd_primary.print("SNAKE");
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(t->text, t->hud_bg);
    g_lcd_primary.setCursor(4, 34);
    g_lcd_primary.printf("SCORE: %d", _score);
    g_lcd_primary.setCursor(4, 48);
    g_lcd_primary.setTextColor(t->secondary, t->hud_bg);
    g_lcd_primary.printf("BEST:  %d", _hiscore);
    g_lcd_primary.setCursor(4, 70);
    g_lcd_primary.setTextColor(t->text_dim, t->hud_bg);
    g_lcd_primary.print("WASD move  ESC quit");
}

static void drawBoardFrame() {
    const ColorTheme* t = g_theme;
    g_lcd_secondary.fillScreen(t->bg);
    // Title bar
    g_lcd_secondary.fillRect(0, 0, 320, 22, t->title_bg);
    g_lcd_secondary.drawFastHLine(0, 21, 320, t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary, t->title_bg);
    g_lcd_secondary.setCursor(8, 7);
    g_lcd_secondary.print("SNAKE  //  eat the dots");
    // Board border
    g_lcd_secondary.drawRect(BOARD_X - 2, BOARD_Y - 2,
                             COLS * CELL + 3, ROWS * CELL + 3, t->border);
}

// =============================================================================
void start() {
    loadHiScore();
    _len = 3;
    const int startX = COLS / 2, startY = ROWS / 2;
    for (int i = 0; i < _len; i++) { _sx[i] = startX - i; _sy[i] = startY; }
    _dx = 1; _dy = 0; _ndx = 1; _ndy = 0;
    _score = 0; _step_ms = 180;
    _gameover = false; _running = true;
    _last_step = millis();
    placeFood();

    drawBoardFrame();
    drawHUD();
    // initial draw
    const ColorTheme* t = g_theme;
    for (int i = 0; i < _len; i++)
        drawCell(_sx[i], _sy[i], i == 0 ? t->text : t->primary);
    drawCell(_fx, _fy, t->alert);
}

void handleKey(char key) {
    if (_gameover) {
        if (key == '\n' || key == '\r') start();   // restart
        return;
    }
    switch (key) {
        case 'w': case 'W': if (_dy == 0) { _ndx = 0; _ndy = -1; } break;
        case 's': case 'S': if (_dy == 0) { _ndx = 0; _ndy =  1; } break;
        case 'a': case 'A': if (_dx == 0) { _ndx = -1; _ndy = 0; } break;
        case 'd': case 'D': if (_dx == 0) { _ndx =  1; _ndy = 0; } break;
    }
}

void tick(uint32_t now) {
    if (!_running || _gameover) return;
    if (now - _last_step < _step_ms) return;
    _last_step = now;

    _dx = _ndx; _dy = _ndy;
    const int nhx = _sx[0] + _dx;
    const int nhy = _sy[0] + _dy;

    // Wall collision
    if (nhx < 0 || nhx >= COLS || nhy < 0 || nhy >= ROWS) { 
        _gameover = true;
    } else {
        // Self collision
        for (int i = 0; i < _len; i++)
            if (_sx[i] == nhx && _sy[i] == nhy) { _gameover = true; break; }
    }

    if (_gameover) {
        if (_score > _hiscore) { _hiscore = _score; saveHiScore(); }
        const ColorTheme* t = g_theme;
        g_lcd_secondary.fillRect(60, 95, 200, 50, t->title_bg);
        g_lcd_secondary.drawRect(60, 95, 200, 50, t->alert);
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(t->alert, t->title_bg);
        g_lcd_secondary.setCursor(96, 104);
        g_lcd_secondary.print("GAME OVER");
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(t->text, t->title_bg);
        g_lcd_secondary.setCursor(76, 128);
        g_lcd_secondary.print("ENTER restart   ESC quit");
        return;
    }

    const ColorTheme* t = g_theme;
    const bool ate = (nhx == _fx && nhy == _fy);

    // Erase tail unless we grew
    if (!ate) {
        drawCell(_sx[_len - 1], _sy[_len - 1], t->bg);
    } else {
        if (_len < MAX_LEN) _len++;
        _score += 10;
        // speed up every 50 points, floor at 70ms
        _step_ms = 180 - (_score / 50) * 12;
        if (_step_ms < 70) _step_ms = 70;
        placeFood();
        drawCell(_fx, _fy, t->alert);
        drawHUD();
    }

    // Shift body
    for (int i = _len - 1; i > 0; i--) { _sx[i] = _sx[i-1]; _sy[i] = _sy[i-1]; }
    _sx[0] = nhx; _sy[0] = nhy;

    // Redraw head + neck
    drawCell(_sx[0], _sy[0], t->text);
    if (_len > 1) drawCell(_sx[1], _sy[1], t->primary);
}

bool isRunning() { return _running; }
void stop() { _running = false; _gameover = false; }

} } // namespace Games::Snake
