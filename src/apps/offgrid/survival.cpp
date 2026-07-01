// =============================================================================
// survival.cpp — Offline Survival Reference
// =============================================================================
#include "survival.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"

namespace OffGrid { namespace Survival {

struct Topic { const char* title; const char* sub; const char* const* lines; int n; };

// ── First Aid ────────────────────────────────────────────────────────────────
static const char* A1[] = {
    "SEVERE BLEEDING:",
    "1. Apply firm direct pressure with",
    "   cloth for 10-15 min. Don't peek.",
    "2. If soaked, add more cloth ON TOP -",
    "   never remove the first layer.",
    "3. Elevate the wound above the heart.",
    "4. Tourniquet only as last resort, 5-8cm",
    "   above wound, note the time applied.",
    "",
    "SHOCK (pale, cold, rapid pulse):",
    "- Lay flat, raise legs ~30cm",
    "- Keep warm, reassure, don't give food",
    "",
    "BURNS:",
    "- Cool with running water 20 min",
    "- Don't pop blisters, cover loosely",
    "- Never use ice or butter",
    "",
    "CPR (no pulse, not breathing):",
    "- 30 chest compressions (fast, 5cm deep)",
    "- 2 rescue breaths, repeat",
    "- Push to the beat of 'Stayin Alive'",
    "",
    "CHOKING:",
    "- 5 back blows between shoulder blades",
    "- 5 abdominal thrusts (Heimlich)",
    "- Repeat until clear",
};

// ── Water ────────────────────────────────────────────────────────────────────
static const char* A2[] = {
    "FINDING WATER:",
    "- Follow valleys downhill, animal tracks,",
    "  and green vegetation.",
    "- Morning dew: drag cloth on grass, wring.",
    "- Rainwater is usually safe to drink.",
    "",
    "PURIFYING (assume all wild water is unsafe):",
    "1. BOIL - rolling boil 1 min (3 min at",
    "   high altitude). Most reliable method.",
    "2. FILTER - cloth/sand/charcoal removes",
    "   debris but NOT germs - boil after.",
    "3. SOLAR - clear bottle, 6 hrs full sun",
    "   (SODIS method) kills most pathogens.",
    "",
    "DO NOT DRINK:",
    "- Saltwater (worsens dehydration)",
    "- Urine, blood, alcohol",
    "- Cloudy/foaming/strong-smelling water",
    "",
    "SIGNS OF DEHYDRATION:",
    "- Dark urine, headache, dizziness",
    "- Ration sweat, not water: rest in shade",
    "  during heat, travel at dawn/dusk.",
};

// ── Fire ─────────────────────────────────────────────────────────────────────
static const char* A3[] = {
    "FIRE NEEDS 3 THINGS:",
    "Heat + Fuel + Oxygen. Remove any one",
    "and the fire dies.",
    "",
    "BUILD ORDER (small to big):",
    "1. TINDER - dry grass, bark, cotton,",
    "   birch bark, char cloth. Catches spark.",
    "2. KINDLING - pencil-thick dry twigs.",
    "3. FUEL - thumb-thick then wrist-thick.",
    "",
    "STRUCTURE:",
    "- Teepee: cone of kindling over tinder.",
    "- Log cabin: stack fuel in a square.",
    "- Keep a gap for airflow.",
    "",
    "IGNITION WITHOUT MATCHES:",
    "- Ferro rod: scrape hard onto tinder.",
    "- Lens: magnify sun onto tinder.",
    "- Battery + steel wool: touch terminals.",
    "",
    "KEEP IT GOING:",
    "- Add fuel gradually, don't smother.",
    "- Bank coals with ash overnight to",
    "  relight in the morning.",
};

// ── Knots ────────────────────────────────────────────────────────────────────
static const char* A4[] = {
    "ESSENTIAL KNOTS:",
    "",
    "BOWLINE - fixed loop that won't slip.",
    "'Rabbit comes out of the hole, around",
    "the tree, back down the hole.' For",
    "rescue loops, securing to anchors.",
    "",
    "SQUARE (REEF) KNOT - joins two ropes.",
    "Right over left, then left over right.",
    "For bandages, bundling - not for loads.",
    "",
    "CLOVE HITCH - quick anchor to a post.",
    "Two loops around, tuck the end under.",
    "For starting lashings, tarp lines.",
    "",
    "TAUT-LINE HITCH - adjustable tension.",
    "Slides to tighten, grips under load.",
    "For tent guy lines, ridge lines.",
    "",
    "FIGURE-8 - stopper, won't jam.",
    "For rope ends, climbing.",
    "",
    "TWO HALF HITCHES - secure to ring/tree.",
    "Wrap around, then two hitches. General",
    "purpose tie-off.",
};

// ── Signaling / Rescue ───────────────────────────────────────────────────────
static const char* A5[] = {
    "ATTRACTING RESCUE:",
    "",
    "RULE OF THREE = distress signal.",
    "3 fires in a triangle, 3 whistle blasts,",
    "3 flashes. Repeat. Pause. Repeat.",
    "",
    "SIGNAL MIRROR:",
    "- Flash toward aircraft/searchers.",
    "- Visible for miles. Aim via V of fingers.",
    "",
    "GROUND-TO-AIR SYMBOLS (stamp big in snow,",
    "rocks, or logs - bigger is better):",
    "  V  = Need assistance",
    "  X  = Need medical help",
    "  ->  = Going this direction",
    "  I  = Serious injury",
    "",
    "WHISTLE > SHOUTING:",
    "Carries farther, saves energy.",
    "3 blasts = help. 1 = where are you.",
    "",
    "STAY OR GO?:",
    "- Usually STAY PUT if lost - easier to find",
    "  a stationary target. Make yourself big",
    "  and visible. Conserve energy.",
};

// ── Navigation ───────────────────────────────────────────────────────────────
static const char* A6[] = {
    "NAVIGATION WITHOUT GPS:",
    "",
    "SUN:",
    "- Rises east, sets west (roughly).",
    "- At noon it's due SOUTH (N. hemisphere),",
    "  due NORTH (S. hemisphere).",
    "",
    "SHADOW-STICK METHOD:",
    "1. Stick upright, mark shadow tip.",
    "2. Wait 15 min, mark new tip.",
    "3. First mark = WEST, second = EAST.",
    "",
    "STARS (N. hemisphere):",
    "- Find Big Dipper, follow the two end",
    "  stars to Polaris (North Star) = NORTH.",
    "",
    "STARS (S. hemisphere):",
    "- Southern Cross points toward SOUTH.",
    "",
    "ANALOG WATCH TRICK:",
    "- Point hour hand at sun. Halfway between",
    "  hand and 12 = SOUTH (N. hemisphere).",
    "",
    "WITHOUT TOOLS:",
    "- Moss is NOT a reliable compass.",
    "- Follow water downhill to find people.",
};

static const Topic TOPICS[] = {
    {"First Aid",     "bleeding, CPR, burns",   A1, sizeof(A1)/sizeof(A1[0])},
    {"Water",         "find & purify",          A2, sizeof(A2)/sizeof(A2[0])},
    {"Fire",          "build & sustain",        A3, sizeof(A3)/sizeof(A3[0])},
    {"Knots",         "6 essential knots",      A4, sizeof(A4)/sizeof(A4[0])},
    {"Signaling",     "attract rescue",         A5, sizeof(A5)/sizeof(A5[0])},
    {"Navigation",    "without GPS",            A6, sizeof(A6)/sizeof(A6[0])},
};
static const int TOPIC_COUNT = sizeof(TOPICS)/sizeof(TOPICS[0]);

// ─── State ────────────────────────────────────────────────────────────────────
static bool _running=false, _reading=false;
static int  _sel=0, _scroll=0;
static const int VIS=13;

static const ColorTheme* T(){ return g_theme; }

static void drawIndex(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,22,T()->title_bg);
    g_lcd_secondary.drawFastHLine(0,21,320,T()->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,7);
    g_lcd_secondary.print("SURVIVAL  //  off-grid guide");

    for(int i=0;i<TOPIC_COUNT;i++){
        int y=28+i*30;
        bool sel=(i==_sel);
        if(sel){g_lcd_secondary.fillRect(0,y,320,28,T()->highlight_bg);
                g_lcd_secondary.setTextColor(T()->highlight_text,T()->highlight_bg);}
        else   {g_lcd_secondary.fillRect(0,y,320,28,(i%2)?T()->row_b:T()->row_a);
                g_lcd_secondary.setTextColor(T()->text,(i%2)?T()->row_b:T()->row_a);}
        g_lcd_secondary.setCursor(8,y+4);
        g_lcd_secondary.printf("%d. %s",i+1,TOPICS[i].title);
        g_lcd_secondary.setTextColor(sel?T()->highlight_text:T()->text_dim,
                                     sel?T()->highlight_bg:((i%2)?T()->row_b:T()->row_a));
        g_lcd_secondary.setCursor(20,y+15);
        g_lcd_secondary.print(TOPICS[i].sub);
    }
    g_lcd_secondary.fillRect(0,222,320,18,T()->title_bg);
    g_lcd_secondary.drawFastHLine(0,222,320,T()->border);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(6,227);
    g_lcd_secondary.print("[W/S] pick  [ENTER] read  [ESC] back");
}

static void drawReader(){
    const Topic& tp=TOPICS[_sel];
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,22,T()->title_bg);
    g_lcd_secondary.drawFastHLine(0,21,320,T()->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,7);
    g_lcd_secondary.print(tp.title);
    char pos[12]; snprintf(pos,sizeof(pos),"%d/%d",_scroll+1,tp.n);
    g_lcd_secondary.setTextColor(T()->secondary,T()->title_bg);
    g_lcd_secondary.setCursor(318-g_lcd_secondary.textWidth(pos),7);
    g_lcd_secondary.print(pos);

    int y=28;
    for(int i=0;i<VIS;i++){
        int li=_scroll+i;
        if(li>=tp.n)break;
        const char* line=tp.lines[li];
        int len=strlen(line);
        bool hdr=(len>2 && line[len-1]==':');
        g_lcd_secondary.setTextColor(hdr?T()->secondary:T()->text,T()->bg);
        g_lcd_secondary.setCursor(8,y);
        g_lcd_secondary.print(line);
        y+=14;
    }
    g_lcd_secondary.fillRect(0,222,320,18,T()->title_bg);
    g_lcd_secondary.drawFastHLine(0,222,320,T()->border);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(6,227);
    g_lcd_secondary.print("[W/S] scroll   [ESC] back to list");
}

static void drawHUD(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2); g_lcd_primary.setCursor(4,6);
    g_lcd_primary.print("SURVIVE");
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,32);
    if(_reading){
        char nm[28];strncpy(nm,TOPICS[_sel].title,27);nm[27]=0;
        g_lcd_primary.print(nm);
    } else {
        g_lcd_primary.printf("%d topics",TOPIC_COUNT);
    }
    g_lcd_primary.setCursor(4,52);
    g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.print("Works with no signal");
    g_lcd_primary.setCursor(4,64);
    g_lcd_primary.print("No SD card needed");
    g_lcd_primary.setCursor(4,84);
    g_lcd_primary.setTextColor(T()->alert,T()->hud_bg);
    g_lcd_primary.print("Real emergency? Call 911");
}

void start(){
    _running=true;_reading=false;_sel=0;_scroll=0;
    drawIndex(); drawHUD();
}

void handleKey(char key){
    if(!_reading){
        if(key=='w'||key=='W'){if(_sel>0)_sel--;drawIndex();}
        else if(key=='s'||key=='S'){if(_sel<TOPIC_COUNT-1)_sel++;drawIndex();}
        else if(key=='\n'||key=='\r'){_reading=true;_scroll=0;drawReader();drawHUD();}
    } else {
        const Topic& tp=TOPICS[_sel];
        int maxS=(tp.n>VIS)?(tp.n-VIS):0;
        if(key=='s'||key=='S'){if(_scroll<maxS)_scroll++;drawReader();}
        else if(key=='w'||key=='W'){if(_scroll>0)_scroll--;drawReader();}
        else if(key==27||key=='`'){_reading=false;drawIndex();drawHUD();}
    }
}

bool isRunning(){ return _running; }
bool atIndex(){ return !_reading; }
void stop(){ _running=false;_reading=false; }

} } // namespace OffGrid::Survival
