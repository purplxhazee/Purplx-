// =============================================================================
// edc_misc.cpp — Flashlight, Dice, Notes, Calculator, Converter
// =============================================================================
#include "edc_tools.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <SD.h>

// =============================================================================
// FLASHLIGHT — full-bright screen, adjustable; SOS strobe option
// =============================================================================
namespace EDC { namespace Flashlight {
static bool _running=false;
static uint8_t _level=3;      // 1..3 brightness
static bool _strobe=false;
static bool _sos=false;
static uint32_t _last=0;
static int _sosIdx=0;

static void draw(){
    uint16_t c = _level==3?TFT_WHITE : _level==2?RGB565(180,180,180):RGB565(90,90,90);
    g_lcd_secondary.fillScreen(_sos?TFT_BLACK:c);
    g_lcd_primary.fillScreen(_sos?TFT_BLACK:c);
    if(!_sos&&!_strobe){
        // tiny hint in corner
        g_lcd_secondary.setTextColor(RGB565(40,40,40),c);
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setCursor(4,228);
        g_lcd_secondary.print("W/S bright  F strobe  O SOS  ESC");
    }
}
void start(){_running=true;_level=3;_strobe=false;_sos=false;draw();}
void handleKey(char key){
    if(key=='w'||key=='W'){if(_level<3)_level++;draw();}
    else if(key=='s'||key=='S'){if(_level>1)_level--;draw();}
    else if(key=='f'||key=='F'){_strobe=!_strobe;_sos=false;draw();}
    else if(key=='o'||key=='O'){_sos=!_sos;_strobe=false;_sosIdx=0;draw();}
}
void tick(uint32_t now){
    if(!_running)return;
    if(_strobe){
        if(now-_last>60){_last=now;
            static bool on=false;on=!on;
            uint16_t c=on?TFT_WHITE:TFT_BLACK;
            g_lcd_secondary.fillScreen(c);g_lcd_primary.fillScreen(c);
        }
    } else if(_sos){
        // SOS morse pattern: ...---...
        static const int pat[]={1,1,1, 0, 3,3,3, 0, 1,1,1, 5}; // 1=dot 3=dash 0/5=gaps
        static const int N=12;
        if(now-_last> (pat[_sosIdx]==1?200: pat[_sosIdx]==3?500: pat[_sosIdx]==5?1400:200)){
            _last=now;
            bool lit = pat[_sosIdx]==1||pat[_sosIdx]==3;
            uint16_t c=lit?TFT_WHITE:TFT_BLACK;
            g_lcd_secondary.fillScreen(c);g_lcd_primary.fillScreen(c);
            _sosIdx=(_sosIdx+1)%N;
        }
    }
}
bool isRunning(){return _running;}
void stop(){_running=false;}
}} // EDC::Flashlight

// =============================================================================
// DICE — roll dice + flip coin
// =============================================================================
namespace EDC { namespace Dice {
static bool _running=false;
static int _sides=6,_count=1,_results[8],_n=0;
static bool _coin=false;
static const ColorTheme* T(){return g_theme;}

static void draw(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8);g_lcd_secondary.print("DICE & COIN");

    if(_coin){
        g_lcd_secondary.setTextSize(3);
        g_lcd_secondary.setTextColor(T()->secondary,T()->bg);
        const char* r=_results[0]?"HEADS":"TAILS";
        int tw=strlen(r)*18;
        g_lcd_secondary.setCursor(160-tw/2,100);g_lcd_secondary.print(r);
    } else {
        // show dice
        int total=0;
        int per=(_count<=4)?_count:4;
        for(int i=0;i<_n;i++){
            int x=40+(i%4)*64, y=60+(i/4)*64;
            g_lcd_secondary.fillRoundRect(x,y,50,50,6,T()->text);
            g_lcd_secondary.setTextColor(TFT_BLACK);
            g_lcd_secondary.setTextSize(3);
            char b[4];snprintf(b,sizeof(b),"%d",_results[i]);
            int tw=strlen(b)*18;
            g_lcd_secondary.setCursor(x+25-tw/2,y+16);g_lcd_secondary.print(b);
            total+=_results[i];
        }
        g_lcd_secondary.setTextSize(2);
        g_lcd_secondary.setTextColor(T()->secondary,T()->bg);
        g_lcd_secondary.setCursor(8,190);g_lcd_secondary.printf("Total: %d",total);
    }
    g_lcd_secondary.fillRect(0,224,320,16,T()->title_bg);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(4,228);
    g_lcd_secondary.print("ENTER roll  C coin  D sides  N count  ESC");

    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("DICE");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,32);g_lcd_primary.printf("%dd%d",_count,_sides);
    g_lcd_primary.setCursor(4,46);g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.print("ENTER=roll C=coin");
}
static void roll(){
    if(_coin){_results[0]=random(0,2);_n=1;}
    else{_n=_count;for(int i=0;i<_count;i++)_results[i]=random(1,_sides+1);}
    draw();
}
void start(){_running=true;_sides=6;_count=1;_coin=false;_n=0;draw();}
void handleKey(char key){
    if(key=='\n'||key=='\r')roll();
    else if(key=='c'||key=='C'){_coin=!_coin;draw();}
    else if(key=='d'||key=='D'){int s[]={4,6,8,10,12,20};static int idx=1;idx=(idx+1)%6;_sides=s[idx];draw();}
    else if(key=='n'||key=='N'){_count=(_count%8)+1;draw();}
}
bool isRunning(){return _running;}
void stop(){_running=false;}
}} // EDC::Dice

// =============================================================================
// NOTES — type and save text notes to SD
// =============================================================================
namespace EDC { namespace Notes {
static bool _running=false;
static char _buf[512];
static int  _len=0;
static const ColorTheme* T(){return g_theme;}

static void draw(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8);g_lcd_secondary.print("NOTES  //  /purplx/notes.txt");

    // text area with word wrap
    g_lcd_secondary.setTextColor(T()->text,T()->bg);
    g_lcd_secondary.setTextSize(1);
    int x=6,y=30;
    for(int i=0;i<_len;i++){
        char c=_buf[i];
        if(c=='\n'){x=6;y+=12;continue;}
        g_lcd_secondary.setCursor(x,y);
        char s[2]={c,0};g_lcd_secondary.print(s);
        x+=6;
        if(x>310){x=6;y+=12;}
        if(y>200)break;
    }
    // cursor
    g_lcd_secondary.fillRect(x,y,5,10,T()->primary);

    g_lcd_secondary.fillRect(0,224,320,16,T()->title_bg);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(4,228);
    g_lcd_secondary.print("Type  ENTER newline  TAB save  ESC exit");
}
static void load(){
    _len=0;_buf[0]=0;
    File f=SD.open("/purplx/notes.txt");
    if(f){_len=f.read((uint8_t*)_buf,511);if(_len<0)_len=0;_buf[_len]=0;f.close();}
}
static void saveNote(){
    File f=SD.open("/purplx/notes.txt",FILE_WRITE);
    if(f){f.write((uint8_t*)_buf,_len);f.close();
        g_lcd_secondary.fillRect(100,100,120,24,T()->success);
        g_lcd_secondary.setTextColor(TFT_BLACK,T()->success);
        g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setCursor(130,108);g_lcd_secondary.print("SAVED!");
        delay(600);draw();
    }
}
void start(){_running=true;load();draw();}
void handleKey(char key){
    if(key=='\t'){saveNote();return;}
    if(key=='\n'||key=='\r'){if(_len<510){_buf[_len++]='\n';_buf[_len]=0;draw();}return;}
    if(key==8||key==127){if(_len>0){_len--;_buf[_len]=0;draw();}return;}
    if(key>=32&&key<127&&_len<510){_buf[_len++]=key;_buf[_len]=0;draw();}
}
bool isRunning(){return _running;}
void stop(){saveNote();_running=false;}
}} // EDC::Notes

// =============================================================================
// CALCULATOR — basic 4-function, type expression
// =============================================================================
namespace EDC { namespace Calc {
static bool _running=false;
static char _expr[32];
static int  _len=0;
static double _result=0;
static bool _hasResult=false;
static const ColorTheme* T(){return g_theme;}

// tiny recursive-descent parser for + - * / and parens
static const char* _p;
static double parseExpr();
static double parseNum(){
    while(*_p==' ')_p++;
    if(*_p=='('){_p++;double v=parseExpr();if(*_p==')')_p++;return v;}
    double v=0;bool neg=false;
    if(*_p=='-'){neg=true;_p++;}
    while(*_p>='0'&&*_p<='9'){v=v*10+(*_p-'0');_p++;}
    if(*_p=='.'){_p++;double f=0.1;while(*_p>='0'&&*_p<='9'){v+=(*_p-'0')*f;f*=0.1;_p++;}}
    return neg?-v:v;
}
static double parseTerm(){
    double v=parseNum();
    while(1){while(*_p==' ')_p++;
        if(*_p=='*'){_p++;v*=parseNum();}
        else if(*_p=='/'){_p++;double d=parseNum();v=(d!=0)?v/d:0;}
        else break;
    }
    return v;
}
static double parseExpr(){
    double v=parseTerm();
    while(1){while(*_p==' ')_p++;
        if(*_p=='+'){_p++;v+=parseTerm();}
        else if(*_p=='-'){_p++;v-=parseTerm();}
        else break;
    }
    return v;
}

static void draw(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8);g_lcd_secondary.print("CALCULATOR");

    // expression
    g_lcd_secondary.drawRect(10,50,300,40,T()->border);
    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(T()->text,T()->bg);
    g_lcd_secondary.setCursor(16,62);
    g_lcd_secondary.print(_len?_expr:"0");

    // result
    if(_hasResult){
        g_lcd_secondary.setTextSize(3);
        g_lcd_secondary.setTextColor(T()->secondary,T()->bg);
        char rb[24];
        if(_result==(long)_result)snprintf(rb,sizeof(rb),"= %ld",(long)_result);
        else snprintf(rb,sizeof(rb),"= %.4f",_result);
        g_lcd_secondary.setCursor(16,120);g_lcd_secondary.print(rb);
    }
    g_lcd_secondary.fillRect(0,224,320,16,T()->title_bg);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(4,228);
    g_lcd_secondary.print("Type 0-9 + - * / ( )  ENTER = solve  C clear");

    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("CALC");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.setCursor(4,34);g_lcd_primary.print("Type math, ENTER");
}
void start(){_running=true;_len=0;_expr[0]=0;_hasResult=false;draw();}
void handleKey(char key){
    if(key=='\n'||key=='\r'){
        if(_len>0){_p=_expr;_result=parseExpr();_hasResult=true;draw();}
    }
    else if(key=='c'||key=='C'){_len=0;_expr[0]=0;_hasResult=false;draw();}
    else if(key==8||key==127){if(_len>0){_len--;_expr[_len]=0;draw();}}
    else if((( key>='0'&&key<='9')||key=='+'||key=='-'||key=='*'||key=='/'||key=='.'||key=='('||key==')')&&_len<30){
        _expr[_len++]=key;_expr[_len]=0;_hasResult=false;draw();
    }
}
bool isRunning(){return _running;}
void stop(){_running=false;}
}} // EDC::Calc

// =============================================================================
// CONVERTER — common unit conversions
// =============================================================================
namespace EDC { namespace Convert {
static bool _running=false;
static int _cat=0;        // category
static double _input=1.0;
static const ColorTheme* T(){return g_theme;}

struct Conv { const char* name; const char* fromU; const char* toU; double factor; };
static const Conv CONVS[]={
    {"Length","km","miles",0.621371},
    {"Length","miles","km",1.60934},
    {"Length","m","feet",3.28084},
    {"Length","cm","inches",0.393701},
    {"Weight","kg","lbs",2.20462},
    {"Weight","lbs","kg",0.453592},
    {"Temp","C","F",0}, // special
    {"Temp","F","C",0}, // special
    {"Speed","kmh","mph",0.621371},
    {"Speed","mph","kmh",1.60934},
    {"Volume","L","gallons",0.264172},
};
static const int NCONV=sizeof(CONVS)/sizeof(CONVS[0]);

static double doConv(int i,double v){
    if(strcmp(CONVS[i].name,"Temp")==0){
        if(strcmp(CONVS[i].fromU,"C")==0)return v*9.0/5.0+32.0;
        else return (v-32.0)*5.0/9.0;
    }
    return v*CONVS[i].factor;
}

static void draw(){
    g_lcd_secondary.fillScreen(T()->bg);
    g_lcd_secondary.fillRect(0,0,320,24,T()->title_bg);
    g_lcd_secondary.setTextSize(1);g_lcd_secondary.setTextColor(T()->primary,T()->title_bg);
    g_lcd_secondary.setCursor(8,8);g_lcd_secondary.printf("CONVERT  //  %s",CONVS[_cat].name);

    g_lcd_secondary.setTextSize(2);
    g_lcd_secondary.setTextColor(T()->text,T()->bg);
    char b[40];
    snprintf(b,sizeof(b),"%.2f %s",_input,CONVS[_cat].fromU);
    g_lcd_secondary.setCursor(20,70);g_lcd_secondary.print(b);

    g_lcd_secondary.setTextColor(T()->text_dim,T()->bg);
    g_lcd_secondary.setCursor(20,100);g_lcd_secondary.print("=");

    g_lcd_secondary.setTextSize(3);
    g_lcd_secondary.setTextColor(T()->secondary,T()->bg);
    snprintf(b,sizeof(b),"%.3f %s",doConv(_cat,_input),CONVS[_cat].toU);
    g_lcd_secondary.setCursor(20,130);g_lcd_secondary.print(b);

    g_lcd_secondary.fillRect(0,224,320,16,T()->title_bg);
    g_lcd_secondary.setTextColor(T()->text_dim,T()->title_bg);
    g_lcd_secondary.setCursor(4,228);
    g_lcd_secondary.print("TAB type  W/S value  A/D x10  ESC");

    g_lcd_primary.fillScreen(T()->hud_bg);
    g_lcd_primary.setTextColor(T()->primary,T()->hud_bg);
    g_lcd_primary.setTextSize(2);g_lcd_primary.setCursor(4,6);g_lcd_primary.print("CONV");
    g_lcd_primary.setTextSize(1);g_lcd_primary.setTextColor(T()->text,T()->hud_bg);
    g_lcd_primary.setCursor(4,32);g_lcd_primary.print(CONVS[_cat].name);
    g_lcd_primary.setCursor(4,46);g_lcd_primary.setTextColor(T()->text_dim,T()->hud_bg);
    g_lcd_primary.print("TAB=next unit");
}
void start(){_running=true;_cat=0;_input=1.0;draw();}
void handleKey(char key){
    if(key=='\t'){_cat=(_cat+1)%NCONV;draw();}
    else if(key=='w'||key=='W'){_input+=1;draw();}
    else if(key=='s'||key=='S'){_input-=1;if(_input<0)_input=0;draw();}
    else if(key=='d'||key=='D'){_input*=10;draw();}
    else if(key=='a'||key=='A'){_input/=10;draw();}
}
bool isRunning(){return _running;}
void stop(){_running=false;}
}} // EDC::Convert
