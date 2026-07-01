// =============================================================================
// tron.cpp — Tron Light Cycles Implementation
// =============================================================================
#include "tron.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <Preferences.h>

namespace Games { namespace Tron {

// ─── Grid ─────────────────────────────────────────────────────────────────────
static const int CELL = 5;
static const int GW = 320 / CELL;   // 64
static const int GH = 200 / CELL;   // 40 (board area 200px tall)
static const int OY = 24;           // board starts below title
static uint8_t _grid[40][64];        // 0=empty, 1=P1 trail, 2=P2 trail

// ─── Player state ─────────────────────────────────────────────────────────────
struct Cycle { int x,y,dx,dy; bool alive; uint8_t id; };
static Cycle _p1, _p2;

static bool     _running=false;
static bool     _selecting=true;    // mode-select screen
static int      _modeSel=0;         // 0 = 2P, 1 = AI
static bool     _vsAI=false;
static bool     _over=false;
static int      _winner=0;          // 0=none,1=P1,2=P2,3=draw
static uint32_t _lastStep=0;
static uint32_t _stepMs=70;
static int      _p1wins=0,_p2wins=0;

static const ColorTheme* T(){ return g_theme; }

// =============================================================================
static void drawModeSelect(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8);
    g_lcd_secondary.print("TRON  //  light cycles");

    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(T()->secondary,T()->bg);
    g_lcd_secondary.setCursor(70,50);
    g_lcd_secondary.print("CHOOSE MODE");

    const char* opts[2]={"2 PLAYERS","1 PLAYER vs AI"};
    for(int i=0;i<2;i++){
        int y=90+i*34;
        bool sel=(i==_modeSel);
        if(sel){g_lcd_secondary.fillRect(50,y,220,28,T()->highlight_bg);
                g_lcd_secondary.setTextColor(T()->highlight_text,T()->highlight_bg);}
        else g_lcd_secondary.setTextColor(T()->text,T()->bg);
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setCursor(70,y+5);
        g_lcd_secondary.print(opts[i]);
    }
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->bg);
    g_lcd_secondary.setCursor(60,180);
    g_lcd_secondary.print("W/S choose   ENTER start   ESC quit");

    // HUD
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2); g_lcd_primary.setCursor(4,6);
    g_lcd_primary.print("TRON");
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,32); g_lcd_primary.print("P1 cyan = WASD");
    g_lcd_primary.setCursor(4,46); g_lcd_primary.print("P2 red  = arrows");
    g_lcd_primary.setCursor(4,60); g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.print("Don't crash!");
}

static void drawCellAt(int gx,int gy,uint16_t col){
    g_lcd_secondary.fillRect(gx*CELL, OY+gy*CELL, CELL, CELL, col);
}

static void newRound(){
    memset(_grid,0,sizeof(_grid));
    _p1={8, GH/2, 1,0, true, 1};
    _p2={GW-8, GH/2, -1,0, true, 2};
    _grid[_p1.y][_p1.x]=1;
    _grid[_p2.y][_p2.x]=2;
    _over=false; _winner=0;
    _stepMs=70;

    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,22,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,7);
    g_lcd_secondary.print(_vsAI?"TRON  //  vs AI":"TRON  //  2 player");
    // board border
    g_lcd_secondary.drawRect(0,OY-1,320,202,T()->border);

    uint16_t c1=RGB565(0,220,255), c2=RGB565(255,40,40);
    drawCellAt(_p1.x,_p1.y,c1);
    drawCellAt(_p2.x,_p2.y,c2);
    _lastStep=millis();
}

static void drawHUD(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2); g_lcd_primary.setCursor(4,6);
    g_lcd_primary.print("TRON");
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(RGB565(0,220,255),T()->hud_bg);
    g_lcd_primary.setCursor(4,34); g_lcd_primary.printf("P1: %d", _p1wins);
    g_lcd_primary.setTextColor(RGB565(255,40,40),T()->hud_bg);
    g_lcd_primary.setCursor(4,48); g_lcd_primary.printf("%s: %d", _vsAI?"AI":"P2", _p2wins);
    g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.setCursor(4,72); g_lcd_primary.print("P1=WASD");
    if(!_vsAI){g_lcd_primary.setCursor(4,84);g_lcd_primary.print("P2=arrows/IJKL");}
}

// AI: simple survival — if next cell ahead is blocked, turn to a safe side
static void aiThink(){
    if(!_p2.alive)return;
    int nx=_p2.x+_p2.dx, ny=_p2.y+_p2.dy;
    auto blocked=[&](int x,int y){
        return x<0||x>=GW||y<0||y>=GH||_grid[y][x]!=0;
    };
    if(blocked(nx,ny)){
        // try turning left or right (relative)
        int leftdx=_p2.dy, leftdy=-_p2.dx;
        int rightdx=-_p2.dy, rightdy=_p2.dx;
        bool leftOk=!blocked(_p2.x+leftdx,_p2.y+leftdy);
        bool rightOk=!blocked(_p2.x+rightdx,_p2.y+rightdy);
        if(leftOk&&rightOk){ if(random(0,2)){_p2.dx=leftdx;_p2.dy=leftdy;}else{_p2.dx=rightdx;_p2.dy=rightdy;} }
        else if(leftOk){_p2.dx=leftdx;_p2.dy=leftdy;}
        else if(rightOk){_p2.dx=rightdx;_p2.dy=rightdy;}
        // else doomed, keep going
    } else {
        // occasionally make a random safe turn to be less predictable
        if(random(0,12)==0){
            int leftdx=_p2.dy,leftdy=-_p2.dx;
            if(!blocked(_p2.x+leftdx,_p2.y+leftdy)){_p2.dx=leftdx;_p2.dy=leftdy;}
        }
    }
}

static void endRound(int w){
    _over=true; _winner=w;
    if(w==1)_p1wins++; else if(w==2)_p2wins++;
    g_lcd_secondary.fillRect(60,90,200,46,T()->title_bg);
    g_lcd_secondary.drawRect(60,90,200,46,T()->primary);
    g_lcd_secondary.setTextSize(2);
    uint16_t wc = (w==1)?RGB565(0,220,255):(w==2)?RGB565(255,40,40):T()->text;
    g_lcd_secondary.setTextColor(wc,T()->title_bg);
    g_lcd_secondary.setCursor(78,98);
    if(w==3)g_lcd_secondary.print("DRAW!");
    else g_lcd_secondary.printf("%s WINS", w==1?"P1":(_vsAI?"AI":"P2"));
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->text,T()->title_bg);
    g_lcd_secondary.setCursor(78,122);
    g_lcd_secondary.print("ENTER again  ESC quit");
    drawHUD();
}

// =============================================================================
void start(){
    _running=true;
    _selecting=true;
    _modeSel=0;
    _p1wins=_p2wins=0;
    drawModeSelect();
}

void handleKey(char key){
    if(_selecting){
        if(key=='w'||key=='W'||key=='s'||key=='S'){_modeSel=1-_modeSel;drawModeSelect();}
        else if(key=='\n'||key=='\r'){
            _vsAI=(_modeSel==1);
            _selecting=false;
            drawHUD();
            newRound();
        }
        return;
    }
    if(_over){
        if(key=='\n'||key=='\r')newRound();
        return;
    }
    // P1 controls: WASD
    switch(key){
        case 'w': case 'W': if(_p1.dy==0){_p1.dx=0;_p1.dy=-1;} break;
        case 's': case 'S': if(_p1.dy==0){_p1.dx=0;_p1.dy=1;} break;
        case 'a': case 'A': if(_p1.dx==0){_p1.dx=-1;_p1.dy=0;} break;
        case 'd': case 'D': if(_p1.dx==0){_p1.dx=1;_p1.dy=0;} break;
    }
    // P2 controls (only if not AI): IJKL (arrows not always available on Cardputer)
    if(!_vsAI){
        switch(key){
            case 'i': case 'I': if(_p2.dy==0){_p2.dx=0;_p2.dy=-1;} break;
            case 'k': case 'K': if(_p2.dy==0){_p2.dx=0;_p2.dy=1;} break;
            case 'j': case 'J': if(_p2.dx==0){_p2.dx=-1;_p2.dy=0;} break;
            case 'l': case 'L': if(_p2.dx==0){_p2.dx=1;_p2.dy=0;} break;
            case ';': if(_p2.dx==0){_p2.dx=1;_p2.dy=0;} break;
        }
    }
}

void tick(uint32_t now){
    if(!_running||_selecting||_over)return;
    if(now-_lastStep<_stepMs)return;
    _lastStep=now;

    if(_vsAI)aiThink();

    // advance both
    int n1x=_p1.x+_p1.dx, n1y=_p1.y+_p1.dy;
    int n2x=_p2.x+_p2.dx, n2y=_p2.y+_p2.dy;

    auto hit=[&](int x,int y){return x<0||x>=GW||y<0||y>=GH||_grid[y][x]!=0;};

    bool d1=hit(n1x,n1y);
    bool d2=hit(n2x,n2y);
    // head-on collision (same target cell)
    if(n1x==n2x && n1y==n2y){ endRound(3); return; }

    if(d1&&d2){ endRound(3); return; }
    if(d1){ endRound(2); return; }
    if(d2){ endRound(1); return; }

    // move
    _p1.x=n1x;_p1.y=n1y;_grid[n1y][n1x]=1;
    _p2.x=n2x;_p2.y=n2y;_grid[n2y][n2x]=2;
    drawCellAt(_p1.x,_p1.y,RGB565(0,220,255));
    drawCellAt(_p2.x,_p2.y,RGB565(255,40,40));

    // gradually speed up
    static uint32_t spdTimer=0;
    if(now-spdTimer>3000 && _stepMs>40){_stepMs-=5;spdTimer=now;}
}

bool isRunning(){return _running;}
void stop(){_running=false;_selecting=true;_over=false;}

} } // namespace Games::Tron
