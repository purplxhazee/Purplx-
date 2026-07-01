// =============================================================================
// game2048.cpp — 2048
// =============================================================================
#include "game2048.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <Preferences.h>

namespace Games { namespace G2048 {

static int _g[4][4];
static int _score, _best;
static bool _running=false, _over=false, _won=false;

static const ColorTheme* T(){ return g_theme; }

// External screen is 320x240 (rotated). Board must fit in the HEIGHT (240),
// below the 24px title bar. 4*CELL + 3*GAP must be <= ~210.
static const int CELL=44, GAP=6;
static const int BOARD=(4*CELL+3*GAP);              // 194
static const int OX=(320-BOARD)/2;                  // center horizontally
static const int OY=24+((240-24-BOARD)/2);          // center in area below title

static uint16_t tileColor(int v){
    switch(v){
        case 2:    return RGB565(238,228,218);
        case 4:    return RGB565(237,224,200);
        case 8:    return RGB565(242,177,121);
        case 16:   return RGB565(245,149,99);
        case 32:   return RGB565(246,124,95);
        case 64:   return RGB565(246,94,59);
        case 128:  return RGB565(237,207,114);
        case 256:  return RGB565(237,204,97);
        case 512:  return RGB565(237,200,80);
        case 1024: return RGB565(237,197,63);
        case 2048: return RGB565(237,194,46);
        default:   return v>2048?RGB565(60,58,50):RGB565(205,193,180);
    }
}

static void loadBest(){Preferences p;p.begin("purplx",true);_best=p.getInt("g2048_best",0);p.end();}
static void saveBest(){Preferences p;p.begin("purplx",false);p.putInt("g2048_best",_best);p.end();}

static void addTile(){
    int empties[16][2],n=0;
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)if(_g[r][c]==0){empties[n][0]=r;empties[n][1]=c;n++;}
    if(n==0)return;
    int idx=random(0,n);
    _g[empties[idx][0]][empties[idx][1]] = (random(0,10)<9)?2:4;
}

static void drawTile(int r,int c){
    int x=OX+c*(CELL+GAP), y=OY+r*(CELL+GAP);
    int v=_g[r][c];
    g_lcd_secondary.fillRoundRect(x,y,CELL,CELL,5,tileColor(v));
    if(v>0){
        g_lcd_secondary.setTextColor(v<=4?RGB565(110,100,90):TFT_WHITE);
        int sz=(v<100)?3:(v<1000)?2:2;
        g_lcd_secondary.setTextSize(sz);
        char buf[6];snprintf(buf,sizeof(buf),"%d",v);
        int tw=strlen(buf)*6*sz;
        g_lcd_secondary.setCursor(x+CELL/2-tw/2, y+CELL/2-4*sz);
        g_lcd_secondary.print(buf);
    }
}

static void drawAll(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8); g_lcd_secondary.print("2048  //  WASD to slide");
    for(int r=0;r<4;r++)for(int c=0;c<4;c++)drawTile(r,c);
}

static void drawHUD(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("2048");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,32);g_lcd_primary.printf("SCORE %d",_score);
    g_lcd_primary.setCursor(4,46);g_lcd_primary.setTextColor(T()->secondary,T()->hud_bg);
    g_lcd_primary.printf("BEST  %d",_best);
    g_lcd_primary.setCursor(4,70);g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.print("WASD slide tiles");
    g_lcd_primary.setCursor(4,82);g_lcd_primary.print("Combine to 2048!");
}

// slide+merge one row toward index 0; returns true if changed
static bool slideRow(int* row){
    bool changed=false;
    int tmp[4],n=0;
    for(int i=0;i<4;i++)if(row[i])tmp[n++]=row[i];
    for(int i=n;i<4;i++)tmp[i]=0;
    for(int i=0;i<3;i++){
        if(tmp[i]&&tmp[i]==tmp[i+1]){
            tmp[i]*=2; _score+=tmp[i];
            if(tmp[i]==2048)_won=true;
            for(int j=i+1;j<3;j++)tmp[j]=tmp[j+1];
            tmp[3]=0;
        }
    }
    for(int i=0;i<4;i++){ if(row[i]!=tmp[i])changed=true; row[i]=tmp[i]; }
    return changed;
}

static bool move(int dir){ // 0=left 1=right 2=up 3=down
    bool changed=false;
    if(dir==0||dir==1){
        for(int r=0;r<4;r++){
            int row[4];
            for(int c=0;c<4;c++)row[c]=(dir==0)?_g[r][c]:_g[r][3-c];
            if(slideRow(row))changed=true;
            for(int c=0;c<4;c++){ if(dir==0)_g[r][c]=row[c]; else _g[r][3-c]=row[c]; }
        }
    } else {
        for(int c=0;c<4;c++){
            int row[4];
            for(int r=0;r<4;r++)row[r]=(dir==2)?_g[r][c]:_g[3-r][c];
            if(slideRow(row))changed=true;
            for(int r=0;r<4;r++){ if(dir==2)_g[r][c]=row[r]; else _g[3-r][c]=row[r]; }
        }
    }
    return changed;
}

static bool canMove(){
    for(int r=0;r<4;r++)for(int c=0;c<4;c++){
        if(_g[r][c]==0)return true;
        if(c<3&&_g[r][c]==_g[r][c+1])return true;
        if(r<3&&_g[r][c]==_g[r+1][c])return true;
    }
    return false;
}

static void newGame(){
    memset(_g,0,sizeof(_g));
    _score=0;_over=false;_won=false;
    addTile();addTile();
    drawAll();drawHUD();
}

void start(){ _running=true; loadBest(); newGame(); }

void handleKey(char key){
    if(_over){ if(key=='\n'||key=='\r')newGame(); return; }
    int dir=-1;
    if(key=='a'||key=='A')dir=0;
    else if(key=='d'||key=='D')dir=1;
    else if(key=='w'||key=='W')dir=2;
    else if(key=='s'||key=='S')dir=3;
    if(dir<0)return;
    if(move(dir)){
        addTile();
        if(_score>_best){_best=_score;saveBest();}
        drawAll();drawHUD();
        if(!canMove()){
            _over=true;
            g_lcd_secondary.fillRect(60,100,200,44,T()->title_bg);
            g_lcd_secondary.drawRect(60,100,200,44,T()->alert);
            g_lcd_secondary.setTextSize(2);
            g_lcd_secondary.setTextColor(T()->alert,T()->title_bg);
            g_lcd_secondary.setCursor(86,108);g_lcd_secondary.print("GAME OVER");
            g_lcd_secondary.setTextSize(1);
            g_lcd_secondary.setTextColor(T()->text,T()->title_bg);
            g_lcd_secondary.setCursor(96,132);g_lcd_secondary.print("ENTER retry");
        } else if(_won){
            g_lcd_secondary.fillRect(70,108,180,24,T()->success);
            g_lcd_secondary.setTextColor(TFT_BLACK,T()->success);
            g_lcd_secondary.setTextSize(2);
            g_lcd_secondary.setCursor(96,112);g_lcd_secondary.print("2048!");
            _won=false; // only show once
            delay(800); drawAll();
        }
    }
}

bool isRunning(){return _running;}
void stop(){_running=false;}

} } // namespace Games::G2048
