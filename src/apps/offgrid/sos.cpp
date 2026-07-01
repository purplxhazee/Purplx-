// =============================================================================
// sos.cpp — Emergency SOS Beacon
// =============================================================================
// Flashes the screen + beeps the speaker in morse SOS (...---...) on repeat.
// Designed to be seen/heard from a distance. SPACE toggles sound on/off.
// =============================================================================

#include "sos.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <M5Unified.h>

namespace OffGrid { namespace SOS {

static bool _running=false;
static bool _active=false;
static bool _sound=true;
static uint32_t _last=0;
static int _idx=0;

// SOS pattern: dot dot dot (gap) dash dash dash (gap) dot dot dot (long gap)
// durations in ms; positive = light ON, we encode element then gap
struct Elem { bool on; int ms; int hz; };
static const Elem PATTERN[] = {
    {true,200,800},{false,150,0},{true,200,800},{false,150,0},{true,200,800},{false,400,0}, // S
    {true,550,800},{false,150,0},{true,550,800},{false,150,0},{true,550,800},{false,400,0}, // O
    {true,200,800},{false,150,0},{true,200,800},{false,150,0},{true,200,800},{false,1600,0},// S + long pause
};
static const int PN = sizeof(PATTERN)/sizeof(PATTERN[0]);

static const ColorTheme* T(){ return g_theme; }

static void drawIdle(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->alert,T()->title_bg);
    g_lcd_secondary.setCursor(8,8);
    g_lcd_secondary.print("SOS BEACON  //  emergency");

    g_lcd_secondary.setTextSize(3);
    g_lcd_secondary.setTextColor(T()->alert,T()->bg);
    g_lcd_secondary.setCursor(90,70);
    g_lcd_secondary.print("S O S");

    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->text,T()->bg);
    g_lcd_secondary.setCursor(40,120);
    g_lcd_secondary.print("Flashes screen + beeps morse SOS");
    g_lcd_secondary.setCursor(40,134);
    g_lcd_secondary.print("Visible and audible from distance");

    g_lcd_secondary.setTextColor(T()->success,T()->bg);
    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setCursor(70,165);
    g_lcd_secondary.print("ENTER = START");

    g_lcd_secondary.fillRect(0,224,320,16,T()->title_bg);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(4,228);
    g_lcd_secondary.print("ENTER start/stop  SPACE mute  ESC exit");

    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->alert,T()->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("SOS");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.setCursor(4,34);g_lcd_primary.print("ENTER to start");
    g_lcd_primary.setCursor(4,46);g_lcd_primary.print("Real emergency?");
    g_lcd_primary.setCursor(4,58);g_lcd_primary.print("Call 911 if able");
}

void start(){
    _running=true;_active=false;_sound=true;_idx=0;
    M5.Speaker.begin();
    drawIdle();
}

void handleKey(char key){
    if(key=='\n'||key=='\r'){
        _active=!_active;_idx=0;_last=0;
        if(!_active){ M5.Speaker.stop(); drawIdle(); }
    }
    else if(key==' '){ _sound=!_sound; if(!_sound)M5.Speaker.stop(); }
}

void tick(uint32_t now){
    if(!_running||!_active)return;
    const Elem& e=PATTERN[_idx];
    if(now-_last < (uint32_t)e.ms)return;
    _last=now;
    _idx=(_idx+1)%PN;
    const Elem& cur=PATTERN[_idx];

    uint16_t c = cur.on?TFT_WHITE:TFT_BLACK;
    g_lcd_secondary.fillScreen(c);
    g_lcd_primary.fillScreen(c);
    if(cur.on){
        // big SOS label visible during flash
        g_lcd_secondary.setTextSize(4);
        g_lcd_secondary.setTextColor(TFT_RED,TFT_WHITE);
        g_lcd_secondary.setCursor(80,100);
        g_lcd_secondary.print("SOS");
        if(_sound&&cur.hz>0) M5.Speaker.tone(cur.hz,cur.ms);
    } else {
        if(_sound) M5.Speaker.stop();
    }
}

bool isRunning(){return _running;}
void stop(){_active=false;_running=false;M5.Speaker.stop();}

} } // namespace OffGrid::SOS
