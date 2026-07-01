// =============================================================================
// edc_clock.cpp — Clock / Stopwatch / Timer
// =============================================================================
#include "edc_tools.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <M5Unified.h>
#include <time.h>

namespace EDC { namespace Clock {

static const ColorTheme* T(){ return g_theme; }

static int      _mode=0;        // 0=clock 1=stopwatch 2=timer
static bool     _running=false;
static bool     _swRunning=false;
static uint32_t _swStart=0,_swAccum=0;
static int      _timerSet=300;  // seconds (default 5min)
static int      _timerLeft=0;
static bool     _timerRunning=false;
static uint32_t _lastTick=0;

static void drawScreen(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8);
    const char* names[]={"CLOCK","STOPWATCH","TIMER"};
    g_lcd_secondary.printf("TIME  //  %s",names[_mode]);
    // mode tabs
    for(int i=0;i<3;i++){
        int x=180+i*46;
        g_lcd_secondary.setTextColor(i==_mode?T()->primary:T()->text_dim,T()->title_bg);
        g_lcd_secondary.setCursor(x,8);
        const char* t[]={"CLK","SW","TMR"};
        g_lcd_secondary.print(t[i]);
    }
    g_lcd_secondary.fillRect(0,224,320,16,T()->title_bg);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(4,228);
    if(_mode==0) g_lcd_secondary.print("TAB switch mode   ESC home");
    else if(_mode==1) g_lcd_secondary.print("SPACE start/stop  R reset  TAB mode");
    else g_lcd_secondary.print("SPACE start  W/S set  R reset  TAB mode");

    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("TIME");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.setCursor(4,34);g_lcd_primary.print("TAB = switch:");
    g_lcd_primary.setCursor(4,46);g_lcd_primary.print("Clock/SW/Timer");
}

static void renderBig(){
    char buf[16];
    if(_mode==0){
        time_t t=time(nullptr);
        struct tm* lt=localtime(&t);
        if(t>100000) snprintf(buf,sizeof(buf),"%02d:%02d:%02d",lt->tm_hour,lt->tm_min,lt->tm_sec);
        else snprintf(buf,sizeof(buf),"--:--:--");
    } else if(_mode==1){
        uint32_t ms=_swAccum + (_swRunning?(millis()-_swStart):0);
        int m=(ms/60000),s=(ms/1000)%60,cs=(ms/10)%100;
        snprintf(buf,sizeof(buf),"%02d:%02d.%02d",m,s,cs);
    } else {
        int v=_timerRunning?_timerLeft:_timerSet;
        int m=v/60,s=v%60;
        snprintf(buf,sizeof(buf),"%02d:%02d",m,s);
    }
    g_lcd_secondary.fillRect(0,80,320,80,T()->bg);
    g_lcd_secondary.setTextSize(5);
    uint16_t col=T()->secondary;
    if(_mode==2&&_timerRunning&&_timerLeft<=10)col=T()->alert;
    g_lcd_secondary.setTextColor(col,T()->bg);
    int tw=strlen(buf)*6*5;
    g_lcd_secondary.setCursor(160-tw/2,100);
    g_lcd_secondary.print(buf);

    // mirror on primary big
    g_lcd_primary.fillRect(0,62,240,40,T()->hud_bg);
    g_lcd_primary.setTextSize(3);
    g_lcd_primary.setTextColor(col,T()->hud_bg);
    int tw2=strlen(buf)*6*3;
    g_lcd_primary.setCursor(120-tw2/2,70);
    g_lcd_primary.print(buf);
}

void start(){
    _running=true; _mode=0;
    M5.Rtc.begin();
    drawScreen(); renderBig();
    _lastTick=millis();
}

void handleKey(char key){
    if(key=='\t'){ _mode=(_mode+1)%3; drawScreen(); renderBig(); return; }
    if(_mode==1){
        if(key==' '){
            if(_swRunning){_swAccum+=millis()-_swStart;_swRunning=false;}
            else{_swStart=millis();_swRunning=true;}
        } else if(key=='r'||key=='R'){_swRunning=false;_swAccum=0;renderBig();}
    } else if(_mode==2){
        if(key==' '){
            if(!_timerRunning){_timerLeft=_timerSet;_timerRunning=true;}
            else _timerRunning=false;
        } else if(key=='r'||key=='R'){_timerRunning=false;_timerLeft=_timerSet;renderBig();}
        else if(key=='w'||key=='W'){ if(!_timerRunning){_timerSet+=30;if(_timerSet>5999)_timerSet=5999;renderBig();} }
        else if(key=='s'||key=='S'){ if(!_timerRunning){_timerSet-=30;if(_timerSet<30)_timerSet=30;renderBig();} }
    }
}

void tick(uint32_t now){
    if(!_running)return;
    if(now-_lastTick<100)return;
    _lastTick=now;
    if(_mode==2&&_timerRunning){
        static uint32_t sec=0;
        if(now-sec>=1000){
            sec=now;
            _timerLeft--;
            if(_timerLeft<=0){
                _timerRunning=false;_timerLeft=0;
                // beep alarm
                M5.Speaker.tone(880,500);
                g_lcd_secondary.fillRect(60,170,200,30,T()->alert);
                g_lcd_secondary.setTextColor(TFT_WHITE,T()->alert);
                g_lcd_secondary.setTextSize(2);
                g_lcd_secondary.setCursor(96,176);g_lcd_secondary.print("TIME UP!");
            }
        }
    }
    renderBig();
}

bool isRunning(){return _running;}
void stop(){_running=false;M5.Speaker.stop();}

} } // namespace EDC::Clock
