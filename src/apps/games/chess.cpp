// =============================================================================
// chess.cpp — Two-Player Chess Implementation
// =============================================================================
// Board representation: 8x8 array. Pieces are chars:
//   Uppercase = White (P N B R Q K), lowercase = Black (p n b r q k), '.'=empty
// White moves up the board (decreasing row index visually), Black moves down.
// Row 0 = top (Black home), Row 7 = bottom (White home).
// =============================================================================

#include "chess.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"

namespace Games { namespace Chess {

// ─── Board state ──────────────────────────────────────────────────────────────
static char _b[8][8];
static bool _whiteToMove = true;
static int  _curR, _curC;        // cursor position
static int  _selR, _selC;        // selected piece (-1 = none)
static bool _hasSel = false;
static bool _running = false;
static bool _gameover = false;
static char _winner = 0;

// Castling rights
static bool _wK_moved=false,_wRk_moved=false,_wRq_moved=false;
static bool _bK_moved=false,_bRk_moved=false,_bRq_moved=false;
// En passant target (-1 if none)
static int _epR=-1,_epC=-1;

// Captured pieces (for HUD)
static char _capW[16]; static int _capWn=0;  // black pieces white captured
static char _capB[16]; static int _capBn=0;

// ─── Geometry ─────────────────────────────────────────────────────────────────
static const int SQ = 26;                 // square size px
static const int BX = (320 - 8*SQ) / 2;   // board origin x (centered)
static const int BY = 16;

// =============================================================================
static bool isWhite(char p){ return p>='A'&&p<='Z'; }
static bool isBlack(char p){ return p>='a'&&p<='z'; }
static bool isEmpty(char p){ return p=='.'; }
static bool sameSide(char a,char b){
    if(isEmpty(a)||isEmpty(b))return false;
    return (isWhite(a)&&isWhite(b))||(isBlack(a)&&isBlack(b));
}
static char toLower(char p){ return (p>='A'&&p<='Z')?p+32:p; }

static void resetBoard() {
    const char* back = "rnbqkbnr";
    for (int c=0;c<8;c++){
        _b[0][c]=back[c];          // black back rank
        _b[1][c]='p';              // black pawns
        _b[6][c]='P';              // white pawns
        _b[7][c]=back[c]-32;       // white back rank (uppercase)
        for(int r=2;r<6;r++)_b[r][c]='.';
    }
    _whiteToMove=true; _hasSel=false;
    _curR=6;_curC=4; _selR=_selC=-1;
    _wK_moved=_wRk_moved=_wRq_moved=false;
    _bK_moved=_bRk_moved=_bRq_moved=false;
    _epR=_epC=-1; _capWn=_capBn=0;
    _gameover=false; _winner=0;
}

// Find king of a side
static void findKing(bool white,int&kr,int&kc){
    char k = white?'K':'k';
    for(int r=0;r<8;r++)for(int c=0;c<8;c++)if(_b[r][c]==k){kr=r;kc=c;return;}
    kr=kc=-1;
}

// Can piece at (fr,fc) pseudo-legally move to (tr,tc)? (ignores check)
static bool pseudoLegal(int fr,int fc,int tr,int tc){
    if(tr<0||tr>7||tc<0||tc>7)return false;
    char p=_b[fr][fc];
    if(isEmpty(p))return false;
    char tp=_b[tr][tc];
    if(sameSide(p,tp))return false;
    int dr=tr-fr, dc=tc-fc;
    char lp=toLower(p);

    switch(lp){
        case 'p':{
            int dir = isWhite(p)?-1:1;       // white moves up (row-)
            int startRow = isWhite(p)?6:1;
            // forward 1
            if(dc==0 && dr==dir && isEmpty(tp))return true;
            // forward 2
            if(dc==0 && dr==2*dir && fr==startRow && isEmpty(tp)
               && isEmpty(_b[fr+dir][fc]))return true;
            // capture
            if(abs(dc)==1 && dr==dir){
                if(!isEmpty(tp) && !sameSide(p,tp))return true;
                // en passant
                if(tr==_epR && tc==_epC)return true;
            }
            return false;
        }
        case 'n':
            return (abs(dr)==2&&abs(dc)==1)||(abs(dr)==1&&abs(dc)==2);
        case 'b':
            if(abs(dr)!=abs(dc))return false;
            break;
        case 'r':
            if(dr!=0&&dc!=0)return false;
            break;
        case 'q':
            if(dr!=0&&dc!=0&&abs(dr)!=abs(dc))return false;
            break;
        case 'k':
            if(abs(dr)<=1&&abs(dc)<=1)return true;
            // castling handled separately
            return false;
    }
    // sliding pieces: path must be clear
    int sr=(dr>0)?1:(dr<0)?-1:0;
    int sc=(dc>0)?1:(dc<0)?-1:0;
    int r=fr+sr,c=fc+sc;
    while(r!=tr||c!=tc){
        if(!isEmpty(_b[r][c]))return false;
        r+=sr;c+=sc;
    }
    return true;
}

// Is square (r,c) attacked by the given side?
static bool attackedBy(bool byWhite,int r,int c){
    for(int fr=0;fr<8;fr++)for(int fc=0;fc<8;fc++){
        char p=_b[fr][fc];
        if(isEmpty(p))continue;
        if(byWhite&&!isWhite(p))continue;
        if(!byWhite&&!isBlack(p))continue;
        // pawn attacks are diagonal only (pseudoLegal handles forward, not attack)
        if(toLower(p)=='p'){
            int dir=isWhite(p)?-1:1;
            if(r==fr+dir && (c==fc-1||c==fc+1))return true;
            continue;
        }
        if(toLower(p)=='k'){
            if(abs(r-fr)<=1&&abs(c-fc)<=1)return true;
            continue;
        }
        if(pseudoLegal(fr,fc,r,c))return true;
    }
    return false;
}

// Would moving fr->tr leave own king in check? (full legality test)
static bool leavesKingInCheck(int fr,int fc,int tr,int tc){
    char save_from=_b[fr][fc];
    char save_to=_b[tr][tc];
    bool white=isWhite(save_from);
    // handle en passant capture removal
    char epCap='.'; int epr=-1,epc=-1;
    if(toLower(save_from)=='p' && tc!=fc && isEmpty(save_to) && tr==_epR && tc==_epC){
        epr=fr; epc=tc; epCap=_b[epr][epc]; _b[epr][epc]='.';
    }
    _b[tr][tc]=save_from; _b[fr][fc]='.';
    int kr,kc; findKing(white,kr,kc);
    bool inCheck = attackedBy(!white,kr,kc);
    // undo
    _b[fr][fc]=save_from; _b[tr][tc]=save_to;
    if(epr>=0)_b[epr][epc]=epCap;
    return inCheck;
}

static bool legalMove(int fr,int fc,int tr,int tc){
    if(!pseudoLegal(fr,fc,tr,tc))return false;
    return !leavesKingInCheck(fr,fc,tr,tc);
}

// Does the side to move have any legal move?
static bool anyLegalMove(bool white){
    for(int fr=0;fr<8;fr++)for(int fc=0;fc<8;fc++){
        char p=_b[fr][fc];
        if(isEmpty(p))continue;
        if(white&&!isWhite(p))continue;
        if(!white&&!isBlack(p))continue;
        for(int tr=0;tr<8;tr++)for(int tc=0;tc<8;tc++)
            if(legalMove(fr,fc,tr,tc))return true;
    }
    return false;
}

// =============================================================================
// Rendering — Unicode-free piece glyphs (letters), colored by side
// =============================================================================
static const ColorTheme* T(){ return g_theme; }

static void drawSquare(int r,int c){
    const int x=BX+c*SQ, y=BY+r*SQ;
    bool light = ((r+c)%2==0);
    uint16_t base = light ? RGB565(210,210,220) : RGB565(90,90,110);

    // highlight selected + cursor
    if(_hasSel && r==_selR && c==_selC) base = T()->success;
    else if(r==_curR && c==_curC)       base = T()->primary;

    g_lcd_secondary.fillRect(x,y,SQ,SQ,base);

    char p=_b[r][c];
    if(!isEmpty(p)){
        // White pieces drawn white, black pieces drawn dark
        uint16_t pc = isWhite(p) ? RGB565(255,255,255) : RGB565(20,20,30);
        // outline for contrast
        uint16_t oc = isWhite(p) ? RGB565(0,0,0) : RGB565(200,200,210);
        char up = (p>='a'&&p<='z')?p-32:p;
        char g[2]={up,0};
        g_lcd_secondary.setTextSize(2);
        int tx=x+SQ/2-6, ty=y+SQ/2-7;
        g_lcd_secondary.setTextColor(oc);
        g_lcd_secondary.setCursor(tx+1,ty+1);
        g_lcd_secondary.print(g);
        g_lcd_secondary.setTextColor(pc);
        g_lcd_secondary.setCursor(tx,ty);
        g_lcd_secondary.print(g);
    }
}

static void drawBoard(){
    g_lcd_secondary.fillScreen(T()->bg);
    // title
    g_lcd_secondary.fillRect(0,0,320,14,T()->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(BX,3);
    g_lcd_secondary.print("CHESS  //  2 players");
    for(int r=0;r<8;r++)for(int c=0;c<8;c++)drawSquare(r,c);
    // footer
    g_lcd_secondary.fillRect(0,226,320,14,T()->title_bg);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(4,229);
    g_lcd_secondary.print("WASD move  ENTER select  ESC quit");
}

static void drawHUD(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2);
    g_lcd_primary.setCursor(4,6);
    g_lcd_primary.print("CHESS");
    g_lcd_primary.setTextSize(1);

    if(_gameover){
        g_lcd_primary.setTextColor(T()->alert,T()->hud_bg);
        g_lcd_primary.setTextSize(2);
        g_lcd_primary.setCursor(4,34);
        if(_winner=='d') g_lcd_primary.print("STALEMATE");
        else g_lcd_primary.printf("%s WINS", _winner=='W'?"WHITE":"BLACK");
        g_lcd_primary.setTextSize(1);
        g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
        g_lcd_primary.setCursor(4,60);
        g_lcd_primary.print("ENTER new game");
        return;
    }

    g_lcd_primary.setTextColor(_whiteToMove?TFT_WHITE:T()->secondary,T()->hud_bg);
    g_lcd_primary.setCursor(4,32);
    g_lcd_primary.printf("Turn: %s", _whiteToMove?"WHITE":"BLACK");

    // check indicator
    int kr,kc; findKing(_whiteToMove,kr,kc);
    if(attackedBy(!_whiteToMove,kr,kc)){
        g_lcd_primary.setTextColor(T()->alert,T()->hud_bg);
        g_lcd_primary.setCursor(4,48);
        g_lcd_primary.print("** CHECK **");
    }

    // captured
    g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.setCursor(4,68);
    g_lcd_primary.print("W took:");
    g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(54,68);
    for(int i=0;i<_capWn&&i<10;i++){char g[2]={(char)(_capW[i]-32),0};g_lcd_primary.print(g);}
    g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.setCursor(4,80);
    g_lcd_primary.print("B took:");
    g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(54,80);
    for(int i=0;i<_capBn&&i<10;i++){char g[2]={_capB[i],0};g_lcd_primary.print(g);}

    g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.setCursor(4,98);
    g_lcd_primary.print("Pass device each turn");
}

// =============================================================================
static void doMove(int fr,int fc,int tr,int tc){
    char p=_b[fr][fc];
    char captured=_b[tr][tc];

    // en passant capture
    if(toLower(p)=='p' && tc!=fc && isEmpty(captured) && tr==_epR && tc==_epC){
        captured=_b[fr][tc];
        _b[fr][tc]='.';
    }

    // record capture
    if(!isEmpty(captured)){
        if(isWhite(p)){ if(_capWn<15)_capW[_capWn++]=captured; }
        else          { if(_capBn<15)_capB[_capBn++]=captured; }
    }

    // move piece
    _b[tr][tc]=p; _b[fr][fc]='.';

    // pawn promotion (auto-queen)
    if(toLower(p)=='p' && (tr==0||tr==7)){
        _b[tr][tc]= isWhite(p)?'Q':'q';
    }

    // set en passant target
    _epR=_epC=-1;
    if(toLower(p)=='p' && abs(tr-fr)==2){
        _epR=(fr+tr)/2; _epC=fc;
    }

    // track king/rook moves for castling (basic)
    if(p=='K'){_wK_moved=true;} if(p=='k'){_bK_moved=true;}

    _whiteToMove=!_whiteToMove;

    // checkmate / stalemate detection
    if(!anyLegalMove(_whiteToMove)){
        int kr,kc; findKing(_whiteToMove,kr,kc);
        if(attackedBy(!_whiteToMove,kr,kc)){
            _gameover=true; _winner=_whiteToMove?'B':'W'; // side to move is mated
        } else {
            _gameover=true; _winner='d'; // stalemate
        }
    }
}

// =============================================================================
// Basic AI (plays Black). Greedy 1-ply: pick the legal move that maximizes
// material gain, with a tiny random tiebreak so it's not fully deterministic.
// Beatable on purpose, but always legal.
// =============================================================================
static bool _vsAI=false;
static bool _selecting=true;
static int  _modeSel=0;

static int pieceValue(char p){
    switch(toLower(p)){
        case 'p':return 1; case 'n':return 3; case 'b':return 3;
        case 'r':return 5; case 'q':return 9; case 'k':return 100;
        default:return 0;
    }
}

static void aiMove(){
    // gather all legal black moves
    int bestFr=-1,bestFc=-1,bestTr=-1,bestTc=-1,bestScore=-1000;
    for(int fr=0;fr<8;fr++)for(int fc=0;fc<8;fc++){
        char p=_b[fr][fc];
        if(!isBlack(p))continue;
        for(int tr=0;tr<8;tr++)for(int tc=0;tc<8;tc++){
            if(!legalMove(fr,fc,tr,tc))continue;
            int score=pieceValue(_b[tr][tc]);          // capture value
            // bonus for advancing pawns, slight center preference
            score = score*10 + random(0,3);
            if(toLower(p)=='p') score += tr;           // push pawns downward
            if((tc==3||tc==4)&&(tr==3||tr==4)) score+=1;
            if(score>bestScore){
                bestScore=score;bestFr=fr;bestFc=fc;bestTr=tr;bestTc=tc;
            }
        }
    }
    if(bestFr>=0){
        doMove(bestFr,bestFc,bestTr,bestTc);
        drawBoard();
        drawHUD();
    }
}

static void drawModeSelect(){
    g_lcd_secondary.fillScreen(g_theme->bg);
    g_lcd_secondary.fillRect(0,0,320,24,g_theme->title_bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(g_theme->primary,g_theme->title_bg);
    g_lcd_secondary.setCursor(8,8);g_lcd_secondary.print("CHESS");
    const char* o[2]={"2 PLAYERS","vs AI (you=White)"};
    for(int i=0;i<2;i++){
        int y=90+i*34;bool s=(i==_modeSel);
        if(s){g_lcd_secondary.fillRect(50,y,220,28,g_theme->highlight_bg);
              g_lcd_secondary.setTextColor(g_theme->highlight_text,g_theme->highlight_bg);}
        else g_lcd_secondary.setTextColor(g_theme->text,g_theme->bg);
        g_lcd_secondary.setTextSize(2);g_lcd_secondary.setCursor(70,y+5);
        g_lcd_secondary.print(o[i]);
    }
    g_lcd_secondary.setTextSize(1);g_lcd_secondary.setTextColor(g_theme->text_dim,g_theme->bg);
    g_lcd_secondary.setCursor(60,180);g_lcd_secondary.print("W/S choose  ENTER start  ESC quit");
    g_lcd_primary.fillScreen(g_theme->hud_bg);
    g_lcd_primary.setTextColor(g_theme->primary,g_theme->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("CHESS");
}

// =============================================================================
void start(){
    _running=true;
    _selecting=true;
    _modeSel=0;
    drawModeSelect();
}

void handleKey(char key){
    if(_selecting){
        if(key=='w'||key=='W'||key=='s'||key=='S'){_modeSel=1-_modeSel;drawModeSelect();}
        else if(key=='\n'||key=='\r'){
            _vsAI=(_modeSel==1);
            _selecting=false;
            resetBoard();
            drawBoard();
            drawHUD();
        }
        return;
    }
    if(_gameover){
        if(key=='\n'||key=='\r'){ resetBoard(); drawBoard(); drawHUD(); }
        return;
    }

    int pr=_curR,pc=_curC;
    bool moved=false;

    switch(key){
        case 'w': case 'W': if(_curR>0){_curR--;moved=true;} break;
        case 's': case 'S': if(_curR<7){_curR++;moved=true;} break;
        case 'a': case 'A': if(_curC>0){_curC--;moved=true;} break;
        case 'd': case 'D': if(_curC<7){_curC++;moved=true;} break;
        case '\n': case '\r': {
            char p=_b[_curR][_curC];
            if(!_hasSel){
                // select own piece
                if(!isEmpty(p) &&
                   ((_whiteToMove&&isWhite(p))||(!_whiteToMove&&isBlack(p)))){
                    _hasSel=true; _selR=_curR; _selC=_curC;
                    drawSquare(_selR,_selC);
                }
            } else {
                // attempt move
                if(_curR==_selR && _curC==_selC){
                    // deselect
                    _hasSel=false;
                    int sr=_selR,sc=_selC; _selR=_selC=-1;
                    drawSquare(sr,sc);
                } else if(legalMove(_selR,_selC,_curR,_curC)){
                    doMove(_selR,_selC,_curR,_curC);
                    _hasSel=false; _selR=_selC=-1;
                    drawBoard();
                    drawHUD();
                    // AI responds as Black
                    if(_vsAI && !_gameover && !_whiteToMove){
                        delay(350);
                        aiMove();
                    }
                    return;
                } else {
                    // invalid — if clicking another own piece, reselect
                    if(!isEmpty(p) &&
                       ((_whiteToMove&&isWhite(p))||(!_whiteToMove&&isBlack(p)))){
                        int osr=_selR,osc=_selC;
                        _selR=_curR;_selC=_curC;
                        drawSquare(osr,osc);
                        drawSquare(_selR,_selC);
                    }
                }
            }
            return;
        }
    }

    if(moved){
        // redraw old + new cursor squares (and selected stays highlighted)
        drawSquare(pr,pc);
        drawSquare(_curR,_curC);
    }
}

bool isRunning(){ return _running; }
void stop(){ _running=false; _selecting=true; }

} } // namespace Games::Chess
