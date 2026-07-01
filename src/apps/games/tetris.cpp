// =============================================================================
// tetris.cpp — Tetris Implementation
// =============================================================================
#include "tetris.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <Preferences.h>

namespace Games { namespace Tetris {

// ─── Playfield ────────────────────────────────────────────────────────────────
static const int FW = 10;   // field width (cells)
static const int FH = 20;   // field height (cells)
static const int CELL = 11; // px per cell
static const int OX = 70;   // board origin x on ILI9341 (centered-ish)
static const int OY = 16;   // board origin y

// Board stores color index (0 = empty, else theme-mapped palette idx 1..7)
static uint8_t _board[FH][FW];

// ─── Tetromino definitions (4 rotations each, 4x4 bitmask) ───────────────────
// Each piece is 4 rotations, each rotation is 16 bits (4x4).
static const uint16_t PIECES[7][4] = {
    // I
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    // O
    {0x6600, 0x6600, 0x6600, 0x6600},
    // T
    {0x4E00, 0x4640, 0x0E40, 0x4C40},
    // S
    {0x6C00, 0x4620, 0x06C0, 0x8C40},
    // Z
    {0xC600, 0x2640, 0x0C60, 0x4C80},
    // J
    {0x8E00, 0x6440, 0x0E20, 0x44C0},
    // L
    {0x2E00, 0x4460, 0x0E80, 0xC440},
};

// Piece colors (mapped to theme accents at draw time)
static const uint8_t PIECE_COLOR[7] = { 1, 2, 3, 4, 5, 6, 7 };

// ─── Game state ───────────────────────────────────────────────────────────────
static int      _piece, _rot, _px, _py;
static int      _nextPiece;
static int      _score, _lines, _level;
static int      _hiscore;
static bool     _running = false, _gameover = false;
static uint32_t _last_drop = 0;
static uint32_t _drop_ms = 600;

// =============================================================================
static uint16_t colorFor(uint8_t idx) {
    const ColorTheme* t = g_theme;
    switch (idx) {
        case 1: return t->primary;
        case 2: return t->secondary;
        case 3: return t->success;
        case 4: return t->alert;
        case 5: return RGB565(255,160,0);
        case 6: return RGB565(160,80,255);
        case 7: return RGB565(0,200,255);
        default:return t->bg;
    }
}

static void loadHi() { Preferences p; p.begin("purplx",true); _hiscore=p.getInt("tetris_hi",0); p.end(); }
static void saveHi() { Preferences p; p.begin("purplx",false); p.putInt("tetris_hi",_hiscore); p.end(); }

static bool cellSet(int piece, int rot, int cx, int cy) {
    return (PIECES[piece][rot] >> (15 - (cy * 4 + cx))) & 1;
}

static bool collides(int piece, int rot, int px, int py) {
    for (int cy = 0; cy < 4; cy++)
        for (int cx = 0; cx < 4; cx++) {
            if (!cellSet(piece, rot, cx, cy)) continue;
            int bx = px + cx, by = py + cy;
            if (bx < 0 || bx >= FW || by >= FH) return true;
            if (by >= 0 && _board[by][bx]) return true;
        }
    return false;
}

static void drawCell(int bx, int by, uint16_t col) {
    g_lcd_secondary.fillRect(OX + bx*CELL, OY + by*CELL, CELL-1, CELL-1, col);
}

static void drawBoard() {
    const ColorTheme* t = g_theme;
    // field background
    g_lcd_secondary.fillRect(OX-2, OY-2, FW*CELL+3, FH*CELL+3, t->bg);
    g_lcd_secondary.drawRect(OX-2, OY-2, FW*CELL+3, FH*CELL+3, t->border);
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++)
            drawCell(x, y, _board[y][x] ? colorFor(_board[y][x]) : t->bg);
}

static void drawPiece(bool erase) {
    for (int cy = 0; cy < 4; cy++)
        for (int cx = 0; cx < 4; cx++) {
            if (!cellSet(_piece, _rot, cx, cy)) continue;
            int bx = _px+cx, by = _py+cy;
            if (by < 0) continue;
            drawCell(bx, by, erase ? g_theme->bg : colorFor(PIECE_COLOR[_piece]));
        }
}

static void drawHUD() {
    const ColorTheme* t = g_theme;
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.setTextColor(t->primary, t->hud_bg);
    g_lcd_primary.setTextSize(2);
    g_lcd_primary.setCursor(4,4);
    g_lcd_primary.print("TETRIS");
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(t->text, t->hud_bg);
    g_lcd_primary.setCursor(4,28);  g_lcd_primary.printf("SCORE %d", _score);
    g_lcd_primary.setCursor(4,40);  g_lcd_primary.printf("LINES %d", _lines);
    g_lcd_primary.setCursor(4,52);  g_lcd_primary.printf("LEVEL %d", _level);
    g_lcd_primary.setTextColor(t->secondary, t->hud_bg);
    g_lcd_primary.setCursor(4,66);  g_lcd_primary.printf("BEST  %d", _hiscore);

    // Next piece preview
    g_lcd_primary.setTextColor(t->text_dim, t->hud_bg);
    g_lcd_primary.setCursor(140,28); g_lcd_primary.print("NEXT");
    for (int cy=0;cy<4;cy++)
        for(int cx=0;cx<4;cx++)
            if (cellSet(_nextPiece,0,cx,cy))
                g_lcd_primary.fillRect(150+cx*8, 40+cy*8, 7,7, colorFor(PIECE_COLOR[_nextPiece]));

    g_lcd_primary.setTextColor(t->text_dim, t->hud_bg);
    g_lcd_primary.setCursor(4,92);
    g_lcd_primary.print("A/D move W rot SPC drop");
}

static void spawn() {
    _piece = _nextPiece;
    _nextPiece = random(0,7);
    _rot = 0; _px = 3; _py = -1;
    if (collides(_piece,_rot,_px,_py)) {
        _gameover = true;
        if (_score > _hiscore) { _hiscore=_score; saveHi(); }
        const ColorTheme* t=g_theme;
        g_lcd_secondary.fillRect(50,100,220,46,t->title_bg);
        g_lcd_secondary.drawRect(50,100,220,46,t->alert);
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(t->alert,t->title_bg);
        g_lcd_secondary.setCursor(86,108); g_lcd_secondary.print("GAME OVER");
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(t->text,t->title_bg);
        g_lcd_secondary.setCursor(70,132); g_lcd_secondary.print("ENTER restart  ESC quit");
    }
    drawHUD();
}

static void lockPiece() {
    for (int cy=0;cy<4;cy++)
        for(int cx=0;cx<4;cx++)
            if (cellSet(_piece,_rot,cx,cy)) {
                int by=_py+cy, bx=_px+cx;
                if (by>=0 && by<FH && bx>=0 && bx<FW)
                    _board[by][bx] = PIECE_COLOR[_piece];
            }
    // clear full lines
    int cleared=0;
    for (int y=FH-1;y>=0;y--) {
        bool full=true;
        for (int x=0;x<FW;x++) if(!_board[y][x]){full=false;break;}
        if (full) {
            cleared++;
            for (int yy=y;yy>0;yy--)
                for(int x=0;x<FW;x++) _board[yy][x]=_board[yy-1][x];
            for(int x=0;x<FW;x++) _board[0][x]=0;
            y++; // recheck same row
        }
    }
    if (cleared) {
        static const int pts[5]={0,40,100,300,1200};
        _score += pts[cleared]*(_level+1);
        _lines += cleared;
        _level = _lines/10;
        _drop_ms = 600 - _level*45; if(_drop_ms<120)_drop_ms=120;
        drawBoard();
    }
    spawn();
}

// =============================================================================
void start() {
    loadHi();
    memset(_board,0,sizeof(_board));
    _score=0;_lines=0;_level=0;_drop_ms=600;
    _gameover=false;_running=true;
    _nextPiece=random(0,7);
    const ColorTheme* t=g_theme;
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0,0,320,14,t->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(OX-2,3); g_lcd_secondary.print("TETRIS");
    drawBoard();
    spawn();
    drawPiece(false);
    _last_drop=millis();
}

void handleKey(char key) {
    if (_gameover) { if(key=='\n'||key=='\r') start(); return; }
    drawPiece(true);  // erase at old pos
    switch(key) {
        case 'a': case 'A': if(!collides(_piece,_rot,_px-1,_py)) _px--; break;
        case 'd': case 'D': if(!collides(_piece,_rot,_px+1,_py)) _px++; break;
        case 's': case 'S': if(!collides(_piece,_rot,_px,_py+1)) _py++; break;
        case 'w': case 'W': {
            int nr=(_rot+1)%4;
            if(!collides(_piece,nr,_px,_py)) _rot=nr;
            else if(!collides(_piece,nr,_px-1,_py)){_px--;_rot=nr;}
            else if(!collides(_piece,nr,_px+1,_py)){_px++;_rot=nr;}
            break;
        }
        case ' ': // hard drop
            while(!collides(_piece,_rot,_px,_py+1)) _py++;
            drawPiece(false); lockPiece(); drawPiece(false); return;
    }
    drawPiece(false); // redraw at new pos
}

void tick(uint32_t now) {
    if (!_running || _gameover) return;
    if (now - _last_drop < _drop_ms) return;
    _last_drop = now;
    drawPiece(true);
    if (!collides(_piece,_rot,_px,_py+1)) {
        _py++; drawPiece(false);
    } else {
        drawPiece(false); lockPiece(); drawPiece(false);
    }
}

bool isRunning() { return _running; }
void stop() { _running=false; _gameover=false; }

} } // namespace Games::Tetris
