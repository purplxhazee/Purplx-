// =============================================================================
// wordle.cpp — Wordle Implementation
// =============================================================================
#include "wordle.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <SD.h>
#include <Preferences.h>

namespace Games { namespace Wordle {

// ─── Built-in fallback word list (used if SD file missing) ───────────────────
// Kept modest so it lives in flash without bloat. The SD list (words5.txt)
// can hold thousands more.
static const char* BUILTIN[] = {
    "APPLE","BRAVE","CRANE","DRIVE","EAGLE","FLAME","GRAPE","HOUSE","IMAGE","JOKER",
    "KNIFE","LEMON","MONEY","NIGHT","OCEAN","PIANO","QUIET","RIVER","SNAKE","TIGER",
    "ULTRA","VOICE","WATER","XENON","YACHT","ZEBRA","BREAD","CLOUD","DREAM","EARTH",
    "FROST","GHOST","HEART","IVORY","JUICE","KOALA","LIGHT","MAGIC","NORTH","OPERA",
    "POWER","QUEEN","ROBOT","STORM","TRAIN","UNITY","VAULT","WORLD","YOUTH","ZESTY",
    "BLAZE","CHARM","DANCE","EMBER","FAITH","GLORY","HONEY","INPUT","JEWEL","KARMA",
    "LUNAR","MARSH","NOBLE","ORBIT","PEARL","QUILT","RUSTY","SHINE","TOWER","URBAN",
    "VIRUS","WHEAT","CYBER","HACKS","RADIO","MORSE","SOLAR","PROXY","NINJA","PIXEL",
};
static const int BUILTIN_COUNT = sizeof(BUILTIN)/sizeof(BUILTIN[0]);

// ─── Game state ───────────────────────────────────────────────────────────────
static char     _answer[6]   = "";
static char     _guesses[6][6];     // 6 attempts, 5 letters + null
static uint8_t  _state[6][5];       // 0=empty 1=grey 2=yellow 3=green
static int      _row         = 0;   // current guess row
static int      _col         = 0;   // current letter
static bool     _running     = false;
static bool     _won         = false;
static bool     _over        = false;
static int      _wins        = 0;
static int      _streak      = 0;

// SD word list (loaded into PSRAM if available)
static char*    _words       = nullptr;   // flat array, 6 bytes per word
static int      _word_count  = 0;
static bool     _words_on_sd = false;

// =============================================================================
static void loadStats() {
    Preferences p; p.begin("purplx", true);
    _wins   = p.getInt("wordle_w", 0);
    _streak = p.getInt("wordle_s", 0);
    p.end();
}
static void saveStats() {
    Preferences p; p.begin("purplx", false);
    p.putInt("wordle_w", _wins);
    p.putInt("wordle_s", _streak);
    p.end();
}

// Try to load /purplx/games/words5.txt (one 5-letter word per line)
static void loadWordList() {
    _words_on_sd = false;
    _word_count = 0;
    if (_words) { free(_words); _words = nullptr; }

    File f = SD.open("/purplx/games/words5.txt");
    if (!f) return;

    // Count lines first (cap to keep PSRAM use sane)
    const int MAX_WORDS = 8000;
    int count = 0;
    char line[16];
    while (f.available() && count < MAX_WORDS) {
        int n = f.readBytesUntil('\n', line, sizeof(line)-1);
        if (n >= 5) count++;
    }
    if (count < 1) { f.close(); return; }

    _words = (char*)ps_malloc((size_t)count * 6);
    if (!_words) { f.close(); return; }

    f.seek(0);
    int idx = 0;
    while (f.available() && idx < count) {
        int n = f.readBytesUntil('\n', line, sizeof(line)-1);
        if (n < 5) continue;
        // copy 5 letters uppercased
        bool valid = true;
        for (int i = 0; i < 5; i++) {
            char c = line[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c < 'A' || c > 'Z') { valid = false; break; }
            _words[idx*6 + i] = c;
        }
        if (!valid) continue;
        _words[idx*6 + 5] = 0;
        idx++;
    }
    f.close();
    _word_count = idx;
    _words_on_sd = (idx > 0);
}

static void pickAnswer() {
    if (_words_on_sd && _word_count > 0) {
        int r = random(0, _word_count);
        memcpy(_answer, &_words[r*6], 6);
    } else {
        int r = random(0, BUILTIN_COUNT);
        strncpy(_answer, BUILTIN[r], 6);
    }
    _answer[5] = 0;
}

// Check if a guessed word is in our dictionary (SD or builtin).
// If no SD list, we accept any 5-letter input (lenient for small builtin).
static bool isValidWord(const char* w) {
    if (!_words_on_sd) return true;   // lenient mode
    for (int i = 0; i < _word_count; i++)
        if (strncmp(w, &_words[i*6], 5) == 0) return true;
    return false;
}

// =============================================================================
// Drawing
// =============================================================================
static const int TILE = 34;
static const int GAP  = 6;
static const int GRID_X = (320 - (5*TILE + 4*GAP)) / 2;
static const int GRID_Y = 30;

static uint16_t tileColor(uint8_t s) {
    const ColorTheme* t = g_theme;
    switch (s) {
        case 3: return t->success;            // green
        case 2: return RGB565(200,170,0);     // yellow
        case 1: return RGB565(60,60,70);      // grey
        default:return t->bg;
    }
}

static void drawTile(int r, int c) {
    const ColorTheme* t = g_theme;
    const int x = GRID_X + c*(TILE+GAP);
    const int y = GRID_Y + r*(TILE+GAP);
    uint16_t fill = tileColor(_state[r][c]);
    g_lcd_secondary.fillRoundRect(x, y, TILE, TILE, 4, fill);
    g_lcd_secondary.drawRoundRect(x, y, TILE, TILE, 4,
        _state[r][c]==0 ? t->border : fill);
    if (_guesses[r][c]) {
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(_state[r][c]==0 ? t->text : TFT_WHITE);
        g_lcd_secondary.setCursor(x + TILE/2 - 6, y + TILE/2 - 7);
        char ch[2] = { _guesses[r][c], 0 };
        g_lcd_secondary.print(ch);
    }
}

static void drawBoard() {
    const ColorTheme* t = g_theme;
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0,0,320,24,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,23,320,t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(8,8);
    g_lcd_secondary.print("WORDLE  //  guess the word");
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(250,8);
    g_lcd_secondary.print(_words_on_sd ? "SD list" : "builtin");

    for (int r = 0; r < 6; r++)
        for (int c = 0; c < 5; c++)
            drawTile(r, c);

    // Footer
    g_lcd_secondary.fillRect(0,222,320,18,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,222,320,t->border);
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,227);
    g_lcd_secondary.print("Type letters  ENTER submit  BKSP delete");
}

static void drawHUD() {
    const ColorTheme* t = g_theme;
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.setTextColor(t->primary,t->hud_bg);
    g_lcd_primary.setTextSize(2);
    g_lcd_primary.setCursor(4,6);
    g_lcd_primary.print("WORDLE");
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(t->text,t->hud_bg);
    g_lcd_primary.setCursor(4,32);
    g_lcd_primary.printf("Guess %d of 6", _row+1 <= 6 ? _row+1 : 6);
    g_lcd_primary.setCursor(4,46);
    g_lcd_primary.setTextColor(t->secondary,t->hud_bg);
    g_lcd_primary.printf("Wins: %d  Streak: %d", _wins, _streak);
    g_lcd_primary.setCursor(4,68);
    g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);
    g_lcd_primary.print("Green=right spot");
    g_lcd_primary.setCursor(4,80);
    g_lcd_primary.print("Yellow=wrong spot");
    g_lcd_primary.setCursor(4,92);
    g_lcd_primary.print("Grey=not in word");
}

static void showResult() {
    const ColorTheme* t = g_theme;
    g_lcd_secondary.fillRect(40,95,240,52,t->title_bg);
    g_lcd_secondary.drawRect(40,95,240,52, _won?t->success:t->alert);
    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(_won?t->success:t->alert,t->title_bg);
    g_lcd_secondary.setCursor(_won?96:104,102);
    g_lcd_secondary.print(_won?"YOU WON!":"GAME OVER");
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->text,t->title_bg);
    g_lcd_secondary.setCursor(78,126);
    if (_won) g_lcd_secondary.print("ENTER play again  ESC quit");
    else {
        g_lcd_secondary.printf("Word was: %s", _answer);
        g_lcd_secondary.setCursor(70,138);
    }
    // second line for loss
    if (!_won) {
        g_lcd_secondary.setCursor(60,138);
        g_lcd_secondary.print("ENTER again   ESC quit");
    }
}

// =============================================================================
// Game logic
// =============================================================================
static void scoreRow(int r) {
    // Standard Wordle scoring with letter-count handling
    bool used[5] = {false,false,false,false,false};
    // First pass: greens
    for (int i = 0; i < 5; i++) {
        if (_guesses[r][i] == _answer[i]) {
            _state[r][i] = 3;
            used[i] = true;
        } else {
            _state[r][i] = 1;  // default grey
        }
    }
    // Second pass: yellows
    for (int i = 0; i < 5; i++) {
        if (_state[r][i] == 3) continue;
        for (int j = 0; j < 5; j++) {
            if (!used[j] && _guesses[r][i] == _answer[j]) {
                _state[r][i] = 2;
                used[j] = true;
                break;
            }
        }
    }
}

static void newGame() {
    memset(_guesses, 0, sizeof(_guesses));
    memset(_state,   0, sizeof(_state));
    _row = 0; _col = 0; _won = false; _over = false;
    pickAnswer();
    drawBoard();
    drawHUD();
    Serial.printf("[Wordle] answer: %s\n", _answer);  // debug; remove for release
}

// =============================================================================
void start() {
    _running = true;
    loadStats();
    loadWordList();
    newGame();
}

void handleKey(char key) {
    if (_over) {
        if (key=='\n' || key=='\r') newGame();
        return;
    }

    // Letter input
    if (((key>='a'&&key<='z')||(key>='A'&&key<='Z')) && _col < 5) {
        char up = (key>='a'&&key<='z') ? key-32 : key;
        _guesses[_row][_col] = up;
        _state[_row][_col] = 0;
        drawTile(_row, _col);
        _col++;
    }
    // Backspace
    else if ((key==8 || key==127) && _col > 0) {
        _col--;
        _guesses[_row][_col] = 0;
        _state[_row][_col] = 0;
        drawTile(_row, _col);
    }
    // Submit
    else if ((key=='\n'||key=='\r') && _col == 5) {
        _guesses[_row][5] = 0;
        if (!isValidWord(_guesses[_row])) {
            // flash "not a word"
            const ColorTheme* t = g_theme;
            g_lcd_secondary.fillRect(60,200,200,16,t->alert);
            g_lcd_secondary.setTextColor(TFT_WHITE,t->alert);
            g_lcd_secondary.setTextSize(1);
            g_lcd_secondary.setCursor(96,204);
            g_lcd_secondary.print("Not in word list");
            delay(600);
            g_lcd_secondary.fillRect(60,200,200,16,t->bg);
            return;
        }
        scoreRow(_row);
        for (int c = 0; c < 5; c++) drawTile(_row, c);

        // Win?
        if (strncmp(_guesses[_row], _answer, 5) == 0) {
            _won = true; _over = true;
            _wins++; _streak++;
            saveStats();
            drawHUD();
            showResult();
            return;
        }
        _row++;
        _col = 0;
        // Out of guesses?
        if (_row >= 6) {
            _over = true; _won = false;
            _streak = 0;
            saveStats();
            drawHUD();
            showResult();
            return;
        }
        drawHUD();
    }
}

bool isRunning() { return _running; }
void stop() {
    _running = false;
    if (_words) { free(_words); _words = nullptr; }
}

} } // namespace Games::Wordle
