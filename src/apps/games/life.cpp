// =============================================================================
// life.cpp — Conway's Game of Life
// =============================================================================
// Animated cellular automaton. Looks great on the big screen. Controls:
// SPACE = pause/resume, R = randomize, C = clear, +/- speed, ENTER = step once.
// =============================================================================

#include "life.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"

namespace Games { namespace Life {

static const int CELL=4;
static const int GW=320/CELL;   // 80
static const int GH=200/CELL;   // 50
static const int OY=24;
static uint8_t _cur[50][80];
static uint8_t _nxt[50][80];
static bool    _running=false, _paused=false;
static uint32_t _last=0, _stepMs=120;
static uint32_t _gen=0;

static const ColorTheme* T(){ return g_theme; }

static void randomize(){
    for(int r=0;r<GH;r++)for(int c=0;c<GW;c++)
        _cur[r][c]=(random(0,100)<28)?1:0;
    _gen=0;
}
static void clearGrid(){
    memset(_cur,0,sizeof(_cur)); _gen=0;
}

static void drawHeader(){
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8);
    g_lcd_secondary.print("LIFE  //  Conway's cells");
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(210,8);
    g_lcd_secondary.print(_paused?"PAUSED":"running");
}

static void drawFooter(){
    g_lcd_secondary.fillRect(0,224,320,16,T()->title_bg);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(4,228);
    g_lcd_secondary.print("SPACE pause  R rand  C clear  +/- spd  ESC");
}

static void drawFull(){
    g_lcd_secondary.fillScreen(T()->bg);
    drawHeader(); drawFooter();
    for(int r=0;r<GH;r++)for(int c=0;c<GW;c++)
        if(_cur[r][c])
            g_lcd_secondary.fillRect(c*CELL,OY+r*CELL,CELL-1,CELL-1,T()->primary);
}

static void drawHUD(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("LIFE");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,32);g_lcd_primary.printf("Gen: %lu",(unsigned long)_gen);
    int pop=0; for(int r=0;r<GH;r++)for(int c=0;c<GW;c++)pop+=_cur[r][c];
    g_lcd_primary.setCursor(4,46);g_lcd_primary.printf("Pop: %d",pop);
    g_lcd_primary.setCursor(4,66);g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.print("SPACE pause");
    g_lcd_primary.setCursor(4,78);g_lcd_primary.print("R=random C=clear");
}

static void step(){
    for(int r=0;r<GH;r++){
        for(int c=0;c<GW;c++){
            int n=0;
            for(int dr=-1;dr<=1;dr++)for(int dc=-1;dc<=1;dc++){
                if(dr==0&&dc==0)continue;
                int rr=(r+dr+GH)%GH, cc=(c+dc+GW)%GW;  // wrap edges
                n+=_cur[rr][cc];
            }
            _nxt[r][c]=(_cur[r][c]) ? (n==2||n==3) : (n==3);
        }
    }
    // draw diff
    for(int r=0;r<GH;r++)for(int c=0;c<GW;c++){
        if(_nxt[r][c]!=_cur[r][c]){
            g_lcd_secondary.fillRect(c*CELL,OY+r*CELL,CELL-1,CELL-1,
                _nxt[r][c]?T()->primary:T()->bg);
        }
        _cur[r][c]=_nxt[r][c];
    }
    _gen++;
}

void start(){
    _running=true;_paused=false;_stepMs=120;
    randomize();
    drawFull();drawHUD();
    _last=millis();
}

void handleKey(char key){
    if(key==' '){ _paused=!_paused; drawHeader(); }
    else if(key=='r'||key=='R'){ randomize(); drawFull(); drawHUD(); }
    else if(key=='c'||key=='C'){ clearGrid(); drawFull(); drawHUD(); }
    else if(key=='+'||key=='='){ if(_stepMs>30)_stepMs-=30; }
    else if(key=='-'||key=='_'){ _stepMs+=30; }
    else if((key=='\n'||key=='\r')&&_paused){ step(); drawHUD(); }
}

void tick(uint32_t now){
    if(!_running||_paused)return;
    if(now-_last<_stepMs)return;
    _last=now;
    step();
    static uint32_t hudT=0;
    if(now-hudT>500){hudT=now;drawHUD();}
}

bool isRunning(){return _running;}
void stop(){_running=false;}

} } // namespace Games::Life
