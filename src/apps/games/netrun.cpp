// =============================================================================
// netrun.cpp — NETRUN: cyberpunk roguelike
// =============================================================================
#include "netrun.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <Preferences.h>

namespace Games { namespace Netrun {

static const ColorTheme* T(){ return g_theme; }

// ─── Map geometry ────────────────────────────────────────────────────────────
// Big screen is 320x240. We reserve a top title strip; the rest is the map.
// Tiles are 14px so the grid is readable. 21 cols x 14 rows fits nicely.
static const int TILE   = 14;
static const int MAP_X  = 8;
static const int MAP_Y  = 24;
static const int COLS   = 21;          // (320-16)/14 ~= 21
static const int ROWS   = 14;          // (240-24-8)/14 ~= 14

// Tile types
enum { T_WALL=0, T_FLOOR=1, T_EXIT=2 };

// Entity / item kinds
enum { E_NONE=0, E_DRONE=1, E_ICE=2, E_DAEMON=3 };
enum { I_NONE=0, I_STIM=1, I_CREDITS=2, I_WEAPON=3, I_ARMOR=4 };

struct Entity {
    bool alive;
    int  x, y;
    int  kind;      // E_*
    int  hp, maxhp;
    int  atk;
};

struct Item {
    bool present;
    int  x, y;
    int  kind;      // I_*
    int  amount;    // credits amount / heal amount / upgrade tier
};

// ─── Game state ──────────────────────────────────────────────────────────────
static uint8_t _map[ROWS][COLS];
static Entity  _en[24];
static int     _enN;
static Item    _items[24];
static int     _itemN;

static int  _px, _py;             // player pos
static int  _depth;               // current floor (1+)
static int  _best;                // best depth (NVS)
static int  _hp, _maxhp;
static int  _atk;                 // attack power (from weapon)
static int  _def;                 // defense (from armor)
static int  _credits;
static int  _stims;
static int  _wTier, _aTier;       // weapon/armor tiers (for display)

static bool _running=false;
static bool _over=false;
static bool _descend=false;       // flag: redraw next floor

// combat log (internal screen) — 5 lines, ring
static char _log[5][26];
static int  _logHead=0;

static void logMsg(const char* m){
    strncpy(_log[_logHead], m, 25);
    _log[_logHead][25]=0;
    _logHead=(_logHead+1)%5;
}
static void logClear(){ for(int i=0;i<5;i++)_log[i][0]=0; _logHead=0; }

// ─── NVS ─────────────────────────────────────────────────────────────────────
static void loadBest(){ Preferences p; p.begin("purplx",true); _best=p.getInt("netrun_best",0); p.end(); }
static void saveBest(){ Preferences p; p.begin("purplx",false); p.putInt("netrun_best",_best); p.end(); }

// ─── Map generation: simple rooms + corridors ────────────────────────────────
static int _rooms[6][4];   // x,y,w,h
static int _roomN;

static void carveRoom(int rx,int ry,int rw,int rh){
    for(int y=ry;y<ry+rh && y<ROWS-1;y++)
        for(int x=rx;x<rx+rw && x<COLS-1;x++)
            if(x>0&&y>0) _map[y][x]=T_FLOOR;
}
static void carveH(int x1,int x2,int y){
    if(x1>x2){int t=x1;x1=x2;x2=t;}
    for(int x=x1;x<=x2;x++) if(x>0&&x<COLS-1&&y>0&&y<ROWS-1) _map[y][x]=T_FLOOR;
}
static void carveV(int y1,int y2,int x){
    if(y1>y2){int t=y1;y1=y2;y2=t;}
    for(int y=y1;y<=y2;y++) if(y>0&&y<ROWS-1&&x>0&&x<COLS-1) _map[y][x]=T_FLOOR;
}

static void genMap(){
    for(int y=0;y<ROWS;y++) for(int x=0;x<COLS;x++) _map[y][x]=T_WALL;
    _roomN = 3 + random(0,3);              // 3..5 rooms
    if(_roomN>6)_roomN=6;
    for(int i=0;i<_roomN;i++){
        int rw=3+random(0,4), rh=2+random(0,3);
        int rx=1+random(0,COLS-rw-2), ry=1+random(0,ROWS-rh-2);
        _rooms[i][0]=rx; _rooms[i][1]=ry; _rooms[i][2]=rw; _rooms[i][3]=rh;
        carveRoom(rx,ry,rw,rh);
    }
    // connect room centers
    for(int i=1;i<_roomN;i++){
        int ax=_rooms[i-1][0]+_rooms[i-1][2]/2, ay=_rooms[i-1][1]+_rooms[i-1][3]/2;
        int bx=_rooms[i][0]+_rooms[i][2]/2,     by=_rooms[i][1]+_rooms[i][3]/2;
        if(random(0,2)){ carveH(ax,bx,ay); carveV(ay,by,bx); }
        else           { carveV(ay,by,ax); carveH(ax,bx,by); }
    }
    // place player in first room
    _px=_rooms[0][0]+_rooms[0][2]/2;
    _py=_rooms[0][1]+_rooms[0][3]/2;
    // place exit in last room
    int ex=_rooms[_roomN-1][0]+_rooms[_roomN-1][2]/2;
    int ey=_rooms[_roomN-1][1]+_rooms[_roomN-1][3]/2;
    _map[ey][ex]=T_EXIT;
}

static bool isFloor(int x,int y){
    if(x<0||y<0||x>=COLS||y>=ROWS) return false;
    return _map[y][x]!=T_WALL;
}
static bool occupied(int x,int y){
    if(x==_px&&y==_py) return true;
    for(int i=0;i<_enN;i++) if(_en[i].alive&&_en[i].x==x&&_en[i].y==y) return true;
    return false;
}
static void randFloorCell(int& ox,int& oy){
    for(int tries=0;tries<100;tries++){
        int x=random(1,COLS-1), y=random(1,ROWS-1);
        if(_map[y][x]==T_FLOOR && !occupied(x,y) && !(x==_px&&y==_py)){ ox=x;oy=y;return; }
    }
    ox=_px; oy=_py;
}

static void spawnEntities(){
    _enN=0;
    int count = 2 + _depth;                 // more enemies deeper
    if(count>10) count=10;
    for(int i=0;i<count;i++){
        int x,y; randFloorCell(x,y);
        Entity& e=_en[_enN++];
        e.alive=true; e.x=x; e.y=y;
        // kind weighted by depth
        int r=random(0,100);
        if(_depth>=4 && r<25)      { e.kind=E_DAEMON; e.maxhp=14+_depth; e.atk=5+_depth/2; }
        else if(r<50)              { e.kind=E_ICE;    e.maxhp=8+_depth;  e.atk=3+_depth/3; }
        else                       { e.kind=E_DRONE;  e.maxhp=4+_depth/2;e.atk=2+_depth/4; }
        e.hp=e.maxhp;
    }
}

static void spawnItems(){
    _itemN=0;
    int count = 1 + random(0,3);
    for(int i=0;i<count;i++){
        int x,y; randFloorCell(x,y);
        Item& it=_items[_itemN++];
        it.present=true; it.x=x; it.y=y;
        int r=random(0,100);
        if(r<40)      { it.kind=I_STIM;    it.amount=8+_depth; }
        else if(r<75) { it.kind=I_CREDITS; it.amount=5+random(0,10*_depth); }
        else if(r<90) { it.kind=I_WEAPON;  it.amount=1; }
        else          { it.kind=I_ARMOR;   it.amount=1; }
    }
}

// ─── Rendering (big screen = map) ────────────────────────────────────────────
static uint16_t dim(uint16_t c,float f){
    uint8_t r=((c>>11)&0x1F)<<3, g=((c>>5)&0x3F)<<2, b=(c&0x1F)<<3;
    return RGB565((int)(r*f),(int)(g*f),(int)(b*f));
}

static void drawTileAt(int x,int y){
    int sx=MAP_X+x*TILE, sy=MAP_Y+y*TILE;
    uint8_t t=_map[y][x];
    // base tile
    if(t==T_WALL){
        g_lcd_secondary.fillRect(sx,sy,TILE,TILE, dim(T()->primary,0.18f));
        g_lcd_secondary.drawRect(sx,sy,TILE,TILE, dim(T()->primary,0.30f));
    } else {
        g_lcd_secondary.fillRect(sx,sy,TILE,TILE, T()->bg);
        // faint grid dot
        g_lcd_secondary.drawPixel(sx+TILE/2,sy+TILE/2, dim(T()->secondary,0.25f));
        if(t==T_EXIT){
            g_lcd_secondary.drawRect(sx+2,sy+2,TILE-4,TILE-4, T()->success);
            g_lcd_secondary.setTextColor(T()->success,T()->bg);
            g_lcd_secondary.setTextSize(1);
            g_lcd_secondary.setCursor(sx+3,sy+3);
            g_lcd_secondary.print(">");
        }
    }
}

static void drawItemAt(const Item& it){
    if(!it.present) return;
    int sx=MAP_X+it.x*TILE, sy=MAP_Y+it.y*TILE;
    uint16_t c; const char* g;
    switch(it.kind){
        case I_STIM:    c=T()->success;   g="+"; break;
        case I_CREDITS: c=RGB565(255,215,0); g="$"; break;
        case I_WEAPON:  c=T()->alert;     g="/"; break;
        case I_ARMOR:   c=T()->secondary; g="]"; break;
        default: return;
    }
    g_lcd_secondary.setTextColor(c,T()->bg);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setCursor(sx+4,sy+3);
    g_lcd_secondary.print(g);
}

static void drawEntityAt(const Entity& e){
    if(!e.alive) return;
    int sx=MAP_X+e.x*TILE, sy=MAP_Y+e.y*TILE;
    uint16_t c; const char* g;
    switch(e.kind){
        case E_DRONE:  c=T()->secondary; g="d"; break;
        case E_ICE:    c=RGB565(120,200,255); g="I"; break;
        case E_DAEMON: c=T()->alert;     g="D"; break;
        default: return;
    }
    g_lcd_secondary.fillCircle(sx+TILE/2,sy+TILE/2,TILE/2-2, dim(c,0.3f));
    g_lcd_secondary.setTextColor(c,dim(c,0.3f));
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setCursor(sx+4,sy+3);
    g_lcd_secondary.print(g);
}

static void drawPlayer(){
    int sx=MAP_X+_px*TILE, sy=MAP_Y+_py*TILE;
    g_lcd_secondary.fillCircle(sx+TILE/2,sy+TILE/2,TILE/2-1, T()->primary);
    g_lcd_secondary.setTextColor(T()->bg,T()->primary);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setCursor(sx+4,sy+3);
    g_lcd_secondary.print("@");
}

static void drawMap(){
    g_lcd_secondary.fillScreen(T()->bg);
    // title
    g_lcd_secondary.fillRect(0,0,320,20,T()->title_bg);
    g_lcd_secondary.drawFastHLine(0,19,320,T()->primary);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(6,6); g_lcd_secondary.print("NETRUN");
    g_lcd_secondary.setTextColor(T()->secondary,T()->title_bg);
    char d[20]; snprintf(d,sizeof(d),"DEPTH %d", _depth);
    g_lcd_secondary.setCursor(260,6); g_lcd_secondary.print(d);
    // tiles
    for(int y=0;y<ROWS;y++) for(int x=0;x<COLS;x++) drawTileAt(x,y);
    for(int i=0;i<_itemN;i++) drawItemAt(_items[i]);
    for(int i=0;i<_enN;i++)   drawEntityAt(_en[i]);
    drawPlayer();
}

// ─── Stats panel (internal screen) ───────────────────────────────────────────
static void drawStats(){
    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.fillRect(0,0,240,3,T()->primary);
    // HP bar
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,8); g_lcd_primary.print("INTEGRITY");
    int bx=70,bw=150,bh=9;
    g_lcd_primary.drawRect(bx,7,bw,bh,T()->border);
    int fw=(bw-2)*_hp/(_maxhp>0?_maxhp:1); if(fw<0)fw=0;
    uint16_t hpc = (_hp*100/_maxhp < 30)? T()->alert : T()->success;
    g_lcd_primary.fillRect(bx+1,8,fw,bh-2,hpc);
    g_lcd_primary.setCursor(bx+bw-44,8); 
    g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    char hp[16]; snprintf(hp,sizeof(hp),"%d/%d",_hp,_maxhp);
    // (printed over bar end; fine)
    // stat line
    g_lcd_primary.setCursor(4,24);
    g_lcd_primary.setTextColor(T()->secondary,T()->hud_bg);
    g_lcd_primary.printf("ATK %d  DEF %d", _atk, _def);
    g_lcd_primary.setCursor(4,36);
    g_lcd_primary.setTextColor(RGB565(255,215,0),T()->hud_bg);
    g_lcd_primary.printf("$ %d", _credits);
    g_lcd_primary.setCursor(90,36);
    g_lcd_primary.setTextColor(T()->success,T()->hud_bg);
    g_lcd_primary.printf("STIM %d [E]", _stims);
    g_lcd_primary.setCursor(150,24);
    g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.printf("W%d A%d", _wTier, _aTier);
    // log
    g_lcd_primary.drawFastHLine(0,50,240,T()->border);
    for(int i=0;i<5;i++){
        int idx=(_logHead+i)%5;
        g_lcd_primary.setCursor(4,56+i*11);
        g_lcd_primary.setTextColor(i==4?T()->text:T()->text_dim,T()->hud_bg);
        g_lcd_primary.print(_log[idx]);
    }
}

// ─── Combat ──────────────────────────────────────────────────────────────────
static const char* enName(int k){ return k==E_DAEMON?"Daemon":k==E_ICE?"ICE":"Drone"; }

static void playerAttack(Entity& e){
    int dmg = _atk + random(0,3);
    e.hp -= dmg;
    char m[26]; snprintf(m,sizeof(m),"You hit %s -%d", enName(e.kind), dmg);
    logMsg(m);
    if(e.hp<=0){
        e.alive=false;
        snprintf(m,sizeof(m),"%s deleted!", enName(e.kind));
        logMsg(m);
        _credits += 2+random(0,3*_depth);
    }
}

static void enemyTurn(){
    for(int i=0;i<_enN;i++){
        Entity& e=_en[i];
        if(!e.alive) continue;
        int dx=_px-e.x, dy=_py-e.y;
        int adx=dx<0?-dx:dx, ady=dy<0?-dy:dy;
        if(adx<=1 && ady<=1 && (adx+ady)>0){
            // adjacent: attack player
            int dmg = e.atk - _def; if(dmg<1)dmg=1;
            dmg += random(0,2);
            _hp -= dmg;
            char m[26]; snprintf(m,sizeof(m),"%s hits you -%d", enName(e.kind), dmg);
            logMsg(m);
            if(_hp<=0){ _hp=0; _over=true; return; }
        } else {
            // move toward player (greedy, only onto floor & unoccupied)
            int sx = dx>0?1:(dx<0?-1:0);
            int sy = dy>0?1:(dy<0?-1:0);
            // try horizontal then vertical
            if(sx!=0 && isFloor(e.x+sx,e.y) && !occupied(e.x+sx,e.y)){ e.x+=sx; }
            else if(sy!=0 && isFloor(e.x,e.y+sy) && !occupied(e.x,e.y+sy)){ e.y+=sy; }
        }
    }
}

// ─── Item pickup ─────────────────────────────────────────────────────────────
static void pickup(){
    for(int i=0;i<_itemN;i++){
        Item& it=_items[i];
        if(!it.present||it.x!=_px||it.y!=_py) continue;
        char m[26];
        switch(it.kind){
            case I_STIM:    _stims++; logMsg("Got a stim pack"); break;
            case I_CREDITS: _credits+=it.amount; snprintf(m,sizeof(m),"Found %d credits",it.amount); logMsg(m); break;
            case I_WEAPON:  _wTier++; _atk+=2; logMsg("Weapon upgrade! ATK+2"); break;
            case I_ARMOR:   _aTier++; _def+=1; logMsg("Armor upgrade! DEF+1"); break;
        }
        it.present=false;
    }
}

// ─── Floor transition ────────────────────────────────────────────────────────
static void newFloor(bool deeper){
    if(deeper){ _depth++; if(_depth>_best){_best=_depth;saveBest();} }
    genMap();
    spawnEntities();
    spawnItems();
    drawMap();
    drawStats();
}

// ─── Player move (one turn) ──────────────────────────────────────────────────
static void tryMove(int dx,int dy){
    if(_over) return;
    int nx=_px+dx, ny=_py+dy;
    // attack if enemy there
    for(int i=0;i<_enN;i++){
        if(_en[i].alive && _en[i].x==nx && _en[i].y==ny){
            playerAttack(_en[i]);
            enemyTurn();
            drawMap(); drawStats();
            return;
        }
    }
    if(!isFloor(nx,ny)) return;          // wall: no move
    _px=nx; _py=ny;
    pickup();
    // exit?
    if(_map[_py][_px]==T_EXIT){
        logMsg("Descending...");
        newFloor(true);
        return;
    }
    enemyTurn();
    drawMap(); drawStats();
    if(_over){
        // game over overlay
        g_lcd_secondary.fillRect(60,90,200,60,T()->title_bg);
        g_lcd_secondary.drawRect(60,90,200,60,T()->alert);
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(T()->alert,T()->title_bg);
        g_lcd_secondary.setCursor(96,100); g_lcd_secondary.print("FLATLINE");
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setTextColor(T()->text,T()->title_bg);
        char m[24]; snprintf(m,sizeof(m),"Reached depth %d",_depth);
        g_lcd_secondary.setCursor(90,126); g_lcd_secondary.print(m);
        g_lcd_secondary.setCursor(86,138); g_lcd_secondary.print("ENTER to retry");
    }
}

static void useStim(){
    if(_over||_stims<=0) return;
    _stims--;
    int heal=12+_depth;
    _hp+=heal; if(_hp>_maxhp)_hp=_maxhp;
    char m[26]; snprintf(m,sizeof(m),"Stim +%d HP",heal); logMsg(m);
    enemyTurn();
    drawMap(); drawStats();
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────
static void newRun(){
    randomSeed(micros());
    _depth=1; _maxhp=30; _hp=30; _atk=4; _def=0;
    _credits=0; _stims=1; _wTier=0; _aTier=0;
    _over=false;
    logClear();
    logMsg("Jacked into the net");
    genMap(); spawnEntities(); spawnItems();
    drawMap(); drawStats();
}

void start(){
    _running=true;
    loadBest();
    newRun();
}

void handleKey(char key){
    if(_over){
        if(key=='\n'||key=='\r') newRun();
        return;
    }
    if(key=='w'||key=='W') tryMove(0,-1);
    else if(key=='s'||key=='S') tryMove(0,1);
    else if(key=='a'||key=='A') tryMove(-1,0);
    else if(key=='d'||key=='D') tryMove(1,0);
    else if(key=='e'||key=='E') useStim();
}

void tick(uint32_t now){
    // Turn-based: nothing animates per-frame. Rendering happens on input.
    (void)now;
}

bool isRunning(){ return _running; }
void stop(){ _running=false; }

} }  // namespace Games::Netrun
