// =============================================================================
// pong.cpp — Pong (2-player or vs AI)
// =============================================================================
#include "pong.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"

namespace Games { namespace Pong {

static const int W=320, H=240, PADW=6, PADH=44, PADX=12;
static float _ballX,_ballY,_velX,_velY;
static float _p1y,_p2y;
static int   _s1,_s2;
static bool  _running=false,_selecting=true,_vsAI=false,_over=false;
static int   _modeSel=0;
static uint32_t _last=0;
static const float PADSPD=6.0f;

static const ColorTheme* T(){ return g_theme; }

static void drawModeSelect(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,W,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8); g_lcd_secondary.print("PONG");
    const char* o[2]={"2 PLAYERS","vs AI"};
    for(int i=0;i<2;i++){
        int y=90+i*34; bool s=(i==_modeSel);
        if(s){g_lcd_secondary.fillRect(70,y,180,28,T()->highlight_bg);
              g_lcd_secondary.setTextColor(T()->highlight_text,T()->highlight_bg);}
        else g_lcd_secondary.setTextColor(T()->text,T()->bg);
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setCursor(100,y+5); g_lcd_secondary.print(o[i]);
    }
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->bg);
    g_lcd_secondary.setCursor(70,180); g_lcd_secondary.print("W/S choose  ENTER start");
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2); g_lcd_primary.setCursor(4,6); g_lcd_primary.print("PONG");
    g_lcd_primary.setTextSize(1); g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.setCursor(4,34); g_lcd_primary.print("P1: W/S");
    g_lcd_primary.setCursor(4,46); g_lcd_primary.print("P2: I/K");
}

static void resetBall(int dir){
    _ballX=W/2; _ballY=H/2;
    _velX=dir*4.0f; _velY=((random(0,2))?1:-1)*3.0f;
}

static void newGame(){
    _p1y=_p2y=H/2-PADH/2; _s1=_s2=0; _over=false;
    resetBall((random(0,2))?1:-1);
    _last=millis();
    g_lcd_secondary.fillScreen(T()->bg);
}

static void drawHUD(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2); g_lcd_primary.setCursor(4,6); g_lcd_primary.print("PONG");
    g_lcd_primary.setTextSize(3);
    g_lcd_primary.setTextColor(T()->secondary,T()->hud_bg);
    g_lcd_primary.setCursor(20,40); g_lcd_primary.printf("%d : %d",_s1,_s2);
    g_lcd_primary.setTextSize(1); g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.setCursor(4,84); g_lcd_primary.print(_vsAI?"P1=W/S  AI right":"P1=W/S P2=I/K");
}

void start(){ _running=true;_selecting=true;_modeSel=0; drawModeSelect(); }

void handleKey(char key){
    if(_selecting){
        if(key=='w'||key=='W'||key=='s'||key=='S'){_modeSel=1-_modeSel;drawModeSelect();}
        else if(key=='\n'||key=='\r'){_vsAI=(_modeSel==1);_selecting=false;newGame();drawHUD();}
        return;
    }
    if(_over){ if(key=='\n'||key=='\r'){newGame();drawHUD();} return; }
    if(key=='w'||key=='W'){ _p1y-=PADSPD*3; }
    else if(key=='s'||key=='S'){ _p1y+=PADSPD*3; }
    if(!_vsAI){
        if(key=='i'||key=='I'){ _p2y-=PADSPD*3; }
        else if(key=='k'||key=='K'){ _p2y+=PADSPD*3; }
    }
    if(_p1y<0)_p1y=0; if(_p1y>H-PADH)_p1y=H-PADH;
    if(_p2y<0)_p2y=0; if(_p2y>H-PADH)_p2y=H-PADH;
}

void tick(uint32_t now){
    if(!_running||_selecting||_over)return;
    if(now-_last<16)return;
    _last=now;

    // erase old
    g_lcd_secondary.fillRect(PADX,(int)_p1y-1,PADW,PADH+2,T()->bg);
    g_lcd_secondary.fillRect(W-PADX-PADW,(int)_p2y-1,PADW,PADH+2,T()->bg);
    g_lcd_secondary.fillRect((int)_ballX-4,(int)_ballY-4,8,8,T()->bg);

    // AI tracks ball
    if(_vsAI){
        float target=_ballY-PADH/2;
        if(_p2y<target-4)_p2y+=PADSPD*0.85f;
        else if(_p2y>target+4)_p2y-=PADSPD*0.85f;
        if(_p2y<0)_p2y=0; if(_p2y>H-PADH)_p2y=H-PADH;
    }

    _ballX+=_velX; _ballY+=_velY;
    if(_ballY<4){_ballY=4;_velY=-_velY;}
    if(_ballY>H-4){_ballY=H-4;_velY=-_velY;}

    // paddle collisions
    if(_ballX-4<PADX+PADW && _ballY>_p1y && _ballY<_p1y+PADH && _velX<0){
        _velX=-_velX*1.05f; _velY+=( _ballY-(_p1y+PADH/2) )*0.1f;
    }
    if(_ballX+4>W-PADX-PADW && _ballY>_p2y && _ballY<_p2y+PADH && _velX>0){
        _velX=-_velX*1.05f; _velY+=( _ballY-(_p2y+PADH/2) )*0.1f;
    }
    // clamp speed
    if(_velX>9)_velX=9; if(_velX<-9)_velX=-9;

    // scoring
    if(_ballX<0){ _s2++; resetBall(1); drawHUD(); g_lcd_secondary.fillScreen(T()->bg); }
    if(_ballX>W){ _s1++; resetBall(-1); drawHUD(); g_lcd_secondary.fillScreen(T()->bg); }

    if(_s1>=7||_s2>=7){
        _over=true;
        g_lcd_secondary.fillRect(60,100,200,40,T()->title_bg);
        g_lcd_secondary.drawRect(60,100,200,40,T()->primary);
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(T()->success,T()->title_bg);
        g_lcd_secondary.setCursor(80,108);
        g_lcd_secondary.printf("%s WINS",_s1>=7?"LEFT":(_vsAI?"AI":"RIGHT"));
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(T()->text,T()->title_bg);
        g_lcd_secondary.setCursor(96,130); g_lcd_secondary.print("ENTER again");
        return;
    }

    // draw
    g_lcd_secondary.fillRect(PADX,(int)_p1y,PADW,PADH,RGB565(0,220,255));
    g_lcd_secondary.fillRect(W-PADX-PADW,(int)_p2y,PADW,PADH,RGB565(255,80,80));
    g_lcd_secondary.fillRect((int)_ballX-4,(int)_ballY-4,8,8,T()->text);
    // center dashes
    for(int y=0;y<H;y+=20) g_lcd_secondary.fillRect(W/2-1,y,2,10,T()->border);
}

bool isRunning(){return _running;}
void stop(){_running=false;_selecting=true;_over=false;}

} } // namespace Games::Pong
