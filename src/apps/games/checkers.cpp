// =============================================================================
// checkers.cpp — Checkers (2-player or vs basic AI)
// =============================================================================
// 8x8 board. Red (bottom, player 1) moves up, Black (top) moves down.
// Kings move both ways. Captures by jumping. Simplified: no forced-capture
// enforcement, single jumps per turn (keeps it beginner friendly).
// Pieces: 0 empty, 1 red man, 2 red king, 3 black man, 4 black king.
// =============================================================================

#include "checkers.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"

namespace Games { namespace Checkers {

static uint8_t _b[8][8];
static bool _redTurn=true;     // red = player 1 (human in AI mode)
static bool _vsAI=false;
static bool _selecting=true;
static int  _modeSel=0;
static int  _curR=5,_curC=0;
static int  _selR=-1,_selC=-1;
static bool _hasSel=false;
static bool _running=false,_over=false;
static int  _winner=0;

static const int SQ=26;
static const int BX=(320-8*SQ)/2;
static const int BY=18;

static const ColorTheme* T(){ return g_theme; }

static bool isRed(uint8_t p){return p==1||p==2;}
static bool isBlack(uint8_t p){return p==3||p==4;}
static bool isKing(uint8_t p){return p==2||p==4;}

static void reset(){
    memset(_b,0,sizeof(_b));
    for(int r=0;r<3;r++)for(int c=0;c<8;c++)if((r+c)%2==1)_b[r][c]=3; // black top
    for(int r=5;r<8;r++)for(int c=0;c<8;c++)if((r+c)%2==1)_b[r][c]=1; // red bottom
    _redTurn=true;_hasSel=false;_selR=_selC=-1;_curR=5;_curC=0;
    _over=false;_winner=0;
}

static void drawModeSelect(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8);g_lcd_secondary.print("CHECKERS");
    const char* o[2]={"2 PLAYERS","vs AI"};
    for(int i=0;i<2;i++){
        int y=90+i*34;bool s=(i==_modeSel);
        if(s){g_lcd_secondary.fillRect(70,y,180,28,T()->highlight_bg);
              g_lcd_secondary.setTextColor(T()->highlight_text,T()->highlight_bg);}
        else g_lcd_secondary.setTextColor(T()->text,T()->bg);
        g_lcd_secondary.setTextSize(2);g_lcd_secondary.setCursor(100,y+5);
        g_lcd_secondary.print(o[i]);
    }
    g_lcd_secondary.setTextSize(1);g_lcd_secondary.setTextColor(T()->text_dim,T()->bg);
    g_lcd_secondary.setCursor(70,180);g_lcd_secondary.print("W/S choose  ENTER start");
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("CHECK");
}

static void drawSquare(int r,int c){
    int x=BX+c*SQ,y=BY+r*SQ;
    bool dark=((r+c)%2==1);
    uint16_t base=dark?RGB565(60,40,30):RGB565(210,200,180);
    if(_hasSel&&r==_selR&&c==_selC)base=T()->success;
    else if(r==_curR&&c==_curC)base=T()->primary;
    g_lcd_secondary.fillRect(x,y,SQ,SQ,base);
    uint8_t p=_b[r][c];
    if(p){
        uint16_t pc=isRed(p)?RGB565(230,40,40):RGB565(40,40,40);
        g_lcd_secondary.fillCircle(x+SQ/2,y+SQ/2,SQ/2-3,pc);
        g_lcd_secondary.drawCircle(x+SQ/2,y+SQ/2,SQ/2-3,TFT_WHITE);
        if(isKing(p)){
            g_lcd_secondary.setTextColor(RGB565(255,215,0));
            g_lcd_secondary.setTextSize(1);
            g_lcd_secondary.setCursor(x+SQ/2-3,y+SQ/2-3);
            g_lcd_secondary.print("K");
        }
    }
}

static void drawBoard(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,14,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(BX,3);g_lcd_secondary.print("CHECKERS");
    for(int r=0;r<8;r++)for(int c=0;c<8;c++)drawSquare(r,c);
    g_lcd_secondary.fillRect(0,226,320,14,T()->title_bg);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(4,229);
    g_lcd_secondary.print("WASD move ENTER select ESC quit");
}

static void drawHUD(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("CHECK");
    g_lcd_primary.setTextSize(1);
    if(_over){
        g_lcd_primary.setTextColor(T()->alert,T()->hud_bg);
        g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,34);
        g_lcd_primary.printf("%s WINS",_winner==1?"RED":"BLACK");
        g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
        g_lcd_primary.setCursor(4,60);g_lcd_primary.print("ENTER new game");
        return;
    }
    int red=0,blk=0;
    for(int r=0;r<8;r++)for(int c=0;c<8;c++){if(isRed(_b[r][c]))red++;if(isBlack(_b[r][c]))blk++;}
    g_lcd_primary.setTextColor(RGB565(230,40,40),T()->hud_bg);
    g_lcd_primary.setCursor(4,34);g_lcd_primary.printf("Red:   %d",red);
    g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,48);g_lcd_primary.printf("Black: %d",blk);
    g_lcd_primary.setTextColor(_redTurn?RGB565(230,40,40):T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,68);
    g_lcd_primary.printf("Turn: %s",_redTurn?"RED":(_vsAI?"AI":"BLACK"));
}

// Validate a move from (fr,fc)->(tr,tc). Sets capture coords if a jump.
static bool validMove(int fr,int fc,int tr,int tc,int* capR,int* capC){
    if(tr<0||tr>7||tc<0||tc>7)return false;
    if(_b[tr][tc]!=0)return false;
    uint8_t p=_b[fr][fc];
    if(p==0)return false;
    int dr=tr-fr,dc=tc-fc;
    *capR=-1;*capC=-1;
    // direction limits for men
    bool fwdOK = isKing(p) || (isRed(p)&&dr<0) || (isBlack(p)&&dr>0);
    if(!fwdOK)return false;
    if(abs(dr)==1&&abs(dc)==1)return true;            // simple move
    if(abs(dr)==2&&abs(dc)==2){                        // jump
        int mr=(fr+tr)/2,mc=(fc+tc)/2;
        uint8_t mid=_b[mr][mc];
        if(mid==0)return false;
        bool enemy = isRed(p)?isBlack(mid):isRed(mid);
        if(!enemy)return false;
        *capR=mr;*capC=mc;
        return true;
    }
    return false;
}

static void checkWin(){
    int red=0,blk=0;
    for(int r=0;r<8;r++)for(int c=0;c<8;c++){if(isRed(_b[r][c]))red++;if(isBlack(_b[r][c]))blk++;}
    if(red==0){_over=true;_winner=2;}
    else if(blk==0){_over=true;_winner=1;}
}

static void crown(int r,int c){
    if(_b[r][c]==1&&r==0)_b[r][c]=2;
    if(_b[r][c]==3&&r==7)_b[r][c]=4;
}

// very basic AI for black: find any capture, else any forward move
static void aiMove(){
    // collect black pieces' moves
    int bestFr=-1,bestFc=-1,bestTr=-1,bestTc=-1,bestCapR=-1,bestCapC=-1;
    bool foundCap=false;
    for(int r=0;r<8&&!foundCap;r++)for(int c=0;c<8&&!foundCap;c++){
        if(!isBlack(_b[r][c]))continue;
        int dirs[4][2]={{2,2},{2,-2},{-2,2},{-2,-2}};
        for(int d=0;d<4;d++){
            int capR,capC;
            if(validMove(r,c,r+dirs[d][0],c+dirs[d][1],&capR,&capC)&&capR>=0){
                bestFr=r;bestFc=c;bestTr=r+dirs[d][0];bestTc=c+dirs[d][1];
                bestCapR=capR;bestCapC=capC;foundCap=true;break;
            }
        }
    }
    if(!foundCap){
        // any simple move (collect all, pick random)
        int moves[64][4],n=0;
        for(int r=0;r<8;r++)for(int c=0;c<8;c++){
            if(!isBlack(_b[r][c]))continue;
            int dirs[4][2]={{1,1},{1,-1},{-1,1},{-1,-1}};
            for(int d=0;d<4;d++){
                int capR,capC;
                if(validMove(r,c,r+dirs[d][0],c+dirs[d][1],&capR,&capC)){
                    moves[n][0]=r;moves[n][1]=c;
                    moves[n][2]=r+dirs[d][0];moves[n][3]=c+dirs[d][1];n++;
                }
            }
        }
        if(n==0){_over=true;_winner=1;return;}
        int pick=random(0,n);
        bestFr=moves[pick][0];bestFc=moves[pick][1];
        bestTr=moves[pick][2];bestTc=moves[pick][3];
    }
    // execute
    _b[bestTr][bestTc]=_b[bestFr][bestFc];
    _b[bestFr][bestFc]=0;
    if(bestCapR>=0)_b[bestCapR][bestCapC]=0;
    crown(bestTr,bestTc);
    _redTurn=true;
    checkWin();
}

void start(){ _running=true;_selecting=true;_modeSel=0; drawModeSelect(); }

void handleKey(char key){
    if(_selecting){
        if(key=='w'||key=='W'||key=='s'||key=='S'){_modeSel=1-_modeSel;drawModeSelect();}
        else if(key=='\n'||key=='\r'){_vsAI=(_modeSel==1);_selecting=false;reset();drawBoard();drawHUD();}
        return;
    }
    if(_over){ if(key=='\n'||key=='\r'){reset();drawBoard();drawHUD();} return; }

    int pr=_curR,pc=_curC;bool moved=false;
    switch(key){
        case 'w':case 'W':if(_curR>0){_curR--;moved=true;}break;
        case 's':case 'S':if(_curR<7){_curR++;moved=true;}break;
        case 'a':case 'A':if(_curC>0){_curC--;moved=true;}break;
        case 'd':case 'D':if(_curC<7){_curC++;moved=true;}break;
        case '\n':case '\r':{
            uint8_t p=_b[_curR][_curC];
            if(!_hasSel){
                if(_redTurn&&isRed(p)){_hasSel=true;_selR=_curR;_selC=_curC;drawSquare(_selR,_selC);}
            } else {
                if(_curR==_selR&&_curC==_selC){_hasSel=false;int s1=_selR,s2=_selC;_selR=_selC=-1;drawSquare(s1,s2);}
                else {
                    int capR,capC;
                    if(validMove(_selR,_selC,_curR,_curC,&capR,&capC)){
                        _b[_curR][_curC]=_b[_selR][_selC];_b[_selR][_selC]=0;
                        if(capR>=0)_b[capR][capC]=0;
                        crown(_curR,_curC);
                        _hasSel=false;_selR=_selC=-1;
                        _redTurn=false;
                        checkWin();
                        drawBoard();drawHUD();
                        if(!_over&&_vsAI){ delay(400); aiMove(); drawBoard(); drawHUD(); }
                        return;
                    }
                }
            }
            return;
        }
    }
    if(moved){drawSquare(pr,pc);drawSquare(_curR,_curC);}
}

bool isRunning(){return _running;}
void stop(){_running=false;_selecting=true;_over=false;}

} } // namespace Games::Checkers
