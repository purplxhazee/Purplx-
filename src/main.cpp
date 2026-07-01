// =============================================================================
// main.cpp — Purplx (pure cyberdeck)
// Boot "Purplx" splash -> categorized home launcher -> built-in apps.
// =============================================================================

#include "../include/main.h"
#include "../include/themes.h"
#include "core/display.h"
#include "core/radio_manager.h"
#include "ui/cyberfx.h"

// Apps
#include "apps/csi/csi.h"
#include "apps/games/snake.h"
#include "apps/games/netrun.h"
#include "apps/games/tetris.h"
#include "apps/games/wordle.h"
#include "apps/games/chess.h"
#include "apps/games/checkers.h"
#include "apps/games/tron.h"
#include "apps/games/pong.h"
#include "apps/games/game2048.h"
#include "apps/games/life.h"
#include "apps/offgrid/survival.h"
#include "apps/offgrid/morse.h"
#include "apps/offgrid/sos.h"
#include "apps/edc/edc_tools.h"
#include "apps/media/wav_player.h"
#include "apps/learn/learn.h"
#include "apps/system/charging.h"
#include "apps/system/pet.h"
#include "apps/system/sd_launcher.h"
#include "apps/system/wifi_ota.h"
#include "apps/tools/notes.h"
#include "apps/tools/filebrowser.h"

#include <M5Cardputer.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <esp_ota_ops.h>

// =============================================================================
// Menu definition — categorized
// =============================================================================
struct MenuItem { const char* icon; const char* label; const char* desc; AppState target; bool header; };

static const MenuItem MENU[] = {
    { "",     "TOOLS",       "",                          AppState::HOME,         true  },
    { "((o))","CSI Radar",   "Detect people via WiFi",    AppState::TOOL_CSI,     false },
    { "[oo]", "My Pet",      "Virtual cyber-pet",         AppState::TOOL_PET,     false },
    { "==",   "Notes",       "SD journal / text files",   AppState::TOOL_NOTES,   false },
    { "FF",   "Files",       "SD browser + hex view",     AppState::TOOL_FILES,   false },

    { "",     "GAMES",       "",                          AppState::HOME,         true  },
    { "~S~",  "Snake",       "Classic snake",             AppState::G_SNAKE,      false },
    { "[T]",  "Tetris",      "Block stacker",             AppState::G_TETRIS,     false },
    { "Wo",   "Wordle",      "5-letter guess",            AppState::G_WORDLE,     false },
    { "Kg",   "Chess",       "2P or vs AI",               AppState::G_CHESS,      false },
    { "oo",   "Checkers",    "2P or vs AI",               AppState::G_CHECKERS,   false },
    { ">|",   "Tron",        "Light cycles 2P/AI",        AppState::G_TRON,       false },
    { "::",   "Pong",        "2P or vs AI",               AppState::G_PONG,       false },
    { "#",    "2048",        "Slide & combine",           AppState::G_2048,       false },
    { "::.",  "Life",        "Conway's cells",            AppState::G_LIFE,       false },
    { "#>",   "Netrun",      "Cyberpunk roguelike",       AppState::G_NETRUN,     false },

    { "",     "OFF-GRID",    "",                          AppState::HOME,         true  },
    { "(+)",  "Survival",    "Offline guide",             AppState::OG_SURVIVAL,  false },
    { ".-",   "Morse",       "Trainer & sender",          AppState::OG_MORSE,     false },
    { "SOS",  "SOS Beacon",  "Emergency signal",          AppState::OG_SOS,       false },
    { "(o)",  "GPS",         "Needs CapLoRa module",      AppState::OG_GPS,       false },
    { "))",   "LoRa Msg",    "Needs CapLoRa module",      AppState::OG_LORA,      false },
    { "N^",   "Compass",     "Needs CapLoRa module",      AppState::OG_COMPASS,   false },

    { "",     "EDC TOOLS",   "",                          AppState::HOME,         true  },
    { "(t)",  "Clock",       "Time/stopwatch/timer",      AppState::EDC_CLOCK,    false },
    { "-O-",  "Flashlight",  "Light + SOS strobe",        AppState::EDC_FLASHLIGHT,false },
    { ":::",  "Dice/Coin",   "Roll & flip",               AppState::EDC_DICE,     false },
    { "==",   "Notes",       "Save text to SD",           AppState::EDC_NOTES,    false },
    { "+-",   "Calculator",  "Basic math",                AppState::EDC_CALC,     false },
    { "<>",   "Converter",   "Units",                     AppState::EDC_CONVERT,  false },

    { "",     "MEDIA",       "",                          AppState::HOME,         true  },
    { ">>",   "Music",       "WAV player",                AppState::MEDIA_MUSIC,  false },

    { "",     "LEARN",       "",                          AppState::HOME,         true  },
    { "</>",  "Hacking 101", "Ethical hacking",           AppState::LEARN,        false },

    { "",     "SYSTEM",      "",                          AppState::HOME,         true  },
    { "%",    "Settings",    "Theme/background",          AppState::SYS_SETTINGS, false },
    { "[B]",  "Battery",     "Charging mode",             AppState::SYS_CHARGING, false },
    { "[>]",  "SD Launch",   "Boot firmware from SD",      AppState::SYS_FIRMWARES, false },
    { "[W]",  "WiFi Flash",  "Download+boot from web",     AppState::SYS_WIFI_OTA,  false },
    { "i",    "Device Info", "Hardware/memory",           AppState::SYS_INFO,     false },
};
static constexpr int MENU_LEN = sizeof(MENU)/sizeof(MENU[0]);

static int  g_cursor=1;
static int  g_scroll=0;
static bool g_sd_ok=false;
static int  g_set_cursor=0;
static int  g_screen_sel=0;

static LGFX_Sprite* g_home_bg=nullptr;
static bool g_home_bg_ready=false;

// =============================================================================
// Forward declarations
// =============================================================================
static void drawHome();
static void renderHome(uint32_t now);
static void drawSettings();
static void handleSettings(char key);
static void drawSysInfo();
static void drawFirmwares();
static void handleFirmwares(char key);
static void drawSlotScreen(const char* title, const char* module);
static void drawScreenSelect();
static void handleScreenSelect(char key);
static void enterSleep();

// Persist helpers
static void saveDualScreen(){ Preferences p;p.begin("purplx",false);p.putBool("dual",g_dual_screen);p.end(); }
static bool loadDualWasSet(){ Preferences p;p.begin("purplx",true);bool s=p.isKey("dual");if(s)g_dual_screen=p.getBool("dual",true);p.end();return s; }

// =============================================================================
// Menu navigation (skip headers)
// =============================================================================
// =============================================================================
// HOW TO PLAY — shown when a game launches; first keypress dismisses + starts.
// =============================================================================
static bool g_howto_active=false;     // true while a how-to screen is showing
static AppState g_howto_for=AppState::HOME;

struct GameHelp { AppState st; const char* title; const char* goal; const char* controls[5]; };
static const GameHelp GAME_HELP[] = {
  { AppState::G_SNAKE, "SNAKE", "Eat food to grow. Don't hit walls or yourself.",
    {"Arrows : steer", "Eat : grow longer", "ESC : quit", "", ""} },
  { AppState::G_TETRIS, "TETRIS", "Stack blocks. Clear full lines. Don't top out.",
    {"L/R : move piece", "Up : rotate", "Down : drop faster", "ESC : quit", ""} },
  { AppState::G_WORDLE, "WORDLE", "Guess the 5-letter word in 6 tries.",
    {"Type : enter letters", "Enter : submit guess", "Del : backspace", "Green=right spot", "Yellow=wrong spot"} },
  { AppState::G_CHESS, "CHESS", "Checkmate the enemy king. 2P or vs AI.",
    {"Arrows : move cursor", "Enter : pick/place", "ESC : quit", "", ""} },
  { AppState::G_CHECKERS, "CHECKERS", "Jump and capture all enemy pieces.",
    {"Arrows : move cursor", "Enter : pick/place", "ESC : quit", "", ""} },
  { AppState::G_TRON, "TRON", "Outlast your rival. Don't crash into trails.",
    {"Arrows : turn", "Avoid : walls+trails", "ESC : quit", "", ""} },
  { AppState::G_PONG, "PONG", "Bounce the ball past your opponent to score.",
    {"Up/Down : move paddle", "First to win pts", "ESC : quit", "", ""} },
  { AppState::G_2048, "2048", "Slide tiles. Combine matches to reach 2048.",
    {"Arrows : slide all", "Same+same : merge", "ESC : quit", "", ""} },
  { AppState::G_LIFE, "LIFE", "Conway's cells. Watch patterns evolve.",
    {"Enter : start/pause", "Arrows : move cursor", "Space : toggle cell", "ESC : quit", ""} },
  { AppState::G_NETRUN, "NETRUN", "Dive the net. Fight, loot, descend. Don't flatline.",
    {"Arrows : move/attack", "Bump enemy : hack it", "E : use stim pack", "> tile : go deeper", "ESC : quit"} },
};
static const int GAME_HELP_N = sizeof(GAME_HELP)/sizeof(GAME_HELP[0]);

static const GameHelp* findHelp(AppState st){
    for(int i=0;i<GAME_HELP_N;i++) if(GAME_HELP[i].st==st) return &GAME_HELP[i];
    return nullptr;
}

// Draw the how-to screen on the BIG screen; mirror a short hint on internal.
static void drawHowTo(AppState st){
    const GameHelp* h=findHelp(st);
    const ColorTheme* t=g_theme;
    g_lcd_secondary.fillScreen(t->bg);
    // title bar
    g_lcd_secondary.fillRect(0,0,320,26,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,25,320,t->primary);
    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(10,6);
    g_lcd_secondary.print(h?h->title:"HOW TO PLAY");
    if(h){
        // goal
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(t->secondary,t->bg);
        g_lcd_secondary.setCursor(12,38);
        g_lcd_secondary.print("GOAL:");
        g_lcd_secondary.setTextColor(t->text,t->bg);
        g_lcd_secondary.setCursor(12,50);
        g_lcd_secondary.print(h->goal);
        // controls
        g_lcd_secondary.setTextColor(t->secondary,t->bg);
        g_lcd_secondary.setCursor(12,76);
        g_lcd_secondary.print("CONTROLS:");
        int y=90;
        for(int i=0;i<5;i++){
            if(h->controls[i] && h->controls[i][0]){
                g_lcd_secondary.setTextColor(t->text,t->bg);
                g_lcd_secondary.setCursor(20,y);
                g_lcd_secondary.print(h->controls[i]);
                y+=15;
            }
        }
    }
    // dismiss prompt
    g_lcd_secondary.setTextColor(t->success,t->bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setCursor(60,210);
    g_lcd_secondary.print(">> Press any key to START <<");

    // internal screen: quick reminder
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.fillRect(0,0,240,4,t->primary);
    g_lcd_primary.setTextSize(2);
    g_lcd_primary.setTextColor(t->primary,t->hud_bg);
    g_lcd_primary.setCursor(8,20);
    g_lcd_primary.print(h?h->title:"GAME");
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(t->text,t->hud_bg);
    g_lcd_primary.setCursor(8,50);
    g_lcd_primary.print("Read controls ->");
    g_lcd_primary.setTextColor(t->success,t->hud_bg);
    g_lcd_primary.setCursor(8,68);
    g_lcd_primary.print("Any key = start");
}

static int nextSel(int from,int dir){
    int i=from;
    for(int n=0;n<MENU_LEN;n++){
        i+=dir; if(i<0)i=MENU_LEN-1; if(i>=MENU_LEN)i=0;
        if(!MENU[i].header)return i;
    }
    return from;
}

// Grid row move: step ~3 selectable items in `dir`, landing on a selectable item.
static int gridRowMove(int from,int dir){
    int i=from;
    for(int step=0;step<3;step++){
        int j=i+dir;
        while(j>=0 && j<MENU_LEN && MENU[j].header) j+=dir;   // skip headers
        if(j<0||j>=MENU_LEN) break;                            // clamp at ends
        i=j;
    }
    return i;
}

// =============================================================================
// OS_SetState
// =============================================================================
bool OS_SetState(AppState next){
    // stop outgoing
    switch(g_app_state){
        case AppState::TOOL_CSI:      CSI::stop_scanning(); break;
        case AppState::TOOL_PET:      Pet::stop(); break;
        case AppState::G_SNAKE:       Games::Snake::stop(); break;
        case AppState::G_TETRIS:      Games::Tetris::stop(); break;
        case AppState::G_WORDLE:      Games::Wordle::stop(); break;
        case AppState::G_CHESS:       Games::Chess::stop(); break;
        case AppState::G_CHECKERS:    Games::Checkers::stop(); break;
        case AppState::G_TRON:        Games::Tron::stop(); break;
        case AppState::G_PONG:        Games::Pong::stop(); break;
        case AppState::G_2048:        Games::G2048::stop(); break;
        case AppState::G_LIFE:        Games::Life::stop(); break;
        case AppState::G_NETRUN:      Games::Netrun::stop(); break;
        case AppState::OG_SURVIVAL:   OffGrid::Survival::stop(); break;
        case AppState::OG_MORSE:      OffGrid::Morse::stop(); break;
        case AppState::OG_SOS:        OffGrid::SOS::stop(); break;
        case AppState::EDC_CLOCK:     EDC::Clock::stop(); break;
        case AppState::EDC_FLASHLIGHT:EDC::Flashlight::stop(); break;
        case AppState::EDC_DICE:      EDC::Dice::stop(); break;
        case AppState::EDC_NOTES:     EDC::Notes::stop(); break;
        case AppState::EDC_CALC:      EDC::Calc::stop(); break;
        case AppState::EDC_CONVERT:   EDC::Convert::stop(); break;
        case AppState::MEDIA_MUSIC:   Music::stop(); break;
        case AppState::LEARN:         Learn::stop(); break;
        case AppState::SYS_CHARGING:  Charging::stop(); break;
        case AppState::SYS_FIRMWARES: SDLauncher::stop(); break;
                case AppState::SYS_WIFI_OTA:   WiFiOTA::stop();    break;
        case AppState::TOOL_NOTES:     Tools::Notes::stop(); break;
        case AppState::TOOL_FILES:     Tools::FileBrowser::stop(); break;
        default: break;
    }

    g_app_state=next;

    // start incoming
    // GAME HOW-TO GATE: if entering a game, show how-to first; real start happens
    // on first keypress (handled in the key loop). Arm the gate and draw it.
    if(findHelp(next)){
        g_howto_active=true;
        g_howto_for=next;
        drawHowTo(next);
        return true;   // do NOT start the game yet
    }
    switch(next){
        case AppState::HOME:          drawHome(); break;
        case AppState::TOOL_CSI:      CSI::start_scanning(); break;
        case AppState::TOOL_PET:      Pet::start(); break;
        case AppState::G_SNAKE:       Games::Snake::start(); break;
        case AppState::G_TETRIS:      Games::Tetris::start(); break;
        case AppState::G_WORDLE:      Games::Wordle::start(); break;
        case AppState::G_CHESS:       Games::Chess::start(); break;
        case AppState::G_CHECKERS:    Games::Checkers::start(); break;
        case AppState::G_TRON:        Games::Tron::start(); break;
        case AppState::G_PONG:        Games::Pong::start(); break;
        case AppState::G_2048:        Games::G2048::start(); break;
        case AppState::G_LIFE:        Games::Life::start(); break;
        case AppState::G_NETRUN:      Games::Netrun::start(); break;
        case AppState::OG_SURVIVAL:   OffGrid::Survival::start(); break;
        case AppState::OG_MORSE:      OffGrid::Morse::start(); break;
        case AppState::OG_SOS:        OffGrid::SOS::start(); break;
        case AppState::OG_GPS:        drawSlotScreen("GPS","CapLoRa SX1262 / ATGM336H"); break;
        case AppState::OG_LORA:       drawSlotScreen("LoRa Messenger","CapLoRa SX1262"); break;
        case AppState::OG_COMPASS:    drawSlotScreen("Compass","CapLoRa GPS module"); break;
        case AppState::EDC_CLOCK:     EDC::Clock::start(); break;
        case AppState::EDC_FLASHLIGHT:EDC::Flashlight::start(); break;
        case AppState::EDC_DICE:      EDC::Dice::start(); break;
        case AppState::EDC_NOTES:     EDC::Notes::start(); break;
        case AppState::EDC_CALC:      EDC::Calc::start(); break;
        case AppState::EDC_CONVERT:   EDC::Convert::start(); break;
        case AppState::MEDIA_MUSIC:   Music::start(); break;
        case AppState::LEARN:         Learn::start(); break;
        case AppState::SYS_SETTINGS:  g_set_cursor=0; drawSettings(); break;
        case AppState::SYS_CHARGING:  Charging::start(); break;
        case AppState::SYS_FIRMWARES: SDLauncher::start(); break;
                case AppState::SYS_WIFI_OTA:   WiFiOTA::start();   break;
        case AppState::TOOL_NOTES:     Tools::Notes::start(); break;
        case AppState::TOOL_FILES:     Tools::FileBrowser::start(); break;
        case AppState::SYS_INFO:      drawSysInfo(); break;
        case AppState::SCREEN_SELECT: g_screen_sel=0; drawScreenSelect(); break;
        default: break;
    }
    return true;
}

// =============================================================================
// Boot splash — "Purplx" over matrix rain
// =============================================================================
static void bootSplash(){
    const ColorTheme* t=g_theme;
    LGFX_Sprite spr(&g_lcd_secondary);
    spr.setPsram(true);
    if(!spr.createSprite(320,240)){
        g_lcd_secondary.fillScreen(TFT_BLACK);
        g_lcd_secondary.setTextSize(5);
        g_lcd_secondary.setTextColor(t->primary);
        g_lcd_secondary.setCursor(40,100);
        g_lcd_secondary.print("PURPLX");
        delay(1500);
        return;
    }
    CyberFX::begin(CyberFX::Effect::MATRIX,320,240);
    for(int f=0;f<55;f++){
        spr.fillSprite(TFT_BLACK);
        CyberFX::draw(spr,millis(),0.85f);
        spr.setTextSize(6);
        if(f>28) spr.setTextColor(t->primary);
        else { uint8_t a=(uint8_t)(f*9); spr.setTextColor(((a>>3)<<11)|((a>>2)<<5)|(a>>3)); }
        spr.setCursor(34,90);
        spr.print("PURPLX");
        if(f>34){
            spr.setTextSize(1);
            spr.setTextColor(t->secondary);
            spr.setCursor(96,150);
            spr.print("ADV  CYBERDECK");
        }
        if(f>44){
            spr.setTextColor(t->text_dim);
            spr.setCursor(120,170);
            spr.print("online");
        }
        spr.pushSprite(0,0);
        delay(30);
    }
    spr.deleteSprite();

    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.setTextColor(t->primary,t->hud_bg);
    g_lcd_primary.setTextSize(3);g_lcd_primary.setCursor(8,20);
    g_lcd_primary.print("PURPLX");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);
    g_lcd_primary.setCursor(8,58);g_lcd_primary.print("booting...");
    delay(300);
}

// =============================================================================
// HOME
// =============================================================================
static void drawHomeContext();  // fwd

static void drawHome(){
    if(!g_home_bg_ready){
        g_home_bg=new LGFX_Sprite(&g_lcd_secondary);
        g_home_bg->setColorDepth(8);          // 8-bit: fits regular RAM (no PSRAM on ADV)
        if(g_home_bg->createSprite(320,240)){
            g_home_bg_ready=true;
            CyberFX::begin(CyberFX::Effect::MATRIX,320,240);
        }
    }
    drawHomeContext();   // paint the internal-screen context panel for current selection
}

// Internal ST7789: live "context panel" describing the hovered app.
static int g_ctx_last=-2;
static void drawHomeContext(){
    const ColorTheme* t=g_theme;
    const MenuItem& m=MENU[g_cursor];
    // top accent strip
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.fillRect(0,0,240,4,t->primary);
    // big icon glyph
    g_lcd_primary.setTextSize(4);
    g_lcd_primary.setTextColor(t->primary,t->hud_bg);
    g_lcd_primary.setCursor(8,16);
    g_lcd_primary.print(m.icon);
    // app name (large)
    g_lcd_primary.setTextSize(2);
    g_lcd_primary.setTextColor(t->text,t->hud_bg);
    g_lcd_primary.setCursor(8,58);
    g_lcd_primary.print(m.label);
    // description
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(t->secondary,t->hud_bg);
    g_lcd_primary.setCursor(8,86);
    g_lcd_primary.print(m.desc);
    // enter prompt
    g_lcd_primary.setTextColor(t->success,t->hud_bg);
    g_lcd_primary.setCursor(8,104);
    g_lcd_primary.print(">> ENTER opens");
    // status footer
    g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);
    g_lcd_primary.setCursor(8,120);
    g_lcd_primary.printf("Heap %uKB  %s", ESP.getFreeHeap()/1024, g_sd_ok?"SD ok":"no SD");
    g_ctx_last=g_cursor;
}

static void renderHome(uint32_t now){
    if(!g_home_bg_ready)return;
    const ColorTheme* t=g_theme;

    // dim animated background
    g_home_bg->fillSprite(t->bg);
    CyberFX::draw(*g_home_bg,now,0.22f);

    // ---- title bar ----
    g_home_bg->fillRect(0,0,320,20,t->title_bg);
    g_home_bg->drawFastHLine(0,19,320,t->primary);
    g_home_bg->setTextSize(1);
    g_home_bg->setTextColor(t->primary,t->title_bg);
    g_home_bg->setCursor(8,6);g_home_bg->print("P U R P L X");
    g_home_bg->setTextColor(t->secondary,t->title_bg);
    char badge[14];snprintf(badge,sizeof(badge),"[%s]",t->name);
    g_home_bg->setCursor(318-g_home_bg->textWidth(badge),6);
    g_home_bg->print(badge);

    // ---- tile grid geometry ----
    const int COLS=3, TW=101, TH=44, GAP=3, GX=4, GY=24;
    const int viewH=240-GY-14;

    // PASS 1: compute layout Y for each item; capture selected Y for scroll
    int ly=0, col=0, selY=0;
    for(int i=0;i<MENU_LEN;i++){
        if(MENU[i].header){
            if(col!=0){ly+=TH+GAP;col=0;}
            ly += (i==0?0:5);
            if(i==g_cursor) selY=ly;
            ly += 15;
        } else {
            if(i==g_cursor) selY=ly;
            col++;
            if(col>=COLS){col=0;ly+=TH+GAP;}
        }
    }
    if(col!=0) ly+=TH+GAP;
    int contentH=ly;

    // smooth scroll toward centering the selected tile
    static float scl=0;
    int target=selY-(viewH/2)+TH/2;
    int maxS=contentH-viewH; if(maxS<0)maxS=0;
    if(target<0)target=0; if(target>maxS)target=maxS;
    scl += (target-scl)*0.25f;
    int sy=(int)scl;

    // PASS 2: render
    ly=0; col=0;
    for(int i=0;i<MENU_LEN;i++){
        if(MENU[i].header){
            if(col!=0){ly+=TH+GAP;col=0;}
            ly += (i==0?0:5);
            int dy=GY+ly-sy;
            if(dy>GY-15 && dy<240-12){
                g_home_bg->setTextColor(t->secondary,t->bg);
                g_home_bg->setCursor(GX+2,dy+2);
                g_home_bg->print(MENU[i].label);
                g_home_bg->drawFastHLine(GX+2,dy+12,308,t->border);
            }
            ly += 15;
        } else {
            int tx=GX+col*(TW+GAP);
            int ty=GY+ly-sy;
            bool sel=(i==g_cursor);
            if(ty>GY-TH && ty<240-12){
                uint16_t bg = sel? t->highlight_bg : t->title_bg;
                g_home_bg->fillRoundRect(tx,ty,TW,TH,4,bg);
                if(sel){
                    uint8_t p=(uint8_t)(150+105*sinf(now*0.006f));
                    uint16_t glow=((p>>3)<<11)|((p>>4)<<5)|(p>>3);
                    g_home_bg->drawRoundRect(tx,ty,TW,TH,4,t->primary);
                    g_home_bg->drawRoundRect(tx-1,ty-1,TW+2,TH+2,5,glow);
                } else {
                    g_home_bg->drawRoundRect(tx,ty,TW,TH,4,t->border);
                }
                // icon
                g_home_bg->setTextSize(2);
                g_home_bg->setTextColor(sel?t->highlight_text:t->primary, bg);
                g_home_bg->setCursor(tx+6,ty+5);
                g_home_bg->print(MENU[i].icon);
                // name
                g_home_bg->setTextSize(1);
                g_home_bg->setTextColor(sel?t->highlight_text:t->text, bg);
                g_home_bg->setCursor(tx+6,ty+29);
                g_home_bg->print(MENU[i].label);
            }
            col++;
            if(col>=COLS){col=0;ly+=TH+GAP;}
        }
    }

    // ---- footer ----
    g_home_bg->fillRect(0,226,320,14,t->title_bg);
    g_home_bg->setTextColor(t->text_dim,t->title_bg);
    g_home_bg->setCursor(6,229);
    g_home_bg->print("[WASD]move [ENTER]open [T]heme [B]g [?]help");

    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(15))==pdTRUE){
        g_home_bg->pushSprite(0,0);
        xSemaphoreGive(g_display_mutex);
    }

    // update internal context panel only when selection changes (no flicker)
    if(g_cursor!=g_ctx_last) drawHomeContext();
}

// =============================================================================
// SETTINGS
// =============================================================================
static void drawSettings(){
    const ColorTheme* t=g_theme;
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0,0,320,24,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,23,320,t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(8,8);g_lcd_secondary.print("SETTINGS");

    struct Row{const char* l;const char* d;};
    static const Row R[]={
        {"Theme","Color scheme (8)"},
        {"Background","Home animation"},
        {"CSI Channel","WiFi ch for radar"},
        {"CSI Sensitivity","Motion threshold"},
        {"Sleep Now","Low-power deep sleep"},
    };
    static const int RC=5;
    char v[5][20];
    snprintf(v[0],20,"%s",g_theme->name);
    snprintf(v[1],20,"%s",CyberFX::name());
    snprintf(v[2],20,"CH %d",CSI::g_config.fixed_channel);
    snprintf(v[3],20,"%d%%",(int)(CSI::g_config.motion_threshold*100));
    snprintf(v[4],20,"press ,");

    for(int i=0;i<RC;i++){
        int y=30+i*30;bool s=(i==g_set_cursor);
        if(s){g_lcd_secondary.fillRect(0,y,320,28,t->highlight_bg);
              g_lcd_secondary.setTextColor(t->highlight_text,t->highlight_bg);}
        else{g_lcd_secondary.fillRect(0,y,320,28,(i%2)?t->row_b:t->row_a);
             g_lcd_secondary.setTextColor(t->text,(i%2)?t->row_b:t->row_a);}
        g_lcd_secondary.setCursor(6,y+4);g_lcd_secondary.print(s?">":" ");
        g_lcd_secondary.setCursor(16,y+4);g_lcd_secondary.print(R[i].l);
        g_lcd_secondary.setTextColor(s?t->highlight_text:t->text_dim,s?t->highlight_bg:((i%2)?t->row_b:t->row_a));
        g_lcd_secondary.setCursor(16,y+15);g_lcd_secondary.print(R[i].d);
        g_lcd_secondary.setTextColor(s?t->highlight_text:t->secondary,s?t->highlight_bg:((i%2)?t->row_b:t->row_a));
        int vw=g_lcd_secondary.textWidth(v[i]);
        g_lcd_secondary.setCursor(312-vw,y+9);g_lcd_secondary.print(v[i]);
    }
    // theme swatches
    for(int i=0;i<(int)ThemeID::COUNT;i++){
        int sw=320/(int)ThemeID::COUNT;bool a=(i==(int)g_theme_id);
        g_lcd_secondary.fillRect(i*sw,232,sw,8,a?TFT_WHITE:THEMES[i]->primary);
    }
    g_lcd_secondary.fillRect(0,222,320,10,t->title_bg);
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,223);g_lcd_secondary.print("[W/S]pick [,][/]change [ESC]back");

    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.setTextColor(t->primary,t->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("CONFIG");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);
    g_lcd_primary.setCursor(4,34);g_lcd_primary.print("Saves instantly");
}

static void handleSettings(char key){
    static const int RC=5;
    if(key=='w'||key=='W'){if(g_set_cursor>0)g_set_cursor--;drawSettings();}
    else if(key=='s'||key=='S'){if(g_set_cursor<RC-1)g_set_cursor++;drawSettings();}
    else if(key=='a'||key=='A'||key=='d'||key=='D'){
        bool inc=(key=='d'||key=='D');
        switch(g_set_cursor){
            case 0: inc?Themes::next():Themes::prev(); drawSettings(); break;
            case 1: CyberFX::next(320,240); drawSettings(); break;
            case 2:
                if(inc){if(CSI::g_config.fixed_channel<13)CSI::g_config.fixed_channel++;}
                else{if(CSI::g_config.fixed_channel>1)CSI::g_config.fixed_channel--;}
                drawSettings(); break;
            case 3:
                if(inc){if(CSI::g_config.motion_threshold<0.9f)CSI::g_config.motion_threshold+=0.05f;}
                else{if(CSI::g_config.motion_threshold>0.1f)CSI::g_config.motion_threshold-=0.05f;}
                drawSettings(); break;
            case 4: enterSleep(); break;
        }
    }
}

// =============================================================================
// SYSINFO
// =============================================================================
static void drawSysInfo(){
    const ColorTheme* t=g_theme;
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0,0,320,24,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,23,320,t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(8,8);g_lcd_secondary.print("DEVICE INFO");

    int y=34;
    auto row=[&](const char* k,const char* val){
        g_lcd_secondary.setTextColor(t->text_dim,t->bg);
        g_lcd_secondary.setCursor(12,y);g_lcd_secondary.print(k);
        g_lcd_secondary.setTextColor(t->text,t->bg);
        g_lcd_secondary.setCursor(150,y);g_lcd_secondary.print(val);
        y+=17;
    };
    char b[32];
    row("Device","Cardputer ADV");
    row("Chip","ESP32-S3");
    snprintf(b,sizeof(b),"%u MHz",ESP.getCpuFreqMHz());row("CPU",b);
    snprintf(b,sizeof(b),"%u MB",ESP.getFlashChipSize()/(1024*1024));row("Flash",b);
    snprintf(b,sizeof(b),"%u MB",ESP.getPsramSize()/(1024*1024));row("PSRAM",b);
    snprintf(b,sizeof(b),"%u KB",ESP.getFreeHeap()/1024);row("Free Heap",b);
    snprintf(b,sizeof(b),"%u KB",ESP.getFreePsram()/1024);row("Free PSRAM",b);
    row("Screens", g_dual_screen?"Dual (ST7789+ILI)":"Single (ST7789)");
    row("SD Card", g_sd_ok?"Mounted":"Not found");
    snprintf(b,sizeof(b),"v%s",PURPLX_VERSION);row("Firmware",b);

    g_lcd_secondary.fillRect(0,224,320,16,t->title_bg);
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,228);g_lcd_secondary.print("[ESC] back");

    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.setTextColor(t->primary,t->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("SYSTEM");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(t->success,t->hud_bg);
    g_lcd_primary.setCursor(4,34);g_lcd_primary.print("All nominal");
}

// =============================================================================
// FIRMWARES info screen
// =============================================================================
static const char* FW_LINES[] = { "(unused — replaced by SDLauncher)" };
static const int FW_N = 1;
static int  g_fw_scroll = 0;
static void drawFirmwares(){ (void)FW_N; (void)g_fw_scroll; SDLauncher::start(); }
static void handleFirmwares(char key){ SDLauncher::handleKey(key); }
// =============================================================================
// SLOT screen (GPS/LoRa/Compass coming soon)
// =============================================================================
static void drawSlotScreen(const char* title,const char* module){
    const ColorTheme* t=g_theme;
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0,0,320,24,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,23,320,t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(8,8);g_lcd_secondary.print(title);

    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(t->secondary,t->bg);
    g_lcd_secondary.setCursor(60,70);g_lcd_secondary.print("COMING SOON");

    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->text,t->bg);
    g_lcd_secondary.setCursor(40,110);g_lcd_secondary.print("This feature needs:");
    g_lcd_secondary.setTextColor(t->primary,t->bg);
    g_lcd_secondary.setCursor(40,128);g_lcd_secondary.print(module);
    g_lcd_secondary.setTextColor(t->text_dim,t->bg);
    g_lcd_secondary.setCursor(40,150);g_lcd_secondary.print("Plug in the module, and a future");
    g_lcd_secondary.setCursor(40,162);g_lcd_secondary.print("Purplx update will enable it.");

    g_lcd_secondary.fillRect(0,224,320,16,t->title_bg);
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,228);g_lcd_secondary.print("[ESC] back to home");

    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.setTextColor(t->primary,t->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print(title);
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);
    g_lcd_primary.setCursor(4,40);g_lcd_primary.print("Needs hardware");
}

// =============================================================================
// SCREEN SELECT (first boot)
// =============================================================================
static void drawScreenSelect(){
    const ColorTheme* t=g_theme;
    g_lcd_primary.fillScreen(t->bg);
    g_lcd_primary.setTextColor(t->primary,t->bg);
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setCursor(4,6);g_lcd_primary.print("SCREEN SETUP");
    g_lcd_primary.setTextColor(t->text,t->bg);
    g_lcd_primary.setCursor(4,22);g_lcd_primary.print("Do you have the 2nd");
    g_lcd_primary.setCursor(4,32);g_lcd_primary.print("(ILI9341) screen?");
    const char* o[2]={"YES - Dual screen","NO - Internal only"};
    for(int i=0;i<2;i++){
        int y=52+i*22;bool s=(i==g_screen_sel);
        if(s){g_lcd_primary.fillRect(2,y,236,18,t->highlight_bg);
              g_lcd_primary.setTextColor(t->highlight_text,t->highlight_bg);}
        else g_lcd_primary.setTextColor(t->text,t->bg);
        g_lcd_primary.setCursor(8,y+5);g_lcd_primary.print(s?">":" ");
        g_lcd_primary.setCursor(20,y+5);g_lcd_primary.print(o[i]);
    }
    g_lcd_primary.setTextColor(t->text_dim,t->bg);
    g_lcd_primary.setCursor(4,104);g_lcd_primary.print("W/S choose ENTER ok");

    if(g_screen_sel==0){
        g_lcd_secondary.fillScreen(t->bg);
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(t->success,t->bg);
        g_lcd_secondary.setCursor(40,90);g_lcd_secondary.print("If you can read");
        g_lcd_secondary.setCursor(40,115);g_lcd_secondary.print("THIS, pick YES!");
    }
}
static void handleScreenSelect(char key){
    Serial.printf("[SCREEN_SELECT] key=0x%02x ('%c')\n", (int)key, (key>=32&&key<127)?key:'.');
    if(key=='w'||key=='W'||key=='s'||key=='S'){
        g_screen_sel=1-g_screen_sel; drawScreenSelect();
    }
    else if(key=='\n'||key=='\r' || key==' '){
        g_dual_screen=(g_screen_sel==0);
        saveDualScreen();
        OS_SetState(AppState::HOME);
    }
    else if(key=='y'||key=='Y'){
        g_dual_screen=true; saveDualScreen(); OS_SetState(AppState::HOME);
    }
    else if(key=='n'||key=='N'){
        g_dual_screen=false; saveDualScreen(); OS_SetState(AppState::HOME);
    }
}

// =============================================================================
// SLEEP
// =============================================================================
static void enterSleep(){
    const ColorTheme* t=g_theme;
    g_lcd_secondary.fillScreen(TFT_BLACK);
    g_lcd_primary.fillScreen(TFT_BLACK);
    g_lcd_primary.setTextColor(t->primary,TFT_BLACK);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(10,40);
    g_lcd_primary.print("SLEEPING");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(t->text_dim,TFT_BLACK);
    g_lcd_primary.setCursor(10,70);g_lcd_primary.print("Press side button");
    g_lcd_primary.setCursor(10,82);g_lcd_primary.print("to wake");
    delay(1500);
    g_lcd_primary.setBrightness(0);
    g_lcd_secondary.setBrightness(0);
    // Wake on G0 button (GPIO0) — held LOW when pressed
    esp_sleep_enable_ext0_wakeup((gpio_num_t)0, 0);
    esp_deep_sleep_start();
}

// =============================================================================
// setup()
// =============================================================================
void setup(){
    // ── Initialize SPI2 bus with correct MISO pin ───────────────────────────────
    // Gives Arduino's SPI object a valid SPI2 bus handle.
    // NOTE: LovyanGFX's panel init later calls gpio_matrix_in(GPIO_FUNC_IN_HIGH,
    // SPIQ_IN_IDX) which DISCONNECTS GPIO39 from SPI2 MISO in the GPIO matrix.
    // The real MISO fix is in SDLauncher::_initAndScan() via
    // esp_rom_gpio_connect_in_signal() which re-wires it right before SD.begin().
    SPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    Serial.begin(115200);
    delay(300);
    Serial.println("BOOT0");
    Serial.println("\n\n=== Purplx v" PURPLX_VERSION " ===");
    Serial.println("BOOT1");

    // ── Mark Purplx as the confirmed/valid boot partition ────────────────────
    // With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y, any partition that does not
    // call this is treated as PENDING_VERIFY and will be rolled back on next reset.
    // Calling it here ensures factory (Purplx) stays as the known-good slot,
    // so that after launching a guest binary, a reset always returns here.
    esp_ota_mark_app_valid_cancel_rollback();

    Themes::begin();
    Serial.println("BOOT2");

    auto cfg=M5.config();
    cfg.fallback_board = m5::board_t::board_M5CardputerADV;
    cfg.clear_display = false;
    Serial.println("BOOT3");
    M5Cardputer.begin(cfg);
    Serial.println("BOOT4");
    OS::Keyboard_begin();
    Serial.println("BOOT5");

    if(!Display::init()){
        Serial.println("[FATAL] display init failed");
        while(true)vTaskDelay(pdMS_TO_TICKS(1000));
    }
    Serial.println("BOOT6");
    bootSplash();
    Serial.println("BOOT7");
    g_sd_ok=false;   // SD disabled (shares bus with internal display)

    Pet::boot_load();   // age the pet in background

    if(!loadDualWasSet()){
        g_app_state=AppState::SCREEN_SELECT;
        g_screen_sel=0;
        drawScreenSelect();
    } else {
        g_app_state=AppState::HOME;
        g_cursor=1;
        drawHome();
    }
    Serial.println("[Boot] ready");
}

// =============================================================================
// loop()
// =============================================================================
void loop(){
    OS::KeyEvent key;
    const uint32_t now=millis();

    if(OS::Keyboard_poll(key) && key.pressed){
        AppState s=g_app_state;

        // Global theme/night/background keys (work in most screens)
        bool globalHandled=false;
        if(s==AppState::HOME){
            if(key.key=='w'||key.key=='W'){g_cursor=nextSel(g_cursor,-1);globalHandled=true;}
            else if(key.key=='s'||key.key=='S'){g_cursor=nextSel(g_cursor,1);globalHandled=true;}
            else if(key.key=='a'||key.key=='A'){g_cursor=nextSel(g_cursor,-1);globalHandled=true;}
            else if(key.key=='d'||key.key=='D'){g_cursor=nextSel(g_cursor,1);globalHandled=true;}
            else if(key.key=='\n'||key.key=='\r'){if(!MENU[g_cursor].header)OS_SetState(MENU[g_cursor].target);globalHandled=true;}
            else if(key.key=='t'||key.key=='T'){Themes::next();drawHome();globalHandled=true;}
            else if(key.key=='b'||key.key=='B'){CyberFX::next(320,240);globalHandled=true;}
            else if(key.key=='n'||key.key=='N'){Themes::set(ThemeID::NIGHT);drawHome();globalHandled=true;}
        }
        if(globalHandled){ /* done */ }
        // HOW-TO GATE: if a how-to screen is showing, the first key starts the game.
        else if(g_howto_active){
            g_howto_active=false;
            if(key.key==27||key.key=='`'){ OS_SetState(AppState::HOME); }
            else switch(g_howto_for){
                case AppState::G_SNAKE:    Games::Snake::start(); break;
                case AppState::G_TETRIS:   Games::Tetris::start(); break;
                case AppState::G_WORDLE:   Games::Wordle::start(); break;
                case AppState::G_CHESS:    Games::Chess::start(); break;
                case AppState::G_CHECKERS: Games::Checkers::start(); break;
                case AppState::G_TRON:     Games::Tron::start(); break;
                case AppState::G_PONG:     Games::Pong::start(); break;
                case AppState::G_2048:     Games::G2048::start(); break;
                case AppState::G_LIFE:     Games::Life::start(); break;
                case AppState::G_NETRUN:   Games::Netrun::start(); break;
                default: break;
            }
        }
        else {
            switch(s){
                case AppState::SCREEN_SELECT: handleScreenSelect(key.key); break;
                case AppState::SYS_SETTINGS:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else handleSettings(key.key);
                    break;
                case AppState::SYS_FIRMWARES:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else SDLauncher::handleKey(key.key);
                    break;
                case AppState::SYS_WIFI_OTA:
                    WiFiOTA::handleKey(key.key);
                    break;
                case AppState::SYS_INFO:
                case AppState::OG_GPS: case AppState::OG_LORA: case AppState::OG_COMPASS:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    break;

                // Games / apps that self-handle, ESC exits
                case AppState::TOOL_CSI:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else if(key.key=='s'||key.key=='S'){ /* sensitivity cycle ... */ }
                    else if(key.key=='m'||key.key=='M'){ CSI::toggleMode(); }
                    break;
                case AppState::TOOL_PET:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Pet::handleKey(key.key); break;
                case AppState::G_SNAKE:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::Snake::handleKey(key.key); break;
                case AppState::G_TETRIS:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::Tetris::handleKey(key.key); break;
                case AppState::G_WORDLE:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::Wordle::handleKey(key.key); break;
                case AppState::G_CHESS:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::Chess::handleKey(key.key); break;
                case AppState::G_CHECKERS:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::Checkers::handleKey(key.key); break;
                case AppState::G_TRON:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::Tron::handleKey(key.key); break;
                case AppState::G_PONG:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::Pong::handleKey(key.key); break;
                case AppState::G_2048:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::G2048::handleKey(key.key); break;
                case AppState::G_LIFE:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::Life::handleKey(key.key); break;
                case AppState::G_NETRUN:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Games::Netrun::handleKey(key.key); break;
                case AppState::OG_SURVIVAL:
                    if((key.key==27||key.key=='`')&&OffGrid::Survival::atIndex())OS_SetState(AppState::HOME);
                    else OffGrid::Survival::handleKey(key.key); break;
                case AppState::OG_MORSE:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else OffGrid::Morse::handleKey(key.key); break;
                case AppState::OG_SOS:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else OffGrid::SOS::handleKey(key.key); break;
                case AppState::EDC_CLOCK:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else EDC::Clock::handleKey(key.key); break;
                case AppState::EDC_FLASHLIGHT:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else EDC::Flashlight::handleKey(key.key); break;
                case AppState::EDC_DICE:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else EDC::Dice::handleKey(key.key); break;
                case AppState::EDC_NOTES:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else EDC::Notes::handleKey(key.key); break;
                case AppState::EDC_CALC:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else EDC::Calc::handleKey(key.key); break;
                case AppState::EDC_CONVERT:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else EDC::Convert::handleKey(key.key); break;
                case AppState::MEDIA_MUSIC:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    else Music::handleKey(key.key); break;
                case AppState::LEARN:
                    if((key.key==27||key.key=='`')&&Learn::atIndex())OS_SetState(AppState::HOME);
                    else Learn::handleKey(key.key); break;
                case AppState::SYS_CHARGING:
                    if(key.key==27||key.key=='`')OS_SetState(AppState::HOME);
                    break;
                case AppState::TOOL_NOTES:
                    Tools::Notes::handleKey(key.key); break;
                case AppState::TOOL_FILES:
                    Tools::FileBrowser::handleKey(key.key); break;
                default: break;
            }
        }
    }

    // ── Per-frame render / tick ───────────────────────────────────────────────
    // While a how-to screen is up, skip per-frame rendering (leave the static screen).
    if(g_howto_active) return;
    switch(g_app_state){
        case AppState::HOME: renderHome(now); break;
        case AppState::TOOL_CSI:
            CSI::render_waterfall(g_lcd_secondary);
            CSI::render_hud(g_lcd_primary);
            break;
        case AppState::TOOL_PET:       Pet::tick(now); break;
        case AppState::G_SNAKE:        Games::Snake::tick(now); break;
        case AppState::G_TETRIS:       Games::Tetris::tick(now); break;
        case AppState::G_TRON:         Games::Tron::tick(now); break;
        case AppState::G_PONG:         Games::Pong::tick(now); break;
        case AppState::G_LIFE:         Games::Life::tick(now); break;
        case AppState::G_NETRUN:       Games::Netrun::tick(now); break;
        case AppState::OG_MORSE:       OffGrid::Morse::tick(now); break;
        case AppState::OG_SOS:         OffGrid::SOS::tick(now); break;
        case AppState::EDC_CLOCK:      EDC::Clock::tick(now); break;
        case AppState::EDC_FLASHLIGHT: EDC::Flashlight::tick(now); break;
        case AppState::MEDIA_MUSIC:    Music::tick(now); break;
        case AppState::SYS_CHARGING:   Charging::tick(now); break;
        default: break;
    }

    vTaskDelay(pdMS_TO_TICKS(16));
}

// =============================================================================
// OS namespace
// =============================================================================
namespace OS {

void Keyboard_begin(){ Serial.println("[KB] ready"); }

bool Keyboard_poll(KeyEvent& out){
    // Ensure Cardputer ADV keyboard state is updated (ADV BSP may require this)
    M5Cardputer.update();
    M5.update();
    if(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()){
        auto st=M5Cardputer.Keyboard.keysState();
        out.key = (!st.word.empty()) ? st.word[0] : 0;
        out.shift=st.del; out.fn=st.fn; out.pressed=true;
        if(out.key==0 && st.enter) out.key='\n';
        if(out.key==0 && st.tab)   out.key='\t';
        if(out.key==0 && st.del)   out.key=8;     // backspace
        if(out.key==0 && st.space) out.key=' ';
        // Arrow cluster -> WASD (every game/menu reads WASD; both now work)
        if(out.key==';') out.key='w';   // up
        if(out.key=='.') out.key='s';   // down
        if(out.key==',') out.key='a';   // left
        if(out.key=='/') out.key='d';   // right
        return (out.key!=0);
    }
    return false;
}

static bool _sd_ready=false;
// SD is DISABLED: it shares the SPI2 bus (MOSI=35, SCLK=40) with the internal
// display, and the two cannot coexist without breaking the internal screen.
// These are harmless stubs so the rest of the code still links.
bool SD_init(){ _sd_ready=false; return false; }
bool SD_mount(){ return false; }
void SD_unmount(){ }
bool SD_ready(){return _sd_ready;}
void SD_ensureDirectories(){
    static const char* D[]={"/purplx","/purplx/music","/purplx/games","/purplx/notes"};
    for(const char* d:D)if(!SD.exists(d))SD.mkdir(d);
}
float getBatteryVoltage(){
    if(PIN_BATTERY_ADC<0)return 3.85f;
    return (analogRead(PIN_BATTERY_ADC)/4095.0f)*3.3f*2.0f;
}
void reboot(){ESP.restart();}
void deepSleep(uint32_t ms){
    if(ms>0)esp_sleep_enable_timer_wakeup((uint64_t)ms*1000ULL);
    esp_deep_sleep_start();
}

} // namespace OS
