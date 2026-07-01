// =============================================================================
// pet.cpp — Purplx Pet (tamagotchi)
// =============================================================================
// Stats range 0-100. They decay over real time. If a stat bottoms out for too
// long the pet's health drops; health hitting zero means it runs away (resets).
// State persists to NVS with a timestamp so the pet ages while powered off.
// =============================================================================

#include "pet.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include "sprites.h"
#include <Preferences.h>
#include <time.h>

namespace Pet {

// ─── Persistent state ─────────────────────────────────────────────────────────
struct PetState {
    char     name[12];
    uint8_t  hunger;     // 100 = full, 0 = starving
    uint8_t  fun;        // 100 = happy, 0 = bored
    uint8_t  clean;      // 100 = clean, 0 = filthy
    uint8_t  energy;     // 100 = rested, 0 = exhausted
    uint8_t  health;     // 100 = healthy, 0 = gone
    uint32_t ageMin;     // age in minutes
    uint8_t  stage;      // 0=egg 1=baby 2=child 3=teen 4=adult
    bool     sleeping;
    bool     alive;
    uint8_t  species;    // 0=dino 1=spider 2=dog 3=cat
    bool     sick;       // true when neglected; recovers with care (never dies)
};
static PetState _p;
static uint32_t _lastSaveEpoch = 0;

static bool     _running=false;
static int      _action=0;       // selected action
static uint32_t _lastTick=0;
static uint32_t _animFrame=0;
static bool     _hatchingPrompt=false;

static const char* ACTIONS[] = { "FEED", "PLAY", "CLEAN", "SLEEP", "SWITCH", "HELP" };
static const int   ACTION_N = 6;

static const char* SPECIES_NAME[] = { "Dino", "Spider", "Dog", "Cat" };
static const int   SPECIES_N = 4;
static bool _showHelp = false;        // help overlay toggle
static bool _showSwitch = false;      // creature-picker overlay toggle
static int  _switchSel = 0;           // which creature highlighted in picker

static const ColorTheme* T(){ return g_theme; }

// ─── Persistence ──────────────────────────────────────────────────────────────
static uint32_t nowEpoch(){
    time_t t = time(nullptr);
    if (t < 100000) return millis()/1000;  // RTC not set; use uptime seconds
    return (uint32_t)t;
}

static void save(){
    Preferences pr; pr.begin("purplxpet", false);
    pr.putBytes("state", &_p, sizeof(_p));
    pr.putUInt("epoch", nowEpoch());
    pr.end();
}

static void applyDecay(uint32_t elapsedMin){
    if (!_p.alive || elapsedMin == 0) return;
    if (_p.stage == 0) {
        // egg just ages until hatch
        _p.ageMin += elapsedMin;
        return;
    }

    // decay rates per minute (slow enough to be manageable)
    auto dec=[&](uint8_t& v,float perMin){
        float d = perMin*elapsedMin;
        if (d >= v) v=0; else v -= (uint8_t)d;
    };
    if (_p.sleeping) {
        // sleeping restores energy, slows other decay
        float e = 0.5f*elapsedMin;
        _p.energy = (_p.energy + (uint8_t)e > 100) ? 100 : _p.energy+(uint8_t)e;
        dec(_p.hunger, 0.05f);
        dec(_p.fun,    0.03f);
    } else {
        dec(_p.hunger, 0.10f);
        dec(_p.fun,    0.08f);
        dec(_p.clean,  0.06f);
        dec(_p.energy, 0.07f);
    }

    // health logic: suffer if any need is at zero
    int suffering = 0;
    if (_p.hunger==0) suffering++;
    if (_p.energy==0) suffering++;
    if (_p.clean==0)  suffering++;
    if (suffering>0) {
        float hd = suffering*0.08f*elapsedMin;
        if (hd>=_p.health) _p.health=0; else _p.health-=(uint8_t)hd;
    } else if (_p.health<100) {
        // recover slowly when all needs met
        float hr=0.04f*elapsedMin;
        _p.health = (_p.health+(uint8_t)hr>100)?100:_p.health+(uint8_t)hr;
    }

    _p.ageMin += elapsedMin;

    // growth stages by age
    uint8_t newStage=_p.stage;
    if      (_p.ageMin <   5) newStage=1;     // baby
    else if (_p.ageMin <  60) newStage=2;     // child
    else if (_p.ageMin < 240) newStage=3;     // teen
    else                      newStage=4;     // adult
    if (newStage>_p.stage) _p.stage=newStage;

    // Neglect makes the pet SICK (recoverable) rather than killing it.
    _p.sick = (_p.health < 25);
    _p.alive = true;   // never dies in this build

    // auto-wake if fully rested
    if (_p.sleeping && _p.energy>=100) _p.sleeping=false;
}

void boot_load(){
    Preferences pr; pr.begin("purplxpet", true);
    bool has = pr.isKey("state");
    if (has) {
        pr.getBytes("state", &_p, sizeof(_p));
        uint32_t savedEpoch = pr.getUInt("epoch", 0);
        pr.end();
        uint32_t now = nowEpoch();
        if (now > savedEpoch && savedEpoch>0) {
            uint32_t elapsedMin = (now - savedEpoch)/60;
            if (elapsedMin > 0) applyDecay(elapsedMin);
        }
    } else {
        pr.end();
        // fresh egg (default Dino)
        strncpy(_p.name,"Purp",sizeof(_p.name));
        _p.hunger=_p.fun=_p.clean=_p.energy=_p.health=100;
        _p.ageMin=0; _p.stage=0; _p.sleeping=false; _p.alive=true;
        _p.species=0; _p.sick=false;
        save();
    }
}

// Start a brand-new egg of the given species (used when switching creatures).
static void freshEgg(uint8_t species){
    strncpy(_p.name, SPECIES_NAME[species%SPECIES_N], sizeof(_p.name));
    _p.name[sizeof(_p.name)-1]=0;
    _p.hunger=_p.fun=_p.clean=_p.energy=_p.health=100;
    _p.ageMin=0; _p.stage=0; _p.sleeping=false; _p.alive=true;
    _p.species=species%SPECIES_N; _p.sick=false;
    save();
}

// =============================================================================
// Theme-colored sprite drawing
// =============================================================================
// Map a shade index (1=outline..4=light) to a color derived from the theme.
static uint16_t shadeColor(uint8_t shade){
    // Pull RGB from theme primary, scale brightness per shade.
    uint16_t p = T()->primary;
    uint8_t r = ((p>>11)&0x1F)<<3;
    uint8_t g = ((p>>5)&0x3F)<<2;
    uint8_t b = (p&0x1F)<<3;
    float mul;
    switch(shade){
        case 1: mul=0.25f; break;   // outline (dark)
        case 2: mul=0.55f; break;   // dark
        case 3: mul=1.00f; break;   // mid (full primary)
        case 4: mul=1.35f; break;   // light (brightened)
        default: return T()->bg;
    }
    int rr=(int)(r*mul), gg=(int)(g*mul), bb=(int)(b*mul);
    if(rr>255)rr=255; if(gg>255)gg=255; if(bb>255)bb=255;
    return RGB565(rr,gg,bb);
}

// Draw a sprite (shade-index grid) centered at cx,cy, scaled, on the big screen.
// sickTint=true shifts toward sickly green.
static void drawSprite(const uint8_t* spr,uint8_t w,uint8_t h,int cx,int cy,int scale,bool sickTint){
    int ox=cx-(w*scale)/2, oy=cy-(h*scale)/2;
    for(int y=0;y<h;y++){
        for(int x=0;x<w;x++){
            uint8_t shade=spr[y*w+x];
            if(shade==0)continue;          // transparent
            uint16_t col=shadeColor(shade);
            if(sickTint){
                // blend toward green when sick
                uint8_t r=((col>>11)&0x1F)<<3, g=((col>>5)&0x3F)<<2, b=(col&0x1F)<<3;
                r=r/2; g=(g+200)/2; b=b/2;
                col=RGB565(r,g,b);
            }
            g_lcd_secondary.fillRect(ox+x*scale,oy+y*scale,scale,scale,col);
        }
    }
}

// =============================================================================
// Pet creature drawing — simple animated pixel blob that changes by stage/mood
// =============================================================================
static void drawCreature(int cx,int cy){
    uint16_t body = T()->primary;
    uint16_t eye  = TFT_WHITE;
    bool blink = (_animFrame % 40) < 3;
    int bob = ((_animFrame/8)%2==0) ? 0 : 2;
    cy += bob;

    // egg stage (same for all species until hatch)
    if (_p.stage==0){
        g_lcd_secondary.fillEllipse(cx,cy,26,32, RGB565(220,220,180));
        g_lcd_secondary.drawEllipse(cx,cy,26,32, body);
        for(int i=-2;i<3;i++)
            g_lcd_secondary.drawLine(cx+i*10,cy, cx+i*10+5,cy+8, body);
        // little species hint label under egg
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(T()->text_dim,T()->bg);
        const char* n=SPECIES_NAME[_p.species%SPECIES_N];
        g_lcd_secondary.setCursor(cx-(int)(strlen(n)*3),cy+40);
        g_lcd_secondary.print(n);
        return;
    }

    bool sleeping = _p.sleeping;
    int s = _p.stage;                 // 1..4

    // scale the sprite by life stage (baby small -> adult big)
    int scale = 3 + s;                // stage1=4x ... stage4=7x  (16px sprite -> 64..112px)

    // pick this species' sprite
    int sp = _p.species % SPECIES_N;
    const uint8_t* spr = SPECIES_SPR[sp];
    uint8_t sw = SPECIES_SPR_W[sp];
    uint8_t sh = SPECIES_SPR_H[sp];

    if (sleeping){
        // draw dimmer + Zzz handled below; still show the creature resting
        drawSprite(spr, sw, sh, cx, cy, scale, _p.sick);
    } else {
        drawSprite(spr, sw, sh, cx, cy, scale, _p.sick);
    }

    int r = sh*scale/2;   // approx radius for overlay positioning below

    // Zzz when sleeping
    if (sleeping){
        g_lcd_secondary.setTextColor(T()->secondary,T()->bg);
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setCursor(cx+r+6,cy-r-4);
        g_lcd_secondary.print("z");
    }
    // sick indicator
    if (_p.sick && !sleeping){
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(T()->alert,T()->bg);
        g_lcd_secondary.setCursor(cx-12,cy-r-14);
        g_lcd_secondary.print("* sick *");
    }
    // dirt if filthy
    if (_p.clean<30 && !sleeping){
        for(int i=0;i<3;i++){
            int dx=cx + (int)(r*1.5f)*((i%2)?1:-1);
            int dy=cy - r + i*10 + (_animFrame/6+i)%6;
            g_lcd_secondary.fillCircle(dx,dy,2,RGB565(120,90,40));
        }
    }
}

// =============================================================================
static void statBar(int x,int y,const char* label,uint8_t val,uint16_t col){
    g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(x,y);
    g_lcd_primary.print(label);
    int bx=x+34, bw=70, bh=7;
    g_lcd_primary.drawRect(bx,y,bw,bh,T()->border);
    int fw=(bw-2)*val/100;
    uint16_t c = (val<25)?T()->alert:col;
    g_lcd_primary.fillRect(bx+1,y+1,fw,bh-2,c);
}

static void drawHUD(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setCursor(4,4);
    g_lcd_primary.printf("%s", _p.name);
    const char* stages[]={"Egg","Baby","Child","Teen","Adult"};
    g_lcd_primary.setTextColor(T()->secondary,T()->hud_bg);
    g_lcd_primary.setCursor(70,4);
    g_lcd_primary.printf("%s  %lum", stages[_p.stage], (unsigned long)_p.ageMin);

    if (_p.alive && _p.stage>0){
        statBar(4,18,"Food", _p.hunger, T()->success);
        statBar(4,30,"Fun ", _p.fun,    RGB565(255,180,0));
        statBar(4,42,"Wash", _p.clean,  RGB565(0,180,255));
        statBar(4,54,"Rest", _p.energy, RGB565(180,120,255));
        statBar(4,66,"HP  ", _p.health, T()->alert);
    } else if (_p.stage==0){
        g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
        g_lcd_primary.setCursor(4,30);
        g_lcd_primary.print("An egg! Keep it");
        g_lcd_primary.setCursor(4,42);
        g_lcd_primary.print("warm... it hatches");
        g_lcd_primary.setCursor(4,54);
        g_lcd_primary.print("as it ages.");
    }

    // action bar
    g_lcd_primary.fillRect(0,82,240,14,T()->title_bg);
    int ax=4;
    for(int i=0;i<ACTION_N;i++){
        bool sel=(i==_action);
        if(sel){g_lcd_primary.setTextColor(T()->highlight_text,T()->highlight_bg);
                int w=strlen(ACTIONS[i])*6+4;
                g_lcd_primary.fillRect(ax-2,82,w,14,T()->highlight_bg);}
        else g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
        g_lcd_primary.setCursor(ax,85);
        g_lcd_primary.print(ACTIONS[i]);
        ax += strlen(ACTIONS[i])*6+8;
    }
}

static void drawScene(){
    g_lcd_secondary.fillScreen(T()->bg);
    // title
    g_lcd_secondary.fillRect(0,0,320,22,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,7);
    g_lcd_secondary.print("MY PET");
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(250,7);
    g_lcd_secondary.print(_p.sleeping?"sleeping":"awake");
    // ground
    g_lcd_secondary.fillRect(0,200,320,40,T()->title_bg);
    g_lcd_secondary.drawFastHLine(0,200,320,T()->border);
    // footer
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(6,224);
    g_lcd_secondary.print("A/D action  ENTER do it  ESC home");

    drawCreature(160,120);
}

// Brief action feedback bubble
static void bubble(const char* msg){
    g_lcd_secondary.fillRoundRect(90,40,140,26,4,T()->highlight_bg);
    g_lcd_secondary.drawRoundRect(90,40,140,26,4,T()->primary);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->highlight_text,T()->highlight_bg);
    int w=strlen(msg)*6;
    g_lcd_secondary.setCursor(160-w/2,50);
    g_lcd_secondary.print(msg);
}

static void drawSwitch();   // fwd
static void drawHelp();     // fwd

static void doAction(int a){
    switch(a){
        case 0: // feed
            _p.hunger=(_p.hunger+30>100)?100:_p.hunger+30;
            if(_p.sick) _p.health=(_p.health+8>100)?100:_p.health+8; // food helps heal
            bubble("Yum! *munch*"); break;
        case 1: // play
            if(_p.sleeping){ bubble("Shh, sleeping..."); break; }
            if(_p.energy<15){ bubble("Too tired to play"); break; }
            _p.fun=(_p.fun+30>100)?100:_p.fun+30;
            _p.energy=(_p.energy<10)?0:_p.energy-10;
            bubble("Wheee! :D"); break;
        case 2: // clean
            _p.clean=100;
            if(_p.sick) _p.health=(_p.health+10>100)?100:_p.health+10; // hygiene heals
            bubble("All clean! *sparkle*"); break;
        case 3: // sleep toggle (also restores health a bit)
            _p.sleeping=!_p.sleeping;
            if(_p.sick) _p.health=(_p.health+6>100)?100:_p.health+6;
            bubble(_p.sleeping?"Goodnight...":"Good morning!"); break;
        case 4: // SWITCH creature -> open picker overlay
            _showSwitch=true; _switchSel=_p.species; drawSwitch(); return;
        case 5: // HELP -> open instructions overlay
            _showHelp=true; drawHelp(); return;
    }
    // recovered?
    if(_p.sick && _p.health>=25) _p.sick=false;
    save();
    delay(600);
    drawScene();
    drawHUD();
}

// Creature picker overlay (big screen)
// Draw a small recognizable creature for the picker tiles.
static void drawMiniCreature(int sp, int cx, int cy, uint16_t body){
    uint16_t eye=TFT_WHITE;
    switch(sp % SPECIES_N){
      case 0: { // DINO
        g_lcd_secondary.fillCircle(cx,cy,14,body);
        g_lcd_secondary.fillTriangle(cx-14,cy+2,cx-24,cy+5,cx-14,cy+12,body); // tail
        g_lcd_secondary.fillCircle(cx+12,cy-7,8,body);                        // head
        for(int i=-1;i<2;i++)                                                 // spikes
            g_lcd_secondary.fillTriangle(cx+i*6,cy-14,cx+i*6-3,cy-9,cx+i*6+3,cy-9,T()->secondary);
        g_lcd_secondary.fillCircle(cx+14,cy-7,2,eye);
      } break;
      case 1: { // SPIDER
        for(int i=0;i<4;i++){
            int ly=cy-7+i*5;
            g_lcd_secondary.drawLine(cx-3,cy,cx-18,ly,body);
            g_lcd_secondary.drawLine(cx+3,cy,cx+18,ly,body);
        }
        g_lcd_secondary.fillCircle(cx,cy,12,body);
        g_lcd_secondary.fillCircle(cx,cy-11,6,body);
        g_lcd_secondary.fillCircle(cx-3,cy-12,1,eye);
        g_lcd_secondary.fillCircle(cx+3,cy-12,1,eye);
      } break;
      case 2: { // DOG
        g_lcd_secondary.fillCircle(cx,cy,14,body);
        g_lcd_secondary.fillEllipse(cx-14,cy-6,4,10,body);  // ears
        g_lcd_secondary.fillEllipse(cx+14,cy-6,4,10,body);
        g_lcd_secondary.fillCircle(cx-5,cy-3,3,eye); g_lcd_secondary.fillCircle(cx+5,cy-3,3,eye);
        g_lcd_secondary.fillCircle(cx-5,cy-3,1,TFT_BLACK); g_lcd_secondary.fillCircle(cx+5,cy-3,1,TFT_BLACK);
        g_lcd_secondary.fillCircle(cx,cy+4,4,RGB565(60,40,30)); // snout
      } break;
      default: { // CAT
        g_lcd_secondary.fillCircle(cx,cy,14,body);
        g_lcd_secondary.fillTriangle(cx-12,cy-10,cx-18,cy-20,cx-3,cy-13,body); // ears
        g_lcd_secondary.fillTriangle(cx+12,cy-10,cx+18,cy-20,cx+3,cy-13,body);
        g_lcd_secondary.fillCircle(cx-5,cy-3,3,eye); g_lcd_secondary.fillCircle(cx+5,cy-3,3,eye);
        g_lcd_secondary.fillCircle(cx-5,cy-3,1,TFT_BLACK); g_lcd_secondary.fillCircle(cx+5,cy-3,1,TFT_BLACK);
        g_lcd_secondary.fillCircle(cx,cy+3,2,RGB565(255,150,180)); // nose
        g_lcd_secondary.drawLine(cx-3,cy+3,cx-14,cy+1,T()->text_dim); // whiskers
        g_lcd_secondary.drawLine(cx+3,cy+3,cx+14,cy+1,T()->text_dim);
      } break;
    }
}

static void drawSwitch(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,22,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,7); g_lcd_secondary.print("PICK A CREATURE");
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(150,7); g_lcd_secondary.print("A/D pick ENTER hatch");
    // four choice tiles
    const char* glyph[]={"DINO","SPIDER","DOG","CAT"};
    for(int i=0;i<SPECIES_N;i++){
        int tx=12+i*76, ty=70, tw=70, th=90;
        bool sel=(i==_switchSel);
        uint16_t bg=sel?T()->highlight_bg:T()->title_bg;
        g_lcd_secondary.fillRoundRect(tx,ty,tw,th,6,bg);
        g_lcd_secondary.drawRoundRect(tx,ty,tw,th,6,sel?T()->primary:T()->border);
        // mini preview: draw the actual creature shape
        // draw the actual sprite, small (scale 2)
        drawSprite(SPECIES_SPR[i], SPECIES_SPR_W[i], SPECIES_SPR_H[i], tx+tw/2, ty+34, 2, false);
        g_lcd_secondary.setTextColor(sel?T()->highlight_text:T()->text,bg);
        int w=strlen(glyph[i])*6;
        g_lcd_secondary.setCursor(tx+tw/2-w/2,ty+68);
        g_lcd_secondary.print(glyph[i]);
    }
    g_lcd_secondary.setTextColor(T()->alert,T()->bg);
    g_lcd_secondary.setCursor(40,190);
    g_lcd_secondary.print("Note: switching starts a NEW egg!");
    g_lcd_secondary.setTextColor(T()->text_dim,T()->bg);
    g_lcd_secondary.setCursor(70,210);
    g_lcd_secondary.print("ESC to cancel");
}

// How-to-play overlay (big screen)
static void drawHelp(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,22,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,7); g_lcd_secondary.print("HOW TO PLAY");
    int y=32;
    auto line=[&](const char* a,const char* b){
        g_lcd_secondary.setTextColor(T()->secondary,T()->bg);
        g_lcd_secondary.setCursor(10,y); g_lcd_secondary.print(a);
        g_lcd_secondary.setTextColor(T()->text,T()->bg);
        g_lcd_secondary.setCursor(78,y); g_lcd_secondary.print(b);
        y+=16;
    };
    line("FEED","fills Food. Do it when hungry.");
    line("PLAY","fills Fun. Costs energy.");
    line("CLEAN","fills Wash. Keeps pet healthy.");
    line("SLEEP","toggles rest. Restores energy.");
    line("SWITCH","pick a new creature (new egg).");
    y+=4;
    g_lcd_secondary.setTextColor(T()->alert,T()->bg);
    g_lcd_secondary.setCursor(10,y); g_lcd_secondary.print("If you neglect it, it gets SICK"); y+=14;
    g_lcd_secondary.setCursor(10,y); g_lcd_secondary.print("(green + 'sick'). Feed/clean/rest"); y+=14;
    g_lcd_secondary.setCursor(10,y); g_lcd_secondary.print("to heal it. It never dies!"); y+=18;
    g_lcd_secondary.setTextColor(T()->text_dim,T()->bg);
    g_lcd_secondary.setCursor(10,y);
    g_lcd_secondary.print("Stats drop over time, even while off.");
    y+=14;
    g_lcd_secondary.setTextColor(T()->success,T()->bg);
    g_lcd_secondary.setCursor(70,222);
    g_lcd_secondary.print("Press ENTER or ESC to go back");
}

// =============================================================================
void start(){
    _running=true;
    _action=0;
    _showHelp=false; _showSwitch=false;
    boot_load();   // ensure fresh decay applied
    drawScene();
    drawHUD();
}

void handleKey(char key){
    // HELP overlay open: any of ENTER/ESC/backtick closes it
    if(_showHelp){
        if(key=='\n'||key=='\r'||key==27||key=='`'){ _showHelp=false; drawScene(); drawHUD(); }
        return;
    }
    // SWITCH picker open: A/D choose, ENTER hatches new egg, ESC cancels
    if(_showSwitch){
        if(key=='a'||key=='A'){ if(_switchSel>0)_switchSel--; drawSwitch(); }
        else if(key=='d'||key=='D'){ if(_switchSel<SPECIES_N-1)_switchSel++; drawSwitch(); }
        else if(key=='\n'||key=='\r'){ freshEgg(_switchSel); _showSwitch=false; drawScene(); drawHUD(); }
        else if(key==27||key=='`'){ _showSwitch=false; drawScene(); drawHUD(); }
        return;
    }
    // normal action bar
    if(key=='a'||key=='A'){ if(_action>0)_action--; drawHUD(); }
    else if(key=='d'||key=='D'){ if(_action<ACTION_N-1)_action++; drawHUD(); }
    else if(key=='\n'||key=='\r'){ doAction(_action); }
}

void tick(uint32_t now){
    if(!_running)return;
    if(_showHelp||_showSwitch) return;   // overlay open: leave it on screen
    _animFrame++;
    // redraw creature ~12fps for animation
    static uint32_t lastAnim=0;
    if(now-lastAnim>80){
        lastAnim=now;
        // only redraw the creature area to avoid flicker
        g_lcd_secondary.fillRect(60,30,200,168,T()->bg);
        drawCreature(160,120);
    }
    // apply 1 min of decay every real minute of play
    static uint32_t lastMin=0;
    if(now-lastMin>60000){
        lastMin=now;
        applyDecay(1);
        save();
        drawHUD();
    }
}

bool isRunning(){return _running;}
void stop(){ save(); _running=false; }

} // namespace Pet
