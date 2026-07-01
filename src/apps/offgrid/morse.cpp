// =============================================================================
// morse.cpp — Morse Code Trainer & Sender
// =============================================================================
// Non-blocking transmission: tick() advances through the morse element queue
// using millis() timing, flashing the ILI9341 and beeping M5.Speaker.
// =============================================================================

#include "morse.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <M5Unified.h>

namespace OffGrid { namespace Morse {

// ─── Morse table (A-Z, 0-9) ──────────────────────────────────────────────────
struct MEntry { char c; const char* code; };
static const MEntry TABLE[] = {
    {'A',".-"},{'B',"-..."},{'C',"-.-."},{'D',"-.."},{'E',"."},{'F',"..-."},
    {'G',"--."},{'H',"...."},{'I',".."},{'J',".---"},{'K',"-.-"},{'L',".-.."},
    {'M',"--"},{'N',"-."},{'O',"---"},{'P',".--."},{'Q',"--.-"},{'R',".-."},
    {'S',"..."},{'T',"-"},{'U',"..-"},{'V',"...-"},{'W',".--"},{'X',"-..-"},
    {'Y',"-.--"},{'Z',"--.."},
    {'0',"-----"},{'1',".----"},{'2',"..---"},{'3',"...--"},{'4',"....-"},
    {'5',"....."},{'6',"-...."},{'7',"--..."},{'8',"---.."},{'9',"----."},
};
static const int TABLE_N = sizeof(TABLE)/sizeof(TABLE[0]);

static const char* codeFor(char c){
    if(c>='a'&&c<='z')c-=32;
    for(int i=0;i<TABLE_N;i++)if(TABLE[i].c==c)return TABLE[i].code;
    return nullptr; // space or unknown
}

// ─── Timing (ms) — standard morse ratios ─────────────────────────────────────
static const int DOT   = 120;       // dot length
static const int DASH  = DOT*3;
static const int GAP_EL= DOT;        // between elements in a char
static const int GAP_CH= DOT*3;      // between characters
static const int GAP_WD= DOT*7;      // between words
static const int TONE_HZ = 700;

// ─── Input buffer ─────────────────────────────────────────────────────────────
static char _msg[40];
static int  _len = 0;

// ─── Transmission state ───────────────────────────────────────────────────────
static bool     _running = false;
static bool     _showChart = false;
static bool     _txActive = false;
static int      _txIdx = 0;          // index into _msg
static const char* _curCode = nullptr;
static int      _codePos = 0;
static uint32_t _elemUntil = 0;      // when current element/gap ends
static bool     _toneOn = false;
static int      _txCharHighlight = -1;

static const ColorTheme* T(){ return g_theme; }

// =============================================================================
static void drawScreen(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,22,T()->title_bg);
    g_lcd_secondary.drawFastHLine(0,21,320,T()->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,7);
    g_lcd_secondary.print("MORSE  //  type & transmit");

    if(_showChart){
        // Reference chart (3 columns)
        int col=0,row=0;
        for(int i=0;i<TABLE_N;i++){
            int x=8+col*108;
            int y=30+row*16;
            g_lcd_secondary.setTextColor(T()->secondary,T()->bg);
            g_lcd_secondary.setCursor(x,y);
            char ch[2]={TABLE[i].c,0};
            g_lcd_secondary.print(ch);
            g_lcd_secondary.setTextColor(T()->text,T()->bg);
            g_lcd_secondary.setCursor(x+14,y);
            g_lcd_secondary.print(TABLE[i].code);
            row++;
            if(row>=12){row=0;col++;}
        }
        g_lcd_secondary.fillRect(0,222,320,18,T()->title_bg);
        g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
        g_lcd_secondary.setCursor(6,227);
        g_lcd_secondary.print("[TAB] back to sender   [ESC] quit");
        return;
    }

    // Message box
    g_lcd_secondary.drawRect(8,30,304,30,T()->border);
    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(T()->text,T()->bg);
    g_lcd_secondary.setCursor(14,38);
    g_lcd_secondary.print(_len>0?_msg:"type a message...");

    // Morse preview
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->secondary,T()->bg);
    g_lcd_secondary.setCursor(8,72);
    g_lcd_secondary.print("Morse:");
    int x=8,y=88;
    for(int i=0;i<_len;i++){
        const char* code=codeFor(_msg[i]);
        uint16_t col=(i==_txCharHighlight)?T()->alert:T()->text;
        g_lcd_secondary.setTextColor(col,T()->bg);
        if(!code){ x+=10; continue; } // space
        for(const char* p=code;*p;p++){
            g_lcd_secondary.setCursor(x,y);
            g_lcd_secondary.print(*p=='.'?".":"-");
            x+=(*p=='.')?5:8;
        }
        x+=8;
        if(x>290){x=8;y+=14; if(y>180)break;}
    }

    // Big TX indicator area
    g_lcd_secondary.drawRect(8,150,304,60,T()->border);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->bg);
    g_lcd_secondary.setCursor(14,156);
    g_lcd_secondary.print(_txActive?"TRANSMITTING...":"Ready");

    g_lcd_secondary.fillRect(0,222,320,18,T()->title_bg);
    g_lcd_secondary.drawFastHLine(0,222,320,T()->border);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(6,227);
    g_lcd_secondary.print("ENTER send  TAB chart  BKSP del  ESC quit");
}

static void drawHUD(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2); g_lcd_primary.setCursor(4,6);
    g_lcd_primary.print("MORSE");
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,32);
    g_lcd_primary.printf("Chars: %d/39", _len);
    g_lcd_primary.setCursor(4,50);
    g_lcd_primary.setTextColor(T()->secondary,T()->hud_bg);
    g_lcd_primary.print("SOS = ... --- ...");
    g_lcd_primary.setCursor(4,68);
    g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.print("Screen flashes +");
    g_lcd_primary.setCursor(4,80);
    g_lcd_primary.print("speaker beeps the");
    g_lcd_primary.setCursor(4,92);
    g_lcd_primary.print("message in morse");
}

// =============================================================================
// Transmission engine
// =============================================================================
static void beginTx(){
    if(_len==0)return;
    _txActive=true;
    _txIdx=0;
    _curCode=codeFor(_msg[0]);
    _codePos=0;
    _elemUntil=0;
    _toneOn=false;
    _txCharHighlight=0;
    M5.Speaker.begin();
    drawScreen();
}

static void flashOn(){
    _toneOn=true;
    g_lcd_secondary.fillRect(8,150,304,60,T()->success);
    g_lcd_secondary.setTextColor(TFT_BLACK,T()->success);
    g_lcd_secondary.setTextSize(3);
    g_lcd_secondary.setCursor(110,165);
    g_lcd_secondary.print("BEEP");
    M5.Speaker.tone(TONE_HZ, 10000);  // long tone; we stop it manually
}
static void flashOff(){
    _toneOn=false;
    M5.Speaker.stop();
    g_lcd_secondary.fillRect(8,150,304,60,T()->bg);
    g_lcd_secondary.drawRect(8,150,304,60,T()->border);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setCursor(14,156);
    g_lcd_secondary.print("TRANSMITTING...");
}

void tick(uint32_t now){
    if(!_running || !_txActive) return;
    if(now < _elemUntil) return;

    // advance state machine
    if(_toneOn){
        // we just finished a tone element -> element gap
        flashOff();
        _elemUntil = now + GAP_EL;
        // move to next element in code
        _codePos++;
        return;
    }

    // we're in a gap or starting fresh; figure out next element
    if(_curCode==nullptr){
        // current char is a space -> word gap, then next char
        _txIdx++;
        if(_txIdx>=_len){ // done
            _txActive=false; _txCharHighlight=-1;
            drawScreen();
            return;
        }
        _curCode=codeFor(_msg[_txIdx]);
        _codePos=0;
        _txCharHighlight=_txIdx;
        drawScreen();
        _elemUntil = now + GAP_WD;
        return;
    }

    if(_curCode[_codePos]=='\0'){
        // finished this character -> char gap, advance
        _txIdx++;
        if(_txIdx>=_len){
            _txActive=false; _txCharHighlight=-1;
            drawScreen();
            return;
        }
        _curCode=codeFor(_msg[_txIdx]);
        _codePos=0;
        _txCharHighlight=_txIdx;
        // redraw to move highlight
        drawScreen();
        _elemUntil = now + GAP_CH;
        return;
    }

    // emit the next dot or dash
    char el=_curCode[_codePos];
    flashOn();
    _elemUntil = now + ((el=='.')?DOT:DASH);
}

// =============================================================================
void start(){
    _running=true;
    _showChart=false;
    _txActive=false;
    _len=0; _msg[0]=0;
    drawScreen();
    drawHUD();
}

void handleKey(char key){
    if(_txActive){
        // allow ESC-as-cancel handled by main; ignore typing during tx
        return;
    }
    if(key=='\t'){ _showChart=!_showChart; drawScreen(); return; }

    if(_showChart) return; // only TAB/ESC in chart mode

    if(((key>='a'&&key<='z')||(key>='A'&&key<='Z')||(key>='0'&&key<='9')||key==' ')
       && _len<39){
        char up=(key>='a'&&key<='z')?key-32:key;
        _msg[_len++]=up; _msg[_len]=0;
        drawScreen(); drawHUD();
    }
    else if((key==8||key==127) && _len>0){
        _len--; _msg[_len]=0;
        drawScreen(); drawHUD();
    }
    else if((key=='\n'||key=='\r') && _len>0){
        beginTx();
    }
}

bool isRunning(){ return _running; }
void stop(){
    _running=false;
    _txActive=false;
    M5.Speaker.stop();
}

} } // namespace OffGrid::Morse
